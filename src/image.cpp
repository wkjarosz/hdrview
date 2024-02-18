//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "image.h"
#include "colorspace.h"
#include "common.h"
#include "dithermatrix256.h"
#include "parallelfor.h"
#include "shader.h"
#include "texture.h"

#include <sstream>

#include <spdlog/spdlog.h>

#include <stdexcept> // for runtime_error, out_of_range

using namespace std;

//
// static methods and member definitions
//

static std::unique_ptr<Texture> s_white_texture = nullptr, s_black_texture = nullptr, s_dither_texture = nullptr;

pair<string, string> Channel::split(const string &channel)
{
    size_t dot = channel.rfind(".");
    if (dot != string::npos)
        return {channel.substr(0, dot + 1), channel.substr(dot + 1)};

    return {"", channel};
}

void Image::make_default_textures()
{
    static constexpr float s_black{0.f};
    static constexpr float s_white{1.f};
    s_black_texture  = std::make_unique<Texture>(Texture::PixelFormat::R, Texture::ComponentFormat::Float32, int2{1, 1},
                                                Texture::InterpolationMode::Nearest,
                                                Texture::InterpolationMode::Nearest, Texture::WrapMode::Repeat);
    s_white_texture  = std::make_unique<Texture>(Texture::PixelFormat::R, Texture::ComponentFormat::Float32, int2{1, 1},
                                                Texture::InterpolationMode::Nearest,
                                                Texture::InterpolationMode::Nearest, Texture::WrapMode::Repeat);
    s_dither_texture = std::make_unique<Texture>(Texture::PixelFormat::R, Texture::ComponentFormat::Float32,
                                                 int2{256, 256}, Texture::InterpolationMode::Nearest,
                                                 Texture::InterpolationMode::Nearest, Texture::WrapMode::Repeat);
    s_black_texture->upload((const uint8_t *)&s_black);
    s_white_texture->upload((const uint8_t *)&s_white);
    s_dither_texture->upload((const uint8_t *)dither_matrix256);
}

Texture *Image::black_texture() { return s_black_texture.get(); }
Texture *Image::white_texture() { return s_white_texture.get(); }
Texture *Image::dither_texture() { return s_dither_texture.get(); }

std::set<std::string> Image::loadable_formats()
{
    return {"dng", "jpg", "jpeg", "png", "bmp", "psd", "pfm", "tga", "gif", "hdr", "pic", "ppm", "pgm", "exr"};
}

std::set<std::string> Image::savable_formats()
{
    return {"bmp", "exr", "pfm", "ppm", "png", "hdr", "jpg", "jpeg", "tga"};
}

//
// end static methods
//

