//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "array2d.h"
#include "box.h"
#include "colorspace.h"
#include "common.h"
#include "fwd.h"
#include <cfloat>
#include <map>
#include <set>
#include <string>
#include <vector>

struct Histogram
{
    static constexpr int NUM_BINS = 256;

    AxisScale_ x_scale  = AxisScale_Linear;
    AxisScale_ y_scale  = AxisScale_Linear;
    float2     x_limits = {0.f, 1.f};
    float2     y_limits = {0.f, 1.f};

    std::array<float, NUM_BINS> xs{};
    std::array<float, NUM_BINS> ys{};

    int    clamp_idx(int i) const { return std::clamp(i, 0, NUM_BINS - 1); }
    float &bin_x(int i) { return xs[clamp_idx(i)]; }
    float &bin_y(int i) { return ys[clamp_idx(i)]; }
};

struct PixelStatistics
{
    float exposure;
    float minimum;
    float maximum;
    float average;
    int   invalid_pixels = 0;

    Histogram histogram;

    PixelStatistics(const Array2Df &img, float new_exposure, AxisScale_ x_scale, AxisScale_ y_scale);
    bool needs_update(float exposure, AxisScale_ x_scale, AxisScale_ y_scale) const;
};

inline double axis_scale_fwd_xform(double value, void *user_data)
{
    static constexpr double eps     = 0.0001;
    static constexpr double log_eps = -4;        // std::log10(eps);
    static constexpr double a_0     = eps * 1.8; // 1.8 makes asinh and our symlog looks roughly the same

    auto x_scale = *(AxisScale_ *)user_data;
    if (x_scale == AxisScale_SRGB)
        return LinearToSRGB(value);
    else if (x_scale == AxisScale_SymLog)
        return value > 0 ? (std::log10(value + eps) - log_eps) : -(std::log10(-value + eps) - log_eps);
    else if (x_scale == AxisScale_Asinh)
        return a_0 * std::asinh(value / a_0);
    else
        return value;
}

inline double axis_scale_inv_xform(double value, void *user_data)
{
    static constexpr double eps     = 0.0001;
    static constexpr double log_eps = -4;        // std::log10(eps);
    static constexpr double a_0     = eps * 1.8; // 1.8 makes asinh and our symlog looks roughly the same

    auto x_scale = *(AxisScale_ *)user_data;
    if (x_scale == AxisScale_SRGB)
        return SRGBToLinear(value);
    else if (x_scale == AxisScale_SymLog)
        return value > 0 ? (std::pow(10., value + log_eps) - eps) : -(pow(10., -value + log_eps) - eps);
    else if (x_scale == AxisScale_Asinh)
        return a_0 * std::sinh(value / a_0);
    else
        return value;
}

struct Channel : public Array2Df
{
public:
    static std::pair<std::string, std::string> split(const std::string &full_name);
    static std::string                         tail(const std::string &full_name) { return split(full_name).second; }
    static std::string                         head(const std::string &full_name) { return split(full_name).first; }

    std::string name;

    std::unique_ptr<Texture> texture;
    bool                     texture_is_dirty = true;

    std::unique_ptr<PixelStatistics> statistics;
    bool                             statistics_dirty = true;

    Channel(const std::string &name, int2 size);

    Texture         *get_texture();
    PixelStatistics *get_statistics();
    PixelStatistics *get_statistics(float exposure, AxisScale_ x_scale, AxisScale_ y_scale);
};

// A ChannelGroup collects up to 4 channels into a single unit
struct ChannelGroup
{
public:
    std::string name;

    int4 channels{0};     ///< Indices into Image::channels
    int  num_channels{0}; ///< Number of channels that are grouped together
    enum Type : int
    {
        RGBA_Channels = 0,
        RGB_Channels,
        XYZA_Channels,
        XYZ_Channels,
        YCA_Channels,
        YC_Channels,
        UVorXY_Channels,
        Z_Channel,
        Single_Channel
    } type{Single_Channel};

    float4x4 colors() const;
};

struct Layer
{
public:
    std::string      name;
    std::vector<int> channels;
    std::vector<int> groups;
};

struct Image
{
public:
    static std::set<std::string> loadable_formats(); /// Set of supported formats for image loading
    static std::set<std::string> savable_formats();  /// Set of supported formats for image saving
    static void                  make_default_textures();
    static Texture              *black_texture();
    static Texture              *white_texture();
    static Texture              *dither_texture();
    static const float3          Rec709_luminance_weights;

    std::string               filename;
    std::string               partname;
    Box2i                     data_window;
    Box2i                     display_window;
    std::vector<Channel>      channels;
    std::vector<Layer>        layers;
    std::vector<ChannelGroup> groups;

    int selected_group = 0;

    float4x4 M_to_Rec709       = la::identity;
    float3   luminance_weights = Rec709_luminance_weights;

    Image(int2 size, int num_channels);
    Image()              = default;
    Image(const Image &) = delete;
    Image(Image &&)      = default;

    bool contains(int2 p) const
    {
        return p.x >= data_window.min.x && p.y >= data_window.min.y && p.x < data_window.max.x &&
               p.y < data_window.max.y;
    }
    int2 size() const { return data_window.size(); }

    static void                set_null_texture(Shader &shader, const std::string &target = "primary");
    void                       set_as_texture(int group_idx, Shader &shader, const std::string &target = "primary");
    std::map<std::string, int> channels_in_layer(const std::string &layer) const;
    void                       build_layers_and_groups();
    void                       finalize();
    std::string                to_string() const;

    /**
        Load the an image from the input stream.

        \param [] is        The input stream to read from
        \param [] filename  The corresponding filename if `is` was opened from a file
        \return             A vector of possibly multiple images (e.g. from multi-part EXR files)
    */
    static std::vector<ImagePtr> load(std::istream &is, const std::string &filename);

    /// This is just a wrapper, it opens a file stream and loads the image using the stream-based load function above.
    static std::vector<ImagePtr> load(const std::string &filename);

    /**
        Write the image to the output stream.

        The output image format is deduced from the filename extension.

        If the format is OpenEXR, then all channels of this Image are written to the file.
        For all other formats, only the selected channel group \ref selected_group is written.

        \param os        The output stream to write to
        \param filename  The filename to save to
        \param gain      Multiply all pixel values by gain before saving
        \param gamma     If not saving to an HDR format, tonemap the image to sRGB
        \param sRGB      If not saving to an HDR format, tonemap the image using this gamma value
        \param dither    If not saving to an HDR format, dither when tonemapping down to 8-bit
        \return          True if writing was successful
    */
    bool save(std::ostream &os, const std::string &filename, float gain = 1.f, float gamma = 2.2f, bool sRGB = true,
              bool dither = true) const;

    /// This is just a wrapper, it opens a file stream and saves the image using the stream-based function above
    bool save(const std::string &filename, float gain = 1.f, float gamma = 2.2f, bool sRGB = true,
              bool dither = true) const;

    void draw_histogram(float exposure);
    void draw_channels_list();
};

// void draw_histogram(Image *img, float exposure);
