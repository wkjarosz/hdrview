//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "fwd.h"

#include "app.h"
#include "colorspace.h"
#include "common.h"
#include "dithermatrix256.h"
#include "image.h"

#include "imageio/xmp.h"
#include "shader.h"
#include "stb_image_resize2.h"
#include "timer.h"

#include <numeric>
#include <sstream>

#include <stdexcept> // for runtime_error, out_of_range

using namespace std;

//
// static local functions
//

//
// static methods and member definitions
//

static unique_ptr<Texture> s_white_texture = nullptr, s_black_texture = nullptr, s_dither_texture = nullptr,
                           s_chromaticity_texture = nullptr;
static atomic<int> s_next_image_id                = 1;

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
    static constexpr int   chr_w = 256;
    s_black_texture  = std::make_unique<Texture>(Texture::PixelFormat::R, Texture::ComponentFormat::Float32, int2{1, 1},
                                                 Texture::InterpolationMode::Nearest,
                                                 Texture::InterpolationMode::Nearest, Texture::WrapMode::Repeat);
    s_white_texture  = std::make_unique<Texture>(Texture::PixelFormat::R, Texture::ComponentFormat::Float32, int2{1, 1},
                                                 Texture::InterpolationMode::Nearest,
                                                 Texture::InterpolationMode::Nearest, Texture::WrapMode::Repeat);
    s_dither_texture = std::make_unique<Texture>(Texture::PixelFormat::R, Texture::ComponentFormat::UInt8,
                                                 int2{dither_texture_width()}, Texture::InterpolationMode::Nearest,
                                                 Texture::InterpolationMode::Nearest, Texture::WrapMode::Repeat);
    s_chromaticity_texture = std::make_unique<Texture>(
        Texture::PixelFormat::RGBA, Texture::ComponentFormat::UInt8, int2{chr_w}, Texture::InterpolationMode::Bilinear,
        Texture::InterpolationMode::Bilinear, Texture::WrapMode::ClampToEdge);

    auto create_locus = []()
    {
        constexpr int    sample_count = 200;
        ImVector<ImVec2> poly;
        poly.resize(sample_count);

        constexpr float wavelength_min = 380.f;
        constexpr float wavelength_max = 700.f;

        auto &illum = white_point_spectrum(WhitePoint_D65);
        auto &XYZ   = CIE_XYZ_spectra();

        // Compute chromaticity line
        for (int i = 0; i < sample_count; ++i)
        {
            float wavelength = lerp(wavelength_min, wavelength_max, ((float)i) / ((float)(sample_count - 1)));
            auto  xyz        = illum.eval(wavelength) * XYZ.eval(wavelength);
            poly[i]          = xyz.xy() / la::sum(xyz); // normalize
        }

        return poly;
    };

    static const ImVector<ImVec2> locus = create_locus();

    // lambda to check if a point is inside a polygon using the ray-casting algorithm
    auto is_inside = [](float2 point) -> bool
    {
        int n     = locus.size(); // Number of vertices in the polygon
        int count = 0;            // Count of intersections

        // Iterate through each edge of the polygon
        for (int i = 0; i < n; i++)
        {
            ImVec2 p1 = locus[i];
            ImVec2 p2 = locus[(i + 1) % n]; // Ensure the last point connects to the first point

            // Check if the point's y-coordinate is within the edge's y-range and if the point is to the left of the
            // edge
            if ((point.y > min(p1.y, p2.y)) && (point.y <= max(p1.y, p2.y)) && (point.x <= max(p1.x, p2.x)))
            {
                // Calculate the x-coordinate of the intersection of the edge with a horizontal line through the point
                auto xIntersect = (point.y - p1.y) * (p2.x - p1.x) / (p2.y - p1.y) + p1.x;
                // If the edge is vertical or the point's x-coordinate is less than or equal to the intersection
                // x-coordinate, increment count
                if (p1.x == p2.x || point.x <= xIntersect)
                    count++;
            }
        }

        return count % 2 == 1;
    };

    auto rgb2xyz = XYZ_to_RGB(Chromaticities{}, 1.f);

    vector<byte4> g_chromaticity_data(chr_w * chr_w);
    for (int y = 0; y < chr_w; ++y)
    {
        for (int x = 0; x < chr_w; ++x)
        {
            float  xn = (x + 0.5f) / chr_w;
            float  yn = (y + 0.5f) / chr_w;
            float3 xyz{xn, yn, 1.f - xn - yn};
            float3 rgb = mul(rgb2xyz, xyz);
            rgb        = linear_to_sRGB(rgb / la::maxelem(rgb));
            byte4 rgba8{quantize_full<uint8_t>(rgb.x), quantize_full<uint8_t>(rgb.y), quantize_full<uint8_t>(rgb.z),
                        is_inside(xyz.xy()) ? (uint8_t)255 : (uint8_t)0};

            g_chromaticity_data[y * chr_w + x] = rgba8;
        }
    }

    s_black_texture->upload((const uint8_t *)&s_black);
    s_white_texture->upload((const uint8_t *)&s_white);
    s_dither_texture->upload((const uint8_t *)dither_texture_data());
    s_chromaticity_texture->upload((const uint8_t *)g_chromaticity_data.data());
}

void Image::cleanup_default_textures()
{
    s_black_texture.reset();
    s_white_texture.reset();
    s_dither_texture.reset();
    s_chromaticity_texture.reset();
}

Texture *Image::black_texture() { return s_black_texture.get(); }
Texture *Image::white_texture() { return s_white_texture.get(); }
Texture *Image::dither_texture() { return s_dither_texture.get(); }
Texture *Image::chromaticity_texture() { return s_chromaticity_texture.get(); }

