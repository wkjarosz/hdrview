//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "edit/envmap.h"

#include "common.h"

#include <smallthreadpool.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <vector>

namespace
{

constexpr float k_pi = shader::ENVMAP_PI;

// the cube's faces, which the image shader shares with the mappings (src/shared/envmap.h)
using shader::cube_column_face_vector;
using shader::cube_face_vector;
using shader::cube_map_face_vector;

} // namespace

const char *envmapping_name(int mapping)
{
    switch (mapping)
    {
    case EnvMapping_Angular: return "Angular map";
    case EnvMapping_MirrorBall: return "Mirror ball";
    case EnvMapping_LatLong: return "Longitude-latitude";
    case EnvMapping_Cylindrical: return "Cylindrical";
    case EnvMapping_CubeMap: return "Cube map (vertical cross)";
    case EnvMapping_CubeMapColumn: return "Cube map (vertical column)";
    case EnvMapping_EqualArea: return "Equal area";
    default: return "";
    }
}

float envmapping_aspect(int mapping)
{
    switch (mapping)
    {
    case EnvMapping_LatLong:
    case EnvMapping_Cylindrical: return 2.f;         // a full turn across, half a turn down
    case EnvMapping_CubeMap: return 3.f / 4.f;       // three faces across by four down
    case EnvMapping_CubeMapColumn: return 1.f / 6.f; // one across by six down
    default: return 1.f;                             // the discs and the equal-area square
    }
}

float envmap_jacobian(EnvMapping mapping, float2 uv)
{
    if (!envmap_uv_is_valid(mapping, uv))
        return 0.f;

    constexpr float k_four_pi = 4.f * k_pi;

    switch (mapping)
    {
    case EnvMapping_LatLong:
        // dw = sin(phi) dtheta dphi, with theta spanning 2*pi across u and phi spanning pi down v.
        return 2.f * k_pi * k_pi * std::sin(k_pi * uv.y);

    case EnvMapping_Cylindrical: return k_four_pi;

    case EnvMapping_EqualArea:
        // equal-area by construction
        return k_four_pi;

    case EnvMapping_MirrorBall:
        // Also equal-area, which is less obvious: the sine of half the polar angle growing linearly with
        // the radius is exactly the condition for it.
        return 16.f;

    case EnvMapping_Angular:
    {
        // Here the polar angle itself grows linearly with the radius, so the sky compresses towards the rim.
        const float r = la::length(2.f * uv - float2{1.f});
        return r < 1e-6f ? k_four_pi * k_pi : k_four_pi * std::sin(k_pi * r) / r;
    }

    case EnvMapping_CubeMapColumn:
    {
        // As below, but the column gives each face a sixth of the image where the cross gives a twelfth.
        const float3 v = cube_column_face_vector(uv);
        const float  l = la::length(v);
        return 24.f / (l * l * l);
    }

    default:
    {
        // A cube face's solid angle per unit face area is one over the cube of the distance to it; the
        // cross packs each face into a sixth of the image, which is where the constant comes from.
        const float3 v = cube_map_face_vector(uv);
        const float  l = la::length(v);
        return 48.f / (l * l * l);
    }
    }
}

// -------------------------------------------------------------------------------------------------
// Reading the source by direction, through src/shared/envmap_sample.h, which the image shader's preview shares
// -------------------------------------------------------------------------------------------------

namespace shader
{
using EnvValue = float;

/// The source as envmap_sample.h reads it on the CPU: each level's charts, a cube map's six faces to a level.
struct EnvSource
{
    int                    mapping;
    int                    levels;
    float                  ratio;
    float                  spread;
    ivec2                  size;
    const Array2Df *const *charts;
};

inline float envmap_fetch(EnvSource src, int level, int chart, ivec2, ivec2 p)
{ return (*src.charts[level * (envmap_is_cube(src.mapping) ? 6 : 1) + chart])(p.x, p.y); }
} // namespace shader

#include "shared/envmap_sample.h"

