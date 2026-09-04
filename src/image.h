//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"

#include "annotations.h"
#include "array2d.h"
#include "box.h"
#include "colorspace.h"
#include "edit/undo.h"
#include "imageio/exif.h"
#include "json.h"
#include "texture.h"
#include "vector_overlay.h"
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdint>
#include <half.h>
#include <map>
#include <optional>
#include <set>
#include <smallthreadpool.h>
#include <string>
#include <type_traits>
#include <vector>

#include <filesystem>
namespace fs = std::filesystem;
using namespace stp;

// A very small value to avoid divisions by zero when converting to unpremultiplied alpha. The technical introduction to
// OpenEXR (https://openexr.com/en/latest/TechnicalIntroduction.html#premultiplied-vs-un-premultiplied-color-channels)
// recommends "a power of two" that is "less than half of the smallest positive 16-bit floating-point value". That
// smallest value happens to be the denormal number 2^-24, so 2^-26 should be a good choice.
static constexpr float k_small_alpha = 1.f / (1u << 26u);

// Knee of the histogram x axis's nonlinear scales: below roughly this magnitude they stay near-linear,
// and above it they turn logarithmic.
inline constexpr double axis_scale_eps     = 0.0001;
inline constexpr double axis_scale_log_eps = -4; // std::log10(axis_scale_eps)

// The asinh scale's knee sits well above axis_scale_eps so the axis doesn't spend most of its width below
// the darkest level an 8-bit source can represent: at 1.8e-4 the curve is logarithmic across all of [0,1],
// which spreads consecutive dark levels tens of bins apart and combs the histogram.
inline constexpr double axis_scale_a_0 = 0.01;

/// Warp \p value into the space the histogram's x axis is drawn in.
/**
    Histogram bins are laid out uniformly in this space, and ImPlot positions the axis linearly in it as
    well, so the bins come out uniformly wide on screen too.
*/
inline double axis_scale_fwd(double value, AxisScale x_scale)
{
    if (x_scale == AxisScale_SRGB)
        return linear_to_sRGB(value);
    else if (x_scale == AxisScale_SymLog)
        return value > 0 ? (std::log10(value + axis_scale_eps) - axis_scale_log_eps)
                         : -(std::log10(-value + axis_scale_eps) - axis_scale_log_eps);
    else if (x_scale == AxisScale_Asinh)
        return axis_scale_a_0 * std::asinh(value / axis_scale_a_0);
    else
        return value;
}

/// Inverse of axis_scale_fwd().
inline double axis_scale_inv(double value, AxisScale x_scale)
{
    if (x_scale == AxisScale_SRGB)
        return sRGB_to_linear(value);
    else if (x_scale == AxisScale_SymLog)
        return value > 0 ? (std::pow(10., value + axis_scale_log_eps) - axis_scale_eps)
                         : -(std::pow(10., -value + axis_scale_log_eps) - axis_scale_eps);
    else if (x_scale == AxisScale_Asinh)
        return axis_scale_a_0 * std::sinh(value / axis_scale_a_0);
    else
        return value;
}

// ImPlotTransform-compatible wrappers, for ImPlot::SetupAxisScale(); \p user_data points at an AxisScale.
inline double axis_scale_fwd_xform(double value, void *user_data)
{
    return axis_scale_fwd(value, *(AxisScale *)user_data);
}

inline double axis_scale_inv_xform(double value, void *user_data)
{
    return axis_scale_inv(value, *(AxisScale *)user_data);
}

struct Channel;
struct Image;

struct PixelStats
{
    static constexpr int MAX_BINS = 512;
    using Ptr                     = std::shared_ptr<PixelStats>;

    /// Number of histogram bins appropriate for a source with \p bits bits per sample.
    /**
        An n-bit source holds at most 2^n distinct levels, so binning any finer than that leaves bins empty
        by construction. \p bits is 0 when the samples are floating point or their depth is unknown, which
        gets the full resolution.
    */
    static constexpr int bins_for_bit_depth(int bits)
    {
        // The shift is guarded first, since 1 << bits is undefined for large bits. A source with more
        // levels than MAX_BINS, or one whose depth is unknown, gets the full resolution.
        if (bits <= 0 || bits >= 16)
            return MAX_BINS;
        return std::min(MAX_BINS, 1 << bits);
    }

    struct Settings
    {
        float      exposure   = 0.f;
        AxisScale  x_scale    = AxisScale_Linear;
        AxisScale  y_scale    = AxisScale_Linear;
        Box2i      roi        = Box2i{int2{0}};
        BlendMode_ blend_mode = BlendMode_Normal;
        int        ref_id     = -1;
        int        ref_group  = -1;

