//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "stb.h"
#include "colorspace.h"
#include "hello_imgui/dpi_aware.h"
#include "image.h"
#include "timer.h"
#include <cstdint>
#include <cstring>
#include <fmt/core.h>
#include <iostream>
#include <stdexcept>

#include "app.h"

#include "fonts.h"
#include "imgui.h"
#include "imgui_ext.h"

// these pragmas ignore warnings about unused static functions
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

// since other libraries might include old versions of stb headers, we declare stb static here
// #define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION
// #undef STB_IMAGE_STATIC

// #define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#undef STB_IMAGE_WRITE_IMPLEMENTATION
// #undef STB_IMAGE_WRITE_STATIC

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

using namespace std;

struct STBSaveOptions
{
    float            gain    = 1.f;
    TransferFunction tf      = TransferFunction::sRGB;
    bool             dither  = true; // only used for LDR formats
    int              quality = 95;   // only used for jpg
};

static STBSaveOptions s_opts{};
static STBSaveOptions s_hdr_opts{1.f, {TransferFunction::Linear, 1.f}, false, 95};

static const stbi_io_callbacks stbi_callbacks = {
    // read
    [](void *user, char *data, int size)
    {
        auto stream = reinterpret_cast<istream *>(user);
        stream->read(data, size);
        return (int)stream->gcount();
    },
    // seek
    [](void *user, int size) { reinterpret_cast<istream *>(user)->seekg(size, ios_base::cur); },
    // eof
    [](void *user) { return (int)reinterpret_cast<istream *>(user)->eof(); },
};

static void ostream_write_func(void *context, void *data, int size)
{
    reinterpret_cast<std::ostream *>(context)->write(reinterpret_cast<char *>(data), size);
}

static bool supported_format(istream &is, json &j) noexcept
{
    is.clear();
    is.seekg(0);
    try
    {
        stbi__context s;
        stbi__start_callbacks(&s, (stbi_io_callbacks *)&stbi_callbacks, &is);

        // these are ordered like stbi__load_main does for speed an reliability
        if (stbi__png_test(&s))
            j["format"] = "png";
        else if (stbi__bmp_test(&s))
            j["format"] = "bmp";
        else if (stbi__gif_test(&s))
            j["format"] = "gif";
        else if (stbi__psd_test(&s))
            j["format"] = "psd";
        else if (stbi__pic_test(&s))
            j["format"] = "pic";
        else if (stbi__jpeg_test(&s))
            j["format"] = "jpeg";
        else if (stbi__pnm_test(&s))
            j["format"] = "pnm";
        else if (stbi__hdr_test(&s))
            j["format"] = "hdr";
        else if (stbi__tga_test(&s))
            j["format"] = "tga";
    }
    catch (...)
    {
        //
    }
    // shouldn't be necessary, but just in case:
    // rewind
    is.clear();
    is.seekg(0);
    return j.contains("format");
}

bool is_stb_image(istream &is) noexcept
{
    json j;
    return supported_format(is, j);
}

