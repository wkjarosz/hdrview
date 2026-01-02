#include "webp.h"
#include "json.h"
//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "app.h"
#include "common.h"
#include "image.h"
#include "webp.h"

using namespace std;

struct WebPSaveOptions
{
    float            gain     = 1.f;
    bool             lossless = false;
    float            quality  = 95.f;
    TransferFunction tf       = TransferFunction::sRGB;
};

static WebPSaveOptions s_opts;

#if !HDRVIEW_ENABLE_LIBWEBP

json get_webp_info() { return {{"name", "libwebp"}}; }

bool is_webp_image(istream &is) noexcept { return false; }

vector<ImagePtr> load_webp_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    throw runtime_error("WebP support not enabled in this build.");
}

void save_webp_image(const Image &img, std::ostream &os, std::string_view filename, float gain, float quality,
                     bool lossless, TransferFunction tf)
{
    throw runtime_error("WebP support not enabled in this build.");
}

WebPSaveOptions *webp_parameters_gui() { return &s_opts; }

void save_webp_image(const Image &img, std::ostream &os, std::string_view filename, const WebPSaveOptions *params)
{
    throw runtime_error("WebP support not enabled in this build.");
}

#else

#include <cstring>
#include <spdlog/fmt/fmt.h>
#include <stdexcept>
#include <vector>

#include <webp/decode.h>
#include <webp/demux.h>
#include <webp/encode.h>
#include <webp/mux.h>
#include <webp/mux_types.h>
#include <webp/types.h>

#include "colorspace.h"
#include "exif.h"
#include "fonts.h"
#include "icc.h"
#include "imgui_ext.h"
#include "timer.h"

using namespace std;

json get_webp_info()
{
    json j;
    j["enabled"]      = true;
    j["name"]         = "libwebp";
    int       webp_v  = WebPGetDecoderVersion();
    const int d_major = (webp_v >> 16) & 0xff;
    const int d_minor = (webp_v >> 8) & 0xff;
    const int d_rev   = webp_v & 0xff;
    j["version"]      = fmt::format("{}.{}.{} ({})", d_major, d_minor, d_rev, webp_v);

#ifdef WEBP_HAVE_ENCODER
    bool enc = true;
#else
    bool enc = false;
#endif

#ifdef WEBP_HAVE_DECODER
    bool dec = true;
#else
    bool dec = false;
#endif

    j["features"] = json::object();
    return j;
}

namespace
{

// Helper to check WebP signature
bool check_webp_signature(istream &is)
{
    // WebP files start with "RIFF" followed by file size, then "WEBP"
    char sig[12];
    is.read(sig, 12);
    bool is_webp = !!is && is.gcount() == 12 && memcmp(sig, "RIFF", 4) == 0 && memcmp(sig + 8, "WEBP", 4) == 0;
    is.clear();
    is.seekg(0);
    return is_webp;
}

} // end anonymous namespace

bool is_webp_image(istream &is) noexcept
{
    auto start = is.tellg();
    bool ret   = false;
    try
    {
        ret = check_webp_signature(is);
    }
    catch (...)
    {
    }
    is.clear();
    is.seekg(start);
    return ret;
}