        /// Image::content_version of the measured image, and of the reference when one is blended in.
        /**
            Everything else here describes how the image is being looked at; these two describe the pixels
            themselves. A statistic computed from pixels that have since been overwritten is stale no matter
            how well the rest of the settings match, which is what these catch.
        */
        int content_version     = 0;
        int ref_content_version = 0;

        bool match(const Settings &other) const;
    };

    Settings settings;

    /// Whether \p v is a marker rather than a measurement.
    /**
        OpenEXR's own sample images store FLT_MAX in a depth channel wherever the ray hit nothing, and
        such a value says only "no sample here" -- at the top of the float range the gap to the next
        representable value is itself larger than any radiance ever recorded. Left in the minimum and
        maximum it sets a range no exposure can fit, so it is counted apart from the measurements, next
        to the NaNs and infinities.
    */
    static bool is_marker(float v)
    {
        return v >= std::numeric_limits<float>::max() || v <= std::numeric_limits<float>::lowest();
    }

    struct Summary
    {
        float  minimum      = std::numeric_limits<float>::infinity();
        float  maximum      = -std::numeric_limits<float>::infinity();
        double average      = 0.0;
        double stddev       = 0.0;
        int    nan_pixels   = 0;
        int    inf_pixels   = 0;
        int    huge_pixels  = 0; ///< Finite, but a marker rather than a measurement; see is_marker()
        int    valid_pixels = 0;

        /// Extremes over every sample, markers and infinities included.
        /**
            Unlike minimum/maximum, which cover the measurements alone. These are what the clip warnings
            ask about, so the histogram's warning triangles see the same samples the viewport stripes. NaN
            is left out of both, as in the shader.
        */
        float extreme_minimum = std::numeric_limits<float>::infinity();
        float extreme_maximum = -std::numeric_limits<float>::infinity();
    };

    Summary summary;

    bool computed = false; ///< Did we finish computing the stats?

    // Histogram, binned separately for every AxisScale, so switching the x axis is free. Fixed-size arrays,
    // not vectors: the GUI reads these from stats objects that have not been computed yet.

    int num_bins = MAX_BINS; ///< Only [0, num_bins) of each histogram below is meaningful

    std::array<float2, AxisScale_COUNT> hist_y_limits{};
    std::array<float2, AxisScale_COUNT> hist_normalization{}; ///< Offset and span of the binned range

    /// Bin edges: hist_xs[s][i] is bin i's left edge, and hist_xs[s][num_bins] the last bin's right edge
    std::array<std::array<float, MAX_BINS + 1>, AxisScale_COUNT> hist_xs{};
    /// Number of samples in each bin
    std::array<std::array<float, MAX_BINS>, AxisScale_COUNT> hist_ys{};

    /// Leaves every histogram empty but with usable ranges.
    /**
        The GUI draws stats objects that have not been computed yet.
    */
    PixelStats()
    {
        hist_y_limits.fill(float2{0.f, 1.f});
        hist_normalization.fill(float2{0.f, 1.f});
    }

    /// Populate the statistics from the provided img and settings
    void calculate(const Channel &img, const Channel *alpha, int2 img_data_origin, const Channel *ref,
                   const Channel *ref_alpha, int2 ref_data_origin, const Settings &desired,
                   std::atomic<bool> &canceled);

    int    clamp_idx(int i) const { return std::clamp(i, 0, num_bins - 1); }
    float &bin_y(int i, AxisScale x_scale) { return hist_ys[x_scale][clamp_idx(i)]; }

    /// Index of the bin containing \p value under \p x_scale.
    /**
        Returns a negative index for a value below the binned range or for one with no bin at all (NaN), and
        num_bins for one above it. The clamp keeps the result representable: casting a non-finite double to
        int is undefined, and the GUI does ask about values far outside the range.
    */
    int value_to_bin(double value, AxisScale x_scale) const
    {
        double t = (axis_scale_fwd(value, x_scale) - hist_normalization[x_scale][0]) / hist_normalization[x_scale][1];
        if (std::isnan(t))
            return -1;
        return int(std::floor(std::clamp(t * num_bins, -1.0, double(num_bins))));
    }

    /// Value at bin edge \p bin under \p x_scale; bin == num_bins gives the last bin's right edge.
    double bin_to_value(double bin, AxisScale x_scale) const
    {
        return axis_scale_inv(hist_normalization[x_scale][1] * bin / num_bins + hist_normalization[x_scale][0],
                              x_scale);
    }

