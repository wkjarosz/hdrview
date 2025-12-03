//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "app.h"
#include "hello_imgui/dpi_aware.h"
#include "imgui.h"
#include "imgui_ext.h"

#include "fonts.h"

#define QOI_NO_STDIO
#define QOI_IMPLEMENTATION
#include "qoi.h"
#include <qoi.h>

#include "colorspace.h"
#include "image.h"
#include "timer.h"
#include <ImfHeader.h>
#include <ImfStandardAttributes.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fmt/core.h>
#include <iostream>
#include <stdexcept>

using namespace std;

struct QOISaveOptions
{
    float             gain   = 1.f;
    TransferFunction_ tf     = TransferFunction_sRGB;
    float             gamma  = 1.f;
    bool              dither = true; // only used for LDR formats
};

static QOISaveOptions s_opts{};

bool is_qoi_image(istream &is) noexcept
{
    bool ret = false;
    try
    {
        char magic[4];
        is.read(magic, sizeof(magic));
        ret = !!is && is.gcount() == sizeof(magic) && string(magic, sizeof(magic)) == "qoif";
    }
    catch (...)
    {
        //
    }

    is.clear();
    is.seekg(0);
    return ret;
}

vector<ImagePtr> load_qoi_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    ScopedMDC mdc{"IO", "QOI"};
    if (!is_qoi_image(is))
        throw invalid_argument{"Invalid magic string"};

    // calculate size of stream
    is.clear();
    is.seekg(0, is.end);
    size_t raw_size = is.tellg();
    is.seekg(0, is.beg);

    // read in the whole stream
    vector<char> raw_data(raw_size);
    is.read(raw_data.data(), raw_size);
    if ((size_t)is.gcount() != raw_size)
        throw invalid_argument{
            fmt::format("Failed to read : {} bytes, read : {} bytes", raw_size, (size_t)is.gcount())};

    qoi_desc                                     desc;
    std::unique_ptr<void, decltype(std::free) *> decoded_data{
        qoi_decode(raw_data.data(), static_cast<int>(raw_size), &desc, 0), std::free};
    if (!decoded_data.get())
        throw invalid_argument{"Failed to decode data from the QOI format."};

    int3 size{static_cast<int>(desc.width), static_cast<int>(desc.height), static_cast<int>(desc.channels)};
    if (product(size) == 0)
        throw invalid_argument{"Image has zero pixels."};

    TransferFunction_ tf = desc.colorspace == QOI_LINEAR ? TransferFunction_Linear : TransferFunction_sRGB;
    if (opts.tf != TransferFunction_Unspecified)
    {
        spdlog::info("This is a {} QOI file, but we are forcing transfer function to {}.", transfer_function_name(tf),
                     transfer_function_name(opts.tf, 1.f / opts.gamma));
        tf = opts.tf;
    }

    auto image                           = make_shared<Image>(size.xy(), size.z);
    image->filename                      = filename;
    image->file_has_straight_alpha       = size.z > 3;
    image->metadata["loader"]            = "qoi";
    image->metadata["pixel format"]      = fmt::format("{}-bit (8 bpc)", size.z * 8);
    image->metadata["transfer function"] = transfer_function_name(tf);

    Timer timer;
    // first convert/copy to float channels
    for (int c = 0; c < size.z; ++c)
        image->channels[c].copy_from_interleaved(reinterpret_cast<uint8_t *>(decoded_data.get()), size.x, size.y,
                                                 size.z, c, [](uint8_t v) { return dequantize_full(v); });
    // then apply transfer function
    if (opts.tf != TransferFunction_Linear)
    {
        int num_color_channels = size.z >= 3 ? 3 : 1;
        to_linear(image->channels[0].data(), size.z > 1 ? image->channels[1].data() : nullptr,
                  size.z > 2 ? image->channels[2].data() : nullptr, size.x * size.y, num_color_channels, tf, opts.gamma,
                  1);
    }

    spdlog::debug("Copying image channels took: {} seconds.", (timer.elapsed() / 1000.f));

    return {image};
}

void save_qoi_image(const Image &img, ostream &os, string_view filename, const QOISaveOptions *opts)
{
    if (!opts)
        throw std::invalid_argument("QOISaveOptions pointer is null.");
    Timer timer;
    // get interleaved LDR pixel data
    int  w = 0, h = 0, n = 0;
    auto pixels = img.as_interleaved<uint8_t>(
        &w, &h, &n, opts->gain, opts->tf == TransferFunction_sRGB ? TransferFunction_sRGB : TransferFunction_Linear,
        2.2f, opts->dither);

    // The QOI image format only supports RGB or RGBA data.
    if (n != 4 && n != 3)
        throw invalid_argument{
            fmt::format("Invalid number of channels {}. QOI format expects either 3 or 4 channels.", n)};

    // write the data
    const qoi_desc desc{
        static_cast<unsigned int>(w),                                                          // width
        static_cast<unsigned int>(h),                                                          // height
        static_cast<unsigned char>(n),                                                         // number of channels
        static_cast<unsigned char>(opts->tf == TransferFunction_sRGB ? QOI_SRGB : QOI_LINEAR), // colorspace
    };
    int encoded_size = 0;

    spdlog::info("Saving {}-channel, {}x{} pixels {} QOI image.", n, w, h,
                 opts->tf == TransferFunction_sRGB ? "sRGB" : "linear");
    std::unique_ptr<void, decltype(std::free) *> encoded_data{qoi_encode(pixels.get(), &desc, &encoded_size),
                                                              std::free};

    if (!encoded_data.get())
        throw invalid_argument{"Failed to encode data into the QOI format."};

    os.write(reinterpret_cast<char *>(encoded_data.get()), encoded_size);
    spdlog::info("Saved QOI image to \"{}\" in {} seconds.", filename, (timer.elapsed() / 1000.f));
}

void save_qoi_image(const Image &img, ostream &os, string_view filename, float gain, bool sRGB, bool dither)
{
    QOISaveOptions opts{gain, sRGB ? TransferFunction_sRGB : TransferFunction_Linear, 2.2f, dither};
    save_qoi_image(img, os, filename, &opts);
}

// GUI parameter function
QOISaveOptions *qoi_parameters_gui()
{
    if (ImGui::PE::Begin("QOI Save Options", ImGuiTableFlags_Resizable))
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
                if (ImGui::BeginCombo("##Transfer function",
                                      transfer_function_name(s_opts.tf, 1.f / s_opts.gamma).c_str()))
                {
                    for (int i = TransferFunction_Linear; i <= TransferFunction_DCI_P3; ++i)
                    {
                        bool is_selected = (s_opts.tf == (TransferFunction_)i);
                        if (ImGui::Selectable(transfer_function_name((TransferFunction_)i, 1.f / s_opts.gamma).c_str(),
                                              is_selected))
                            s_opts.tf = (TransferFunction_)i;
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                return true;
            },
            "Encode the pixel values using this transfer function.\nWARNING: The QOI image format header can only "
            "indicate sRGB or Linear transfer functions. If you choose a different transfer function, we will store "
            "Linear in the QOI header, and the file will likely not be displayed correctly by other software.");

        if (s_opts.tf == TransferFunction_Gamma)
            ImGui::PE::SliderFloat("Gamma", &s_opts.gamma, 0.1f, 5.f, "%.3f", 0,
                                   "When using a gamma transfer function, this is the gamma value to use.");

        ImGui::PE::Checkbox("Dither", &s_opts.dither);

        ImGui::PE::End();
    }

    if (ImGui::Button("Reset options to defaults"))
        s_opts = QOISaveOptions{};

    return &s_opts;
}
