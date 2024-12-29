//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "array2d.h"
#include "async.h"
#include "box.h"
#include "colorspace.h"
#include "common.h"
#include "fwd.h"
#include "scheduler.h"
#include <cfloat>
#include <map>
#include <set>
#include <string>
#include <vector>

inline double axis_scale_fwd_xform(double value, void *user_data)
{
    static constexpr double eps     = 0.0001;
    static constexpr double log_eps = -4;        // std::log10(eps);
    static constexpr double a_0     = eps * 1.8; // 1.8 makes asinh and our symlog looks roughly the same

    const auto x_scale = *(AxisScale_ *)user_data;
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

    const auto x_scale = *(AxisScale_ *)user_data;
    if (x_scale == AxisScale_SRGB)
        return SRGBToLinear(value);
    else if (x_scale == AxisScale_SymLog)
        return value > 0 ? (std::pow(10., value + log_eps) - eps) : -(pow(10., -value + log_eps) - eps);
    else if (x_scale == AxisScale_Asinh)
        return a_0 * std::sinh(value / a_0);
    else
        return value;
}

struct PixelStats
{
    static constexpr int NUM_BINS = 256;
    using Ptr                     = std::shared_ptr<PixelStats>;

    struct Settings
    {
        float      exposure = 0.f;
        AxisScale_ x_scale  = AxisScale_Linear;
        AxisScale_ y_scale  = AxisScale_Linear;

        bool match(const Settings &other) const;
    };

    Settings settings;

    struct Summary
    {
        float minimum      = std::numeric_limits<float>::infinity();
        float maximum      = -std::numeric_limits<float>::infinity();
        float average      = 0.0f;
        int   nan_pixels   = 0;
        int   inf_pixels   = 0;
        int   valid_pixels = 0;
    };

    Summary summary;

    bool computed = false; ///< Did we finish computing the stats?

    // histogram
    float2 hist_y_limits      = {0.f, 1.f};
    float2 hist_normalization = {0.f, 1.f};

    std::array<float, NUM_BINS> hist_xs{}; // {}: value-initialized to zeros
    std::array<float, NUM_BINS> hist_ys{}; // {}: value-initialized to zeros

    PixelStats() = default;

    /// Populate the statistics from the provided img and settings
    void calculate(const Array2Df &img, float exposure, AxisScale_ x_scale, AxisScale_ y_scale,
                   std::atomic<bool> &canceled);

    int    clamp_idx(int i) const { return std::clamp(i, 0, NUM_BINS - 1); }
    float &bin_x(int i) { return hist_xs[clamp_idx(i)]; }
    float &bin_y(int i) { return hist_ys[clamp_idx(i)]; }

    int value_to_bin(double value) const
    {
        return int(std::floor((axis_scale_fwd_xform(value, (void *)&settings.x_scale) - hist_normalization[0]) /
                              hist_normalization[1] * NUM_BINS));
    }

    double bin_to_value(double value)
    {
        static constexpr float inv_bins = 1.f / NUM_BINS;
        return axis_scale_inv_xform(hist_normalization[1] * value * inv_bins + hist_normalization[0],
                                    (void *)&settings.x_scale);
    }

    float2 x_limits(float exposure, AxisScale_ x_scale) const;
};

struct Channel : public Array2Df
{
public:
    static std::pair<std::string, std::string> split(const std::string &full_name);
    static std::vector<std::string>            split_to_path(const std::string &str, char delimiter = '.');
    static std::string                         tail(const std::string &full_name) { return split(full_name).second; }
    static std::string                         head(const std::string &full_name) { return split(full_name).first; }

    std::string name; //!< The full channel name, including the layer path including periods

    std::unique_ptr<Texture> texture;
    bool                     texture_is_dirty = true;

    Channel() = delete;
    Channel(const std::string &name, int2 size);

    Texture *get_texture();

    PixelStats *get_stats();
    void        update_stats();

private:
    PixelStats::Ptr                    cached_stats;
    Scheduler::TaskTracker             async_tracker;
    std::shared_ptr<std::atomic<bool>> async_canceled;
    PixelStats::Ptr                    async_stats;
    PixelStats::Settings               async_settings{};
};

// A ChannelGroup collects up to 4 channels into a single unit
struct ChannelGroup
{
public:
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
    };

    std::string name;                 //!< One of the comma-separated recognized channel group names (e.g. 'R,G,B,A')
    int4        channels{0};          //!< Indices into Image::channels
    int         num_channels{0};      //!< Number of channels that are grouped together
    Type        type{Single_Channel}; //!< Which of the predefined types of channel group
    bool        visible{true};        //!< Whether this group is visible in the GUI

    float4x4 colors() const;
};