    /// Plot-space range the histogram's x axis should cover at \p exposure.
    /**
        \param exposure  The live exposure; display white sits at 2^-exposure
        \param x_scale   Which axis scale is active, since each shows a different amount past white
        \param headroom  The display's headroom, as a multiple of SDR white, or 0 when unknown. Widens
                         the asinh and sRGB axes enough to bring the display's ceiling into view; the
                         linear scale ignores it, having no room to spare
    */
    float2 x_limits(float exposure, AxisScale x_scale, float headroom = 0.f) const;
};

struct Channel : public Array2Df
{
    /// Bits per sample in the file, or 0 when it stores them as floating point or their depth is unknown.
    /**
        Sets the histogram's bin count, via bins_for_bit_depth().
    */
    int bits_per_sample = 0;

    /// Exclude from multi-channel groups on the next build_layers_and_groups(), leaving it on its own.
    /**
        Per channel, so marking an RGBA image's alpha still leaves R, G and B grouped. Not saved and not
        carried into a duplicate.
    */
    bool ungrouped = false;

    /// Whether this channel is part of the multi-selection the Images panel shows and edits act on.
    /**
        Per channel, like `ungrouped`, so it survives build_layers_and_groups() rebuilding the group
        vector. Read through Image::is_group_selected(), which counts a group selected when all its
        channels are.
    */
    bool selected = false;

private:
    PixelStats::Ptr                    cached_stats;
    ThreadPool::TaskTracker            async_tracker;
    std::shared_ptr<std::atomic<bool>> async_canceled; // this is a shared_ptr instead of a bare atomic<bool> because
                                                       // the latter has a deleted move constructor
    PixelStats::Ptr      async_stats;
    PixelStats::Settings async_settings{};

    /// When cached_stats last became a finished computation, for update_stats()'s streaming throttle.
    std::chrono::steady_clock::time_point stats_ready_at{};

    /// Take the finished async statistics as the cache and start a fresh slot for the next computation.
    void adopt_async_stats()
    {
        cached_stats   = async_stats;
        async_stats    = std::make_shared<PixelStats>();
        stats_ready_at = std::chrono::steady_clock::now();
    }

public:
    static std::pair<std::string, std::string> split(const std::string &full_name);
    static std::vector<std::string>            split_to_path(const std::string &str, char delimiter = '.');
    static std::string                         tail(const std::string &full_name) { return split(full_name).second; }
    static std::string                         head(const std::string &full_name) { return split(full_name).first; }

    std::string name; ///< The full channel name, including the layer path including periods

    std::unique_ptr<Texture> texture;
    bool                     texture_is_dirty = true;

    /// Set when the texture's level 0 has changed but its mip chain has not been rebuilt from it yet.
    /**
        Channel textures use manual mipmapping so streaming a tile costs one glTexSubImage2D; get_texture()
        does the rebuild, folding every tile that landed since the last draw into one.
    */
    bool mipmap_is_dirty = true;

    Channel() = delete;
    Channel(const std::string &name, int2 size);

    // Delete copy operations
    Channel(const Channel &)            = delete;
    Channel &operator=(const Channel &) = delete;

    // Define move operations
    Channel(Channel &&other) noexcept            = default;
    Channel &operator=(Channel &&other) noexcept = default;

    /// Stop any in-flight statistics computation for this channel and wait for it to unwind.
    void cancel_stats()
    {
        if (async_canceled)
            async_canceled->store(true);
        async_tracker.wait();
    }

    ~Channel() { cancel_stats(); }

    template <typename Func>
    void apply(Func &&func)
    {
        int block_size = std::max(1, 1024 * 1024 / width());
        parallel_for(blocked_range<int>(0, height(), block_size),
                     [this, &func](int begin_y, int end_y, int, int)
                     {
                         for (int y = begin_y; y < end_y; ++y)
                             for (int x = 0; x < width(); ++x)
                                 this->operator()(x, y) = func(this->operator()(x, y), x, y);
                     });
    }

    /**
        Copy the data from the provided float array into this channel.

        \tparam         T The type of the data array
        \param data     The array to copy from
        \param w        The width of the data
        \param h        The height of the data
        \param n        The number of channels in the data
        \param c        The channel index to copy from the data
        \param func     A function that converts the data to a float
        \param y_stride The stride between rows in the data array. If 0, it is assumed to be equal to w * n
    */
    template <typename T, typename Func>
    void copy_from_interleaved(const T data[], int w, int h, int n, int c, Func &&func, int y_stride = 0)
    {
        y_stride       = y_stride == 0 ? w * n : y_stride;
        int block_size = std::max(1, 1024 * 1024 / w);
        parallel_for(blocked_range<int>(0, h, block_size),
                     [this, n, c, w, &func, &data, y_stride](int begin_y, int end_y, int, int)
                     {
                         for (int y = begin_y; y < end_y; ++y)
                             for (int x = 0; x < w; ++x) this->operator()(x, y) = func(data[n * x + c + y * y_stride]);
                     });
    }