vector<ImagePtr> load_stb_image(istream &is, const string_view filename, const ImageLoadOptions &opts)
{
    ScopedMDC mdc{"IO", "STB"};
    // stbi doesn't do proper srgb, but uses gamma=2.2 instead, so override it.
    // we'll do our own srgb correction
    stbi_ldr_to_hdr_scale(1.0f);
    stbi_ldr_to_hdr_gamma(1.0f);

    bool is_hdr = stbi_is_hdr_from_callbacks(&stbi_callbacks, &is) != 0;
    is.clear();
    is.seekg(0);
    bool is_16_bit = stbi_is_16_bit_from_callbacks(&stbi_callbacks, &is);
    is.clear();
    is.seekg(0);

    using DataBuffer = std::unique_ptr<void, void (*)(void *)>;
    DataBuffer data{nullptr, stbi_image_free};

    int4 size{0, 0, 0, 1}; // width, height, channels, frames

    if (is_hdr)
        data = DataBuffer{(void *)stbi_loadf_from_callbacks(&stbi_callbacks, &is, &size.x, &size.y, &size.z, 0),
                          stbi_image_free};
    else
    {
        stbi__context s;
        stbi__start_callbacks(&s, (stbi_io_callbacks *)&stbi_callbacks, &is);
        bool is_gif = stbi__gif_test(&s);
        is.clear();
        is.seekg(0);

        if (is_gif)
        {
            stbi__start_callbacks(&s, (stbi_io_callbacks *)&stbi_callbacks, &is);
            int *delays; // we'll just load all frames, and ignore delays
            data = DataBuffer{stbi__load_gif_main(&s, &delays, &size.x, &size.y, &size.w, &size.z, 0), stbi_image_free};
        }
        else if (is_16_bit)
            data = DataBuffer{(void *)stbi_load_16_from_callbacks(&stbi_callbacks, &is, &size.x, &size.y, &size.z, 0),
                              stbi_image_free};
        else
            data = DataBuffer{stbi_load_from_callbacks(&stbi_callbacks, &is, &size.x, &size.y, &size.z, 0),
                              stbi_image_free};
    }

    if (!data)
        throw invalid_argument{stbi_failure_reason()};

    if (product(size) == 0)
        throw invalid_argument{"Image has zero pixels."};

    json j;
    if (!supported_format(is, j))
        throw runtime_error{"loaded the image, but then couldn't figure out the format (this should never happen)."};

    TransferFunction tf = TransferFunction::Linear;
    if (!is_hdr && opts.tf_override.type == TransferFunction::Unspecified)
    {
        spdlog::info("Assuming STB image is sRGB encoded, linearizing.");
        tf = TransferFunction::Unspecified;
    }
    if (opts.tf_override.type != TransferFunction::Unspecified)
    {
        spdlog::info("Forcing transfer function to {}.", transfer_function_name(opts.tf_override));
        tf = opts.tf_override;
    }

    Timer            timer;
    vector<ImagePtr> images(size.w);
    void            *data_ptr = (void *)data.get();
    for (int frame = 0; frame < size.w; ++frame)
    {
        auto &image = images[frame];

        image             = make_shared<Image>(size.xy(), size.z);
        image->filename   = filename;
        image->alpha_type = size.z > 3 || size.z == 2 ? AlphaType_Straight : AlphaType_None;
        if (size.w > 1)
            image->partname = fmt::format("frame {:04}", frame);
        image->metadata["loader"] = fmt::format("stb_image ({})", j["format"].get<string>());

        if (is_hdr)
            image->metadata["pixel format"] = "8:8:8:8 rgbe";
        else if (is_16_bit)
            image->metadata["pixel format"] = fmt::format("{}-bit ({} bpc)", 16 * size.z, 16);
        else
            image->metadata["pixel format"] = fmt::format("{} bbp", 8);

        image->metadata["transfer function"] = transfer_function_name(tf);
        if (opts.tf_override.type != TransferFunction::Unspecified)
            try
            {
                // some CICP transfer functions always correspond to certain primaries, try to deduce that
                image->chromaticities = chromaticities_from_cicp(transfer_function_to_cicp(opts.tf_override.type));
            }
            catch (...)
            {
                spdlog::warn("Failed to infer chromaticities from transfer function CICP value: {}",
                             int(opts.tf_override.type));
            }

        // first convert+copy to float channels
        if (is_hdr)
        {
            for (int c = 0; c < size.z; ++c)
                image->channels[c].copy_from_interleaved((float *)data_ptr, size.x, size.y, size.z, c,
                                                         [](float v) { return v; });
            data_ptr = (float *)data_ptr + size.x * size.y * size.z;
        }
        else if (is_16_bit)
        {
            for (int c = 0; c < size.z; ++c)
                image->channels[c].copy_from_interleaved((uint16_t *)data_ptr, size.x, size.y, size.z, c,
                                                         [](uint16_t v) { return dequantize_full(v); });
            data_ptr = (uint16_t *)data_ptr + size.x * size.y * size.z;
        }
        else
        {
            for (int c = 0; c < size.z; ++c)
                image->channels[c].copy_from_interleaved((uint8_t *)data_ptr, size.x, size.y, size.z, c,
                                                         [](uint8_t v) { return dequantize_full(v); });
            data_ptr = (uint8_t *)data_ptr + size.x * size.y * size.z;
        }

        // then apply transfer function
        int num_color_channels = size.z >= 3 ? 3 : 1;
        to_linear(image->channels[0].data(), size.z > 1 ? image->channels[1].data() : nullptr,
                  size.z > 2 ? image->channels[2].data() : nullptr, size.x * size.y, num_color_channels, tf, 1);
    }
    spdlog::debug("Copying image channels took: {} seconds.", (timer.elapsed() / 1000.f));

    return images;
}