struct Layer
{
public:
    std::string      name; //!< The full layer 'path', including trailing period if any, but excluding channel
    std::vector<int> channels;
    std::vector<int> groups;
};

struct LayerTreeNode
{
    std::string                          name; //!< Name of just this level of the layer path (without '.')
    std::map<std::string, LayerTreeNode> children;
    int                                  leaf_layer     = -1; //!< Index into Image::layers, or -1 if
    int                                  visible_groups = 0;  //!< Number of visible descendant groups
    int                                  hidden_groups  = 0;  //!< Number of hidden descendant groups
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

    std::string          filename;
    std::string          partname;
    Box2i                data_window;
    Box2i                display_window;
    std::vector<Channel> channels;
    float4x4             M_to_Rec709       = la::identity;
    float3               luminance_weights = Rec709_luminance_weights;

    //
    // Layers, groups, and the layer node tree are built from the loaded channels in finalize().
    //
    // It is sometimes useful to group channels into layers, that is, into sets of channels that logically belong
    // together. Grouping is done using a naming convention: channel C in layer L is called L.C. Layers can also be
    // nested, producing something akin to a folder hierarchy:
    // For example, a channel named 'light1.specular.R' identifies the R channel in the specular sub-layer of layer
    // light1.
    //
    // All the channels in the file are stored as a flat list in Image::channels.
    // All the leaf layers of the layer hierarchy are stored as a flat list in Image::layers.
    // The hierarchical structure of all layers and channels is represented by the Image::root.
    //
    std::vector<Layer>        layers; //!< All the leaf layers
    std::vector<ChannelGroup> groups;
    LayerTreeNode             root; //!< The root of the layer "folder" hierarchy

    // The following are used for drawing the image in the GUI
    bool        visible            = true;
    bool        any_groups_visible = true;
    std::string short_name;
    int         selected_group  = 0;
    int         reference_group = 0;

    Image(int2 size, int num_channels);
    Image()              = default;
    Image(const Image &) = delete;
    Image(Image &&)      = default;

    std::string file_and_partname() const { return partname.empty() ? filename : filename + ":" + partname; }
    std::string delimiter() const { return partname.empty() ? ":" : "."; }

    bool contains(int2 p) const
    {
        return p.x >= data_window.min.x && p.y >= data_window.min.y && p.x < data_window.max.x &&
               p.y < data_window.max.y;
    }
    int2 size() const { return data_window.size(); }

    bool is_valid_group(int index) const { return index >= 0 && index < (int)groups.size(); }
    int  next_visible_group_index(int index, EDirection direction) const;
    int  nth_visible_group_index(int n) const;

    static void                set_null_texture(Target target = Target_Primary);
    void                       set_as_texture(Target target = Target_Primary);
    std::map<std::string, int> channels_in_layer(const std::string &layer) const;
    void                       build_layers_and_groups();
    void                       finalize();
    bool                       compute_visibility();
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

    void draw_histogram();
    void draw_layer_groups(const Layer &layer, int img_idx, int &id, bool is_current, bool is_reference,
                           bool short_names, int &visible_group);
    void draw_layer_node(const LayerTreeNode &node, int img_idx, int &id, bool is_current, bool is_reference,
                         int &visible_group);
    int  draw_channel_tree(int img_idx, int &id, bool is_current, bool is_reference)
    {
        int visible_group = 0;
        draw_layer_node(root, img_idx, id, is_current, is_reference, visible_group);
        return visible_group;
    }

    /*!
        For each visible channel in the image, draw a row into an imgui table.

        \param img_idx The index of the image in HDRViewApp's list of images (or -1). If non-negative, will be used to
                       set HDRViewApp's current image upon clicking on the row.
        \param id A unique integer id for imgui purposes. Is incremented for each added clickable row.
        \param is_current Is this the current image in HDRViewApp?
        \param is_reference Is this the reference image in HDRViewApp?
        \returns The number of displayed channel groups.
    */
    int  draw_channel_rows(int img_idx, int &id, bool is_current, bool is_reference);
    void draw_channels_list(bool is_reference, bool is_current = true);
    void draw_info();
    void traverse_tree(const LayerTreeNode *node, std::function<void(const LayerTreeNode *, int)> callback,
                       int level = 0) const;
};

// void draw_histogram(Image *img, float exposure);
