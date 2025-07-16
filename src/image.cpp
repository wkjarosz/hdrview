//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "image.h"
#include "app.h"
#include "colorspace.h"
#include "common.h"
#include "dithermatrix256.h"
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
static int                 s_next_image_id = 1;

pair<string, string> Channel::split(const string &channel)
{
    size_t dot = channel.rfind(".");
    if (dot != string::npos)
        return {channel.substr(0, dot + 1), channel.substr(dot + 1)};

    return {"", channel};
}

vector<string> Channel::split_to_path(const string &str, char delimiter)
{
    vector<string> result;
    istringstream  ss(str);
    string         item;

    while (std::getline(ss, item, delimiter)) result.push_back(item);

    return result;
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
                                                 int2{g_dither_matrix_w}, Texture::InterpolationMode::Nearest,
                                                 Texture::InterpolationMode::Nearest, Texture::WrapMode::Repeat);
    s_black_texture->upload((const uint8_t *)&s_black);
    s_white_texture->upload((const uint8_t *)&s_white);
    s_dither_texture->upload((const uint8_t *)g_dither_matrix);
}

void Image::cleanup_default_textures()
{
    s_black_texture.reset();
    s_white_texture.reset();
    s_dither_texture.reset();
}

Texture *Image::black_texture() { return s_black_texture.get(); }
Texture *Image::white_texture() { return s_white_texture.get(); }
Texture *Image::dither_texture() { return s_dither_texture.get(); }

const std::set<std::string> &Image::loadable_formats()
{
    static const std::set<std::string> formats = {"dng",  "jpg",  "jpeg",
#ifdef HDRVIEW_ENABLE_JPEGXL
                                                  "jxl",
#endif
#ifdef HDRVIEW_ENABLE_HEIF
                                                  "heic", "heif", "avif", "avifs",
#endif
                                                  "pic",  "png",  "pnm",  "pgm",   "ppm", "bmp", "psd",
                                                  "pfm",  "tga",  "gif",  "hdr",   "exr", "qoi"};
    return formats;
}