PixelStatistics::PixelStatistics(const Array2Df &img, float the_exposure, AxisScale_ x_scale, AxisScale_ y_scale) :
    exposure(the_exposure), minimum(std::numeric_limits<float>::infinity()),
    maximum(-std::numeric_limits<float>::infinity()), average(0.f), histogram{x_scale, y_scale}
{
    spdlog::trace("recomputing pixel statistics");
    // compute pixel summary statistics
    int    valid_pixels = 0;
    double accum        = 0.0; // reduce numerical precision issues by accumulating in double
    for (int i = 0; i < img.num_elements(); ++i)
    {
        float val = img(i);

        if (isnan(val))
            ++nan_pixels;
        else if (isinf(val))
            ++inf_pixels;
        else
        {
            ++valid_pixels;
            maximum = std::max(maximum, val);
            minimum = std::min(minimum, val);
            accum += val;
        }
    }
    average = valid_pixels ? float(accum / valid_pixels) : 0.f;
    spdlog::trace("Min: {}\nMean: {}\nMax: {}", minimum, average, maximum);
    //

    //
    // compute histograms

    bool LDR_scale = histogram.x_scale == AxisScale_Linear || histogram.x_scale == AxisScale_SRGB;

    histogram.x_limits[1] = pow(2.f, -exposure);
    if (minimum < 0.f)
        histogram.x_limits[0] = -histogram.x_limits[1];
    else
    {
        if (LDR_scale)
            histogram.x_limits[0] = 0.f;
        else
            histogram.x_limits[0] = histogram.x_limits[1] / 10000.f;
    }

    histogram.normalization[0] = axis_scale_fwd_xform(LDR_scale ? histogram.x_limits[0] : minimum, &histogram.x_scale);
    histogram.normalization[1] = axis_scale_fwd_xform(LDR_scale ? histogram.x_limits[1] : maximum, &histogram.x_scale) -
                                 histogram.normalization[0];

    // compute bin center values
    for (int i = 0; i < Histogram::NUM_BINS; ++i) histogram.xs[i] = histogram.bin_to_value(i + 0.5);

    // accumulate bin counts
    for (int i = 0; i < img.num_elements(); ++i) histogram.bin_y(histogram.value_to_bin(img(i))) += 1;

    // normalize histogram density by dividing bin counts by bin sizes
    histogram.y_limits[0] = std::numeric_limits<float>::infinity();
    for (int i = 0; i < Histogram::NUM_BINS; ++i)
    {
        float bin_width = histogram.bin_to_value(i + 1) - histogram.bin_to_value(i);
        histogram.ys[i] /= bin_width;
        histogram.y_limits[0] = min(histogram.y_limits[0], bin_width);
    }

    // Compute y limit for each histogram according to its 10th-largest bin
    auto ys  = histogram.ys; // make a copy, which we partially sort
    auto idx = ys.size() - 10;
    std::nth_element(ys.begin(), ys.begin() + idx, ys.end());
    // for logarithmic y-axis, we need a non-zero lower y-limit, so use half the smallest possible value
    histogram.y_limits[0] = histogram.y_scale == AxisScale_Linear ? 0.f : histogram.y_limits[0]; // / 2.f;
    // for upper y-limit, use the 10th largest value if its non-zero, then the largest, then just 1
    if (ys[idx] != 0.f)
        histogram.y_limits[1] = ys[idx] * 1.15f;
    else if (ys.back() != 0.f)
        histogram.y_limits[1] = ys.back() * 1.15f;
    else
        histogram.y_limits[1] = 1.f;

    spdlog::trace("x_limits: {}; y_limits: {}", histogram.x_limits, histogram.y_limits);

    spdlog::trace("done recomputing pixel statistics");

    // for (auto i : {-0.02, -0.015, -0.01, -0.005, 0.0, 0.005, 0.01, 0.015, 0.02})
    // {
    //     fmt::print("{} -> {}\n", i, axis_scale_fwd_xform(i, &histogram.x_scale));
    //     // fmt::print("{} -> {}\n", i, LinearToSRGB(i));
    //     // fmt::print("{} -> {}\n", i, sign(i) * LinearToSRGB(std::fabs(i)));
    // }
}

bool PixelStatistics::needs_update(float new_exposure, AxisScale_ new_x_scale, AxisScale_ new_y_scale) const
{
    // fmt::print("checking needs_update {} {}\n", new_exposure, new_x_scale);
    // when we use a logarithmic x-scale, we don't need to recompute if the exposure changes
    // otherwise, we need to recompute if either the exposure changes or the x-scale changes.
    // return new_x_scale != histogram.x_scale || (new_exposure != exposure && histogram.x_scale != AxisScale_SymLog);
    return new_x_scale != histogram.x_scale || new_y_scale != histogram.y_scale || new_exposure != exposure;
}

float4x4 ChannelGroup::colors() const
{
    switch (type)
    {
    case RGBA_Channels:
    case RGB_Channels:
        // We'd ideally like to do additive blending, but dear imgui seemingly doesn't support it.
        // Setting the alpha values to 1/(c+1) would ensure that where all three R,G,B histograms overlap we get a
        // neutral gray, but then red is fully opaque, while blue is 2/3 transparent. We instead manually choose values
        // where all three are 0.5 transparent while producing neutral gray when composited using the over operator.
        return float4x4{
            {1.0f, 0.15f, 0.1f, 0.5f}, {.45f, 0.75f, 0.02f, 0.5f}, {.25f, 0.333f, 0.7f, 0.5f}, {1.f, 1.f, 1.f, 0.5f}};
    case YCA_Channels:
    case YC_Channels:
        return float4x4{
            {1, 0.35133642, 0.5, 0.5f}, {1.f, 1.f, 1.f, 0.5f}, {0.5, 0.44952777, 1, 0.5f}, {1.0f, 1.0f, 1.0f, 0.5f}};
    case XYZA_Channels:
    case XYZ_Channels:
    case UVorXY_Channels:
    case Z_Channel:
    case Single_Channel:
        return float4x4{
            {1.0f, 1.0f, 1.0f, 0.5f}, {1.0f, 1.0f, 1.0f, 0.5f}, {1.0f, 1.0f, 1.0f, 0.5f}, {1.0f, 1.0f, 1.0f, 0.5f}};
    }
}