void save_stb_hdr(const Image &img, std::ostream &os, const std::string_view filename, float gain, TransferFunction tf)
{
    spdlog::debug("Saving stb HDR with gain {}, tf {}, gamma {}.", gain, (int)tf.type, tf.gamma);
    Timer timer;
    int   w = 0, h = 0, n = 0;
    auto  pixels = img.as_interleaved<float>(&w, &h, &n, gain, tf, false);
    if (stbi_write_hdr_to_func(ostream_write_func, &os, w, h, n, pixels.get()) == 0)
        throw std::runtime_error("Failed to write HDR image via stb.");
    spdlog::info("Saved HDR image via stb to '{}' in {} seconds.", filename, (timer.elapsed() / 1000.f));
}

void save_stb_hdr(const Image &img, std::ostream &os, const std::string_view filename, const STBSaveOptions *opts)
{
    if (!opts)
        throw std::invalid_argument("STBSaveOptions pointer is null.");
    save_stb_hdr(img, os, filename, opts->gain, opts->tf);
}

void save_stb_jpg(const Image &img, std::ostream &os, const std::string_view filename, float gain, TransferFunction tf,
                  bool dither, float quality)
{
    spdlog::debug("Saving stb JPG with gain {}, tf {}, gamma {}, dither {}, quality {}.", gain, (int)tf.type, tf.gamma,
                  dither ? "true" : "false", quality);
    Timer timer;
    int   w = 0, h = 0, n = 0;
    auto  pixels = img.as_interleaved<uint8_t>(&w, &h, &n, gain, tf, dither);
    if (stbi_write_jpg_to_func(ostream_write_func, &os, w, h, n, pixels.get(), std::clamp(int(quality), 1, 100)) == 0)
        throw std::runtime_error("Failed to write JPG image via stb.");
    spdlog::info("Saved JPG image via stb to '{}' in {} seconds.", filename, (timer.elapsed() / 1000.f));
}

void save_stb_jpg(const Image &img, std::ostream &os, const std::string_view filename, const STBSaveOptions *opts)
{
    if (!opts)
        throw std::invalid_argument("STBSaveOptions pointer is null.");
    save_stb_jpg(img, os, filename, opts->gain, opts->tf, opts->dither, float(opts->quality));
}

void save_stb_tga(const Image &img, std::ostream &os, const std::string_view filename, float gain, TransferFunction tf,
                  bool dither)
{
    spdlog::debug("Saving stb TGA with gain {}, tf {}, gamma {}, dither {}.", gain, (int)tf.type, tf.gamma,
                  dither ? "true" : "false");
    Timer timer;
    int   w = 0, h = 0, n = 0;
    auto  pixels = img.as_interleaved<uint8_t>(&w, &h, &n, gain, tf, dither);
    if (stbi_write_tga_to_func(ostream_write_func, &os, w, h, n, pixels.get()) == 0)
        throw std::runtime_error("Failed to write TGA image via stb.");
    spdlog::info("Saved TGA image via stb to '{}' in {} seconds.", filename, (timer.elapsed() / 1000.f));
}

void save_stb_tga(const Image &img, std::ostream &os, const std::string_view filename, const STBSaveOptions *opts)
{
    if (!opts)
        throw std::invalid_argument("STBSaveOptions pointer is null.");
    save_stb_tga(img, os, filename, opts->gain, opts->tf, opts->dither);
}

void save_stb_bmp(const Image &img, std::ostream &os, const std::string_view filename, float gain, TransferFunction tf,
                  bool dither)
{
    spdlog::debug("Saving stb BMP with gain {}, tf {}, gamma {}, dither {}.", gain, (int)tf.type, tf.gamma,
                  dither ? "true" : "false");
    Timer timer;
    int   w = 0, h = 0, n = 0;
    auto  pixels = img.as_interleaved<uint8_t>(&w, &h, &n, gain, tf, dither);
    if (stbi_write_bmp_to_func(ostream_write_func, &os, w, h, n, pixels.get()) == 0)
        throw std::runtime_error("Failed to write BMP image via stb.");
    spdlog::info("Saved BMP image via stb to '{}' in {} seconds.", filename, (timer.elapsed() / 1000.f));
}