    /**
        Overwrite a rectangle of this channel's pixels and push just that rectangle to the GPU, for pixels
        arriving a tile at a time as a renderer produces them.

        Cancels any in-flight statistics computation, so batch a frame's worth of tiles rather than
        interleaving them with get_stats(). Must be called from the main thread when the channel has a
        texture, since it touches the graphics API; see HDRViewApp::post_to_main_thread(). The caller also
        has to bump the owning image's content_version, which a channel cannot reach.

        \param bounds   Rectangle to overwrite, in channel-local coordinates, clipped to the channel
        \param data     Source samples, row-major within \p bounds
        \param x_stride Distance in floats between consecutive samples in a row; >1 reads one channel out
                        of an interleaved payload without deinterleaving it first
        \param y_stride Distance in floats between consecutive rows, or 0 for tightly packed rows
    */
    void upload_tile(const Box2i &bounds, const float *data, int64_t x_stride = 1, int64_t y_stride = 0);

    Texture *get_texture();

    PixelStats *get_stats();
    void        update_stats(int c, ConstImagePtr img1, ConstImagePtr img2);
};

// A ChannelGroup collects up to 4 channels into a single unit
struct ChannelGroup
{
public:
    enum Type : int
    {
        RGBA_Channels   = 0,
        RGB_Channels    = 1,
        XYZA_Channels   = 2,
        XYZ_Channels    = 3,
        YCA_Channels    = 4,
        YC_Channels     = 5,
        YA_Channels     = 6,
        UVorXY_Channels = 7,
        Z_Channel       = 8,
        Single_Channel  = 9
    };

    std::string name;                 ///< One of the comma-separated recognized channel group names (e.g. 'R,G,B,A')
    int4        channels{0};          ///< Indices into Image::channels
    int         num_channels{0};      ///< Number of channels that are grouped together
    Type        type{Single_Channel}; ///< Which of the predefined types of channel group
    bool        visible{true};        ///< Whether this group is visible in the GUI

    float4x4 colors() const;
};

/// True for the group types whose last channel is an alpha channel.
inline bool group_has_alpha(ChannelGroup::Type type)
{
    return type == ChannelGroup::RGBA_Channels || type == ChannelGroup::YA_Channels ||
           type == ChannelGroup::YCA_Channels || type == ChannelGroup::XYZA_Channels;
}

struct Layer
{
public:
    std::string      name; ///< The full layer 'path', including trailing period if any, but excluding channel
    std::vector<int> channels;
    std::vector<int> groups;
};

struct LayerTreeNode
{
    std::string                          name; ///< Name of just this level of the layer path (without '.')
    std::map<std::string, LayerTreeNode> children;
    int                                  leaf_layer     = -1; ///< Index into Image::layers, or -1 if
    int                                  visible_groups = 0;  ///< Number of visible descendant groups
    int                                  hidden_groups  = 0;  ///< Number of hidden descendant groups

    void calculate_visibility(const Image *img);
};

/// Everything describing an image except its pixel data.
/** Split out from Image so that duplicate() can copy all of it in one assignment. */
struct ImageMetadata
{
    std::string filename;
    std::string partname;
    std::string channel_selector;
    /// The alpha override this image was loaded under, so a reload or a saved session can repeat it.
    /**
        Empty when the file's own interpretation was used, which is then re-derived on reload.
    */
    std::optional<TransparencyType_> transparency_override;
    Box2i                            data_window;
    Box2i                            display_window;
    std::optional<Chromaticities>    chromaticities;             ///< The chromaticities of the file
    std::optional<float2>            adopted_neutral;            ///< The adopted neutral of the file, if any
    float3x3                         M_RGB_to_XYZ, M_XYZ_to_RGB; ///< The RGB to XYZ and XYZ to RGB conversion matrices
    float3x3                         M_to_sRGB         = la::identity;
    float3                           luminance_weights = sRGB_Yw();
    AdaptationMethod                 adaptation_method = AdaptationMethod_Bradford;
    ColorGamut_                      color_space       = ColorGamut_Unspecified;
    WhitePoint_                      white_point       = WhitePoint_Unspecified;
    /// How an 'A' channel is to be read: whether it is coverage at all, and if so how it is applied.
    /**
        Whether the color channels are multiplied by it, and in what space. TransparencyType_None keeps the
        channel out of an alpha-bearing group, so nothing is multiplied by it. Read by finalize(), so set
        it before calling. Equal to transparency_from_file unless transparency_override replaced it.
    */
    TransparencyType_ transparency = TransparencyType_None;
    /// What the loader concluded before any override, so the override can say what it displaced.
    TransparencyType_ transparency_from_file = TransparencyType_None;
    /// True when nothing in the file stated the alpha kind and the loader picked a default.
    bool                 transparency_assumed = false;
    json                 metadata             = json::object();
    Exif                 exif;     ///< The raw EXIF data from the file, if any
    std::vector<uint8_t> xmp_data; ///< The raw XMP data from the file, if any
    std::vector<uint8_t> icc_data; ///< The raw ICC profile data from the file, if any
    bool                 orientation_applied = false;