Channel::Channel(const std::string &name, int2 size) : Array2Df(size), name(name) {}

Texture *Channel::get_texture()
{
    if (texture_is_dirty || !texture)
    {
        texture = std::make_unique<Texture>(Texture::PixelFormat::R, Texture::ComponentFormat::Float32, size(),
                                            Texture::InterpolationMode::Trilinear, Texture::InterpolationMode::Nearest,
                                            Texture::WrapMode::ClampToEdge, 1, Texture::TextureFlags::ShaderRead);
        if (texture->pixel_format() != Texture::PixelFormat::R)
            throw std::invalid_argument("Pixel format not supported by the hardware!");

        texture->upload((const uint8_t *)data());
        texture_is_dirty = false;
    }

    return texture.get();
}

PixelStatistics *Channel::get_statistics()
{
    if (!statistics || statistics_dirty ||
        statistics->needs_update(statistics->exposure, statistics->histogram.x_scale, statistics->histogram.y_scale))
    {
        statistics       = make_unique<PixelStatistics>(*this, 1.f, AxisScale_Linear, AxisScale_Linear);
        statistics_dirty = false;
    }

    return statistics.get();
}

PixelStatistics *Channel::get_statistics(float new_exposure, AxisScale_ new_x_scale, AxisScale_ new_y_scale)
{
    if (!statistics || statistics_dirty || statistics->needs_update(new_exposure, new_x_scale, new_y_scale))
    {
        statistics       = make_unique<PixelStatistics>(*this, new_exposure, new_x_scale, new_y_scale);
        statistics_dirty = false;
    }

    return statistics.get();
}

void Image::set_null_texture(Shader &shader, const string &target)
{
    shader.set_uniform(fmt::format("{}_M_to_Rec709", target), float4x4{la::identity});
    shader.set_uniform(fmt::format("{}_channels_type", target), (int)ChannelGroup::Single_Channel);
    shader.set_uniform(fmt::format("{}_yw", target), Rec709_luminance_weights);

    for (int c = 0; c < 4; ++c) shader.set_texture(fmt::format("{}_{}_texture", target, c), black_texture());
}

void Image::set_as_texture(int group_idx, Shader &shader, const string &target)
{
    const ChannelGroup &group = groups[group_idx];

    shader.set_uniform(fmt::format("{}_M_to_Rec709", target), M_to_Rec709);
    shader.set_uniform(fmt::format("{}_channels_type", target), (int)group.type);
    shader.set_uniform(fmt::format("{}_yw", target), luminance_weights);

    for (int c = 0; c < group.num_channels; ++c)
        shader.set_texture(fmt::format("{}_{}_texture", target, c), channels[group.channels[c]].get_texture());

    if (group.num_channels == 4)
        return;

    shader.set_texture(fmt::format("{}_{}_texture", target, 3), Image::white_texture());

    if (group.num_channels == 1) // if group has 1 channel, replicate it across RGB
    {
        shader.set_texture(fmt::format("{}_{}_texture", target, 1), channels[group.channels[0]].get_texture());
        shader.set_texture(fmt::format("{}_{}_texture", target, 2), channels[group.channels[0]].get_texture());
    }
    else if (group.num_channels == 2) // if group has 2 channels, make third channel black
        shader.set_texture(fmt::format("{}_{}_texture", target, 2), Image::black_texture());
}

Image::Image(int2 size, int num_channels)
{
    if (num_channels < 3)
    {
        channels.emplace_back("Y", size);
        if (num_channels == 2)
            channels.emplace_back("A", size);
    }
    else
    {
        const std::vector<std::string> std_names{"R", "G", "B", "A"};
        for (int c = 0; c < num_channels; ++c)
        {
            std::string name = c < (int)std_names.size() ? std_names[c] : std::to_string(c);
            channels.emplace_back(name, size);
        }
    }
}

