//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "image.h"
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

void Image::set_null_texture(Shader &shader, const string &target)
{
    shader.set_uniform(fmt::format("{}_M_to_Rec709", target), float4x4{la::identity});
    shader.set_uniform(fmt::format("{}_channels_type", target), (int)ChannelGroup::Single_Channel);
    shader.set_uniform(fmt::format("{}_yw", target), Rec709_luminance_weights);

    for (int c = 0; c < 4; ++c)
        shader.set_texture(fmt::format("{}_{}_texture", target, c), black_texture());
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

void Image::build_Layers_and_groups()
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
        fmt::print("Trying to find channels '{}' in {} layer channels\n", fmt::join(g, ","), channels.size());
        for (auto c : channels)
            fmt::print("\t{}: {}\n", c.second, c.first);
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

    fmt::print("Processing {} channels\n", channels.size());
    for (size_t i = 0; i < channels.size(); ++i)
        fmt::print("\t{:>2d}: {}\n", (int)i, channels[i].name);

    set<string> layer_names;
    for (auto &c : channels)
        layer_names.insert(Channel::head(c.name));

    for (const auto &layer_name : layer_names)
    {
        layers.emplace_back(Layer{layer_name, {}, {}});
        auto &layer = layers.back();

        // add all the layer's channels
        auto layer_channels = channels_in_layer(layer_name);
        fmt::print("Adding {} channels to layer '{}':\n", layer_channels.size(), layer_name);
        for (const auto &c : layer_channels)
        {
            fmt::print("\t{:>2d}: {}\n", c.second, c.first);
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
                    fmt::print("Found channel '{}': {}\n", group_channel_names[i2], found[i2]->second);
                }

                layer.groups.emplace_back(groups.size());
                groups.push_back(ChannelGroup{fmt::format("{}", fmt::join(group_channel_names, ",")), group_channels,
                                              (int)found.size(), group_type});
                fmt::print("Created channel group: {}, {}\n", groups.back().name, group_channels);

                // now erase the channels that have been processed
                for (auto &i3 : found)
                    layer_channels.erase(i3);
            }
        }

        if (layer_channels.size())
        {
            fmt::print("Still have {} remaining channels\n", layer_channels.size());
            for (auto i : layer_channels)
            {
                layer.groups.emplace_back(groups.size());
                groups.push_back(
                    ChannelGroup{Channel::tail(i.first), int4{i.second, 0, 0, 0}, 1, ChannelGroup::Single_Channel});
                fmt::print("Creating channel group with single channel '{}' in layer '{}'\n", groups.back().name,
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

    build_Layers_and_groups();

    // sanity check layers, channels, and channel groups
    {
        size_t num_channels = 0;
        for (auto &l : layers)
        {
            size_t num_channels_in_all_groups = 0;
            for (auto &g : l.groups)
                num_channels_in_all_groups += groups[g].num_channels;

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
