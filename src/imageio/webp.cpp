//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "webp.h"
#include "app.h"
#include "colorspace.h"
#include "common.h"
#include "exif.h"
#include "icc.h"
#include "image.h"
#include "timer.h"

#include "imgui_ext.h"
#include <webp/mux_types.h>

using namespace std;

struct WebPSaveOptions
{
    float            gain            = 1.f;
    float            quality         = 95.f;
    bool             lossless        = false;
    TransferFunction tf              = TransferFunction::sRGB;
    int              data_type_index = 0;
};

static WebPSaveOptions s_opts;

#ifndef HDRVIEW_ENABLE_LIBWEBP

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
#include <webp/types.h>

using namespace std;

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

        // Premultiply alpha
        for (int c = 0; c < 3; ++c) bg_color[c] *= bg_color[3];
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
            // we can use frame size as data window
            const bool use_frame_size   = transparent_bg && use_bg;
            const int  img_width        = use_frame_size ? frame_width : canvas_width;
            const int  img_height       = use_frame_size ? frame_height : canvas_height;
            auto       frame_image      = make_shared<Image>(int2{img_width, img_height}, num_channels);
            frame_image->filename       = filename;
            frame_image->partname       = partname;
            frame_image->alpha_type     = has_alpha ? AlphaType_Straight : AlphaType_None;
            frame_image->icc_data       = icc_data;
            frame_image->exif_data      = exif_data;
            frame_image->display_window = Box2i{int2{0, 0}, int2{canvas_width, canvas_height}};
            frame_image->data_window    = use_frame_size
                                              ? Box2i{int2{iter.x_offset, iter.y_offset},
                                                   int2{iter.x_offset + frame_width, iter.y_offset + frame_height}}
                                              : frame_image->display_window;

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
            std::vector<float> fragment_float(frame_width * frame_height * num_channels);
            for (int y = 0; y < frame_height; ++y)
                for (int x = 0; x < frame_width; ++x)
                    for (int c = 0; c < num_channels; ++c)
                        fragment_float[(y * frame_width + x) * num_channels + c] =
                            frame_data[(y * frame_width + x) * num_channels + c] / 255.f;

            // Apply color profile transformations to fragment
            int3 frame_size{frame_width, frame_height, num_channels};
            if (opts.override_profile)
            {
                string         profile_desc = color_profile_name(ColorGamut_Unspecified, TransferFunction::Unspecified);
                Chromaticities chr;
                if (linearize_pixels(fragment_float.data(), frame_size, gamut_chromaticities(opts.gamut_override),
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
                if ((icc_profile.valid() && icc_profile.linearize_pixels(fragment_float.data(), frame_size,
                                                                         opts.keep_primaries, &profile_desc, &chr)) ||
                    linearize_pixels(fragment_float.data(), frame_size, Chromaticities{}, TransferFunction::sRGB,
                                     opts.keep_primaries, &profile_desc, &chr))
                    frame_image->chromaticities = chr;

                frame_image->metadata["color profile"] = profile_desc;
            }

            // Composite fragment onto canvas or use fragment directly
            if (use_frame_size)
            {
                // Transparent background - just premultiply alpha in the fragment (if present)
                if (has_alpha)
                {
                    for (int y = 0; y < frame_height; ++y)
                    {
                        for (int x = 0; x < frame_width; ++x)
                        {
                            const size_t idx   = (y * frame_width + x) * num_channels;
                            const float  alpha = fragment_float[idx + 3];
                            for (int c = 0; c < 3; ++c) fragment_float[idx + c] *= alpha;
                        }
                    }
                }

                // Copy fragment directly to image channels
                for (int c = 0; c < num_channels; ++c)
                    frame_image->channels[c].copy_from_interleaved(fragment_float.data(), frame_width, frame_height,
                                                                   num_channels, c, [](float v) { return v; });
            }
            else
            {
                // Need to composite onto canvas (opaque/semi-transparent background or previous frame)
                vector<float> canvas_float(img_width * img_height * num_channels);

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

                        for (int c = 0; c < num_channels; ++c)
                        {
                            // Get background value
                            float bg_val = use_bg ? bg_color[c] : prev_canvas[canvas_idx + c];

                            if (in_frame)
                            {
                                const size_t fragment_idx = (frame_y * frame_width + frame_x) * num_channels;
                                const float  frag_val     = fragment_float[fragment_idx + c];
                                const float  frag_alpha   = fragment_float[fragment_idx + 3];

                                // Blend based on blend method
                                if (iter.blend_method == WEBP_MUX_NO_BLEND)
                                {
                                    // Replace mode - use fragment value directly (premultiplied)
                                    canvas_float[canvas_idx + c] = c < 3 ? frag_val * frag_alpha : frag_alpha;
                                }
                                else
                                {
                                    // Alpha blend mode - composite over background
                                    canvas_float[canvas_idx + c] =
                                        c < 3 ? frag_val * frag_alpha + bg_val * (1.f - frag_alpha)
                                              : frag_alpha + bg_val * (1.f - frag_alpha);
                                }
                            }
                            else
                            {
                                // Outside fragment - use background
                                canvas_float[canvas_idx + c] = bg_val;
                            }
                        }
                    }
                }

                // Copy canvas to image channels
                for (int c = 0; c < num_channels; ++c)
                    frame_image->channels[c].copy_from_interleaved(canvas_float.data(), canvas_width, canvas_height,
                                                                   num_channels, c, [](float v) { return v; });

                // Store canvas for next frame if not disposing
                disposed = (iter.dispose_method == WEBP_MUX_DISPOSE_BACKGROUND);
                if (!disposed)
                    prev_canvas = std::move(canvas_float);
            }

            images.push_back(frame_image);
            frame_idx++;

        } while (WebPDemuxNextFrame(&iter));
    }

    spdlog::debug("Loaded {} WebP frame(s) in {} seconds", images.size(), timer.elapsed() / 1000.f);

    return images;
}