map<string, int> Image::channels_in_layer(const string &layer) const
{
    map<string, int> result;

    for (size_t i = 0; i < channels.size(); ++i)
        // if the channel starts with the layer name, and there is no dot afterwards, then this channel is in the layer
        if (starts_with(channels[i].name, layer) && channels[i].name.substr(layer.length()).find(".") == string::npos)
            result.insert({channels[i].name, i});

    return result;
}

void Image::build_layers_and_groups()
{
    // set up layers and channel groups
    const vector<pair<ChannelGroup::Type, vector<string>>> recognized_groups = {
        // RGB color (with alpha)
        {ChannelGroup::RGBA_Channels, {"R", "G", "B", "A"}},
        {ChannelGroup::RGBA_Channels, {"r", "g", "b", "a"}},
        {ChannelGroup::RGB_Channels, {"R", "G", "B"}},
        {ChannelGroup::RGB_Channels, {"r", "g", "b"}},
        // XYZ color (with alpha)
        {ChannelGroup::XYZA_Channels, {"X", "Y", "Z", "A"}},
        {ChannelGroup::XYZA_Channels, {"x", "y", "z", "a"}},
        {ChannelGroup::XYZ_Channels, {"X", "Y", "Z"}},
        {ChannelGroup::XYZ_Channels, {"x", "y", "z"}},
        // luminance-chroma color (with alpha)
        {ChannelGroup::YCA_Channels, {"RY", "Y", "BY", "A"}},
        {ChannelGroup::YCA_Channels, {"ry", "y", "by", "a"}},
        {ChannelGroup::YC_Channels, {"RY", "Y", "BY"}},
        {ChannelGroup::YC_Channels, {"ry", "y", "by"}},
        // 2D (uv or xy) coordinates
        {ChannelGroup::UVorXY_Channels, {"U", "V"}},
        {ChannelGroup::UVorXY_Channels, {"u", "v"}},
        {ChannelGroup::UVorXY_Channels, {"X", "Y"}},
        {ChannelGroup::UVorXY_Channels, {"x", "y"}},
        // depth
        {ChannelGroup::Z_Channel, {"Z"}},
        {ChannelGroup::Z_Channel, {"z"}},
    };

    // try to find all channels from group g in layer l
    auto find_group_channels = [](map<string, int> &channels, const string &prefix, const vector<string> &g)
    {
        spdlog::info("Trying to find channels '{}' in {} layer channels", fmt::join(g, ","), channels.size());
        for (auto c : channels) spdlog::info("\t{}: {}", c.second, c.first);
        vector<map<string, int>::iterator> found;
        found.reserve(g.size());
        for (const string &c : g)
        {
            string name = prefix + c;
            auto   it   = channels.find(name);
            if (it != channels.end())
                found.push_back(it);
        }
        return found;
    };

    spdlog::info("Processing {} channels", channels.size());
    for (size_t i = 0; i < channels.size(); ++i) spdlog::info("\t{:>2d}: {}", (int)i, channels[i].name);

    set<string> layer_names;
    for (auto &c : channels) layer_names.insert(Channel::head(c.name));

    for (const auto &layer_name : layer_names)
    {
        layers.emplace_back(Layer{layer_name, {}, {}});
        auto &layer = layers.back();

        // add all the layer's channels
        auto layer_channels = channels_in_layer(layer_name);
        spdlog::info("Adding {} channels to layer '{}':", layer_channels.size(), layer_name);
        for (const auto &c : layer_channels)
        {
            spdlog::info("\t{:>2d}: {}", c.second, c.first);
            layer.channels.emplace_back(c.second);
        }

        for (const auto &group : recognized_groups)
        {
            const auto &group_type          = group.first;
            const auto &group_channel_names = group.second;
            if (layer_channels.empty())
                break;
            if (layer_channels.size() < group_channel_names.size())
                continue;
            auto found = find_group_channels(layer_channels, layer.name, group_channel_names);

            // if we found all the group channels, then create them and remove from list of all channels
            if (found.size() == group_channel_names.size())
            {
                MY_ASSERT(found.size() <= 4, "ChannelGroups can have at most 4 channels!");
                int4 group_channels;
                for (size_t i2 = 0; i2 < found.size(); ++i2)
                {
                    group_channels[i2] = found[i2]->second;
                    spdlog::info("Found channel '{}': {}", group_channel_names[i2], found[i2]->second);
                }

                layer.groups.emplace_back(groups.size());
                groups.push_back(ChannelGroup{fmt::format("{}", fmt::join(group_channel_names, ",")), group_channels,
                                              (int)found.size(), group_type});
                spdlog::info("Created channel group '{}' of type {} with {} channels", groups.back().name,
                             (int)groups.back().type, group_channels);

                // now erase the channels that have been processed
                for (auto &i3 : found) layer_channels.erase(i3);
            }
        }

        if (layer_channels.size())
        {
            spdlog::info("Still have {} remaining channels", layer_channels.size());
            for (auto i : layer_channels)
            {
                layer.groups.emplace_back(groups.size());
                groups.push_back(
                    ChannelGroup{Channel::tail(i.first), int4{i.second, 0, 0, 0}, 1, ChannelGroup::Single_Channel});
                spdlog::info("Creating channel group with single channel '{}' in layer '{}'", groups.back().name,
                             layer.name);
            }
        }
    }
}