namespace
{

/// How many times larger each mip level's texels are than the last's: half an octave.
/**
    Blending two levels (envmap_ewa()) gets the blur's variance right but not its Gaussian shape, and lets more past
    Nyquist through the further apart the levels are: a whole octave apart aliases about three times as much as half
    an octave.
*/
constexpr float k_level_ratio = 1.41421356f; // sqrt(2)

/// The Gaussian blur before each step down, in texels of the level it shrinks.
/**
    Enough to remove most of what the smaller level cannot hold, which would otherwise alias to low frequencies that no
    later filter can tell from real content.
*/
constexpr float k_level_sigma = 0.85f;

/// Bilinear read of \p a at \p uv in [0,1]^2, clamped at its edges.
float bilinear_clamped(const Array2Df &a, float2 uv)
{
    const float x = uv.x * float(a.width()) - 0.5f, y = uv.y * float(a.height()) - 0.5f;
    const int   x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float tx = x - float(x0), ty = y - float(y0);
    auto at = [&a](int i, int j) { return a(std::clamp(i, 0, a.width() - 1), std::clamp(j, 0, a.height() - 1)); };

    return (1.f - ty) * ((1.f - tx) * at(x0, y0) + tx * at(x0 + 1, y0)) +
           ty * ((1.f - tx) * at(x0, y0 + 1) + tx * at(x0 + 1, y0 + 1));
}

/**
    A source image prepared to be read by direction: its charts at every mip level, each a Gaussian blur of the last by
    k_level_sigma, k_level_ratio times smaller, so that what a read at a level blurs by is known exactly.

    A cube map's layout is six charts in one image: texels either side of a face join are not neighboring directions,
    some cells stand for no direction, and a pyramid over the whole picture averages both into the faces. So a cube map
    is taken apart into six faces, each with its own levels, which read past their edges by direction; every other
    mapping is one chart, the image, which wraps as envmap_wrap_texel() says.
*/
class EnvLevels
{
public:
    /// \p with_mips builds the levels EWA reads; supersampling reads only the finest.
    EnvLevels(const Array2Df &src, EnvMapping mapping, bool with_mips)
    {
        m_source.mapping = mapping;
        m_source.levels  = 1;
        m_source.ratio   = k_level_ratio;
        m_source.spread  = k_level_sigma * k_level_sigma;
        m_source.size    = src.size();

        if (shader::envmap_is_cube(mapping))
        {
            // one source texel per face texel whenever the faces divide the image evenly, since the two grids then
            // share centers; any other shape is resampled onto this one
            const int n = shader::envmap_face_size(mapping, src.size());
            for (int f = 0; f < 6; ++f)
            {
                Array2Df &face = m_store.emplace_back(int2{n, n});
                for (int j = 0; j < n; ++j)
                    for (int i = 0; i < n; ++i)
                    {
                        const float2 st{(float(i) + 0.5f) / float(n), (float(j) + 0.5f) / float(n)};
                        face(i, j) = bilinear_clamped(src, envmap_xyz_to_uv(mapping, cube_face_vector(f, st)));
                    }
                m_charts.push_back(&face);
            }
        }
        else
            m_charts.push_back(&src);

        while (with_mips && la::maxelem(shader::envmap_chart_size(source(), m_source.levels - 1)) > 1) add_level();
    }

    /// What envmap_sample.h reads.
    shader::EnvSource source() const
    {
        shader::EnvSource s = m_source;
        s.charts            = m_charts.data();
        return s;
    }

private:
    /// Which texels of the last level one row or column of the next reads, and the normalized Gaussian weights.
    struct Taps
    {
        int   first = 0, count = 0;
        float w[8];
    };

    /// The taps for each of \p n new texels along one axis, spaced \p scale old texels apart.
    static std::vector<Taps> taps_along(int n, float scale)
    {
        const float       r = 3.f * k_level_sigma;
        std::vector<Taps> taps(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            // the new texel's center, in the old level's texels
            const float at = (float(i) + 0.5f) * scale - 0.5f;
            Taps       &t  = taps[size_t(i)];
            t.first        = int(std::ceil(at - r));
            t.count        = std::min(8, int(std::floor(at + r)) - t.first + 1);
            float total    = 0.f;
            for (int k = 0; k < t.count; ++k)
                total += t.w[k] = std::exp(-0.5f * sqr((float(t.first + k) - at) / k_level_sigma));
            for (int k = 0; k < t.count; ++k) t.w[k] /= total;
        }
        return taps;
    }