    fs::path           path;
    fs::file_time_type last_modified;
    size_t             size_bytes = 0;
};

struct Image : ImageMetadata
{
public:
    static bool                         loadable(const std::string &extension);
    static const std::set<std::string> &loadable_formats(); /// Set of supported formats for image loading
    static void                         make_default_textures();
    static void                         cleanup_default_textures();
    static Texture                     *black_texture();
    static Texture                     *white_texture();
    static Texture                     *dither_texture();
    static Texture                     *chromaticity_texture();

    int id;

    std::vector<Channel> channels;

    /// True for an image whose pixels arrive from a running process rather than from a file.
    /**
        Nothing on disk backs it, so it cannot be reloaded and is left out of a saved session. Otherwise an
        ordinary image.
    */
    bool is_live = false;

    /// Drawing commands laid over this image, in its pixel coordinates.
    /**
        Set only by a renderer streaming over IPC, and drawn by the viewport after the image itself. See
        vector_overlay.h.
    */
    std::vector<VgCommand> vector_overlay;
    /// Whether that overlay is drawn, so one can be hidden without disconnecting the renderer sending it.
    bool vector_overlay_visible = true;

    /// The user's own markup over this image, in its pixel coordinates.
    /**
        Not vector_overlay: that is a renderer's stream, replaced wholesale on each send, while these are
        retained shapes the user draws and edits. See annotations.h.
    */
    std::vector<Annotation> annotations;

    /// This image's undo history; each image is its own document, so closing one takes its history with it.
    /**
        Only HDRViewApp::modify_image() adds to it, which is what keeps every edit paired with its reverse.
    */
    CommandHistory history;

    /// Bumped whenever any of this image's pixels change after loading.
    /**
        Statistics and histograms are cached against it (see PixelStats::Settings), so anything that writes
        pixels must bump it.
    */
    int content_version = 0;

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
    std::vector<Layer>        layers; ///< All the leaf layers
    std::vector<ChannelGroup> groups;
    LayerTreeNode             root; ///< The root of the layer "folder" hierarchy

    // The following are used for drawing the image in the GUI
    bool        visible            = true;
    bool        any_groups_visible = true;
    std::string short_name;
    int         selected_group  = 0;
    int         reference_group = 0;

    Image(int2 size, int num_channels);
    /// An image with exactly these channels, in this order, named as given.
    /**
        The channel-count constructor names channels itself, which only suits an image whose channels are a
        plain R,G,B,A or Y,A. A renderer names its own -- `albedo.R`, `variance` -- and the layer and group
        machinery build themselves out of those names in finalize().
    */
    Image(int2 size, const std::vector<std::string> &channel_names);

    /// Record what a loader concluded about the file's alpha, and apply any override to it.
    /**
        The one place transparency, transparency_from_file and transparency_assumed are written, so they cannot drift
        apart. Loaders call this instead of assigning transparency, then read back transparency for the
        premultiplication decisions they have to make while the samples are still encoded. Pass
        `assumed` when nothing in the file stated the kind.
    */
    void set_transparency(TransparencyType_ from_file, const std::optional<TransparencyType_> &override_with,
                          bool assumed = false);

    /// Set transparency from the channel names alone, for an image not built from a file.
    /**
        An image assembled in memory -- over IPC, by an edit, by a test -- already holds samples in
        HDRView's working representation, which is premultiplied and linear, so that is what its alpha
        says. A loader overwrites this the moment it knows what the file declared.
    */
    void set_default_transparency();
    Image();
    Image(const Image &) = delete;
    Image(Image &&)      = default;

    /// Stops every channel's statistics computation before any channel is destroyed.
    /**
        A task computing one channel's statistics also reads its group's alpha channel, and the channel
        vector destroys its elements back to front -- so the alpha channel, being last, would be freed while
        an earlier channel's task was still reading it. Each Channel destructor only cancels its own task,
        which is too late by then.
    */
    ~Image()
    {
        for (auto &c : channels) c.cancel_stats();
    }

    std::string file_and_partname() const { return partname.empty() ? filename : filename + ":" + partname; }
    std::string delimiter() const { return partname.empty() ? ":" : "."; }