void save_stb_bmp(const Image &img, std::ostream &os, const std::string_view filename, const STBSaveOptions *opts)
{
    if (!opts)
        throw std::invalid_argument("STBSaveOptions pointer is null.");
    save_stb_bmp(img, os, filename, opts->gain, opts->tf, opts->dither);
}

void save_stb_png(const Image &img, std::ostream &os, const std::string_view filename, float gain, TransferFunction tf,
                  bool dither)
{
    spdlog::debug("Saving stb PNG with gain {}, tf {}, gamma {}, dither {}.", gain, (int)tf.type, tf.gamma,
                  dither ? "true" : "false");
    Timer timer;
    int   w = 0, h = 0, n = 0;
    auto  pixels = img.as_interleaved<uint8_t>(&w, &h, &n, gain, tf, dither);
    if (stbi_write_png_to_func(ostream_write_func, &os, w, h, n, pixels.get(), 0) == 0)
        throw std::runtime_error("Failed to write PNG image via stb.");
    spdlog::info("Saved PNG image via stb to '{}' in {} seconds.", filename, (timer.elapsed() / 1000.f));
}

void save_stb_png(const Image &img, std::ostream &os, const std::string_view filename, const STBSaveOptions *opts)
{
    if (!opts)
        throw std::invalid_argument("STBSaveOptions pointer is null.");
    save_stb_png(img, os, filename, opts->gain, opts->tf, opts->dither);
}

// void save_stb_image(const Image &img, ostream &os, const string_view filename, float gain, bool sRGB, bool dither)
// {
//     Timer  timer;
//     string extension = to_lower(get_extension(filename));

//     if (extension == ".hdr")
//         save_stb_hdr(img, os, filename);
//     else if (extension == ".jpg" || extension == ".jpeg")
//         save_stb_jpg(img, os, filename, gain, sRGB, dither, 100.0f);
//     else if (extension == ".png")
//         save_stb_png(img, os, filename, gain, sRGB, dither);
//     else if (extension == ".bmp")
//         save_stb_bmp(img, os, filename, gain, sRGB, dither);
//     else if (extension == ".tga")
//         save_stb_tga(img, os, filename, gain, sRGB, dither);
//     else
//         throw invalid_argument(fmt::format("Could not determine desired file type from extension \"{}\".",
//         extension));
// }

// GUI parameter function
STBSaveOptions *stb_parameters_gui(bool is_hdr, bool has_quality)
{
    auto &opts = is_hdr ? s_hdr_opts : s_opts;

    if (ImGui::PE::Begin("STB Save Options", ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize))
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
                if (ImGui::BeginCombo("##Transfer function", transfer_function_name(opts.tf).c_str()))
                {
                    for (int i = TransferFunction::Linear; i <= TransferFunction::DCI_P3; ++i)
                    {
                        bool is_selected = (opts.tf.type == (TransferFunction::Type_)i);
                        if (ImGui::Selectable(
                                transfer_function_name({(TransferFunction::Type_)i, opts.tf.gamma}).c_str(),
                                is_selected))
                            opts.tf.type = (TransferFunction::Type_)i;
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                return true;
            },
            "Encode the pixel values using this transfer function.\nWARNING: The STB library does not "
            "provide a way to signal what transfer function the files were saved with. Without this "
            "metadata, most software will assume LDR files are sRGB encoded, and .hdr files are linear.");

        if (opts.tf.type == TransferFunction::Gamma)
            ImGui::PE::Entry(
                "Gamma", [&] { return ImGui::SliderFloat("##Gamma", &opts.tf.gamma, 0.1f, 5.f); },
                "When using a gamma transfer function, this is the gamma value to use.");

        if (!is_hdr)
            ImGui::PE::Entry("Dither", [&] { return ImGui::Checkbox("##Dither", &opts.dither); });

        if (has_quality)
            ImGui::PE::Entry(
                "Quality", [&] { return ImGui::SliderInt("##Quality", &opts.quality, 1, 100); },
                "For JPEG images, controls the quality of the saved image (1 = worst, 100 = best).");

        ImGui::PE::End();
    }

    if (ImGui::Button("Reset options to defaults"))
        opts = is_hdr ? STBSaveOptions{1.f, {TransferFunction::Linear, 1.f}, false, 95} : STBSaveOptions{};

    return &opts;
}
