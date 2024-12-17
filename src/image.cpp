//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "image.h"
#include "app.h"
#include "colorspace.h"
#include "common.h"
#include "dithermatrix256.h"
#include "parallelfor.h"
#include "shader.h"
#include "texture.h"
#include "timer.h"

#include <numeric>
#include <sstream>

#include <spdlog/spdlog.h>

#include <stdexcept> // for runtime_error, out_of_range

using namespace std;

//
// static local functions
//

//
// static methods and member definitions
//

static unique_ptr<Texture> s_white_texture = nullptr, s_black_texture = nullptr, s_dither_texture = nullptr;

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

float2 PixelStats::x_limits(float e, AxisScale_ scale) const
{
    bool LDR_scale = scale == AxisScale_Linear || scale == AxisScale_SRGB;

    float2 ret;
    ret[1] = pow(2.f, -e);
    if (summary.minimum < 0.f)
        ret[0] = -ret[1];
    else
    {
        if (LDR_scale)
            ret[0] = 0.f;
        else
            ret[0] = ret[1] / 10000.f;
    }

    return ret;
}

void PixelStats::calculate(const Array2Df &img, float new_exposure, AxisScale_ new_x_scale, AxisScale_ new_y_scale,
                           std::atomic<bool> &canceled)
{
    try
    {
        spdlog::trace("Computing pixel statistics");

        // initialize values
        *this             = PixelStats();
        settings.exposure = new_exposure;
        settings.x_scale  = new_x_scale;
        settings.y_scale  = new_y_scale;

        //
        // compute pixel summary statistics

        Timer timer;
        {
            size_t       block_size  = 1024 * 1024;
            const size_t num_threads = estimate_threads(img.num_elements(), block_size, *Scheduler::singleton());
            std::vector<Summary> partials(max<size_t>(1, num_threads));
            spdlog::trace("Breaking summary stats into {} work units.", partials.size());

            parallel_for(
                blocked_range<int>(0, img.num_elements(), block_size),
                [&img, &partials, &canceled](int begin, int end, int unit_index, int thread_index)
                {
                    Summary partial = partials[unit_index]; //< compute over local symbols.

                    for (int i = begin; i != end; ++i)
                    {
                        if (canceled)
                            throw std::runtime_error("canceling summary stats");

                        // computation
                        float val = img(i);

                        if (isnan(val))
                            ++partial.nan_pixels;
                        else if (isinf(val))
                            ++partial.inf_pixels;
                        else
                        {
                            ++partial.valid_pixels;
                            partial.maximum = std::max(partial.maximum, val);
                            partial.minimum = std::min(partial.minimum, val);
                            partial.average += val;
                        }
                    }

                    partials[unit_index] = partial; //< Store partials at the end.
                },
                num_threads);

            // final reduction from partial results
            double accum = 0.f;
            for (auto &p : partials)
            {
                summary.minimum = std::min(p.minimum, summary.minimum);
                summary.maximum = std::max(p.maximum, summary.maximum);
                summary.nan_pixels += p.nan_pixels;
                summary.inf_pixels += p.inf_pixels;
                summary.valid_pixels += p.valid_pixels;
                accum += p.average;
            }
            summary.average = summary.valid_pixels ? float(accum / summary.valid_pixels) : 0.f;
        }

        spdlog::trace("Summary stats computed in {} ms:\nMin: {}\nMean: {}\nMax: {}", timer.lap(), summary.minimum,
                      summary.average, summary.maximum);
        //

        //
        // compute histograms

        bool LDR_scale = settings.x_scale == AxisScale_Linear || settings.x_scale == AxisScale_SRGB;

        auto hist_x_limits = x_limits(settings.exposure, settings.x_scale);

        hist_normalization[0] = axis_scale_fwd_xform(LDR_scale ? hist_x_limits[0] : summary.minimum, &settings.x_scale);
        hist_normalization[1] =
            axis_scale_fwd_xform(LDR_scale ? hist_x_limits[1] : summary.maximum, &settings.x_scale) -
            hist_normalization[0];

        // compute bin center values
        for (int i = 0; i < NUM_BINS; ++i) hist_xs[i] = bin_to_value(i + 0.5);

        // accumulate bin counts
        for (int i = 0; i < img.num_elements(); ++i)
        {
            if (canceled)
                throw std::runtime_error("Canceling histogram accumulation");
            bin_y(value_to_bin(img(i))) += 1;
        }

        // normalize histogram density by dividing bin counts by bin sizes
        hist_y_limits[0] = std::numeric_limits<float>::infinity();
        for (int i = 0; i < NUM_BINS; ++i)
        {
            float bin_width = bin_to_value(i + 1) - bin_to_value(i);
            hist_ys[i] /= bin_width;
            hist_y_limits[0] = min(hist_y_limits[0], bin_width);
        }

        // Compute y limit for each histogram according to its 10th-largest bin
        auto ys  = hist_ys; // make a copy, which we partially sort
        auto idx = 10;
        // put the 10th largest value in index 10
        std::nth_element(ys.begin(), ys.begin() + idx, ys.end(), std::greater<float>());
        // for logarithmic y-axis, we need a non-zero lower y-limit, so use half the smallest possible value
        hist_y_limits[0] = settings.y_scale == AxisScale_Linear ? 0.f : hist_y_limits[0]; // / 2.f;
        // for upper y-limit, use the 10th largest value if its non-zero, then the largest, then just 1
        if (ys[idx] != 0.f)
            hist_y_limits[1] = ys[idx] * 1.15f;
        else if (ys.back() != 0.f)
            hist_y_limits[1] = ys.back() * 1.15f;
        else
            hist_y_limits[1] = 1.f;

        spdlog::trace("Histogram computed in {} ms:\nx_limits: {}\ny_limits: {}", timer.lap(), hist_x_limits,
                      hist_y_limits);

        computed = true;
    }
    catch (...)
    {
        spdlog::trace("Canceled PixelStats::calculate");
        *this = PixelStats(); // reset
    }
    spdlog::trace("Finished PixelStats::calculate");
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

Channel::Channel(const std::string &name, int2 size) :
    Array2Df(size), name(name), cached_stats(make_shared<PixelStats>()), async_stats(make_shared<PixelStats>())
{
}

Texture *Channel::get_texture()
{
    if (texture_is_dirty || !texture)
    {
#if defined(__EMSCRIPTEN__)
        auto mag_mode = Texture::InterpolationMode::Nearest;
#else
        auto mag_mode = Texture::InterpolationMode::Trilinear;
#endif
        texture = std::make_unique<Texture>(Texture::PixelFormat::R, Texture::ComponentFormat::Float32, size(),
                                            mag_mode, Texture::InterpolationMode::Nearest,
                                            Texture::WrapMode::ClampToEdge, 1, Texture::TextureFlags::ShaderRead);
        if (texture->pixel_format() != Texture::PixelFormat::R)
            throw std::invalid_argument("Pixel format not supported by the hardware!");

        texture->upload((const uint8_t *)data());
        texture_is_dirty = false;
    }

    return texture.get();
}

bool PixelStats::Settings::match(const Settings &other) const
{
    return (other.x_scale == x_scale && other.exposure == exposure) ||
           (other.x_scale == x_scale && (x_scale == AxisScale_SymLog || x_scale == AxisScale_Asinh));
}

PixelStats *Channel::get_stats()
{
    MY_ASSERT(cached_stats, "PixelStats::cached_stats should never be null");

    // We always return the cached stats, but before we do we might update the cache from the async stats
    if (async_tracker.ready() && async_stats->computed)
    {
        spdlog::trace("Replacing cached channel stats with async computation");
        cached_stats = async_stats;
        async_stats  = make_shared<PixelStats>();
    }

    return cached_stats.get();
}

void Channel::update_stats()
{
    MY_ASSERT(cached_stats, "PixelStats::cached_stats should never be null");

    PixelStats::Settings desired_settings{hdrview()->exposure(), hdrview()->histogram_x_scale(),
                                          hdrview()->histogram_y_scale()};

    auto recompute_async_stats = [this, desired_settings]()
    {
        // First cancel the potential previous async task
        if (async_canceled)
        {
            spdlog::trace("Canceling outdated stats computation.");
            *async_canceled = true;
        }

        // create the new task
        async_canceled = make_shared<atomic<bool>>(false);
        async_tracker  = do_async(
            [this, desired_settings, canceled = async_canceled]()
            {
                spdlog::trace("Starting a new stats computation");
                async_stats->calculate(*this, desired_settings.exposure, desired_settings.x_scale,
                                        desired_settings.y_scale, *canceled);
            });
        async_settings = desired_settings;
    };

    // if the cached stats match and are valid, no need to recompute
    if (cached_stats->settings.match(desired_settings) && cached_stats->computed)
        return;

    // cached stats are outdated, need to recompute

    // if the async computation settings are outdated, or it was never computed -> recompute
    if (!async_settings.match(desired_settings) || (async_tracker.ready() && !async_stats->computed))
    {
        recompute_async_stats();
        return;
    }

    // if the async computation is ready, grab it and possibly schedule again
    if (async_tracker.ready() && async_stats->computed)
    {
        spdlog::trace("Replacing cached channel stats with async computation");
        // replace cache with newer async stats
        cached_stats = async_stats;
        async_stats  = make_shared<PixelStats>();

        // if these newer stats are still outdated, schedule a new async computation
        if (!cached_stats->settings.match(desired_settings))
            recompute_async_stats();
    }
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
        spdlog::trace("Trying to find channels '{}' in {} layer channels", fmt::join(g, ","), channels.size());
        for (auto c : channels) spdlog::trace("\t{}: {}", c.second, c.first);
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

    spdlog::debug("Processing {} channels", channels.size());
    for (size_t i = 0; i < channels.size(); ++i) spdlog::debug("\t{:>2d}: {}", (int)i, channels[i].name);

    set<string> layer_names;
    for (auto &c : channels) layer_names.insert(Channel::head(c.name));

    for (const auto &layer_name : layer_names)
    {
        layers.emplace_back(Layer{layer_name, {}, {}});
        auto &layer = layers.back();

        // add all the layer's channels
        auto layer_channels = channels_in_layer(layer_name);
        spdlog::debug("Adding {} channels to layer '{}':", layer_channels.size(), layer_name);
        for (const auto &c : layer_channels)
        {
            spdlog::debug("\t{:>2d}: {}", c.second, c.first);
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
                    spdlog::debug("Found channel '{}': {}", group_channel_names[i2], found[i2]->second);
                }

                layer.groups.emplace_back(groups.size());
                groups.push_back(ChannelGroup{fmt::format("{}", fmt::join(group_channel_names, ",")), group_channels,
                                              (int)found.size(), group_type});
                spdlog::debug("Created channel group '{}' of type {} with {} channels", groups.back().name,
                              (int)groups.back().type, group_channels);

                // now erase the channels that have been processed
                for (auto &i3 : found) layer_channels.erase(i3);
            }
        }

        if (layer_channels.size())
        {
            spdlog::debug("Still have {} ungrouped channels", layer_channels.size());
            for (auto i : layer_channels)
            {
                layer.groups.emplace_back(groups.size());
                groups.push_back(
                    ChannelGroup{Channel::tail(i.first), int4{i.second, 0, 0, 0}, 1, ChannelGroup::Single_Channel});
                spdlog::info("\tcreating channel group with single channel '{}' in layer '{}'", groups.back().name,
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

    // update the stats/histograms for all channels
    // for (auto &c : channels) c.update_stats();
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
        out += indent(fmt::format("{}{:::> 8.5f}\n", l, M_to_Rec709), false, l.length());
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
            if (g > 0)
                out += "\n";
            auto &group = groups[layer.groups[g]];
            out += fmt::format("    {:>2d}: '{}'", g, group.name);
        }
    }
    return out;
}