vector<ImagePtr> load_webp_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    ScopedMDC mdc{"IO", "WebP"};
    if (!check_webp_signature(is))
        throw invalid_argument{"Invalid WebP signature"};

    // Calculate size of stream
    is.clear();
    is.seekg(0, ios::end);
    size_t file_size = static_cast<size_t>(is.tellg());
    is.seekg(0, ios::beg);

    if (file_size == 0)
        throw invalid_argument{"Empty file"};

    // Read entire file into memory
    vector<uint8_t> data(file_size);
    is.read(reinterpret_cast<char *>(data.data()), file_size);
    if (static_cast<size_t>(is.gcount()) != file_size)
        throw runtime_error{
            fmt::format("Failed to read file: expected {} bytes, got {} bytes", file_size, is.gcount())};

    Timer timer;

    // Setup demuxer for metadata and animation info
    WebPData     webp_data = {data.data(), file_size};
    WebPDemuxer *demux     = WebPDemux(&webp_data);
    if (!demux)
        throw runtime_error{"Failed to demux WebP image"};
    auto demux_guard = ScopeGuard{[demux] { WebPDemuxDelete(demux); }};

    // Get canvas size (for animations, this may differ from first frame size)
    const int      canvas_width  = (int)WebPDemuxGetI(demux, WEBP_FF_CANVAS_WIDTH);
    const int      canvas_height = (int)WebPDemuxGetI(demux, WEBP_FF_CANVAS_HEIGHT);
    const uint32_t flags         = WebPDemuxGetI(demux, WEBP_FF_FORMAT_FLAGS);
    const uint32_t loop_count    = WebPDemuxGetI(demux, WEBP_FF_LOOP_COUNT);
    const uint32_t frame_count   = WebPDemuxGetI(demux, WEBP_FF_FRAME_COUNT);
    const bool     has_animation = flags & ANIMATION_FLAG;
    const bool     has_alpha     = flags & ALPHA_FLAG;

    constexpr const char *format_str[] = {"Undefined/Mixed", "Lossy", "Lossless"};

    spdlog::debug("WebP image info: {}x{}, frames: {}, animated: {}, alpha: {}", canvas_width, canvas_height,
                  frame_count, has_animation ? "yes" : "no", has_alpha ? "yes" : "no");

    // Create base metadata object that will be shared across frames
    json base_metadata;
    base_metadata["loader"] = "libwebp";

    // Store whether file is animated
    base_metadata["header"]["Animated"] = {{"value", has_animation},
                                           {"string", has_animation ? "yes" : "no"},
                                           {"type", "bool"},
                                           {"description", "Whether this is an animated WebP file"}};

    // Extract metadata (ICC, EXIF, XMP) - shared across all frames
    vector<uint8_t> icc_data;
    vector<uint8_t> exif_data;
    ICCProfile      icc_profile;

    WebPChunkIterator chunk_iter;
    if ((flags & ICCP_FLAG) && WebPDemuxGetChunk(demux, "ICCP", 1, &chunk_iter))
    {
        auto chunk_guard = ScopeGuard{[&chunk_iter] { WebPDemuxReleaseChunkIterator(&chunk_iter); }};
        try
        {
            icc_data.assign(chunk_iter.chunk.bytes, chunk_iter.chunk.bytes + chunk_iter.chunk.size);
            icc_profile = ICCProfile{icc_data};
            spdlog::debug("Found ICC profile ({} bytes)", icc_data.size());
        }
        catch (const exception &e)
        {
            spdlog::warn("Failed to read ICC profile: {}", e.what());
        }
    }

    if ((flags & EXIF_FLAG) && WebPDemuxGetChunk(demux, "EXIF", 1, &chunk_iter))
    {
        auto chunk_guard = ScopeGuard{[&chunk_iter] { WebPDemuxReleaseChunkIterator(&chunk_iter); }};
        try
        {
            exif_data.assign(chunk_iter.chunk.bytes, chunk_iter.chunk.bytes + chunk_iter.chunk.size);
            spdlog::debug("Found EXIF data ({} bytes)", exif_data.size());
        }
        catch (const exception &e)
        {
            spdlog::warn("Failed to parse EXIF data: {}", e.what());
        }

        if (!exif_data.empty())
        {
            try
            {
                base_metadata["exif"] = exif_to_json(exif_data);
            }
            catch (const exception &e)
            {
                spdlog::warn("Failed to parse EXIF data: {}", e.what());
            }
        }
    }

    if ((flags & XMP_FLAG) && WebPDemuxGetChunk(demux, "XMP ", 1, &chunk_iter))
    {
        auto chunk_guard = ScopeGuard{[&chunk_iter] { WebPDemuxReleaseChunkIterator(&chunk_iter); }};
        try
        {
            auto xmp                       = string_view{(const char *)chunk_iter.chunk.bytes, chunk_iter.chunk.size};
            base_metadata["header"]["XMP"] = {
                {"value", xmp}, {"string", xmp}, {"type", "string"}, {"description", "XMP metadata"}};
            spdlog::debug("Found XMP chunk: {}", xmp);
        }
        catch (const exception &e)
        {
            spdlog::warn("Failed to parse XMP data: {}", e.what());
        }
    }

    // Get info related to animations
    float4 bg_color{0.f, 0.f, 0.f, 0.f};
    if (has_animation)
    {
        // Store frame count
        base_metadata["header"]["Frame count"] = {{"value", frame_count},
                                                  {"string", fmt::format("{}", frame_count)},
                                                  {"type", "int"},
                                                  {"description", "Total number of frames"}};
        base_metadata["header"]["Loop count"]  = {
            {"value", loop_count},
            {"string", loop_count == 0 ? "infinite" : fmt::format("{}", loop_count)},
            {"type", "int"},
            {"description", "Number of times to loop animation (0=infinite)"}};

        const uint32_t bg_color_8bit = WebPDemuxGetI(demux, WEBP_FF_BACKGROUND_COLOR);
        // Byte order: BGRA (https://developers.google.com/speed/webp/docs/riff_container#animation)
        const uint8_t *bg_bytes = reinterpret_cast<const uint8_t *>(&bg_color_8bit);
        bg_color = float4{bg_bytes[2] / 255.f, bg_bytes[1] / 255.f, bg_bytes[0] / 255.f, bg_bytes[3] / 255.f};

        // Store background color in metadata
        base_metadata["header"]["Background color"] = {
            {"value", std::vector<int>{bg_bytes[2], bg_bytes[1], bg_bytes[0], bg_bytes[3]}},
            {"string", fmt::format("RGBA({}, {}, {}, {})", bg_bytes[2], bg_bytes[1], bg_bytes[0], bg_bytes[3])},
            {"type", "color"},
            {"description", "Background color for animation canvas (8-bit RGBA)"}};

        // Apply color profile to background color if we have ICC
        if (icc_profile.valid())
            icc_profile.linearize_pixels(&bg_color.x, int3{1, 1, 4}, false, nullptr, nullptr);
        else
            // sRGB transfer function
            for (int c = 0; c < 3; ++c) bg_color[c] = sRGB_to_linear(bg_color[c]);
    }

    // Prepare channel filter
    ImGuiTextFilter filter{opts.channel_selector.c_str()};
    filter.Build();

    // Result images
    vector<ImagePtr> images;

    // Buffer for previous canvas (for frame compositing)
    vector<float> prev_canvas;
    bool          disposed = true; // First frame is always "disposed"

    // Iterate through frames using WebPIterator
    WebPIterator iter;
    if (WebPDemuxGetFrame(demux, 1, &iter))
    {
        int              frame_idx = 0;
        const ScopeGuard iter_guard{[&iter] { WebPDemuxReleaseIterator(&iter); }};
        do {
            // Check channel filter
            string partname = has_animation ? fmt::format("frame {:04}", frame_idx) : "";
            if (!filter.PassFilter(partname.c_str()))
            {
                spdlog::debug("Skipping frame {} (filtered out)", frame_idx);
                frame_idx++;
                continue;
            }

            // Decode frame fragment to RGB or RGBA based on alpha presence
            int      frame_width  = 0;
            int      frame_height = 0;
            uint8_t *frame_data =
                has_alpha ? WebPDecodeRGBA(iter.fragment.bytes, iter.fragment.size, &frame_width, &frame_height)
                          : WebPDecodeRGB(iter.fragment.bytes, iter.fragment.size, &frame_width, &frame_height);
            const ScopeGuard dataGuard{[frame_data] { WebPFree(frame_data); }};
            if (!frame_data)
            {
                spdlog::warn("Failed to decode frame {}", frame_idx);
                frame_idx++;
                continue;
            }

            const int num_channels = has_alpha ? 4 : 3;

            // Determine base canvas (background or previous frame)
            const bool use_bg = disposed || prev_canvas.empty();

            // Check if background is fully transparent
            const bool transparent_bg = (bg_color[3] == 0.f);

            // If background is transparent and we're not compositing over previous frame,
            // we can use frame size as data window, otherwise have to form an image for the full canvas
            const bool use_full_canvas  = !transparent_bg || !use_bg;
            const int  img_width        = use_full_canvas ? canvas_width : frame_width;
            const int  img_height       = use_full_canvas ? canvas_height : frame_height;
            auto       frame_image      = make_shared<Image>(int2{img_width, img_height}, num_channels);
            frame_image->filename       = filename;
            frame_image->partname       = partname;
            frame_image->alpha_type     = has_alpha ? AlphaType_Straight : AlphaType_None;
            frame_image->icc_data       = icc_data;
            frame_image->exif           = Exif{exif_data};
            frame_image->display_window = Box2i{int2{0, 0}, int2{canvas_width, canvas_height}};
            frame_image->data_window    = use_full_canvas
                                              ? frame_image->display_window
                                              : Box2i{int2{iter.x_offset, iter.y_offset},
                                                   int2{iter.x_offset + frame_width, iter.y_offset + frame_height}};

            // Start with base metadata common to all frames
            frame_image->metadata                 = base_metadata;
            frame_image->metadata["pixel format"] = has_alpha ? "RGBA 8-bit" : "RGB 8-bit";

            // Check if frame is lossy or lossless
            WebPBitstreamFeatures features;
            if (WebPGetFeatures(iter.fragment.bytes, iter.fragment.size, &features) == VP8_STATUS_OK &&
                features.format >= 0 && features.format <= 2)
            {
                frame_image->metadata["header"]["Compression"] = {
                    {"value", features.format},
                    {"string", format_str[features.format]},
                    {"type", "int"},
                    {"description", "WebP compression format (1=lossy, 2=lossless)"}};
            }

            if (has_animation)
            {
                frame_image->metadata["header"]["Frame index"]    = {{"value", frame_idx},
                                                                     {"string", fmt::format("{}", frame_idx)},
                                                                     {"type", "int"},
                                                                     {"description", "Frame index in animation"}};
                frame_image->metadata["header"]["Frame duration"] = {
                    {"value", iter.duration},
                    {"string", fmt::format("{} ms", iter.duration)},
                    {"type", "int"},
                    {"description", "Frame display duration in milliseconds"}};
                frame_image->metadata["header"]["Dispose method"] = {
                    {"value", iter.dispose_method},
                    {"string", iter.dispose_method == WEBP_MUX_DISPOSE_BACKGROUND ? "Background" : "None"},
                    {"type", "int"},
                    {"description", "Frame disposal method (0=none, 1=dispose to background)"}};
                frame_image->metadata["header"]["Blend method"] = {
                    {"value", iter.blend_method},
                    {"string", iter.blend_method == WEBP_MUX_NO_BLEND ? "No blend" : "Alpha blend"},
                    {"type", "int"},
                    {"description", "Frame blending method (0=alpha blend, 1=no blend)"}};
            }

            // Convert fragment to float and apply color profile
            std::vector<float> frame_pixels(frame_width * frame_height * num_channels);
            for (int y = 0; y < frame_height; ++y)
                for (int x = 0; x < frame_width; ++x)
                    for (int c = 0; c < num_channels; ++c)
                        frame_pixels[(y * frame_width + x) * num_channels + c] =
                            dequantize_full(frame_data[(y * frame_width + x) * num_channels + c]);

            // Apply color profile transformations to fragment
            int3 frame_size{frame_width, frame_height, num_channels};
            if (opts.override_profile)
            {
                string         profile_desc = color_profile_name(ColorGamut_Unspecified, TransferFunction::Unspecified);
                Chromaticities chr;
                if (linearize_pixels(frame_pixels.data(), frame_size, gamut_chromaticities(opts.gamut_override),
                                     opts.tf_override, opts.keep_primaries, &profile_desc, &chr))
                {
                    frame_image->chromaticities = chr;
                    profile_desc += " (override)";
                }
                frame_image->metadata["color profile"] = profile_desc;
            }
            else
            {
                string         profile_desc = color_profile_name(ColorGamut_sRGB_BT709, TransferFunction::sRGB);
                Chromaticities chr;
                if ((icc_profile.valid() && icc_profile.linearize_pixels(frame_pixels.data(), frame_size,
                                                                         opts.keep_primaries, &profile_desc, &chr)) ||
                    linearize_pixels(frame_pixels.data(), frame_size, Chromaticities{}, TransferFunction::sRGB,
                                     opts.keep_primaries, &profile_desc, &chr))
                    frame_image->chromaticities = chr;

                frame_image->metadata["color profile"] = profile_desc;
            }

            float        *pixels = frame_pixels.data();
            vector<float> canvas_float;

            if (use_full_canvas)
            {
                // Need to composite onto canvas (opaque/semi-transparent background or previous frame)
                canvas_float.resize(canvas_width * canvas_height * num_channels);

                for (int y = 0; y < canvas_height; ++y)
                {
                    for (int x = 0; x < canvas_width; ++x)
                    {
                        const size_t canvas_idx = (y * canvas_width + x) * num_channels;
                        // Position in fragment coordinates
                        const int  frame_x = x - iter.x_offset;
                        const int  frame_y = y - iter.y_offset;
                        const bool in_frame =
                            frame_x >= 0 && frame_x < frame_width && frame_y >= 0 && frame_y < frame_height;

                        // iterate in reverse so we compute alpha first if present, since for straight alpha we need it
                        // to compute the RGB channels
                        for (int c = num_channels - 1; c >= 0; --c)
                        {
                            // Get background value
                            float bg_val = use_bg ? bg_color[c] : prev_canvas[canvas_idx + c];

                            if (!in_frame)
                            {
                                // Outside fragment - use background
                                canvas_float[canvas_idx + c] = bg_val;
                                continue;
                            }

                            const size_t fragment_idx = (frame_y * frame_width + frame_x) * num_channels;
                            const float  frag_val     = frame_pixels[fragment_idx + c];
                            const float  frag_alpha   = frame_pixels[fragment_idx + 3];

                            // Blend based on blend method
                            if (iter.blend_method == WEBP_MUX_NO_BLEND)
                                // Replace mode - use fragment value directly
                                canvas_float[canvas_idx + c] = frag_val;
                            else
                            {
                                // Alpha blend mode - composite over background
                                if (c < 3)
                                    canvas_float[canvas_idx + c] =
                                        (frag_val * frag_alpha + bg_val * (1.f - frag_alpha)) /
                                        canvas_float[canvas_idx + 3];
                                else
                                    canvas_float[canvas_idx + 3] = frag_alpha + bg_val * (1.f - frag_alpha);
                            }
                        }
                    }
                }

                pixels = canvas_float.data();

                // Store canvas for next frame if not disposing
                disposed = (iter.dispose_method == WEBP_MUX_DISPOSE_BACKGROUND);
                if (!disposed)
                    prev_canvas = std::move(canvas_float);
            }

            // Copy pixels to image channels
            for (int c = 0; c < num_channels; ++c)
                frame_image->channels[c].copy_from_interleaved(pixels, img_width, img_height, num_channels, c,
                                                               [](float v) { return v; });

            images.push_back(frame_image);
            frame_idx++;

        } while (WebPDemuxNextFrame(&iter));
    }

    spdlog::debug("Loaded {} WebP frame(s) in {} seconds", images.size(), timer.elapsed() / 1000.f);

    return images;
}