    bool contains(int2 p) const
    {
        return p.x >= data_window.min.x && p.y >= data_window.min.y && p.x < data_window.max.x &&
               p.y < data_window.max.y;
    }
    int2 size() const { return data_window.size(); }

    bool is_valid_group(int index) const { return index >= 0 && index < (int)groups.size(); }

    /**
        \name Multi-selection

        Which of this image's channel groups the user has selected. Stored on the channels (see
        Channel::selected) and only ever derived for a group, so a rebuild of the group vector cannot lose
        it. HDRViewApp owns the rules relating the selection to the current and reference images.
    */
    ///@{

    /// Whether every channel of group \p index is selected.
    bool is_group_selected(int index) const
    {
        if (!is_valid_group(index))
            return false;

        const auto &g = groups[size_t(index)];
        for (int c = 0; c < g.num_channels; ++c)
            if (!channels[size_t(g.channels[c])].selected)
                return false;
        return true;
    }

    /// Whether any of this image's channels is selected.
    bool is_selected() const
    {
        for (const auto &c : channels)
            if (c.selected)
                return true;
        return false;
    }

    /// Mark or unmark every channel of group \p index.
    void select_group(int index, bool select = true)
    {
        if (!is_valid_group(index))
            return;

        const auto &g = groups[size_t(index)];
        for (int c = 0; c < g.num_channels; ++c) channels[size_t(g.channels[c])].selected = select;
    }

    /// Unmark every channel.
    void deselect_all()
    {
        for (auto &c : channels) c.selected = false;
    }

    /// The selected groups, in group order.
    std::vector<int> selected_groups() const;
    ///@}

    /// The group index `target` names: `selected_group` for Target_Primary, `reference_group` for Target_Secondary.
    /**
        Target_Secondary falls back to `selected_group` when `reference_group` is invalid:
        update_visibility() leaves it at -1 when a channel filter hides the group an image is the reference
        for.
    */
    int active_group_index(Target_ target) const
    {
        return (target == Target_Secondary && is_valid_group(reference_group)) ? reference_group : selected_group;
    }

    int next_visible_group_index(int index, Direction_ direction) const;
    int nth_visible_group_index(int n) const;

    static void set_null_texture(Target_ target = Target_Primary);
    void        set_as_texture(Target_ target = Target_Primary);

    /**
        \name Geometric operations

        The edits that change an image's shape. These are methods here because each rewrites the channel
        list or the two windows, which only this class can do; the commands under edit/ reach an image
        through EditContext, which cannot restructure anything, so they are thin wrappers around these.
        duplicate() is the odd one out: it is not an edit, but builds a second image.
    */
    /// @{
    /**
        Move this image's samples, carrying the data and display windows along with them.

        Each is exact -- every sample survives, none is resampled -- so each is its own inverse, which lets
        the undo history record one without storing any pixels.

        None of them touches content_version or the statistics cache, which belong to the caller that owns
        the undo entry. They do mark every channel's texture dirty, since transpose() changes each
        channel's shape and the texture has to be rebuilt.
    */
    void flip_horizontal();
    void flip_vertical();
    void transpose();
    void rotate_90_cw();
    void rotate_90_ccw();

    /// Reduce the image to \p box, in image coordinates, discarding everything outside it.
    /**
        Both windows become the box, so what is left is the whole image afterwards. Samples are moved,
        never resampled.
    */
    void crop(const Box2i &box);

    /// Change the canvas to \p size, keeping the samples and placing them per \p anchor.
    /**
        Nothing is resampled: the image gains empty margins or loses what falls outside. New samples are
        zero.
    */
    enum CanvasAnchor : int
    {
        Anchor_TopLeft = 0,
        Anchor_TopCenter,
        Anchor_TopRight,
        Anchor_MiddleLeft,
        Anchor_MiddleCenter,
        Anchor_MiddleRight,
        Anchor_BottomLeft,
        Anchor_BottomCenter,
        Anchor_BottomRight,

        Anchor_COUNT
    };
    void resize_canvas(int2 size, CanvasAnchor anchor);

    /// Rescale the image to \p size: the same picture at a different number of samples.
    /**
        resize_canvas() changes the frame instead. Reducing averages over the source samples each
        destination one covers; enlarging interpolates bilinearly between them.
    */
    void resample(int2 size);

    /// Rebuild the layers, groups, and tree from the channels, and nothing else.
    /**
        What a structural edit needs after changing the channel list. Not finalize(), which would
        premultiply a second time.
    */
    void rebuild_layers();