void save_webp_image(const Image &img, ostream &os, string_view filename, float gain, float quality, bool lossless,
                     TransferFunction tf)
{
    Timer timer;

    // Get interleaved RGBA data
    int  w = 0, h = 0, n = 0;
    auto pixels = img.as_interleaved<uint8_t>(
        &w, &h, &n, gain, tf.type == TransferFunction::sRGB ? TransferFunction::sRGB : TransferFunction::Linear, true);

    // WebP supports RGB or RGBA
    if (n != 3 && n != 4)
        throw runtime_error{fmt::format("WebP only supports RGB or RGBA images, but image has {} channels", n)};

    spdlog::info("Encoding {}-channel, {}x{} pixels {} WebP image (quality: {}, lossless: {})", n, w, h,
                 tf.type == TransferFunction::sRGB ? "sRGB" : "linear", quality, lossless);

    // Setup WebP encoder config
    WebPConfig config;
    if (!WebPConfigInit(&config))
        throw runtime_error{"Failed to initialize WebP config"};

    config.lossless = lossless ? 1 : 0;
    config.quality  = quality;
    config.method   = 6; // 0=fast, 6=slower but better quality

    if (!WebPValidateConfig(&config))
        throw runtime_error{"Invalid WebP configuration"};

    // Setup picture
    WebPPicture picture;
    if (!WebPPictureInit(&picture))
        throw runtime_error{"Failed to initialize WebP picture"};

    picture.width    = w;
    picture.height   = h;
    picture.use_argb = lossless ? 1 : 0; // Use ARGB for lossless, YUV for lossy

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

WebPSaveOptions *webp_parameters_gui()
{
    static bool first_time = true;
    if (first_time)
    {
        s_opts     = WebPSaveOptions{};
        first_time = false;
    }

    using ImGui::BeginGroup;
    using ImGui::Checkbox;
    using ImGui::Combo;
    using ImGui::EndGroup;
    using ImGui::IsItemDeactivatedAfterEdit;
    using ImGui::RadioButton;
    using ImGui::SameLine;
    using ImGui::SetNextItemWidth;
    using ImGui::SliderFloat;

    bool  modified = false;
    float pad      = ImGui::GetStyle().FramePadding.x;

    // Gain/exposure adjustment
    SetNextItemWidth(ImGui::CalcItemWidth() - ImGui::CalcTextSize("Gain:").x - 3 * pad);
    modified |= ImGui::InputFloat("Gain:", &s_opts.gain, 0.0f, 0.0f, "%.6g");
    ImGui::SetItemTooltip("Multiplication factor to apply to pixel values before quantization");

    // Quality slider (only for lossy mode)
    if (!s_opts.lossless)
    {
        SetNextItemWidth(ImGui::CalcItemWidth() - ImGui::CalcTextSize("Quality:").x - 3 * pad);
        modified |= SliderFloat("Quality:", &s_opts.quality, 0.f, 100.f, "%.1f");
        ImGui::SetItemTooltip("Quality factor (0=smallest file, 100=best quality)");
    }

    // Lossless checkbox
    modified |= Checkbox("Lossless", &s_opts.lossless);
    ImGui::SetItemTooltip("Use lossless compression (larger files but no quality loss)");

    // Transfer function selection
    static const char            *tf_items[] = {"Linear", "sRGB"};
    static const TransferFunction tfs[]      = {{TransferFunction::Linear, 1.f}, TransferFunction::sRGB};
    int                           tf_current = s_opts.tf.type == TransferFunction::Linear ? 0 : 1;
    SetNextItemWidth(ImGui::CalcItemWidth() - ImGui::CalcTextSize("Transfer function:").x - 3 * pad);
    if (Combo("Transfer function:", &tf_current, tf_items, IM_ARRAYSIZE(tf_items)))
    {
        s_opts.tf = tfs[tf_current];
        modified  = true;
    }
    ImGui::SetItemTooltip("Transfer function to apply to pixel values");

    return modified ? &s_opts : nullptr;
}

void save_webp_image(const Image &img, std::ostream &os, std::string_view filename, const WebPSaveOptions *opts)
{
    if (!opts)
        throw invalid_argument{"WebP save options cannot be null"};
    save_webp_image(img, os, filename, opts->gain, opts->quality, opts->lossless, opts->tf);
}

#endif