void save_webp_image(const Image &img, std::ostream &os, std::string_view filename, const WebPSaveOptions *opts)
{
    if (!opts)
        throw invalid_argument{"WebP save options cannot be null"};

    Timer timer;

    // Get interleaved RGBA data
    int  w = 0, h = 0, n = 0;
    auto pixels = img.as_interleaved<uint8_t>(
        &w, &h, &n, opts->gain,
        opts->tf.type == TransferFunction::sRGB ? TransferFunction::sRGB : TransferFunction::Linear, true);

    // WebP supports RGB or RGBA
    if (n != 3 && n != 4)
        throw runtime_error{fmt::format("WebP only supports RGB or RGBA images, but image has {} channels", n)};

    spdlog::info("Encoding {}-channel, {}x{} pixels {} WebP image (quality: {}, lossless: {})", n, w, h,
                 opts->tf.type == TransferFunction::sRGB ? "sRGB" : "linear", opts->quality, opts->lossless);

    // Setup WebP encoder config
    WebPConfig config;
    if (!WebPConfigInit(&config))
        throw runtime_error{"Failed to initialize WebP config"};

    config.lossless = opts->lossless ? 1 : 0;
    config.quality  = opts->quality;
    config.method   = 6; // 0=fast, 6=slower but better quality

    if (!WebPValidateConfig(&config))
        throw runtime_error{"Invalid WebP configuration"};

    // Setup picture
    WebPPicture picture;
    if (!WebPPictureInit(&picture))
        throw runtime_error{"Failed to initialize WebP picture"};

    picture.width    = w;
    picture.height   = h;
    picture.use_argb = opts->lossless ? 1 : 0; // Use ARGB for lossless, YUV for lossy

    // Import pixels
    int import_result = 0;
    if (n == 4)
        import_result = WebPPictureImportRGBA(&picture, pixels.get(), w * 4);
    else
        import_result = WebPPictureImportRGB(&picture, pixels.get(), w * 3);

    if (!import_result)
    {
        WebPPictureFree(&picture);
        throw runtime_error{"Failed to import pixels to WebP picture"};
    }

    // Setup memory writer
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    picture.writer     = WebPMemoryWrite;
    picture.custom_ptr = &writer;

    // Encode
    int encode_result = WebPEncode(&config, &picture);
    WebPPictureFree(&picture);

    if (!encode_result)
    {
        WebPMemoryWriterClear(&writer);
        throw runtime_error{"WebP encoding failed"};
    }

    // Write to output stream
    os.write(reinterpret_cast<const char *>(writer.mem), writer.size);
    WebPMemoryWriterClear(&writer);

    if (!os.good())
        throw runtime_error{"Failed to write WebP data to output stream"};

    spdlog::info("Saved WebP image to \"{}\" in {} seconds.", filename, (timer.elapsed() / 1000.f));
}