const std::set<std::string> &Image::savable_formats()
{
    static const std::set<std::string> formats = {"bmp", "exr", "pfm",  "ppm", "png",
                                                  "hdr", "jpg", "jpeg", "tga", "qoi"};
    return formats;
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

void PixelStats::calculate(const Channel &img, int2 img_data_origin, const Channel *ref, int2 ref_data_origin,
                           const Settings &desired, std::atomic<bool> &canceled)
{
    try
    {
        spdlog::trace("Computing pixel statistics");

        // initialize values
        *this    = PixelStats();
        settings = desired;

        // pixel regions we will loop over in the current and reference channels
        Box2i croi{img_data_origin, img_data_origin + img.size()};
        Box2i rroi = ref ? Box2i{ref_data_origin, ref_data_origin + ref->size()} : croi;

        if (desired.roi.has_volume())
        {
            croi = croi.intersect(desired.roi);
            rroi = rroi.intersect(desired.roi);
        }

        // intersect with reference image window
        if (ref && desired.blend_mode != NORMAL_BLEND)
        {
            croi = croi.intersect(rroi);
            spdlog::info("c and r roi's: {}..{}; {}..{}", croi.min, croi.max, rroi.min, rroi.max);
        }

        spdlog::info("Image ROI: {}..{}", croi.min, croi.max);

        if (croi.size() != rroi.size())
            spdlog::error("Image and reference channel ROIs are not the same size!");

        auto pixel_value = [&img, ref, &croi, &rroi, this](int i)
        {
            int2 i2d{i % croi.size().x, i / croi.size().x}; // convert to 2D coordinates
            if (i2d.x < 0 || i2d.y < 0 || i2d.x >= croi.size().x || i2d.y >= croi.size().y)
            {
                // spdlog::error("Pixel index {} ({}) out of bounds for ROI {}..{}", i, i2d, croi.min, croi.max);
                return std::numeric_limits<float>::quiet_NaN(); // out of bounds
            }
            float val = img(i2d + croi.min);
            if (ref)
                val = blend(val, (*ref)(i2d + rroi.min), settings.blend_mode);
            return val;
        };

        //
        // compute pixel summary statistics

        Timer timer;
        {
            size_t               block_size  = 1024 * 1024;
            const size_t         num_threads = estimate_threads(croi.volume(), block_size, *Scheduler::singleton());
            std::vector<Summary> partials(max<size_t>(1, num_threads));

            spdlog::trace("Breaking summary stats into {} work units.", partials.size());

            parallel_for(
                blocked_range<int>(0, croi.volume(), (int)block_size),
                [&partials, &canceled, &pixel_value](int begin, int end, int unit_index, int)
                {
                    Summary partial = partials[unit_index]; //< compute over local symbols.

                    for (int i = begin; i != end; ++i)
                    {
                        if (canceled)
                            throw std::runtime_error("canceling summary stats");

                        float val = pixel_value(i);

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
                (int)num_threads);

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

        hist_normalization[0] =
            (float)axis_scale_fwd_xform(LDR_scale ? hist_x_limits[0] : summary.minimum, &settings.x_scale);
        hist_normalization[1] =
            (float)axis_scale_fwd_xform(LDR_scale ? hist_x_limits[1] : summary.maximum, &settings.x_scale) -
            hist_normalization[0];

        // compute bin center values
        for (int i = 0; i < NUM_BINS; ++i) hist_xs[i] = (float)bin_to_value(i + 0.5);

        // accumulate bin counts
        for (int i = 0; i < croi.volume(); ++i)
        {
            if (canceled)
                throw std::runtime_error("Canceling histogram accumulation");
            bin_y(value_to_bin(pixel_value(i))) += 1;
        }

        // normalize histogram density by dividing bin counts by bin sizes
        hist_y_limits[0] = std::numeric_limits<float>::infinity();
        for (int i = 0; i < NUM_BINS; ++i)
        {
            float bin_width = float(bin_to_value(i + 1) - bin_to_value(i));
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
            {1.f, 0.15f, 0.1f, 0.5f}, {.45f, 0.75f, 0.02f, 0.5f}, {.25f, 0.333f, 0.7f, 0.5f}, {1.f, 1.f, 1.f, 0.5f}};
    case YCA_Channels:
    case YC_Channels:
        return float4x4{{1.f, 0.35133642f, 0.5f, 0.5f},
                        {1.f, 1.f, 1.f, 0.5f},
                        {0.5, 0.44952777f, 1.f, 0.5f},
                        {1.0f, 1.0f, 1.0f, 0.5f}};
    case YA_Channels:
    case XYZA_Channels:
    case XYZ_Channels:
    case UVorXY_Channels:
    case Z_Channel:
    case Single_Channel:
    default:
        return float4x4{{1.f, 1.f, 1.f, 0.5f}, {1.f, 1.f, 1.f, 0.5f}, {1.f, 1.f, 1.f, 0.5f}, {1.f, 1.f, 1.f, 0.5f}};
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
        auto min_mode = Texture::InterpolationMode::Nearest;
#else
        auto min_mode = Texture::InterpolationMode::Trilinear;
#endif
        texture = std::make_unique<Texture>(Texture::PixelFormat::R, Texture::ComponentFormat::Float32, size(),
                                            min_mode, Texture::InterpolationMode::Nearest,
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
    return (blend_mode == other.blend_mode && ref_id == other.ref_id && ref_group == other.ref_group) &&
           other.roi == roi &&
           ((other.x_scale == x_scale && other.exposure == exposure) ||
            (other.x_scale == x_scale && (x_scale == AxisScale_SymLog || x_scale == AxisScale_Asinh)));
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

void Channel::update_stats(int c, ConstImagePtr img1, ConstImagePtr img2)
{
    MY_ASSERT(cached_stats, "PixelStats::cached_stats should never be null");

    PixelStats::Settings desired_settings{
        hdrview()->exposure(),   hdrview()->histogram_x_scale(), hdrview()->histogram_y_scale(),   hdrview()->roi(),
        hdrview()->blend_mode(), img2 ? img2->id : -1,           img2 ? img2->reference_group : -1};

    auto recompute_async_stats =
        [this, desired_settings, img1, img_data_origin = img1->data_window.min,
         ref             = img2 ? &img2->channels[img2->groups[img2->reference_group].channels[c]] : nullptr,
         ref_data_origin = img2 ? img2->data_window.min : int2{}]()
    {
        spdlog::info("id: {}", img1->id);
        // First cancel the potential previous async task
        if (async_canceled)
        {
            spdlog::trace("Canceling outdated stats computation.");
            *async_canceled = true;
        }

        // create the new task
        async_canceled = make_shared<atomic<bool>>(false);
        async_tracker  = do_async(
            [this, desired_settings, canceled = async_canceled, img_data_origin, ref, ref_data_origin]()
            {
                spdlog::info("Starting a new stats computation");
                async_stats->calculate(*this, img_data_origin, ref, ref_data_origin, desired_settings, *canceled);
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

void Image::set_null_texture(Target target)
{
    auto s = hdrview()->shader();
    auto t = target_name(target);

    for (int c = 0; c < 4; ++c) s->set_texture(fmt::format("{}_{}_texture", t, c), black_texture());
}

void Image::set_as_texture(Target target)
{
    auto                s         = hdrview()->shader();
    auto                t         = target_name(target);
    int                 group_idx = target == Target_Primary ? selected_group : reference_group;
    const ChannelGroup &group     = groups[group_idx];

    for (int c = 0; c < group.num_channels; ++c)
        s->set_texture(fmt::format("{}_{}_texture", t, c), channels[group.channels[c]].get_texture());

    if (group.num_channels == 4)
        return;

    if (group.num_channels == 1) // if group has 1 channel, replicate it across RGB, and set A=1
    {
        s->set_texture(fmt::format("{}_{}_texture", t, 1), channels[group.channels[0]].get_texture());
        s->set_texture(fmt::format("{}_{}_texture", t, 2), channels[group.channels[0]].get_texture());
        s->set_texture(fmt::format("{}_{}_texture", t, 3), Image::white_texture());
    }
    else if (group.num_channels == 2) // if group has 2 channels, depends on the type
    {
        if (group.type == ChannelGroup::YA_Channels)
        {
            // if group is YA, replicate the Y channel across RGB, and put A in the 4th channel
            s->set_texture(fmt::format("{}_{}_texture", t, 1), channels[group.channels[0]].get_texture());
            s->set_texture(fmt::format("{}_{}_texture", t, 2), channels[group.channels[0]].get_texture());
            s->set_texture(fmt::format("{}_{}_texture", t, 3), channels[group.channels[1]].get_texture());
        }
        else
        {
            // for other x-channel groups, set 3rd channel to black, and set A=1
            s->set_texture(fmt::format("{}_{}_texture", t, 1), channels[group.channels[0]].get_texture());
            s->set_texture(fmt::format("{}_{}_texture", t, 2), Image::black_texture());
            s->set_texture(fmt::format("{}_{}_texture", t, 3), Image::white_texture());
        }
    }
    else if (group.num_channels == 3) // if group has 3 channels, set A=1
        s->set_texture(fmt::format("{}_{}_texture", t, 3), Image::white_texture());
}

Image::Image() : id(s_next_image_id++) {}

Image::Image(int2 size, int num_channels) : Image()
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
    display_window = data_window = Box2i{int2{0}, channels.front().size()};
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
        // monochrome images with alpha
        {ChannelGroup::YA_Channels, {"Y", "A"}},
        {ChannelGroup::YA_Channels, {"y", "a"}},
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

        LayerTreeNode *node = &root;
        {
            auto path = Channel::split_to_path(layer.name);

            for (auto d : path)
            {
                auto it = node->children.find(d);
                if (it != node->children.end())
                {
                    // node already contains d as a child, use that
                    node = &it->second;
                }
                else
                {
                    // insert a new entry in child and use that
                    node       = &node->children[d];
                    node->name = d;
                }
            }
            if (node->leaf_layer >= 0)
                spdlog::info("node '{}' already contains a leaf layer", node->name);

            node->leaf_layer = layers.size() - 1;
        }

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

void Image::compute_color_transform()
{
    // get color correction info from the header
    luminance_weights = sRGB_Yw();
    if (chromaticities)
        luminance_weights = computeYw(*chromaticities);

    spdlog::debug("Yw = {}", luminance_weights);

    Chromaticities chr{};
    if (chromaticities)
        chr = *chromaticities;

    if (adopted_neutral)
        chr.white = *adopted_neutral;

    static const Chromaticities bt709_chr{}; // default bt709 (sRGB) primaries
    color_conversion_matrix(M_to_sRGB, chr, bt709_chr, adaptation_method);
    M_RGB_to_XYZ = RGB_to_XYZ(chr, 1.f);
    M_XYZ_to_RGB = XYZ_to_RGB(chr, 1.f);

    // determine if this is (close to) one of the named color spaces
    if (chromaticities)
    {
        color_space = named_color_gamut(*chromaticities);
        spdlog::debug("Detected color space: '{}'", color_gamut_name((ColorGamut)color_space));

        white_point = named_white_point(chr.white);
        spdlog::debug("Detected white point: '{}'", white_point_name(white_point));
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

    // if we have a straight alpha channel, premultiply the other channels by it.
    // this needs to be done after the values have been made linear
    if (file_has_straight_alpha)
    {
        for (auto &g : groups)
        {
            bool has_alpha =
                g.num_channels > 1 && (g.type == ChannelGroup::RGBA_Channels || g.type == ChannelGroup::YA_Channels ||
                                       g.type == ChannelGroup::YCA_Channels || g.type == ChannelGroup::XYZA_Channels);
            if (!has_alpha)
                continue;

            for (int c = 0; c < g.num_channels - 1; ++c)
            {
                spdlog::debug("Premultiplying channel {}", g.channels[c]);
                channels[g.channels[c]].apply([&alpha = channels[g.channels[g.num_channels - 1]]](float v, int x, int y)
                                              { return std::max(k_small_alpha, alpha(x, y)) * v; });
            }
        }
    }

    compute_color_transform();
}

// Recursive function to traverse the LayerTreeNode hierarchy and append names to a string
void Image::traverse_tree(const LayerTreeNode *node, std::function<void(const LayerTreeNode *, int)> callback,
                          int level) const
{
    callback(node, level);
    for (const auto &[child_name, child_node] : node->children) traverse_tree(&child_node, callback, level + 1);
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

    if (luminance_weights != sRGB_Yw())
        out += fmt::format("Luminance weights: {}\n", luminance_weights);

    if (M_to_sRGB != float3x3{la::identity})
    {
        string l = "Color matrix to Rec 709 RGB: ";
        out += indent(fmt::format("{}{:::> 8.5f}\n", l, M_to_sRGB), false, l.length());
    }

    out += fmt::format("Channels ({}):\n", channels.size());
    for (size_t c = 0; c < channels.size(); ++c)
    {
        auto &channel = channels[c];
        out += fmt::format("  {:>2d}: '{}'\n", c, channel.name);
    }
    out += "\n";

    out += fmt::format("Layers and channel groups ({}):\n", layers.size());
    for (size_t l = 0; l < layers.size(); ++l)
    {
        auto &layer = layers[l];
        out += fmt::format("  {:>2d}: layer name '{}'; with {} child groups:\n", l, layer.name, layer.groups.size());
        for (size_t g = 0; g < layer.groups.size(); ++g)
        {
            if (g > 0)
                out += "\n";
            auto &group = groups[layer.groups[g]];
            out += fmt::format("     {:>2d}: group name '{}'", g, group.name);
        }
        out += "\n";
    }
    out += "\n";

    // out += fmt::format("Layer paths:\n");
    // for (size_t l = 0; l < layers.size(); ++l)
    // {
    //     auto &layer = layers[l];
    //     auto  path  = Channel::split_to_path(layer.name);
    //     out += fmt::format("Path for layer '{}':\n", layer.name);
    //     for (auto d : path) out += fmt::format("'{}', ", d);
    //     out += "\n";
    // }
    // out += "\n";

    // out += fmt::format("Layer tree:\n");
    // // Traverse the LayerTreeNode hierarchy and append names to the string
    // auto print_node = [&out, this](const LayerTreeNode *node, int depth)
    // {
    //     string prefix(4 * depth, ' ');
    //     string indent = "+" + string(3, '-');
    //     out += fmt::format("{}'{}' (leaf index: {})\n", prefix, node->name, node->leaf_layer);
    //     if (node->leaf_layer < 0)
    //         return;

    //     auto &layer = layers[node->leaf_layer];
    //     for (size_t g = 0; g < layer.groups.size(); ++g)
    //     {
    //         auto &group = groups[layer.groups[g]];
    //         out += fmt::format("{}{}'{}'\n", prefix, indent, group.name);
    //     }
    // };
    // traverse_tree(&root, print_node);
    // out += "\n";

    return out;
}

int Image::next_visible_group_index(int index, EDirection direction) const
{
    return next_matching_index(groups, index, [](size_t, const ChannelGroup &g) { return g.visible; }, direction);
}

int Image::nth_visible_group_index(int n) const
{
    return (int)nth_matching_index(groups, (size_t)n, [](size_t, const ChannelGroup &g) { return g.visible; });
}

float4 Image::raw_pixel(int2 p, Target target) const
{
    if (!contains(p))
        return float4{0.f};

    int                 group_idx = target == Target_Primary ? selected_group : reference_group;
    const ChannelGroup &group     = groups[group_idx];

    float4 value{0.f};
    for (int c = 0; c < group.num_channels; ++c) value[c] = channels[group.channels[c]](p - data_window.min);

    return value;
}

/// Reconstruct the raw pixel value into an RGBA value (like the first stage of the fragment shader)
float4 Image::rgba_pixel(int2 p, Target target) const
{
    if (!contains(p))
        return float4{0.f};

    int                 group_idx = target == Target_Primary ? selected_group : reference_group;
    const ChannelGroup &group     = groups[group_idx];

    float4 value{float3{0.f}, 1.f};
    {
        for (int c = 0; c < group.num_channels; ++c) value[c] = channels[group.channels[c]](p - data_window.min);
        if (group.num_channels == 1) // if group has 1 channel, replicate it across RGB
            value[1] = value[2] = value[0];
        else if (group.num_channels == 2)
        {
            if (group.type == ChannelGroup::YA_Channels)
            {
                value[3]    = value[1];
                value.xyz() = YC_to_RGB(float3{0.f, value[0], 0.f}, luminance_weights);
                spdlog::info("YA channel group: {}", value);
            }
            else
                value[2] = 0.f;
        }
        if (group.type == ChannelGroup::YCA_Channels || group.type == ChannelGroup::YC_Channels)
            value.xyz() = YC_to_RGB(value.xyz(), luminance_weights);
    }

    value.xyz() = mul(M_to_sRGB, value.xyz());
    return value;
}
