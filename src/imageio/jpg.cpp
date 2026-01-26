//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "jpg.h"
#include "app.h"
#include "colorspace.h"
#include "exif.h"
#include "icc.h"
#include "image.h"
#include <algorithm>
#include <iostream>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ranges.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "fonts.h"
#include "imgui_ext.h"

using namespace std;

struct JPGSaveOptions
{
    float gain        = 1.f;
    int   tf          = 1; // Linear = 0; sRGB = 1
    bool  dither      = true;
    int   quality     = 95;
    bool  progressive = false;
};

static JPGSaveOptions s_opts;

#if !HDRVIEW_ENABLE_LIBJPEG

#include "stb.h"

// Return JSON describing libjpeg availability and features (disabled stub)
json get_jpg_info() { return json{{"name", "libjpeg"}}; }

bool is_jpg_image(istream &is) noexcept { return false; }

std::vector<ImagePtr> load_jpg_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts)
{
    throw runtime_error("Turbo JPEG support not enabled in this build.");
}

void save_jpg_image(const Image &img, std::ostream &os, std::string_view filename, float gain, bool sRGB, bool dither,
                    int quality, bool progressive)
{
    return save_stb_jpg(img, os, filename, gain, sRGB ? TransferFunction::sRGB : TransferFunction::Linear, dither,
                        quality);
}

JPGSaveOptions *jpg_parameters_gui() { return &s_opts; }

void save_jpg_image(const Image &img, std::ostream &os, std::string_view filename, const JPGSaveOptions *params)
{
    throw runtime_error("Turbo JPEG support not enabled in this build.");
}

#else

#include <jerror.h>
#include <jpeglib.h>

// Return JSON describing libjpeg availability and features
json get_jpg_info()
{
    json j;
    j["enabled"] = true;
    j["name"]    = "libjpeg";
#ifdef LIBJPEG_TURBO_VERSION
#define LIBJPEG_STR_HELPER(x) #x
#define LIBJPEG_STR(x)        LIBJPEG_STR_HELPER(x)
    j["version"] = fmt::format("{} (turbo)", LIBJPEG_STR(LIBJPEG_TURBO_VERSION));
#undef LIBJPEG_STR
#undef LIBJPEG_STR_HELPER
#else
    j["version"] = fmt::format("{}", JPEG_LIB_VERSION);
#endif

    json features = json::object();
    // library provides decode and encode functionality
    features["decoder"] = true;
    features["encoder"] = true;
#ifdef LIBJPEG_TURBO_VERSION
    features["turbo"] = true;
#else
    features["turbo"] = false;
#endif

    j["features"] = features;
    return j;
}

bool is_jpg_image(std::istream &is) noexcept
{
    try
    {
        unsigned char magic[2];
        is.read(reinterpret_cast<char *>(magic), 2);
        is.clear();
        is.seekg(0);
        return magic[0] == 0xFF && magic[1] == 0xD8;
    }
    catch (...)
    {
        is.clear();
        is.seekg(0);
        return false;
    }
}