    /**
        A separate image holding a copy of \p region of this one, or of all of it when \p region is empty.

        Deep: the samples are copied, not shared, and everything describing how to read them comes along.
        The copy has no history and no texture, and is never live, which is how one keeps a frame a
        renderer is about to overwrite.
    */
    ImagePtr duplicate(const Box2i &region = Box2i{}) const;

private:
    /// Move the windows the way the samples just moved, so the two stay in the same frame.
    void reflect_windows(bool horizontal);
    void transpose_windows();

public:
    /// @}

    float4      raw_pixel(int2 p, Target_ target = Target_Primary) const;
    float4      rgba_pixel(int2 p, Target_ target = Target_Primary) const;
    void        finalize();
    void        apply_exif_orientation();
    void        compute_color_transform();
    std::string to_string() const;

    /// Record the file's sample depth on every channel; see Channel::bits_per_sample.
    void set_bits_per_sample(int bits)
    {
        for (auto &c : channels) c.bits_per_sample = bits;
    }

    /// An 'A' channel is coverage rather than ordinary data.
    bool alpha_is_transparency() const { return transparency != TransparencyType_None; }

    /// True when finalize() premultiplied `group`, so it must be divided back out; straight-alpha only.
    bool unpremultiplies(const ChannelGroup &group) const
    {
        return transparency == TransparencyType_Straight && group.num_channels > 1 && group_has_alpha(group.type);
    }

    /// Flatten the selected group into an interleaved buffer, applying gain and a transfer function.
    /**
        `unpremultiply` divides the color channels by alpha before the transfer function, yielding straight
        samples. `premultiply_encoded` then multiplies them back once the transfer has been applied, which
        is the associated-alpha convention every TIFF writer uses -- and is not the same as leaving
        `unpremultiply` off, which would multiply in linear light instead. See imageio/alpha.h.
    */
    template <typename T>
    std::unique_ptr<T[]> as_interleaved(int *w, int *h, int *n, float gain = 1.f,
                                        TransferFunction tf = {TransferFunction::Linear, 1.f}, bool dither = true,
                                        bool unpremultiply = true, bool convert_to_sRGB = true,
                                        bool premultiply_encoded = false) const;

    void draw_histogram();
    void draw_layer_groups(const Layer &layer, int img_idx, int &id, bool is_current, bool is_reference,
                           bool short_names, int &visible_group, float &scroll_to);
    void draw_layer_node(const LayerTreeNode &node, int img_idx, int &id, bool is_current, bool is_reference,
                         int &visible_group, float &scroll_to);
    int  draw_channel_tree(int img_idx, int &_id, bool is_current, bool is_reference, float &scroll_to)
    {
        int visible_group = 0;
        draw_layer_node(root, img_idx, _id, is_current, is_reference, visible_group, scroll_to);
        return visible_group;
    }

    /**
        For each visible channel in the image, draw a row into an imgui table.

        \param img_idx The index of the image in HDRViewApp's list of images (or -1). If non-negative, will be used to
                       set HDRViewApp's current image upon clicking on the row.
        \param id A unique integer id for imgui purposes. Is incremented for each added clickable row.
        \param is_current Is this the current image in HDRViewApp?
        \param is_reference Is this the reference image in HDRViewApp?
        \returns The number of displayed channel groups.
    */
    int  draw_channel_rows(int img_idx, int &id, bool is_current, bool is_reference, float &scroll_to);
    void draw_info();
    void draw_chromaticity_diagram(float width);
    void draw_colorspace();
    /// Draws the channel-statistics rows (Minimum/Average/Maximum/Std. Dev./# NaNs/# Infs) as PropertyEditor entries.
    /**
        Must be called between ImGui::PE::Begin()/End(); the caller owns the table, which also hosts
        entries that aren't Image state.
    */
    void draw_channel_stats();

private:
    void                       build_layers_and_groups();
    std::map<std::string, int> channels_in_layer(const std::string &layer) const;
    void traverse_tree(const LayerTreeNode *node, std::function<void(const LayerTreeNode *, int)> callback,
                       int level = 0) const;
};

static_assert(std::is_copy_assignable_v<ImageMetadata>, "duplicate() copies the metadata base in one assignment");
static_assert(!std::is_copy_constructible_v<Image>, "an Image owns textures and statistics tasks; move it instead");

/**
    Widen an interleaved 8-bit buffer to three or four channels, for a format that stores nothing narrower.

    A gray group's luma is replicated across red, green and blue; a U,V pair gets a zero third channel.
    Alpha, when the group carries it, stays last.

    \param src        Interleaved samples, `w * h * n` of them
    \param gray       True when the group is a lone luma channel or luma plus alpha, false for a U,V pair
    \param[out] n_out Channel count of the returned buffer: 3, or 4 when the group carries alpha
*/
std::unique_ptr<uint8_t[]> widen_to_rgb(const uint8_t *src, int w, int h, int n, bool gray, int *n_out);

