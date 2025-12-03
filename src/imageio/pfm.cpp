//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "pfm.h"
#include "colorspace.h"
#include "image.h"
#include "timer.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fmt/core.h>
#include <iostream>
#include <stdexcept>

#include "app.h"

#include "fonts.h"
#include "imgui.h"
#include "imgui_ext.h"

using namespace std;

struct PFMSaveOptions
{
    float             gain  = 1.f;
    TransferFunction_ tf    = TransferFunction_Linear;
    float             gamma = 1.f;
};

static PFMSaveOptions s_opts;

namespace
{

float reinterpret_as_host_endian(float f, bool big_endian)
{
    static_assert(sizeof(float) == sizeof(unsigned int), "Sizes must match");

    const auto *byte = (const unsigned char *)&f;
    uint32_t    i;
    if (big_endian)
        i = (byte[3] << 0) | (byte[2] << 8) | (byte[1] << 16) | (byte[0] << 24);
    else
        i = (byte[0] << 0) | (byte[1] << 8) | (byte[2] << 16) | (byte[3] << 24);

    float ret;
    memcpy(&ret, &i, sizeof(float));
    return ret;
}

} // end namespace

bool is_pfm_image(istream &is) noexcept
{
    if (!is.good())
        return false;

    auto start = is.tellg();
    bool ret   = false;

    try
    {
        string magic;
        int    width, height;
        float  scale;

        is >> magic >> width >> height >> scale;

        ret = is.good() && (magic == "Pf" || magic == "PF" || magic == "PF4") && width > 0 && height > 0 &&
              isfinite(scale) && scale != 0;
    }
    catch (...)
    {
    }

    // rewind
    is.clear();
    is.seekg(start);
    return ret;
}

unique_ptr<float[]> load_pfm_image(istream &is, string_view filename, int *width, int *height, int *num_channels)
{
    try
    {
        Timer  timer;
        string magic;
        float  scale;

        is >> magic >> *width >> *height >> scale;

        if (magic == "Pf")
            *num_channels = 1;
        else if (magic == "PF")
            *num_channels = 3;
        else if (magic == "PF4")
            *num_channels = 4;
        else
            throw invalid_argument(
                fmt::format("load_pfm_image: Could not deduce number of channels from PFM magic string {}", magic));

        if (*width <= 0 || *height <= 0)
            throw invalid_argument(
                fmt::format("load_pfm_image: Invalid image width ({}) or height ({})", *width, *height));

        if (!isfinite(scale) || scale == 0)
            throw invalid_argument(fmt::format("load_pfm_image: Invalid PFM scale {}", scale));

        bool big_endian = scale > 0.f;
        scale           = fabsf(scale);

        size_t                   num_floats = static_cast<size_t>((*width) * (*height) * (*num_channels));
        auto                     num_bytes  = num_floats * sizeof(float);
        std::unique_ptr<float[]> data(new float[num_floats]);

        // skip last newline at the end of the header.
        char c;
        while (is.get(c) && c != '\r' && c != '\n');

        // Read the rest of the file
        is.read(reinterpret_cast<char *>(data.get()), num_bytes);
        if (is.gcount() < (streamsize)num_bytes)
            throw invalid_argument{
                fmt::format("load_pfm_image: Expected {} bytes, but could only read {} bytes", is.gcount(), num_bytes)};

        // multiply data by scale factor
        for (size_t i = 0; i < num_floats; ++i) data[i] = scale * reinterpret_as_host_endian(data[i], big_endian);

        spdlog::debug("Reading PFM image '{}' took: {} seconds.", filename, (timer.elapsed() / 1000.f));

        return data;
    }
    catch (const exception &e)
    {
        throw invalid_argument{fmt::format("{} in file '{}'", e.what(), filename)};
    }
}