std::vector<ImagePtr> load_jpg_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts)
{
    ScopedMDC mdc{"IO", "JPG"};

    struct jpeg_stream_source_mgr : public jpeg_source_mgr
    {
        std::istream       *is;
        std::vector<JOCTET> buffer;
        jpeg_stream_source_mgr(std::istream &input, size_t bufsize = 4096) : is(&input), buffer(bufsize)
        {
            init_source       = [](j_decompress_ptr) {};
            fill_input_buffer = [](j_decompress_ptr cinfo) -> boolean
            {
                auto *src = static_cast<jpeg_stream_source_mgr *>(cinfo->src);
                src->is->read(reinterpret_cast<char *>(src->buffer.data()), src->buffer.size());
                size_t n = src->is->gcount();
                if (n == 0)
                {
                    src->buffer[0]       = (JOCTET)0xFF;
                    src->buffer[1]       = (JOCTET)JPEG_EOI;
                    src->next_input_byte = src->buffer.data();
                    src->bytes_in_buffer = 2;
                    return TRUE;
                }
                src->next_input_byte = src->buffer.data();
                src->bytes_in_buffer = n;
                return TRUE;
            };
            skip_input_data = [](j_decompress_ptr cinfo, long num_bytes)
            {
                auto *src = static_cast<jpeg_stream_source_mgr *>(cinfo->src);
                if (num_bytes > 0)
                {
                    while (num_bytes > (long)src->bytes_in_buffer)
                    {
                        num_bytes -= (long)src->bytes_in_buffer;
                        src->fill_input_buffer(cinfo);
                    }
                    src->next_input_byte += num_bytes;
                    src->bytes_in_buffer -= num_bytes;
                }
            };
            resync_to_restart = jpeg_resync_to_restart;
            term_source       = [](j_decompress_ptr) {};
            next_input_byte   = buffer.data();
            bytes_in_buffer   = 0;
        }
    };

    jpeg_stream_source_mgr src_mgr(is);
    jpeg_decompress_struct cinfo;
    jpeg_error_mgr         jerr;

    cinfo.err       = jpeg_std_error(&jerr);
    jerr.error_exit = [](j_common_ptr cinfo)
    {
        char buffer[JMSG_LENGTH_MAX];
        (*cinfo->err->format_message)(cinfo, buffer);
        throw std::invalid_argument{buffer};
    };

    jpeg_create_decompress(&cinfo);
    auto cinfo_deleter = [](jpeg_decompress_struct *cinfo) { jpeg_destroy_decompress(cinfo); };
    std::unique_ptr<jpeg_decompress_struct, decltype(cinfo_deleter)> cinfo_guard(&cinfo, cinfo_deleter);

    cinfo.src = reinterpret_cast<jpeg_source_mgr *>(&src_mgr);

    try
    {
        jpeg_save_markers(&cinfo, JPEG_APP0 + 1, 0xFFFF); // EXIF, XMP
        jpeg_save_markers(&cinfo, JPEG_APP0 + 2, 0xFFFF); // ICC, ISO
        jpeg_save_markers(&cinfo, JPEG_COM, 0xFFFF);      // comment marker

        if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK)
            throw std::invalid_argument{"Failed to read JPEG header."};

        jpeg_start_decompress(&cinfo);
        int3 size{(int)cinfo.output_width, (int)cinfo.output_height, (int)cinfo.output_components};
        auto image                = make_shared<Image>(size.xy(), size.z);
        image->filename           = filename;
        image->metadata["loader"] = "libjpeg-turbo";
        auto color_space_name     = [](J_COLOR_SPACE cp)
        {
            switch (cp)
            {
            case JCS_GRAYSCALE: return "Grayscale";
            case JCS_RGB: return "RGB";
            case JCS_YCbCr: return "YCbCr";
            case JCS_CMYK: return "CMYK";
            case JCS_YCCK: return "YCCK";
            case JCS_EXT_RGB: return "Extended RGB";
            case JCS_EXT_RGBX: return "Extended RGBX";
            case JCS_EXT_BGR: return "Extended BGR";
            case JCS_EXT_BGRX: return "Extended BGRX";
            case JCS_EXT_XBGR: return "Extended XBGR";
            case JCS_EXT_XRGB: return "Extended XRGB";
            case JCS_EXT_RGBA: return "Extended RGBA";
            case JCS_EXT_BGRA: return "Extended BGRA";
            case JCS_EXT_ABGR: return "Extended ABGR";
            case JCS_EXT_ARGB: return "Extended ARGB";
            case JCS_RGB565: return "RGB565";
            case JCS_UNKNOWN: [[fallthrough]];
            default: return "Unknown";
            }
        };
        image->metadata["pixel format"] =
            fmt::format("{} ({} channel{}, {} bpc)", color_space_name(cinfo.jpeg_color_space), cinfo.num_components,
                        cinfo.num_components > 1 ? "s" : "", cinfo.data_precision);
#if JPEG_LIB_VERSION >= 80
        image->metadata["header"]["Is baseline"] = {
            {"value", cinfo.is_baseline}, {"string", cinfo.is_baseline ? "yes" : "no"}, {"type", "bool"}};
#endif
        image->metadata["header"]["Is progressive"] = {
            {"value", cinfo.progressive_mode}, {"string", cinfo.progressive_mode ? "yes" : "no"}, {"type", "bool"}};

        image->metadata["header"]["Coding method"] = {
            {"value", cinfo.arith_code}, {"string", cinfo.arith_code ? "Arithmetic" : "Huffman"}, {"type", "bool"}};

        // image->metadata["header"]["Has JFIF marker"] = {
        //     {"value", cinfo.saw_JFIF_marker != 0}, {"string", cinfo.saw_JFIF_marker ? "yes" : "no"}, {"type",
        //     "bool"}};
        if (cinfo.saw_JFIF_marker)
        {
            image->metadata["header"]["JFIF version"] = {
                {"value", 100 * cinfo.JFIF_major_version + cinfo.JFIF_minor_version},
                {"type", "float"},
                {"string", fmt::format("{}.{}", cinfo.JFIF_major_version, cinfo.JFIF_minor_version)}};
            auto units = cinfo.density_unit == 1 ? " pixels/inch" : cinfo.density_unit == 2 ? " pixels/cm" : "";
            if (cinfo.density_unit == 0)
                image->metadata["header"]["Pixel aspect ratio"] = {
                    {"value", {cinfo.X_density, cinfo.Y_density}},
                    {"string", fmt::format("{} x {}", cinfo.X_density, cinfo.Y_density)},
                    {"type", "array"}};
            else
                image->metadata["header"]["Pixel density"] = {
                    {"value", {cinfo.X_density, cinfo.Y_density}},
                    {"string", fmt::format("{}{} x {}{}", cinfo.X_density, units, cinfo.Y_density, units)},
                    {"type", "array"}};
            // image->metadata["header"]["Has Adobe marker"] = {{"value", cinfo.saw_Adobe_marker != 0},
            //                                                  {"string", cinfo.saw_Adobe_marker ? "yes" : "no"},
            //                                                  {"type", "bool"}};
            if (cinfo.saw_Adobe_marker)
                image->metadata["header"]["Adobe transform"] = {{"value", cinfo.Adobe_transform},
                                                                {"string", cinfo.Adobe_transform == 1 ? "YCbCr"
                                                                           : cinfo.Adobe_transform == 2
                                                                               ? "YCCK"
                                                                               : "Unknown (RGB or CMYK)"},
                                                                {"type", "uint8"}};
        }
        // APP1 (EXIF and XMP) and comment extraction
        for (jpeg_saved_marker_ptr marker = cinfo.marker_list; marker; marker = marker->next)
        {
            static constexpr char   exif_hdr[]  = "Exif\0";
            static constexpr size_t exif_hdr_sz = std::size(exif_hdr);
            static constexpr char   xmp_hdr[]   = "http://ns.adobe.com/xap/1.0/";
            static constexpr size_t xmp_hdr_sz  = std::size(xmp_hdr);

            if (marker->marker == JPEG_APP0 + 1 && marker->data_length > exif_hdr_sz &&
                std::equal(marker->data, marker->data + exif_hdr_sz, exif_hdr))
            {
                try
                {
                    image->exif             = Exif{marker->data + exif_hdr_sz, marker->data_length - exif_hdr_sz};
                    image->metadata["exif"] = image->exif.to_json();
                    spdlog::debug("EXIF metadata successfully parsed: {}", image->metadata["exif"].dump(2));
                }
                catch (const std::exception &e)
                {
                    spdlog::warn("Exception while parsing EXIF chunk: {}", e.what());
                    image->exif.reset();
                }
            }
            else if (marker->marker == JPEG_APP0 + 1 && marker->data_length > xmp_hdr_sz &&
                     std::equal(marker->data, marker->data + xmp_hdr_sz, xmp_hdr))
            {
                image->xmp_data.assign(marker->data + xmp_hdr_sz, marker->data + marker->data_length);
                spdlog::debug("XMP metadata present ({} bytes)", image->xmp_data.size());
            }
            else if (marker->marker == JPEG_COM)
            {
                std::string_view data((const char *)marker->data, marker->data_length);
                // Additional string metadata can be stored in JPEG files as
                // comment markers in the form "key:value" or "ident:key:value".
                spdlog::warn("JPEG comment marker: {}", data);

                if (auto parts = split(data, ":"); parts.size() >= 2)
                {
                    std::string combined = "";
                    for (size_t i = 0; i < parts.size() - 1; ++i)
                    {
                        if (i > 0)
                            combined += ":";
                        combined += std::string(parts[i]);
                    }
                    image->metadata["header"][combined] = {
                        {"value", parts.back()}, {"string", parts.back()}, {"type", "string"}};
                }
                else
                    // If we made it this far, treat the comment as a description
                    image->metadata["header"]["ImageDescription"] = {
                        {"value", data}, {"string", data}, {"type", "string"}};
            }
        }

        // ICC profile extraction
        {
            unsigned char *icc_data = nullptr;
            unsigned int   icc_len  = 0;
            if (jpeg_read_icc_profile(&cinfo, &icc_data, &icc_len))
            {
                spdlog::debug("Read in ICC profile from JPEG.");
                image->icc_data.assign(icc_data, icc_data + icc_len);
                free(icc_data);
            }
        }

        std::vector<uint8_t> row_buffer(size.x * size.z);
        std::vector<float>   float_pixels(size.x * size.y * size.z);
        for (int y = 0; y < size.y; ++y)
        {
            JSAMPROW row_pointer = row_buffer.data();
            jpeg_read_scanlines(&cinfo, &row_pointer, 1);

            for (int x = 0; x < size.x; ++x)
                for (int c = 0; c < size.z; ++c)
                    float_pixels[(y * size.x + x) * size.z + c] = dequantize_full(row_buffer[x * size.z + c]);
        }
        jpeg_finish_decompress(&cinfo);

        if (opts.override_profile)
        {
            spdlog::info("Ignoring embedded color profile with user-specified profile: {} {}",
                         color_gamut_name(opts.gamut_override), transfer_function_name(opts.tf_override));

            string         profile_desc = color_profile_name(ColorGamut_Unspecified, TransferFunction::Unspecified);
            Chromaticities chr;
            if (linearize_pixels(float_pixels.data(), size, gamut_chromaticities(opts.gamut_override), opts.tf_override,
                                 opts.keep_primaries, &profile_desc, &chr))
            {
                image->chromaticities = chr;
                profile_desc += " (override)";
            }
            image->metadata["color profile"] = profile_desc;
        }
        else
        {
            string profile_desc = color_profile_name(ColorGamut_Unspecified, TransferFunction::Unspecified);
            // ICC profile linearization
            if (!image->icc_data.empty())
            {
                Chromaticities chr;
                if (ICCProfile(image->icc_data)
                        .linearize_pixels(float_pixels.data(), size, opts.keep_primaries, &profile_desc, &chr))
                {
                    spdlog::info("Linearizing colors using ICC profile.");
                    image->chromaticities = chr;
                }
                else
                    // If ICC profile fails, assume sRGB transfer function
                    to_linear(float_pixels.data(), size, TransferFunction::sRGB);
            }
            else
                // If no ICC profile, assume sRGB transfer function
                to_linear(float_pixels.data(), size, TransferFunction::sRGB);

            image->metadata["color profile"] = profile_desc;
        }

        for (int c = 0; c < size.z; ++c)
            image->channels[c].copy_from_interleaved(float_pixels.data(), size.x, size.y, size.z, c,
                                                     [](float v) { return v; });

        return {image};
    }
    catch (const std::exception &e)
    {
        // jpeg_destroy_decompress will be called by unique_ptr deleter
        throw std::runtime_error(fmt::format("Error during decompression: {}", e.what()));
    }
}