template <typename T>
std::unique_ptr<T[]> Image::as_interleaved(int *w, int *h, int *n, float gain, TransferFunction tf, bool dither,
                                           bool unpremultiply, bool convert_to_sRGB, bool premultiply_encoded) const
{
    const ChannelGroup &group = groups[selected_group];

    *w = size().x;
    *h = size().y;
    *n = group.num_channels;

    // Alpha is not a color: it takes neither the exposure gain nor the transfer function, and the group's
    // remaining channels are divided back out by it when the caller wants straight alpha.
    const Channel *alpha = group_has_alpha(group.type) ? &channels[group.channels[*n - 1]] : nullptr;

    const bool luminance_chroma = group.type == ChannelGroup::YC_Channels || group.type == ChannelGroup::YCA_Channels;

    std::unique_ptr<T[]> pixels(new T[(*w) * (*h) * (*n)]);

    int block_size = std::max(1, 1024 * 1024 / (*w));

    if (*n >= 3)
    {
        // process RGB channels together
        parallel_for(blocked_range<int>(0, *h, block_size),
                     [this, alpha, w = *w, n = *n, data = pixels.get(), gain, tf, dither, unpremultiply,
                      convert_to_sRGB, luminance_chroma, premultiply_encoded](int begin_y, int end_y, int, int)
                     {
                         int y_stride = w * n;
                         for (int y = begin_y; y < end_y; ++y)
                             for (int x = 0; x < w; ++x)
                             {
                                 float3 rgb{channels[groups[selected_group].channels[0]](x, y),
                                            channels[groups[selected_group].channels[1]](x, y),
                                            channels[groups[selected_group].channels[2]](x, y)};

                                 // A luminance-chroma group stores RY,Y,BY, not RGB. Convert as the
                                 // viewport does, so an exported file matches what it displays.
                                 if (luminance_chroma)
                                     rgb = YC_to_RGB(rgb, luminance_weights);

                                 rgb *= gain;

                                 if (convert_to_sRGB)
                                     rgb = mul(M_to_sRGB, rgb);

                                 // unpremultiply alpha
                                 if (alpha && unpremultiply)
                                     rgb /= std::max(k_small_alpha, (*alpha)(x, y));

                                 // Apply transfer function to RGB triple
                                 float3 rgb_out = from_linear(rgb, tf);

                                 // Associated alpha multiplies the encoded samples, so this follows the
                                 // transfer function and precedes quantization.
                                 if (alpha && premultiply_encoded)
                                     rgb_out *= (*alpha)(x, y);

                                 auto rgba_pixel = data + y * y_stride + n * x;
                                 for (int c = 0; c < 3; ++c)
                                     rgba_pixel[c] = (std::is_integral_v<T>)
                                                         ? quantize_full<T>(rgb_out[c], x, y, dither)
                                                         : T(rgb_out[c]);

                                 // Copy alpha if present
                                 if (alpha)
                                     rgba_pixel[3] = std::is_integral_v<T>
                                                         ? quantize_full<T>((*alpha)(x, y), x, y, dither)
                                                         : T((*alpha)(x, y));
                             }
                     });
    }
    else
    {
        // A one- or two-channel group: a lone channel, a U,V pair, or gray plus alpha.
        parallel_for(blocked_range<int>(0, *h, block_size),
                     [this, alpha, w = *w, n = *n, data = pixels.get(), gain, tf, dither, unpremultiply,
                      premultiply_encoded](int begin_y, int end_y, int, int)
                     {
                         int y_stride  = w * n;
                         int num_color = alpha ? n - 1 : n;
                         for (int y = begin_y; y < end_y; ++y)
                             for (int x = 0; x < w; ++x)
                             {
                                 auto out = data + y * y_stride + n * x;
                                 for (int c = 0; c < num_color; ++c)
                                 {
                                     float v = channels[groups[selected_group].channels[c]](x, y);
                                     v *= gain;

                                     // unpremultiply alpha
                                     if (alpha && unpremultiply)
                                         v /= std::max(k_small_alpha, (*alpha)(x, y));

                                     v = from_linear(v, tf);
                                     if (alpha && premultiply_encoded)
                                         v *= (*alpha)(x, y);

                                     out[c] = std::is_integral_v<T> ? quantize_full<T>(v, x, y, dither) : T(v);
                                 }

                                 if (alpha)
                                     out[num_color] = std::is_integral_v<T>
                                                          ? quantize_full<T>((*alpha)(x, y), x, y, dither)
                                                          : T((*alpha)(x, y));
                             }
                     });
    }

    return pixels;
}