const std::set<std::string> &Image::loadable_formats()
{
    static const std::set<std::string> formats = {
        "dng",
        "jpg",
        "jpeg",
#if HDRVIEW_ENABLE_LIBJXL
        "jxl",
#endif
#if HDRVIEW_ENABLE_LIBHEIF
        "heif",
        "heifs",
#endif
#if HDRVIEW_ENABLE_HEIC
        "heic",
        "heics",
#endif
#if HDRVIEW_ENABLE_AVIF
        "avif",
        "avifs",
#endif
#if HDRVIEW_ENABLE_AVCI
        "avci",
        "avcs",
#endif
        "pic",
        "png",
        "pnm",
        "pgm",
        "ppm",
        "bmp",
        "dds",
        "psd",
        "pfm",
        "tga",
        "gif",
        "hdr",
        "exr",
        "qoi",
#if HDRVIEW_ENABLE_LIBTIFF
        "tif",
        "tiff",
#endif
#if HDRVIEW_ENABLE_LIBRAW
        // RAW formats supported by LibRaw
        "bay",
        "bmq",
        "cr2",
        "cr3",
        "crw",
        "cs1",
        "dc2",
        "dcr",
        "erf",
        "fff",
        "k25",
        "kdc",
        "mdc",
        "mos",
        "mrw",
        "nef",
        "orf",
        "pef",
        "pxn",
        "raf",
        "raw",
        "rdc",
        "sr2",
        "srf",
#if HDRVIEW_ENABLE_X3F
        "x3f",
#endif
        "arw",
        "3fr",
        "cine",
        "ia",
        "kc2",
        "mef",
        "nrw",
        "qtk",
        "rw2",
        "sti",
        "rwl",
        "srw",
        "drf",
        "dsc",
        "ptx",
        "cap",
        "iiq",
        "rwz",
#endif
#if HDRVIEW_ENABLE_LIBWEBP
        "webp",
#endif
    };
    return formats;
}

bool Image::loadable(const std::string &ext)
{
    try
    {
        // remove period and convert to lowercase
        return Image::loadable_formats().count(to_lower(ext.size() > 1 && ext[0] == '.' ? ext.substr(1) : ext)) > 0;
    }
    catch (...)
    {
        return false;
    }
}

//
// end static methods
//

float2 PixelStats::x_limits(float e, AxisScale scale, float headroom) const
{
    // Each scale reaches some multiple past display white, which sits at 2^-e. Asinh and sRGB compress
    // their far end, so they can clear the display's ceiling (with a margin, so it lands inside the axis)
    // and still leave the data legible; linear keeps a fixed reach, since stretching it that far would
    // leave everything that matters in the first twentieth of the plot.
    const float past_white = scale == AxisScale_Linear ? 1.2f
                             : scale == AxisScale_SRGB ? std::max(1.5f, headroom * 1.15f)
                                                       : std::max(4.f, headroom * 1.15f);

    float2 ret;
    ret[1] = pow(2.f, -e) * past_white;
    if (summary.minimum < -summary.maximum / 255.f)
        // Negatives get the room they occupy, plus a sliver so the extreme value does not land on the axis
        // itself, and are capped at the positive reach so signed data cannot crowd out the range the
        // exposure is set for.
        ret[0] = std::max(-ret[1], 1.05f * summary.minimum);
    else
        // A fixed fraction of display white, not of the axis top, which grows with the display's headroom
        // and would carry display 0 further off the axis the more of it there is. 1/2000 keeps the asinh
        // axis off the decade where its ticks land a couple of pixels apart and their labels collide.
        ret[0] = pow(2.f, -e) / 2000.f;

    return ret;
}