    /// The next level: each chart of the last one, Gaussian-blurred and shrunk, read past its edges as the sphere is.
    /**
        The Gaussian is separable, so a texel whose taps all lie inside the chart is two passes of a few taps each;
        only those near an edge, whose taps wrap, read every tap of the square through envmap_texel().
    */
    void add_level()
    {
        const shader::EnvSource src    = source();
        const int               l      = src.levels - 1;
        const int               charts = shader::envmap_is_cube(src.mapping) ? 6 : 1;
        const int2              from   = shader::envmap_chart_size(src, l);
        const int2              to     = shader::envmap_chart_size(src, l + 1);
        const auto              tx     = taps_along(to.x, float(from.x) / float(to.x)),
                                ty     = taps_along(to.y, float(from.y) / float(to.y));
        auto                    inside = [](const Taps &t, int n) { return t.first >= 0 && t.first + t.count <= n; };

        std::vector<const Array2Df *> added;
        for (int c = 0; c < charts; ++c)
        {
            const Array2Df &a = *src.charts[size_t(l * charts + c)];

            // across every row, for the columns whose taps stay inside it
            Array2Df rows{int2{to.x, from.y}};
            stp::parallel_for(stp::blocked_range<int>(0, from.y, 1),
                              [&](int y0, int y1, int, int)
                              {
                                  for (int y = y0; y < y1; ++y)
                                      for (int x = 0; x < to.x; ++x)
                                          if (const Taps &t = tx[size_t(x)]; inside(t, from.x))
                                          {
                                              float sum = 0.f;
                                              for (int k = 0; k < t.count; ++k) sum += t.w[k] * a(t.first + k, y);
                                              rows(x, y) = sum;
                                          }
                              });

            // then down them, or wherever a tap wraps, over the whole square
            Array2Df &next = m_store.emplace_back(to);
            stp::parallel_for(stp::blocked_range<int>(0, to.y, 1),
                              [&](int y0, int y1, int, int)
                              {
                                  for (int y = y0; y < y1; ++y)
                                      for (int x = 0; x < to.x; ++x)
                                      {
                                          const Taps &u = tx[size_t(x)], &v = ty[size_t(y)];
                                          float       sum = 0.f;
                                          if (inside(u, from.x) && inside(v, from.y))
                                              for (int j = 0; j < v.count; ++j) sum += v.w[j] * rows(x, v.first + j);
                                          else
                                              for (int j = 0; j < v.count; ++j)
                                                  for (int i = 0; i < u.count; ++i)
                                                      sum += v.w[j] * u.w[i] *
                                                             shader::envmap_texel(src, l, c, from,
                                                                                  int2{u.first + i, v.first + j});
                                          next(x, y) = sum;
                                      }
                              });
            added.push_back(&next);
        }

        m_charts.insert(m_charts.end(), added.begin(), added.end());
        ++m_source.levels;
    }

    std::deque<Array2Df>          m_store;  ///< The levels made here, which a deque keeps at fixed addresses
    std::vector<const Array2Df *> m_charts; ///< Every level's charts in order, the source itself first if it is one
    shader::EnvSource             m_source{};
};
} // namespace

float3x3 envmap_rotation(float yaw, float pitch, float roll)
{
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const float cr = std::cos(roll), sr = std::sin(roll);
    // columns, as linalg stores them, of Ry(yaw) Rx(pitch) Rz(roll)
    return float3x3{float3{cy * cr + sy * sp * sr, cp * sr, -sy * cr + cy * sp * sr},
                    float3{-cy * sr + sy * sp * cr, cp * cr, sy * sr + cy * sp * cr}, float3{sy * cp, -sp, cy * cp}};
}

float3 envmap_euler(const float3x3 &R, float roll)
{
    // R[column][row]. R[2][1] = -sin(pitch); the rest of the third column gives the yaw and the rest of the middle row
    // the roll, both scaled by cos(pitch), which vanishes at a quarter turn
    const float pitch = std::asin(std::clamp(-R[2][1], -1.f, 1.f));
    if (std::abs(R[2][1]) > 1.f - 1e-6f)
    {
        // there yaw and roll turn about the same axis, and the first column is a turn about +y by
        // yaw - sin(pitch) * roll
        const float sp = -R[2][1] > 0.f ? 1.f : -1.f;
        return float3{std::remainder(std::atan2(-R[0][2], R[0][0]) + sp * roll, 2.f * k_pi), pitch, roll};
    }
    return float3{std::atan2(R[2][0], R[2][2]), pitch, std::atan2(R[0][1], R[1][1])};
}

float4 rotation_between(float3 from, float3 to)
{
    // a quaternion of half the angle, from the cross product's axis and the dot product's cosine; opposite
    // directions, where both vanish, turn a half turn about any axis perpendicular to them
    const float c = la::dot(from, to);
    if (c < -1.f + 1e-6f)
    {
        float3 axis = la::cross(from, float3{1.f, 0.f, 0.f});
        if (la::length2(axis) < 1e-6f)
            axis = la::cross(from, float3{0.f, 1.f, 0.f});
        return float4{la::normalize(axis), 0.f};
    }
    return la::normalize(float4{la::cross(from, to), 1.f + c});
}