vector<ImagePtr> load_pfm_image(std::istream &is, std::string_view filename, const ImageLoadOptions &opts)
{
    ScopedMDC mdc{"IO", "PFM"};
    int3      size;
    auto      float_data                 = load_pfm_image(is, filename, &size.x, &size.y, &size.z);
    auto      image                      = make_shared<Image>(size.xy(), size.z);
    image->filename                      = filename;
    image->metadata["pixel format"]      = fmt::format("{}-bit (32-bit float per channel)", size.z * 32);
    image->metadata["transfer function"] = transfer_function_name(TransferFunction_Linear);

    to_linear(float_data.get(), size, opts.tf, opts.gamma);

    Timer timer;
    for (int c = 0; c < size.z; ++c)
        image->channels[c].copy_from_interleaved(float_data.get(), size.x, size.y, size.z, c,
                                                 [](float v) { return v; });

    image->metadata["transfer function"] = transfer_function_name(opts.tf, 1.f / opts.gamma);

    spdlog::debug("Copying image data for took: {} seconds.", (timer.elapsed() / 1000.f));
    return {image};
}

void write_pfm_image(ostream &os, string_view filename, int width, int height, int num_channels, const float data[])
{
    if (!os)
        throw invalid_argument(fmt::format("write_pfm_image: Error opening file '{}'", filename));

    string magic;

    if (num_channels == 1)
        magic = "Pf";
    else if (num_channels == 3)
        magic = "PF";
    else if (num_channels == 4)
        magic = "PF4";
    else
        throw invalid_argument(fmt::format("write_pfm_image: Unsupported number of channels {} when writing file "
                                           "\"{}\". PFM format only supports 1, 3, or 4 channels.",
                                           num_channels, filename));
    os << magic << "\n";
    os << width << " " << height << "\n";

    // determine system endianness
    bool little_endian = false;
    {
        constexpr int n = 1;
        // little endian if true
        if (*(char *)&n == 1)
            little_endian = true;
    }

    os << (little_endian ? "-1.0000000\n" : "1.0000000\n");

    os.write((const char *)data, width * height * sizeof(float) * num_channels);
}

void save_pfm_image(const Image &img, ostream &os, string_view filename, float gain, TransferFunction_ tf, float gamma)
{
    Timer timer;
    int   w = 0, h = 0, n = 0;
    auto  pixels = img.as_interleaved<float>(&w, &h, &n, gain, tf, gamma);
    write_pfm_image(os, filename, w, h, n, pixels.get());
    spdlog::info("Saved PFM image to \"{}\" in {} seconds.", filename, (timer.elapsed() / 1000.f));
}

void save_pfm_image(const Image &img, ostream &os, string_view filename, const PFMSaveOptions *opts)
{
    if (!opts)
        throw std::invalid_argument("PFMSaveOptions pointer is null");

    Timer timer;
    // get interleaved LDR pixel data
    int  w = 0, h = 0, n = 0;
    auto pixels = img.as_interleaved<float>(&w, &h, &n, opts->gain, opts->tf, opts->gamma);
    write_pfm_image(os, filename, w, h, n, pixels.get());
    spdlog::info("Saved PFM image to \"{}\" in {} seconds.", filename, (timer.elapsed() / 1000.f));
}

// GUI parameter function
PFMSaveOptions *pfm_parameters_gui()
{
    if (ImGui::PE::Begin("PFM Save Options", ImGuiTableFlags_Resizable))
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
            "Encode the pixel values using this transfer function.\nWARNING: values in a PFM "
            "file are typically assumed linear, and there is no way to signal in the file "
            "that the values are encoded with a different transfer function.");

        if (s_opts.tf == TransferFunction_Gamma)
            ImGui::PE::SliderFloat("Gamma", &s_opts.gamma, 0.1f, 5.f, "%.3f", 0,
                                   "When using a gamma transfer function, this is the gamma value to use.");
        ImGui::PE::End();
    }

    if (ImGui::Button("Reset options to defaults"))
        s_opts = PFMSaveOptions{};

    return &s_opts;
}