void PixelStats::calculate(const Channel &img, const Channel *alpha, int2 img_data_origin, const Channel *ref,
                           const Channel *ref_alpha, int2 ref_data_origin, const Settings &desired,
                           std::atomic<bool> &canceled)
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
        if (ref && desired.blend_mode != BlendMode_Normal)
        {
            croi = croi.intersect(rroi);
            spdlog::debug("c and r roi's: {}..{}; {}..{}", croi.min, croi.max, rroi.min, rroi.max);
        }

        spdlog::debug("Image ROI: {}..{}", croi.min, croi.max);

        if (croi.size() != rroi.size())
            spdlog::error("Image and reference channel ROIs are not the same size!");

        // Number of pixels the two passes below visit. Box::intersect() clamps each bound against the other
        // box without keeping min <= max, so a selection that misses the channel leaves an inverted box,
        // whose volume() is negative in one axis and spuriously positive in two; hence has_volume() first.
        const size_t num_pixels = croi.has_volume() ? (size_t)croi.volume() : 0;

        // Report what the file holds: a straight-alpha channel was premultiplied on load, so divide that
        // back out (`alpha` is null for channels that weren't, and for the alpha channel itself).
        auto sample = [](const Channel &c, const Channel *a, int2 p)
        { return a ? c(p) / std::max(k_small_alpha, (*a)(p)) : c(p); };

        auto pixel_value = [&img, alpha, img_data_origin, ref, ref_alpha, &croi, &rroi, sample, this](int i)
        {
            int2 i2d(i % croi.size().x, i / croi.size().x); // convert to 2D coordinates
            if (i2d.y >= croi.size().y)
            {
                // spdlog::error("Pixel index {} ({}) out of bounds for ROI {}..{}", i, i2d, croi.min, croi.max);
                return std::numeric_limits<float>::quiet_NaN(); // out of bounds
            }
            float val = sample(img, alpha, i2d + croi.min - img_data_origin);
            // BlendMode_Normal discards the reference sample (blend() just returns `val`), and croi is only
            // intersected with rroi above when the reference is going to be sampled, so sampling it here
            // regardless of blend mode could index outside ref's bounds when the two channels differ in size.
            if (ref && settings.blend_mode != BlendMode_Normal)
                val = blend(val, sample(*ref, ref_alpha, i2d + croi.min - rroi.min), settings.blend_mode);
            return val;
        };

        //
        // compute pixel summary statistics using Welford's online algorithm
        // https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance

        Timer timer;
        {
            // Local struct to hold partial statistics for each thread
            struct Partial : Summary
            {
                double M2 = 0.0; // Sum of squared differences from mean (for Welford's algorithm)
            };

            size_t               block_size  = 1024 * 1024;
            const size_t         num_threads = estimate_threads(num_pixels, block_size, *ThreadPool::singleton());
            std::vector<Partial> partials(max<size_t>(1, num_threads));

            spdlog::trace("Breaking summary stats into {} work units.", partials.size());

            parallel_for(
                blocked_range<size_t>(0u, num_pixels, block_size),
                [&partials, &canceled, &pixel_value](size_t begin, size_t end, int unit_index, int)
                {
                    Partial partial = partials[unit_index]; //< compute over local symbols.

                    for (size_t i = begin; i != end; ++i)
                    {
                        if (canceled)
                            throw std::runtime_error("canceling summary stats");

                        float val = pixel_value((int)i);

                        if (isnan(val))
                            ++partial.nan_pixels;
                        else
                        {
                            // Every non-NaN sample takes part in the clip warnings, whether or not it is a
                            // measurement -- see Summary::extreme_minimum.
                            partial.extreme_maximum = std::max(partial.extreme_maximum, val);
                            partial.extreme_minimum = std::min(partial.extreme_minimum, val);

                            if (isinf(val))
                                ++partial.inf_pixels;
                            else if (is_marker(val))
                                ++partial.huge_pixels;
                            else
                            {
                                ++partial.valid_pixels;
                                partial.maximum = std::max(partial.maximum, val);
                                partial.minimum = std::min(partial.minimum, val);

                                // Welford's online algorithm for mean and variance
                                double delta = val - partial.average;
                                partial.average += delta / partial.valid_pixels;
                                double delta2 = val - partial.average;
                                partial.M2 += delta * delta2;
                            }
                        }
                    }

                    partials[unit_index] = partial; //< Store partials at the end.
                },
                (int)num_threads);

            // final reduction from partial results using Chan's parallel variance algorithm
            Partial total;
            for (const auto &p : partials)
            {
                summary.minimum         = std::min(p.minimum, summary.minimum);
                summary.maximum         = std::max(p.maximum, summary.maximum);
                summary.extreme_minimum = std::min(p.extreme_minimum, summary.extreme_minimum);
                summary.extreme_maximum = std::max(p.extreme_maximum, summary.extreme_maximum);
                summary.nan_pixels += p.nan_pixels;
                summary.inf_pixels += p.inf_pixels;
                summary.huge_pixels += p.huge_pixels;

                if (p.valid_pixels > 0)
                {
                    // Combine this partial with the running total using Chan's algorithm
                    int    n_A    = total.valid_pixels;
                    double mean_A = total.average;
                    double M2_A   = total.M2;
                    int    n_B    = p.valid_pixels;
                    double mean_B = p.average;
                    double M2_B   = p.M2;

                    int    n_AB  = n_A + n_B;
                    double delta = mean_B - mean_A;
                    // Use numerically stable mean formula (avoids scaling down errors in delta)
                    double mean_AB = (n_A * mean_A + n_B * mean_B) / n_AB;
                    double M2_AB   = M2_A + M2_B + delta * delta * n_A * n_B / n_AB;

                    total.valid_pixels = n_AB;
                    total.average      = mean_AB;
                    total.M2           = M2_AB;
                }
            }

            summary.valid_pixels = total.valid_pixels;
            summary.average      = total.average;

            // Compute final stddev (using sample variance with Bessel's correction)
            summary.stddev = summary.valid_pixels > 1 ? float(std::sqrt(total.M2 / (summary.valid_pixels - 1))) : 0.f;
        }

        spdlog::trace("Summary stats computed in {} ms:\nMin: {}\nMean: {}\nMax: {}\nStddev: {}", timer.lap(),
                      summary.minimum, summary.average, summary.maximum, summary.stddev);
        //

        //
        // compute histograms
        //
        // Bins are laid out uniformly in the same transformed space the x axis is drawn in, so each bin is
        // equally wide on screen and its height can be a plain count. Every scale is binned in this one
        // pass, which costs an extra transform per sample but leaves switching the x axis free. The bin
        // count comes from the source's bit depth, since an n-bit channel cannot fill more bins than it has
        // levels.
        //
        // The bins span the channel's own range, so the histogram never depends on exposure. On a linear x
        // axis a single very bright sample therefore stretches the bins past the visible range and crowds
        // everything into the first few; the nonlinear scales are unaffected.

        // A blended value lies on neither channel's lattice, so this is only exactly right without a
        // reference; it stays the best available bound on how many distinct values can show up.
        num_bins = bins_for_bit_depth(img.bits_per_sample);

        // A channel holding nothing but NaNs, infinities and markers has no range to bin over, and leaves
        // every histogram empty.
        if (std::isfinite(summary.minimum) && std::isfinite(summary.maximum))
        {
            for (int s = 0; s < AxisScale_COUNT; ++s)
            {
                AxisScale x_scale        = (AxisScale)s;
                hist_normalization[s]    = float2{(float)axis_scale_fwd(summary.minimum, x_scale), 0.f};
                hist_normalization[s][1] = (float)axis_scale_fwd(summary.maximum, x_scale) - hist_normalization[s][0];

                // A channel whose samples are all equal transforms to a zero-width range; widen it so that
                // the bin index stays finite and that single value lands in bin 0.
                if (!(hist_normalization[s][1] > 0.f))
                    hist_normalization[s][1] = std::max(std::abs(hist_normalization[s][0]), 1.f) * 1e-6f;

                for (int i = 0; i <= num_bins; ++i) hist_xs[s][i] = (float)bin_to_value(i, x_scale);
            }

            // Counts are accumulated as integers: a large flat region in a many-megapixel image can push a
            // single bin past 2^24, beyond which incrementing a float stops having any effect.
            using Bins = std::array<std::array<uint32_t, MAX_BINS>, AxisScale_COUNT>;

            size_t            block_size  = 1024 * 1024;
            const size_t      num_threads = estimate_threads(num_pixels, block_size, *ThreadPool::singleton());
            std::vector<Bins> partials(max<size_t>(1, num_threads));

            spdlog::trace("Breaking histogram accumulation into {} work units.", partials.size());

            parallel_for(
                blocked_range<size_t>(0u, num_pixels, block_size),
                [&partials, &canceled, &pixel_value, this](size_t begin, size_t end, int unit_index, int)
                {
                    Bins &bins = partials[unit_index];

                    for (size_t i = begin; i != end; ++i)
                    {
                        if (canceled)
                            throw std::runtime_error("Canceling histogram accumulation");

                        float val = pixel_value((int)i);
                        if (!std::isfinite(val) || is_marker(val)) //< the summary counts these instead
                            continue;

                        for (int s = 0; s < AxisScale_COUNT; ++s) ++bins[s][clamp_idx(value_to_bin(val, (AxisScale)s))];
                    }
                },
                (int)num_threads);

            for (const auto &bins : partials)
                for (int s = 0; s < AxisScale_COUNT; ++s)
                    for (int i = 0; i < num_bins; ++i) hist_ys[s][i] += (float)bins[s][i];
        }

        for (int s = 0; s < AxisScale_COUNT; ++s)
        {
            // ImPlot's SymLog y scale is 2*asinh(y/2), which is defined at zero, so both y scales can start
            // at the baseline.
            hist_y_limits[s][0] = 0.f;

            // Take the (drop+1)-th tallest bin, not the tallest, so the one enormous spike a flat
            // background or a clipped highlight produces doesn't squash everything else flat; those few
            // bins then run off the top of the plot.
            const int                   drop = std::min(num_bins - 1, 1 + num_bins / 128);
            std::array<float, MAX_BINS> sorted;
            std::copy_n(hist_ys[s].begin(), num_bins, sorted.begin());
            std::nth_element(sorted.begin(), sorted.begin() + drop, sorted.begin() + num_bins, std::greater<float>());
            hist_y_limits[s][1] = std::max(1.f, sorted[drop]);
        }

        spdlog::trace("Histogram computed in {} ms into {} bins:\nx_limits: {}\ny_limits: {}", timer.lap(), num_bins,
                      float2{summary.minimum, summary.maximum}, hist_y_limits[settings.x_scale]);

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
    case UVorXY_Channels:
        // The histogram fill is composited additively in software (see Image::draw_histogram), so these are
        // emission colors: R+G+B sums to white where all three channels overlap. The alpha is unused by the
        // fill, which applies one overall wash, and is here for the outline/hover-marker paths.
        return float4x4{{1.f, 0.f, 0.f, 0.5f}, {0.f, 1.f, 0.f, 0.5f}, {0.f, 0.f, 1.f, 0.5f}, {1.f, 1.f, 1.f, 0.5f}};
    case YCA_Channels:
    case YC_Channels:
        return float4x4{{1.f, 0.35133642f, 0.5f, 0.5f},
                        {1.f, 1.f, 1.f, 0.5f},
                        {0.5, 0.44952777f, 1.f, 0.5f},
                        {1.0f, 1.0f, 1.0f, 0.5f}};
    case YA_Channels:
    case XYZA_Channels:
    case XYZ_Channels:
    case Z_Channel:
    case Single_Channel:
    default:
        return float4x4{{1.f, 1.f, 1.f, 0.5f}, {1.f, 1.f, 1.f, 0.5f}, {1.f, 1.f, 1.f, 0.5f}, {1.f, 1.f, 1.f, 0.5f}};
    }
}