Array2Df remapped_envmap(const Array2Df &src, int2 size, EnvMapping dst_mapping, EnvMapping src_mapping,
                         EnvMapSampling sampling, int supersample, float sigma, const float3x3 &rotation,
                         AtomicProgress progress)
{
    Array2Df out{size};

    // supersample means max anisotropy to EWA and samples per axis to point sampling
    const int ss = std::max(1, supersample);

    // prepared once for the whole remap: the levels EWA reads, and, for a cube map, the six faces both read instead
    // of the cross
    // the supersampling set's spread, which is the same for every pixel
    const float scale = shader::envmap_gaussian_scale(ss, std::max(sigma, 0.f));

    const EnvLevels         levels{src, src_mapping, sampling == EnvMapSampling_EWA};
    const shader::EnvSource source = levels.source();

    progress.set_num_steps(size.y);

    stp::parallel_for(
        stp::blocked_range<int>(0, size.y, 1),
        [&](int y0, int y1, int, int)
        {
            for (int y = y0; y < y1; ++y)
            {
                if (progress.canceled())
                    return;

                for (int x = 0; x < size.x; ++x)
                    out(x, y) =
                        sampling == EnvMapSampling_EWA
                            ? shader::envmap_ewa(source, dst_mapping, rotation, int2{x, y}, size, ss)
                            : shader::envmap_supersample(source, dst_mapping, rotation, int2{x, y}, size, ss, scale);

                ++progress;
            }
        });

    return out;
}

std::array<float, 9> irradiance_coefficients(const Array2Df &src, EnvMapping mapping, AtomicProgress progress)
{
    // Ramamoorthi and Hanrahan: the cosine kernel falls off fast enough in frequency that nine spherical harmonic
    // coefficients reproduce the irradiance to about a percent. Summed a row at a time in double, the rows in
    // parallel, then the rows together.
    const int2   in_size = src.size();
    const double dA      = 1.0 / (double(in_size.x) * double(in_size.y));

    std::vector<std::array<double, 9>> rows(size_t(in_size.y), std::array<double, 9>{});
    progress.set_num_steps(in_size.y);
    stp::parallel_for(stp::blocked_range<int>(0, in_size.y, 1),
                      [&](int y0, int y1, int, int)
                      {
                          for (int y = y0; y < y1; ++y)
                          {
                              if (progress.canceled())
                                  return;
                              auto &row = rows[size_t(y)];
                              for (int x = 0; x < in_size.x; ++x)
                              {
                                  const float2 uv{(float(x) + 0.5f) / float(in_size.x),
                                                  (float(y) + 0.5f) / float(in_size.y)};
                                  const float  dw = envmap_jacobian(mapping, uv);
                                  if (dw <= 0.f)
                                      continue; // not sphere here, so there is nothing to gather

                                  const float3x3 basis = shader::envmap_sh_basis(envmap_uv_to_xyz(mapping, uv));
                                  const double   L     = double(src(x, y)) * double(dw) * dA;
                                  for (int k = 0; k < 9; ++k) row[size_t(k)] += L * double(basis[k / 3][k % 3]);
                              }
                              ++progress;
                          }
                      });

    std::array<double, 9> sum{};
    for (const auto &row : rows)
        for (int k = 0; k < 9; ++k) sum[size_t(k)] += row[size_t(k)];
    std::array<float, 9> out;
    for (int k = 0; k < 9; ++k) out[size_t(k)] = float(sum[size_t(k)]);
    return out;
}

Array2Df irradiance_envmap(const Array2Df &src, int2 size, EnvMapping mapping, AtomicProgress progress)
{
    const auto sh = irradiance_coefficients(src, mapping, progress);
    if (progress.canceled())
        return Array2Df{size};

    Array2Df out{size};
    for (int y = 0; y < size.y; ++y)
        for (int x = 0; x < size.x; ++x)
        {
            const float2   uv{(float(x) + 0.5f) / float(size.x), (float(y) + 0.5f) / float(size.y)};
            const float3x3 w = shader::envmap_irradiance_weights(envmap_uv_to_xyz(mapping, uv));
            float          e = 0.f;
            for (int k = 0; k < 9; ++k) e += w[k / 3][k % 3] * sh[size_t(k)];
            out(x, y) = e;
        }
    return out;
}