void Image::finalize()
{
    // check that there is at least 1 channel
    if (channels.empty())
        throw runtime_error{"Image must have at least one channel."};

    // set data and display windows if they are empty
    if (data_window.is_empty())
        data_window = Box2i{int2{0}, channels.front().size()};

    if (display_window.is_empty())
        display_window = Box2i{int2{0}, channels.front().size()};

    // sanity check all channels have the same size as the data window
    for (const auto &c : channels)
        if (c.size() != data_window.size())
            throw runtime_error{
                fmt::format("All channels must have the same size as the data window. ({}:{}x{} != {}x{})", c.name,
                            c.size().x, c.size().y, data_window.size().x, data_window.size().y)};

    build_layers_and_groups();

    // sanity check layers, channels, and channel groups
    {
        size_t num_channels = 0;
        for (auto &l : layers)
        {
            size_t num_channels_in_all_groups = 0;
            for (auto &g : l.groups) num_channels_in_all_groups += groups[g].num_channels;

            if (num_channels_in_all_groups != l.channels.size())
                throw runtime_error{fmt::format(
                    "Number of channels in Layer '{}' doesn't match number of channels in its groups: {} vs. {}.",
                    l.name, l.channels.size(), num_channels_in_all_groups)};

            num_channels += num_channels_in_all_groups;
        }
        if (num_channels != channels.size())
            throw runtime_error{fmt::format(
                "Number of channels in Part '{}' doesn't match number of channels in its layers: {} vs. {}.", partname,
                channels.size(), num_channels)};
    }
}

string Image::to_string() const
{
    string out;

    out += fmt::format("File name: '{}'\n", filename);
    out += fmt::format("Part name: '{}'\n", partname);

    out += fmt::format("Resolution: ({} x {})\n", size().x, size().y);
    if (display_window != data_window || display_window.min != int2{0})
    {
        out += fmt::format("Data window: ({}, {}) : ({}, {})\n", data_window.min.x, data_window.min.y,
                           data_window.max.x, data_window.max.y);
        out += fmt::format("Display window: ({}, {}) : ({}, {})\n", display_window.min.x, display_window.min.y,
                           display_window.max.x, display_window.max.y);
    }

    if (luminance_weights != Image::Rec709_luminance_weights)
        out += fmt::format("Luminance weights: {}\n", luminance_weights);

    if (M_to_Rec709 != float4x4{la::identity})
    {
        string l = "Color matrix to Rec 709 RGB: ";
        out += indent(fmt::format("{}{:> 8.5f}\n", l, M_to_Rec709), false, l.length());
    }

    out += fmt::format("Channels ({}):\n", channels.size());
    for (size_t c = 0; c < channels.size(); ++c)
    {
        auto &channel = channels[c];
        out += fmt::format("  {:>2d}: '{}'\n", c, channel.name);
    }

    out += fmt::format("Layers and channel groups ({}):\n", layers.size());
    for (size_t l = 0; l < layers.size(); ++l)
    {
        auto &layer = layers[l];
        out += fmt::format("  {:>2d}: '{}' ({})\n", l, layer.name, layer.groups.size(), layer.groups.size());
        for (size_t g = 0; g < layer.groups.size(); ++g)
        {
            auto &group = groups[layer.groups[g]];
            out += fmt::format("    {:>2d}: '{}'\n", g, group.name);
        }
    }

    out += "\n";
    return out;
}