Channel::Channel(const std::string &name, int2 size) :
    Array2Df(size), cached_stats(make_shared<PixelStats>()), async_stats(make_shared<PixelStats>()), name(name)
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
        // Manual mipmapping: upload_tile() writes a rectangle at a time, and rebuilding the whole mip chain
        // per tile would cost far more than the tile itself.
        texture = std::make_unique<Texture>(Texture::PixelFormat::R, Texture::ComponentFormat::Float32, size(),
                                            min_mode, Texture::InterpolationMode::Nearest,
                                            Texture::WrapMode::ClampToEdge, 1, Texture::TextureFlags::ShaderRead,
                                            /* manual_mipmapping */ true);
        if (texture->pixel_format() != Texture::PixelFormat::R)
            throw std::invalid_argument("Pixel format not supported by the hardware!");

        texture->upload((const uint8_t *)data());
        texture_is_dirty = false;
        mipmap_is_dirty  = true;
    }

    // Once per draw, whatever landed since the last one. Trilinear minification samples the mip chain, so
    // this has to happen before the texture is used, not lazily at the next upload.
    if (mipmap_is_dirty)
    {
        if (texture->min_interpolation_mode() == Texture::InterpolationMode::Trilinear ||
            texture->mag_interpolation_mode() == Texture::InterpolationMode::Trilinear)
            texture->generate_mipmap();
        mipmap_is_dirty = false;
    }

    return texture.get();
}

void Channel::upload_tile(const Box2i &bounds, const float *data, int64_t x_stride, int64_t y_stride)
{
    // Boxes here are half-open, so a clip that collapses a dimension leaves max == min rather than
    // max < min, and is_empty() would not call that empty.
    Box2i clipped = bounds;
    clipped.intersect(Box2i{int2{0}, size()});
    const int2 extent = clipped.size();
    if (extent.x <= 0 || extent.y <= 0)
        return;

    if (y_stride == 0)
        y_stride = int64_t(bounds.size().x) * x_stride;

    // A statistics task reads these pixels from a worker thread, so it has to be off them before the write.
    cancel_stats();

    const int2 offset = clipped.min - bounds.min;

    for (int y = 0; y < extent.y; ++y)
    {
        const float *src = data + (int64_t(y + offset.y) * y_stride) + (int64_t(offset.x) * x_stride);
        float       *dst = &operator()(clipped.min.x, clipped.min.y + y);
        for (int x = 0; x < extent.x; ++x) dst[x] = src[int64_t(x) * x_stride];
    }

    if (!texture || texture_is_dirty)
        return; // no texture yet, or one that is about to be rebuilt from the channel wholesale

    // glTexSubImage2D wants the rectangle packed on its own, and the channel's rows are the full width.
    std::vector<float> staging(size_t(extent.x) * size_t(extent.y));
    for (int y = 0; y < extent.y; ++y)
        std::copy_n(&operator()(clipped.min.x, clipped.min.y + y), extent.x, &staging[size_t(y) * extent.x]);

    texture->upload_sub_region((const uint8_t *)staging.data(), clipped.min, extent);
    mipmap_is_dirty = true;
}

bool PixelStats::Settings::match(const Settings &other) const
{
    // Only what changes the computed values counts. exposure never reaches the computation, every x_scale
    // is binned, and y_scale only picks which stored histogram the plot draws.
    return (blend_mode == other.blend_mode && ref_id == other.ref_id && ref_group == other.ref_group) &&
           other.roi == roi && content_version == other.content_version &&
           ref_content_version == other.ref_content_version;
}

PixelStats *Channel::get_stats()
{
    MY_ASSERT(cached_stats, "PixelStats::cached_stats should never be null");

    // We always return the cached stats, but before we do we might update the cache from the async stats
    if (async_tracker.ready() && async_stats->computed)
    {
        spdlog::trace("Replacing cached channel stats with async computation");
        adopt_async_stats();
    }

    return cached_stats.get();
}