void save_jpg_image(const Image &img, std::ostream &os, std::string_view /*filename*/, float gain, bool sRGB,
                    bool dither, int quality, bool progressive)
{
    // get interleaved LDR pixel data
    int  w = 0, h = 0, n = 0;
    auto pixels =
        img.as_interleaved<uint8_t>(&w, &h, &n, gain, sRGB ? TransferFunction::sRGB : TransferFunction::Linear, dither);
    // Validation: ensure we actually have pixel data / valid dimensions
    if (!pixels || w <= 0 || h <= 0)
        throw runtime_error("JPEG: empty image or invalid image dimensions");

    if (n > 3)
    {
        // Remove alpha channel: convert RGBA to RGB in-place
        std::unique_ptr<uint8_t[]> rgb_pixels(new uint8_t[w * h * 3]);
        for (int i = 0, j = 0; i < w * h; ++i, j += n)
        {
            rgb_pixels[i * 3 + 0] = pixels[j + 0];
            rgb_pixels[i * 3 + 1] = pixels[j + 1];
            rgb_pixels[i * 3 + 2] = pixels[j + 2];
        }
        pixels.swap(rgb_pixels);
        n = 3;
    }

    jpeg_compress_struct cinfo;
    jpeg_error_mgr       jerr;
    cinfo.err = jpeg_std_error(&jerr);

    auto cinfo_deleter = [](jpeg_compress_struct *cinfo) { jpeg_destroy_compress(cinfo); };
    std::unique_ptr<jpeg_compress_struct, decltype(cinfo_deleter)> cinfo_guard(&cinfo, cinfo_deleter);

    jpeg_create_compress(&cinfo);

    struct ostream_dest_mgr : public jpeg_destination_mgr
    {
        std::ostream       *os;
        std::vector<JOCTET> buffer;
        ostream_dest_mgr(std::ostream &out, size_t bufsize = 4096) : os(&out), buffer(bufsize)
        {
            init_destination = [](j_compress_ptr cinfo)
            {
                auto *dest             = static_cast<ostream_dest_mgr *>(cinfo->dest);
                dest->next_output_byte = dest->buffer.data();
                dest->free_in_buffer   = dest->buffer.size();
            };
            empty_output_buffer = [](j_compress_ptr cinfo) -> boolean
            {
                auto *dest = static_cast<ostream_dest_mgr *>(cinfo->dest);
                dest->os->write(reinterpret_cast<char *>(dest->buffer.data()), dest->buffer.size());
                dest->next_output_byte = dest->buffer.data();
                dest->free_in_buffer   = dest->buffer.size();
                return TRUE;
            };
            term_destination = [](j_compress_ptr cinfo)
            {
                auto  *dest      = static_cast<ostream_dest_mgr *>(cinfo->dest);
                size_t datacount = dest->buffer.size() - dest->free_in_buffer;
                if (datacount > 0)
                    dest->os->write(reinterpret_cast<char *>(dest->buffer.data()), datacount);
            };
        }
    } dest_mgr(os);
    cinfo.dest = reinterpret_cast<jpeg_destination_mgr *>(&dest_mgr);

    cinfo.image_width      = w;
    cinfo.image_height     = h;
    cinfo.input_components = n;
    cinfo.in_color_space   = (img.channels.size() == 1) ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    if (progressive)
        jpeg_simple_progression(&cinfo);

    jpeg_start_compress(&cinfo, TRUE);
    // write scanlines one row at a time with a JSAMPROW pointer to each row
    const size_t row_stride = size_t(w) * size_t(n); // bytes per row
    for (int y = 0; y < h; ++y)
    {
        JSAMPROW row_pointer = pixels.get() + size_t(y) * row_stride;
        jpeg_write_scanlines(&cinfo, &row_pointer, 1);
    }
    jpeg_finish_compress(&cinfo);
}

JPGSaveOptions *jpg_parameters_gui()
{
    if (ImGui::PE::Begin("libJPEG Save Options",
                         ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize))
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

        ImGui::PE::Combo("Transfer function", &s_opts.tf, "Linear\0sRGB IEC61966-2.1\0");

        ImGui::PE::Checkbox("Dither", &s_opts.dither);

        ImGui::PE::SliderInt("Quality", &s_opts.quality, 1, 100);
        ImGui::PE::Checkbox("Progressive", &s_opts.progressive);

        ImGui::PE::End();
    }

    if (ImGui::Button("Reset options to defaults"))
        s_opts = JPGSaveOptions{};

    ImGui::Unindent();
    return &s_opts;
}

// throws on error
void save_jpg_image(const Image &img, std::ostream &os, std::string_view filename, const JPGSaveOptions *params)
{
    if (params == nullptr)
        throw std::invalid_argument("JPGSaveOptions pointer is null");

    save_jpg_image(img, os, filename, params->gain, params->tf == 1, params->dither, params->quality,
                   params->progressive);
}

#endif