void save_webp_image(const Image &img, ostream &os, string_view filename, float gain, float quality, bool lossless,
                     TransferFunction tf)
{
    WebPSaveOptions opts{gain, lossless, quality, tf};
    save_webp_image(img, os, filename, &opts);
}

WebPSaveOptions *webp_parameters_gui()
{
    if (ImGui::PE::Begin("WebP Save Options", ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize))
    {
        ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_None);
        ImGui::TableSetupColumn("two", ImGuiTableColumnFlags_WidthStretch);

        ImGui::PE::Entry(
            "Gain",
            [&]
            {
                ImGui::BeginGroup();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::IconButtonSize().x -
                                        ImGui::GetStyle().ItemInnerSpacing.x);
                auto changed = ImGui::SliderFloat("##Gain", &s_opts.gain, 0.1f, 10.0f);
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
                if (ImGui::IconButton(ICON_MY_EXPOSURE))
                    s_opts.gain = exp2f(hdrview()->exposure());
                ImGui::Tooltip("Set gain from the current viewport exposure value.");
                ImGui::EndGroup();
                return changed;
            },
            "Multiply the pixels by this value before saving.");

        ImGui::PE::Entry(
            "Transfer function",
            [&]
            {
                if (ImGui::BeginCombo("##Transfer function", transfer_function_name(s_opts.tf).c_str()))
                {
                    for (int i = TransferFunction::Linear; i <= TransferFunction::DCI_P3; ++i)
                    {
                        bool is_selected = (s_opts.tf.type == (TransferFunction::Type_)i);
                        if (ImGui::Selectable(
                                transfer_function_name({(TransferFunction::Type_)i, s_opts.tf.gamma}).c_str(),
                                is_selected))
                            s_opts.tf.type = (TransferFunction::Type_)i;
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                return true;
            },
            "Encode the pixel values using this transfer function.");

        if (s_opts.tf.type == TransferFunction::Gamma)
            ImGui::PE::SliderFloat("Gamma", &s_opts.tf.gamma, 0.1f, 5.f, "%.3f", 0,
                                   "When using a gamma transfer function, this is the gamma value to use.");

        ImGui::PE::Checkbox(
            "Lossless", &s_opts.lossless,
            "If enabled, the image will be saved using lossless compression. Quality setting will be ignored.");

        ImGui::BeginDisabled(s_opts.lossless);
        ImGui::PE::SliderFloat("Quality", &s_opts.quality, 1, 100, "%.3f", 0, "Quality level for lossy compression.");
        ImGui::EndDisabled();

        ImGui::PE::End();
    }

    if (ImGui::Button("Reset options to defaults"))
        s_opts = WebPSaveOptions{};

    return &s_opts;
}

#endif