void Channel::update_stats(int c, ConstImagePtr img1, ConstImagePtr img2)
{
    MY_ASSERT(cached_stats, "PixelStats::cached_stats should never be null");

    PixelStats::Settings desired_settings{hdrview()->exposure(),
                                          hdrview()->histogram_x_scale(),
                                          hdrview()->histogram_y_scale(),
                                          hdrview()->roi(),
                                          hdrview()->blend_mode(),
                                          img2 ? img2->id : -1,
                                          img2 ? img2->reference_group : -1,
                                          img1->content_version,
                                          img2 ? img2->content_version : 0};

    // Pixels streaming in from a renderer change the content version faster than a full pass over the image
    // can finish, and chasing every version would cancel each computation partway. So a newer version is
    // only picked up once the cache has stood as a finished result for a moment.
    static constexpr auto k_min_stats_age = std::chrono::milliseconds{200};
    if (cached_stats->computed && std::chrono::steady_clock::now() - stats_ready_at < k_min_stats_age)
    {
        desired_settings.content_version     = cached_stats->settings.content_version;
        desired_settings.ref_content_version = cached_stats->settings.ref_content_version;
    }

    // The group's alpha channel, when this channel needs dividing by it to report the file's values; null
    // for the alpha channel itself, which is stored as the file had it.
    auto alpha_of = [c](const ConstImagePtr &img, int group_idx) -> const Channel *
    {
        if (!img || !img->is_valid_group(group_idx))
            return nullptr;
        const ChannelGroup &g = img->groups[group_idx];
        return img->unpremultiplies(g) && c != g.num_channels - 1 ? &img->channels[g.channels[g.num_channels - 1]]
                                                                  : nullptr;
    };

    // img2 is captured so the reference image outlives the task: the raw Channel pointers below point into
    // it, and closing it drops the app's last reference. img1 owns this Channel, whose destructor cancels
    // and waits for the task.
    auto recompute_async_stats = [this, desired_settings, img1, img2, img_data_origin = img1->data_window.min,
                                  alpha           = alpha_of(img1, img1->selected_group),
                                  ref             = (img2 && img2->is_valid_group(img2->reference_group))
                                                        ? &img2->channels[img2->groups[img2->reference_group].channels[c]]
                                                        : nullptr,
                                  ref_alpha       = alpha_of(img2, img2 ? img2->reference_group : -1),
                                  ref_data_origin = img2 ? img2->data_window.min : int2{}]()
    {
        spdlog::debug("id: {}", img1->id);
        // First cancel the potential previous async task
        if (async_canceled)
        {
            spdlog::trace("Canceling outdated stats computation.");
            *async_canceled = true;
            async_tracker.wait();
        }

        // create the new task
        async_canceled = make_shared<atomic<bool>>(false);
        async_tracker  = do_async(
            [this, desired_settings, canceled = async_canceled, img2, img_data_origin, alpha, ref, ref_alpha,
             ref_data_origin]()
            {
                spdlog::debug("Starting a new stats computation");
                async_stats->calculate(*this, alpha, img_data_origin, ref, ref_alpha, ref_data_origin, desired_settings,
                                        *canceled);
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
        adopt_async_stats();

        // if these newer stats are still outdated, schedule a new async computation
        if (!cached_stats->settings.match(desired_settings))
            recompute_async_stats();
    }
}

void Image::set_null_texture(Target_ target)
{
    auto s = hdrview()->shader();
    auto t = target_name(target);

    for (int c = 0; c < 4; ++c) s->set_texture(fmt::format("{}_{}_texture", t, c), black_texture());
}

void Image::set_as_texture(Target_ target)
{
    auto                s         = hdrview()->shader();
    auto                t         = target_name(target);
    int                 group_idx = active_group_index(target);
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
            s->set_texture(fmt::format("{}_{}_texture", t, 1), channels[group.channels[1]].get_texture());
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
    channels.reserve(num_channels);
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
    set_default_transparency();
}

Image::Image(int2 size, const std::vector<std::string> &channel_names) : Image()
{
    if (channel_names.empty())
        throw std::invalid_argument("An image must have at least one channel.");

    channels.reserve(channel_names.size());
    for (const auto &name : channel_names) channels.emplace_back(name, size);

    display_window = data_window = Box2i{int2{0}, size};
    set_default_transparency();
}

void Image::set_transparency(TransparencyType_ from_file, const std::optional<TransparencyType_> &override_with,
                             bool assumed)
{
    transparency_from_file = from_file;
    transparency_assumed   = assumed;
    transparency           = override_with.value_or(from_file);
}

void Image::set_default_transparency()
{
    for (const auto &c : channels)
        if (auto tail = Channel::tail(c.name); tail == "A" || tail == "a")
        {
            // Assembled rather than read, so nothing declared this; see set_default_transparency()'s comment.
            set_transparency(TransparencyType_PremultipliedLinear, std::nullopt, true);
            return;
        }

    set_transparency(TransparencyType_None, std::nullopt, true);
}

map<string, int> Image::channels_in_layer(const string &layer) const
{
    map<string, int> result;

    for (int i = 0; i < (int)channels.size(); ++i)
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
    auto find_group_channels = [this](map<string, int> &channels, const string &prefix, const vector<string> &g)
    {
        spdlog::trace("Trying to find channels '{}' in {} layer channels", fmt::join(g, ","), channels.size());
        for (auto c : channels) spdlog::trace("\t{}: {}", c.second, c.first);
        vector<map<string, int>::iterator> found;
        found.reserve(g.size());
        for (const string &c : g)
        {
            string name = prefix + c;
            auto   it   = channels.find(name);

            // A channel asked to stand alone is not available to match, so the pattern comes up short and
            // every channel it would have taken falls through to a group of its own. Marking one channel of
            // an RGBA set still leaves the other three as RGB, since that pattern is tried next.
            if (it != channels.end() && !this->channels[size_t(it->second)].ungrouped)
                found.push_back(it);
        }
        return found;
    };

    // Everything below appends, so anything left from a previous build would be duplicated: the layer tree
    // would gain a second leaf at every node and the channel counts would stop agreeing with the channels.
    layers.clear();
    groups.clear();
    root = LayerTreeNode{};

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
            auto path_ = Channel::split_to_path(layer.name);

            for (auto d : path_)
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

            node->leaf_layer = (int)layers.size() - 1;
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
            // Skipping the alpha-bearing patterns lets the alpha-free one match instead, leaving 'A' to
            // fall through to the single-channel groups created below.
            if (!alpha_is_transparency() && group_has_alpha(group_type))
                continue;
            auto found = find_group_channels(layer_channels, layer.name, group_channel_names);

            // if we found all the group channels, then create them and remove from list of all channels
            if (found.size() == group_channel_names.size())
            {
                MY_ASSERT(found.size() <= 4, "ChannelGroups can have at most 4 channels!");
                int4 group_channels;
                for (int i2 = 0; i2 < (int)found.size(); ++i2)
                {
                    group_channels[i2] = found[i2]->second;
                    spdlog::debug("Found channel '{}': {}", group_channel_names[i2], found[i2]->second);
                }

                layer.groups.emplace_back((int)groups.size());
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
                layer.groups.emplace_back((int)groups.size());
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
        spdlog::debug("Detected color space: '{}'", color_gamut_name((ColorGamut_)color_space));

        white_point = named_white_point(chr.white);
        spdlog::debug("Detected white point: '{}'", white_point_name(white_point));
    }
}

// These move the samples, so the windows have to move with them. Only matters when the display window is
// something other than the whole frame, as a raw CFA part's sensor active area is.
void Image::reflect_windows(bool horizontal)
{
    const int extent  = horizontal ? data_window.min.x + data_window.max.x : data_window.min.y + data_window.max.y;
    auto      reflect = [extent, horizontal](Box2i &b)
    {
        int      &lo     = horizontal ? b.min.x : b.min.y;
        int      &hi     = horizontal ? b.max.x : b.max.y;
        const int new_lo = extent - hi, new_hi = extent - lo;
        lo = new_lo;
        hi = new_hi;
    };
    reflect(display_window);
    reflect(data_window);
}

void Image::transpose_windows()
{
    auto swap_axes = [](Box2i &b)
    {
        std::swap(b.min.x, b.min.y);
        std::swap(b.max.x, b.max.y);
    };
    swap_axes(display_window);
    swap_axes(data_window);
}

void Image::flip_horizontal()
{
    for (auto &channel : channels)
    {
        int w          = channel.width();
        int h          = channel.height();
        int block_size = std::max(1, 1024 * 1024 / w);
        stp::parallel_for(stp::blocked_range<int>(0, h, block_size),
                          [&](int y0, int y1, int /*unit*/, int /*thread*/)
                          {
                              for (int y = y0; y < y1; ++y)
                                  for (int x = 0; x < w / 2; ++x) std::swap(channel(x, y), channel(w - 1 - x, y));
                          });
        channel.texture_is_dirty = true;
    }
    reflect_windows(true);
}

void Image::flip_vertical()
{
    for (auto &channel : channels)
    {
        int w          = channel.width();
        int h          = channel.height();
        int block_size = std::max(1, 1024 * 1024 / h);
        stp::parallel_for(stp::blocked_range<int>(0, w, block_size),
                          [&](int x0, int x1, int /*unit*/, int /*thread*/)
                          {
                              for (int x = x0; x < x1; ++x)
                                  for (int y = 0; y < h / 2; ++y) std::swap(channel(x, y), channel(x, h - 1 - y));
                          });
        channel.texture_is_dirty = true;
    }
    reflect_windows(false);
}

void Image::transpose()
{
    for (auto &channel : channels)
    {
        Array2Df tmp(channel.height(), channel.width());
        int      block_size = std::max(1, 1024 * 1024 / channel.width());
        stp::parallel_for(stp::blocked_range<int>(0, channel.height(), block_size),
                          [&](int y0, int y1, int /*unit*/, int /*thread*/)
                          {
                              for (int y = y0; y < y1; ++y)
                                  for (int x = 0; x < channel.width(); ++x) tmp(y, x) = channel(x, y);
                          });
        channel.resize(tmp.size());
        stp::parallel_for(stp::blocked_range<int>(0, tmp.height(), block_size),
                          [&](int y0, int y1, int /*unit*/, int /*thread*/)
                          {
                              for (int y = y0; y < y1; ++y)
                                  for (int x = 0; x < tmp.width(); ++x) channel(x, y) = tmp(x, y);
                          });
        // The channel changed shape, so its texture has to be rebuilt rather than sub-updated.
        channel.texture_is_dirty = true;
    }
    transpose_windows();
}

// Composed from a transpose and a flip, matching how the EXIF orientations below are built. Which flip
// follows the transpose is what distinguishes the two directions.
void Image::rotate_90_cw()
{
    transpose();
    flip_horizontal();
}

void Image::rotate_90_ccw()
{
    transpose();
    flip_vertical();
}

void Image::resample(int2 size)
{
    if (size.x <= 0 || size.y <= 0 || size == data_window.size())
        return;

    const int2 old_size = data_window.size();

    for (auto &channel : channels)
    {
        Array2Df out{size};

        // stb's resampler chooses a filter from the scale -- averaging on the way down, interpolating on
        // the way up -- and deals with the edges. One channel at a time, since these are stored planar.
        if (!stbir_resize_float_linear(channel.data(), old_size.x, old_size.y, 0, out.data(), size.x, size.y, 0,
                                       STBIR_1CHANNEL))
            throw std::runtime_error{"Failed to resample image."};

        channel.resize(size);
        std::copy(out.data(), out.data() + out.num_elements(), channel.data());
        channel.texture_is_dirty = true;
    }

    data_window    = Box2i{data_window.min, data_window.min + size};
    display_window = data_window;
}

void Image::rebuild_layers()
{
    build_layers_and_groups();

    // The channel list may have shrunk, so an index that was valid before need not be now.
    if (!is_valid_group(selected_group))
        selected_group = groups.empty() ? -1 : 0;
    if (!is_valid_group(reference_group))
        reference_group = -1;
}

ImagePtr Image::duplicate(const Box2i &region) const
{
    Box2i clipped = region.has_volume() ? region : data_window;
    clipped.intersect(data_window);
    if (!clipped.has_volume())
        return nullptr;

    auto copy = std::make_shared<Image>();

    // Everything that says what the samples mean. A copy that lost its primaries or its alpha convention
    // would be read differently from the image it was made of, which is not what "duplicate" means.
    copy->filename               = filename;
    copy->partname               = partname;
    copy->channel_selector       = channel_selector;
    copy->chromaticities         = chromaticities;
    copy->adopted_neutral        = adopted_neutral;
    copy->M_RGB_to_XYZ           = M_RGB_to_XYZ;
    copy->M_XYZ_to_RGB           = M_XYZ_to_RGB;
    copy->M_to_sRGB              = M_to_sRGB;
    copy->luminance_weights      = luminance_weights;
    copy->adaptation_method      = adaptation_method;
    copy->color_space            = color_space;
    copy->white_point            = white_point;
    copy->transparency           = transparency;
    copy->transparency_from_file = transparency_from_file;
    copy->transparency_assumed   = transparency_assumed;
    copy->transparency_override  = transparency_override;
    copy->metadata               = metadata;
    copy->exif                   = exif;
    copy->xmp_data               = xmp_data;
    copy->icc_data               = icc_data;
    copy->orientation_applied    = orientation_applied;
    copy->path                   = path;
    copy->last_modified          = last_modified;
    copy->size_bytes             = size_bytes;

    // Not copied: `id`, which the constructor hands out; `history`, since nothing has been done to the
    // copy; `is_live`, since these samples are a snapshot; and `vector_overlay`, which annotates what a
    // renderer is producing.

    const int2 extent = clipped.size();
    const int2 offset = clipped.min - data_window.min;

    copy->channels.reserve(channels.size());
    for (const auto &channel : channels)
    {
        // Channel cannot be copied -- the deleted copy is what stops a texture or a statistics task being
        // duplicated by accident -- so a fresh one is filled from this one's samples.
        Channel out{channel.name, extent};
        out.bits_per_sample = channel.bits_per_sample;

        const int block_size = std::max(1, 1024 * 1024 / std::max(1, extent.x));
        stp::parallel_for(stp::blocked_range<int>(0, extent.y, block_size),
                          [&](int y0, int y1, int, int)
                          {
                              for (int y = y0; y < y1; ++y)
                                  for (int x = 0; x < extent.x; ++x) out(x, y) = channel(offset.x + x, offset.y + y);
                          });

        copy->channels.push_back(std::move(out));
    }

    // A duplicated region is a whole image, not a crop sitting inside the old canvas -- but one of the
    // whole image keeps the frame it had, display window and all.
    if (clipped == data_window)
    {
        copy->data_window    = data_window;
        copy->display_window = display_window;
    }
    else
        copy->data_window = copy->display_window = clipped;

    // The layers and groups follow from the channel names, and finalize() would premultiply a
    // straight-alpha image a second time -- these samples are already whatever this image's are.
    copy->rebuild_layers();

    return copy;
}

void Image::crop(const Box2i &box)
{
    Box2i clipped = box;
    clipped.intersect(data_window);
    const int2 extent = clipped.size();
    if (extent.x <= 0 || extent.y <= 0)
        return;

    const int2 offset = clipped.min - data_window.min;

    for (auto &channel : channels)
    {
        Array2Df  cropped{extent};
        const int block_size = std::max(1, 1024 * 1024 / std::max(1, extent.x));
        stp::parallel_for(stp::blocked_range<int>(0, extent.y, block_size),
                          [&](int y0, int y1, int, int)
                          {
                              for (int y = y0; y < y1; ++y)
                                  for (int x = 0; x < extent.x; ++x)
                                      cropped(x, y) = channel(offset.x + x, offset.y + y);
                          });

        channel.resize(extent);
        std::copy(cropped.data(), cropped.data() + cropped.num_elements(), channel.data());
        // A different shape, so the texture is rebuilt rather than updated in place.
        channel.texture_is_dirty = true;
    }

    // What is left is the whole image now, not a crop sitting inside the old canvas.
    data_window    = clipped;
    display_window = clipped;
}

void Image::resize_canvas(int2 size, CanvasAnchor anchor)
{
    if (size.x <= 0 || size.y <= 0 || size == data_window.size())
        return;

    const int2 old_size = data_window.size();

    // Where the old samples land in the new canvas: the anchor picks which edges absorb the difference,
    // and a negative offset simply means that edge is being cut rather than padded.
    const int  col = int(anchor) % 3, row = int(anchor) / 3;
    const int2 offset{(size.x - old_size.x) * col / 2, (size.y - old_size.y) * row / 2};

    for (auto &channel : channels)
    {
        Array2Df resized{size}; // zero-filled: transparent wherever the image has alpha

        // Only the overlap is copied; everything else keeps the fill.
        const int2 lo{std::max(0, offset.x), std::max(0, offset.y)};
        const int2 hi{std::min(size.x, offset.x + old_size.x), std::min(size.y, offset.y + old_size.y)};
        if (hi.x > lo.x && hi.y > lo.y)
        {
            const int block_size = std::max(1, 1024 * 1024 / std::max(1, hi.x - lo.x));
            stp::parallel_for(stp::blocked_range<int>(lo.y, hi.y, block_size),
                              [&](int y0, int y1, int, int)
                              {
                                  for (int y = y0; y < y1; ++y)
                                      for (int x = lo.x; x < hi.x; ++x)
                                          resized(x, y) = channel(x - offset.x, y - offset.y);
                              });
        }

        channel.resize(size);
        std::copy(resized.data(), resized.data() + resized.num_elements(), channel.data());
        channel.texture_is_dirty = true;
    }

    data_window    = Box2i{data_window.min, data_window.min + size};
    display_window = data_window;
}

void Image::apply_exif_orientation()
{
    // --- EXIF orientation handling ---
    if (!metadata.contains("exif") || !metadata["exif"].is_object())
        return;

    // Look for EXIF orientation field (commonly in "Orientation" or "orientation")
    int  orientation = 1;
    json orientation_metadata;
    for (auto &exif_entry : metadata["exif"].items())
    {
        if (exif_entry.value().contains("orientation"))
        {
            orientation_metadata = exif_entry.value()["orientation"];
            break;
        }
        else if (exif_entry.value().contains("Orientation"))
        {
            orientation_metadata = exif_entry.value()["Orientation"];
            break;
        }
    }

    if (orientation_metadata.is_object() && orientation_metadata.contains("value"))
    {
        // Orientation is usually an integer (1-8)
        if (orientation_metadata["value"].is_number_integer())
            orientation = orientation_metadata["value"].get<int>();
    }

    if (orientation != 1)
    {
        spdlog::debug("Applying EXIF orientation: {}", orientation);

        // Apply orientation according to EXIF spec
        // 1 = Horizontal (normal)
        // 2 = Mirror horizontal
        // 3 = Rotate 180
        // 4 = Mirror vertical
        // 5 = Mirror horizontal and rotate 270 CW
        // 6 = Rotate 90 CW
        // 7 = Mirror horizontal and rotate 90 CW
        // 8 = Rotate 270 CW
        switch (orientation)
        {
        case 2: flip_horizontal(); break;
        case 3:
            flip_horizontal();
            flip_vertical();
            break;
        case 4: flip_vertical(); break;
        case 5: transpose(); break;
        case 6:
            transpose();
            flip_horizontal();
            break;
        case 7:
            transpose();
            flip_vertical();
            flip_horizontal();
            break;
        case 8:
            transpose();
            flip_vertical();
            break;
        default: break;
        }
        orientation_applied = true;
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

    // Centralized XMP parsing: if loaders stored raw XMP into image->xmp_data or into exif metadata, parse
    // it once here and populate metadata["xmp"] with structured JSON. XMP decorates an image, it does not
    // describe its pixels, so anything thrown while deriving it costs the metadata and nothing else.
    try
    {
        if (!metadata.contains("xmp"))
        {
            // spdlog::warn("XMP metadata not yet parsed; attempting to parse from raw data.");
            if (metadata.contains("exif") && metadata["exif"].is_object())
            {
                // Check EXIF entries for raw XMP stored by exif parser (tag 700)
                for (auto &it : metadata["exif"].items())
                {
                    auto &v = it.value();
                    if (!v.is_object())
                        continue;

                    if (v.contains("XMP Metadata") && v["XMP Metadata"].is_object() &&
                        v["XMP Metadata"]["value"].is_string())
                    {
                        std::string s = v["XMP Metadata"]["value"].get<std::string>();
                        if (s.find("http://ns.adobe.com/xap/1.0/") != std::string::npos ||
                            s.find("x:xmpmeta") != std::string::npos || s.find("<?xpacket") != std::string::npos)
                        {
                            if (xmp_data.empty())
                            {
                                xmp_data.assign(s.begin(), s.end());
                                spdlog::debug("Reading XMP data from EXIF XMP Metadata tag.");
                            }
                            else
                            {
                                spdlog::warn("Image contains both xpacket XMP data ({} bytes) and an XMP EXIF tag; "
                                             "prioritizing xpacket buffer.",
                                             xmp_data.size());
                            }
                            break;
                        }
                    }
                }
            }

            if (!xmp_data.empty())
            {
                spdlog::debug("Parsing XMP from {} byte buffer:\n{}", xmp_data.size(),
                              std::string(reinterpret_cast<const char *>(xmp_data.data()), xmp_data.size()));
                Xmp xmp(reinterpret_cast<const char *>(xmp_data.data()), xmp_data.size());
                if (xmp.valid())
                {
                    spdlog::debug("Successfully parsed XMP metadata.");
                    metadata["xmp"] = xmp.to_json();
                }
                else
                {
                    spdlog::warn("Failed to parse XMP metadata from raw xmp_data buffer.");
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        spdlog::warn("Could not read this image's XMP metadata: {}. Loading it without.", e.what());
    }

    // sanity check all channels have the same size as the data window
    for (const auto &c : channels)
        if (c.size() != data_window.size())
            throw runtime_error{
                fmt::format("All channels must have the same size as the data window. ({}:{}x{} != {}x{})", c.name,
                            c.size().x, c.size().y, data_window.size().x, data_window.size().y)};

    // reject images whose channels can't be uploaded as a texture on this graphics backend (e.g. Metal aborts
    // outright rather than failing gracefully when asked to create an oversized texture)
    if (int lim = Texture::max_size(); data_window.size().x > lim || data_window.size().y > lim)
        throw runtime_error{fmt::format("Image dimensions {}x{} exceed the maximum texture size ({}) supported by "
                                        "your graphics hardware.",
                                        data_window.size().x, data_window.size().y, lim)};

    build_layers_and_groups();

    if (!orientation_applied)
        apply_exif_orientation();

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
    if (transparency == TransparencyType_Straight)
    {
        for (auto &g : groups)
        {
            if (g.num_channels <= 1 || !group_has_alpha(g.type))
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
        out += indent(fmt::format("{}{:::> 8.5f}\n", l, M_to_sRGB), false, (int)l.length());
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

std::vector<int> Image::selected_groups() const
{
    std::vector<int> out;
    for (int g = 0; g < (int)groups.size(); ++g)
        if (is_group_selected(g))
            out.push_back(g);
    return out;
}

int Image::next_visible_group_index(int index, Direction_ direction) const
{
    return next_matching_index(groups, index, [](size_t, const ChannelGroup &g) { return g.visible; }, direction);
}

int Image::nth_visible_group_index(int n) const
{
    return (int)nth_matching_index(groups, (size_t)n, [](size_t, const ChannelGroup &g) { return g.visible; });
}

float4 Image::raw_pixel(int2 p, Target_ target) const
{
    if (!contains(p))
        return float4{0.f};

    int                 group_idx = active_group_index(target);
    const ChannelGroup &group     = groups[group_idx];

    float4 value{0.f};
    for (int c = 0; c < group.num_channels; ++c) value[c] = channels[group.channels[c]](p - data_window.min);

    if (unpremultiplies(group))
    {
        float a = std::max(k_small_alpha, value[group.num_channels - 1]);
        for (int c = 0; c < group.num_channels - 1; ++c) value[c] /= a;
    }

    return value;
}

/// Reconstruct the raw pixel value into an RGBA value (like the first stage of the fragment shader)
std::unique_ptr<uint8_t[]> widen_to_rgb(const uint8_t *src, int w, int h, int n, bool gray, int *n_out)
{
    const bool   has_alpha  = gray && n == 2;
    const int    out        = has_alpha ? 4 : 3;
    const size_t num_pixels = (size_t)w * h;

    std::unique_ptr<uint8_t[]> dst(new uint8_t[num_pixels * out]);
    for (size_t i = 0; i < num_pixels; ++i)
    {
        const uint8_t *in = src + i * n;
        uint8_t       *o  = dst.get() + i * out;
        if (gray)
            o[0] = o[1] = o[2] = in[0];
        else
        {
            o[0] = in[0];
            o[1] = n > 1 ? in[1] : uint8_t(0);
            o[2] = uint8_t(0);
        }
        if (has_alpha)
            o[3] = in[1];
    }
    *n_out = out;
    return dst;
}

float4 Image::rgba_pixel(int2 p, Target_ target) const
{
    if (!contains(p))
        return float4{0.f};

    int                 group_idx = active_group_index(target);
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

static void calculate_tree_visibility(LayerTreeNode &node, const Image *image)
{
    node.visible_groups = 0;
    node.hidden_groups  = 0;
    if (node.leaf_layer >= 0)
    {
        auto &layer = image->layers[node.leaf_layer];
        for (size_t g = 0; g < layer.groups.size(); ++g)
            if (image->groups[layer.groups[g]].visible)
                ++node.visible_groups;
            else
                ++node.hidden_groups;
    }

    for (auto &[child_name, child_node] : node.children)
    {
        calculate_tree_visibility(child_node, image);
        node.visible_groups += child_node.visible_groups;
        node.hidden_groups += child_node.hidden_groups;
    }
}

void LayerTreeNode::calculate_visibility(const Image *img) { calculate_tree_visibility(*this, img); }
