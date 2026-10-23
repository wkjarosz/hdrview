/** \file envmap_sample.h
    Reading an environment map by direction, as the remap edits do on the CPU and the image shader's preview of them
    does on the GPU, compiled from this one source. See shim.h for the dialect it is written in.

    The two differ only in where the texels come from, so the includer supplies that before including this file:
    - `EnvValue`, what one texel read gives: one channel's float on the CPU, the four shown channels' vec4 on the GPU;
    - `struct EnvSource`, with at least the fields `int mapping` (an ENVMAP_ constant), `int levels` (how many mip
      levels there are), `float ratio` (how many times larger one level's texels are than the last's), `float spread`
      (the variance each step down blurs by, in squared texels of the level it shrinks) and `ivec2 size` (the image's
      size at level 0);
    - `EnvValue envmap_fetch(EnvSource src, int level, int chart, ivec2 size, ivec2 p)`, the texel \p p of \p chart
      at \p level, which is \p size, where \p p is always inside the chart.

    A chart is a piece of the sphere laid out flat in its own coordinates: the whole image for every mapping but a
    cube map, and one face of the cube, in cube_face_st()'s coordinates, for a cube map. Everything here reads charts
    in [0,1]^2 and wraps a read past a chart's edge as the sphere continues there.
*/

#ifndef HDRVIEW_SHARED_ENVMAP_SAMPLE_H
#define HDRVIEW_SHARED_ENVMAP_SAMPLE_H

#ifdef __cplusplus
#include "envmap.h"
#endif

SHARED_BEGIN

/// Whether \p mapping lays the sphere out as six flat faces, whichever arrangement it uses for them.
SHARED_FN bool envmap_is_cube(int mapping) { return mapping == ENVMAP_CUBE_MAP || mapping == ENVMAP_CUBE_COLUMN; }

/// Side of the faces \p mapping carves out of an image of \p size.
SHARED_FN int envmap_face_size(int mapping, ivec2 size)
{
    if (mapping == ENVMAP_CUBE_COLUMN)
        return max(1, min(size.x, size.y / 6));
    return max(1, min(size.x / 3, size.y / 4));
}

/// Where texel \p p of \p face, \p n texels a side, lies in the image a cube map of \p mapping lays out.
SHARED_FN ivec2 envmap_face_texel_in_image(int mapping, int face, ivec2 p, int n)
{
    if (mapping == ENVMAP_CUBE_COLUMN)
    {
        // column_st() turns each face by one flip
        bool  flips_s = face == FACE_POS_X || face == FACE_NEG_X || face == FACE_POS_Z;
        ivec2 q       = flips_s ? ivec2(n - 1 - p.x, p.y) : ivec2(p.x, n - 1 - p.y);
        return ivec2(q.x, face * n + q.y);
    }
    ivec2 cell = cross_cell(face);
    return ivec2(cell.x * n + p.x, cell.y * n + p.y);
}

/// Size of each chart of \p src at \p level, which shrinks by `ratio` from one level to the next, rounding down.
SHARED_FN ivec2 envmap_chart_size(EnvSource src, int level)
{
    ivec2 s = src.size;
    if (envmap_is_cube(src.mapping))
    {
        int n = envmap_face_size(src.mapping, src.size);
        s     = ivec2(n, n);
    }
    // the nudge keeps an exact quotient from rounding down past itself
    float f = pow(src.ratio, float(level));
    return ivec2(max(1, int(floor(float(s.x) / f + 1e-4f))), max(1, int(floor(float(s.y) / f + 1e-4f))));
}

/// \p x modulo \p n, in [0, n) whatever the sign of \p x, which GLSL's % does not promise.
SHARED_FN int envmap_floor_mod(int x, int n) { return x - n * int(floor(float(x) / float(n))); }

/// Texel \p p of an image of \p size in \p mapping, wrapped as the sphere continues past the image's edges.
/**
    Lat-long and cylindrical wrap in u; past a pole the row reflects and u shifts by half a turn. The equal-area
    square joins each edge to itself folded about its midpoint, so past an edge it mirrors back across that edge and
    flips along it, as in PBRT's WrapEqualAreaSquare() but with texel -1 the mirror of texel 0 on both sides. Every
    other mapping clamps, as does whatever lies further out than one image.
*/
SHARED_FN ivec2 envmap_wrap_texel(int mapping, ivec2 p, ivec2 size)
{
    int x = p.x, y = p.y, w = size.x, h = size.y;
    if (x >= 0 && y >= 0 && x < w && y < h)
        return p;
    if (mapping == ENVMAP_EQUAL_AREA)
    {
        if (x < 0 || x >= w)
        {
            x = x < 0 ? -1 - x : 2 * w - 1 - x;
            y = h - 1 - y;
        }
        if (y < 0 || y >= h)
        {
            y = y < 0 ? -1 - y : 2 * h - 1 - y;
            x = w - 1 - x;
        }
    }
    else if (mapping == ENVMAP_LAT_LONG || mapping == ENVMAP_CYLINDRICAL)
    {
        if (y < 0 || y >= h)
        {
            y = y < 0 ? -1 - y : 2 * h - 1 - y;
            x += w / 2;
        }
        x = envmap_floor_mod(x, w);
    }
    return ivec2(clamp(x, 0, w - 1), clamp(y, 0, h - 1));
}

/// The face and texel that texel \p p of \p face, \p n texels a side, stands for, as (face, x, y).
/**
    Itself inside the face; past an edge, the texel of whichever face the direction through its center lands on.
*/
SHARED_FN ivec3 envmap_wrap_face_texel(int face, ivec2 p, int n)
{
    if (p.x >= 0 && p.y >= 0 && p.x < n && p.y < n)
        return ivec3(face, p.x, p.y);

    vec3 d  = cube_face_vector(face, vec2((float(p.x) + 0.5f) / float(n), (float(p.y) + 0.5f) / float(n)));
    int  g  = cube_face_of(d);
    vec2 st = cube_face_st(g, d);
    return ivec3(g, clamp(int(floor(st.x * float(n))), 0, n - 1), clamp(int(floor(st.y * float(n))), 0, n - 1));
}

/// Texel \p p of \p chart at \p level, which is \p size, wrapped onto the sphere wherever it lies.
SHARED_FN EnvValue envmap_texel(EnvSource src, int level, int chart, ivec2 size, ivec2 p)
{
    if (envmap_is_cube(src.mapping))
    {
        ivec3 q = envmap_wrap_face_texel(chart, p, size.x);
        return envmap_fetch(src, level, q.x, size, ivec2(q.y, q.z));
    }
    return envmap_fetch(src, level, chart, size, envmap_wrap_texel(src.mapping, p, size));
}

/// Bilinear read of \p chart at \p level, which is \p size, at \p st in the chart's [0,1]^2 or a little past it.
SHARED_FN EnvValue envmap_bilinear(EnvSource src, int level, int chart, ivec2 size, vec2 st)
{
    float x = st.x * float(size.x) - 0.5f, y = st.y * float(size.y) - 0.5f;
    float fx = floor(x), fy = floor(y);
    float tx = x - fx, ty = y - fy;
    int   x0 = int(fx), y0 = int(fy);

    return (1.0f - ty) * ((1.0f - tx) * envmap_texel(src, level, chart, size, ivec2(x0, y0)) +
                          tx * envmap_texel(src, level, chart, size, ivec2(x0 + 1, y0))) +
           ty * ((1.0f - tx) * envmap_texel(src, level, chart, size, ivec2(x0, y0 + 1)) +
                 tx * envmap_texel(src, level, chart, size, ivec2(x0 + 1, y0 + 1)));
}

/// Where direction \p d falls in \p mapping, as the point on its chart followed by the chart.
SHARED_FN vec3 envmap_locate(int mapping, vec3 d)
{
    if (envmap_is_cube(mapping))
    {
        int  face = cube_face_of(d);
        vec2 st   = cube_face_st(face, d);
        return vec3(st.x, st.y, float(face));
    }
    vec2 uv = envmap_xyz_to_uv(mapping, d);
    return vec3(uv.x, uv.y, 0.0f);
}

/// Where direction \p d falls in \p chart's coordinates, which for a cube face may be past its edges.
SHARED_FN vec2 envmap_chart_point(int mapping, int chart, vec3 d)
{
    return envmap_is_cube(mapping) ? cube_face_st(chart, d) : envmap_xyz_to_uv(mapping, d);
}

/// Bilinear read at \p level, whose charts are \p size, of the point \p st of \p chart, anywhere on the chart's plane.
/**
    A cube face's plane goes on past its edges, where the direction it names belongs to another face, so the read is
    taken there. Any other chart wraps texel by texel.
*/
SHARED_FN EnvValue envmap_probe(EnvSource src, int level, ivec2 size, int chart, vec2 st)
{
    if (envmap_is_cube(src.mapping) && (st.x < 0.0f || st.x > 1.0f || st.y < 0.0f || st.y > 1.0f))
    {
        vec3 p = envmap_locate(src.mapping, cube_face_vector(chart, st));
        return envmap_bilinear(src, level, int(p.z), size, vec2(p.x, p.y));
    }
    return envmap_bilinear(src, level, chart, size, st);
}

/// The source's direction for \p uv in mapping \p dst, turned by \p rotation.
SHARED_FN vec3 envmap_remap_dir(int dst, mat3 rotation, vec2 uv) { return mul(rotation, envmap_uv_to_xyz(dst, uv)); }

/// Output pixel \p pixel of a remap to \p dst at \p size, as the average of \p ss by \p ss bilinear reads.
/**
    Each read is snapped onto the sphere like the pixel itself, since a sub-sample can land off it even where the
    pixel's center did not.
*/
SHARED_FN EnvValue envmap_supersample(EnvSource src, int dst, mat3 rotation, ivec2 pixel, ivec2 size, int ss)
{
    ivec2    base = envmap_chart_size(src, 0);
    EnvValue sum  = EnvValue(0.0f);
    for (int sy = 0; sy < ss; ++sy)
        for (int sx = 0; sx < ss; ++sx)
        {
            vec2 uv = nearest_valid_envmap_uv(dst, vec2((float(pixel.x) + (float(sx) + 0.5f) / float(ss)) / float(size.x),
                                                        (float(pixel.y) + (float(sy) + 0.5f) / float(ss)) / float(size.y)));
            vec3 p  = envmap_locate(src.mapping, envmap_remap_dir(dst, rotation, uv));
            sum += envmap_bilinear(src, 0, int(p.z), base, vec2(p.x, p.y));
        }
    return sum / float(ss * ss);
}

/// Variance a bilinear read at \p level blurs by, in squared texels of level 0: the levels' own, and bilinear's.
/**
    Each step down blurs by `spread` squared texels of the level it shrinks, which are ratio^2l of level 0's, summing
    to a geometric series; and a bilinear read blurs by a tent of a texel, a sixth of one squared.
*/
SHARED_FN float envmap_read_variance(EnvSource src, int level)
{
    float r2 = src.ratio * src.ratio, t2 = pow(r2, float(level));
    return src.spread * (t2 - 1.0f) / (r2 - 1.0f) + t2 / 6.0f;
}

/// Gaussian of standard deviation \p sigma along \p axis through \p st, as reads at \p level spaced along it.
/**
    \p axis is a unit direction and \p sigma a length, both in texels of level 0, which \p res converts into the
    chart's coordinates. A \p sigma no wider than a read's own blur is three reads, one that blur either side of the
    middle, weighted to have exactly \p sigma's variance: blurred as each read is, they are as smooth as the
    Gaussian. A wider one is reads spaced no further apart than twice that blur, which their sum then smooths over,
    nor than \p sigma, which the weights then reproduce; out to three sigma, and no more than \p max_half either side
    of the middle, past which they spread further apart.
*/
SHARED_FN EnvValue envmap_probe_line(EnvSource src, int level, int chart, vec2 st, vec2 axis, float sigma, vec2 res,
                                     int max_half)
{
    ivec2 size = envmap_chart_size(src, level);
    if (sigma <= 0.0f)
        return envmap_probe(src, level, size, chart, st);

    float own = sqrt(envmap_read_variance(src, level));
    vec2  dir = vec2(axis.x / res.x, axis.y / res.y);
    if (sigma <= own)
    {
        float a = 0.5f * sigma * sigma / (own * own);
        return (1.0f - 2.0f * a) * envmap_probe(src, level, size, chart, st) +
               a * (envmap_probe(src, level, size, chart, st - own * dir) +
                    envmap_probe(src, level, size, chart, st + own * dir));
    }

    float spacing = min(2.0f * own, sigma);
    int   half_n  = int(ceil(3.0f * sigma / spacing));
    if (half_n > max_half)
    {
        half_n  = max_half;
        spacing = 3.0f * sigma / float(half_n);
    }

    vec2     step  = dir * spacing;
    EnvValue sum   = EnvValue(0.0f);
    float    total = 0.0f;
    for (int i = -half_n; i <= half_n; ++i)
    {
        float t = float(i) * spacing;
        float w = exp(-0.5f * t * t / (sigma * sigma));
        sum += w * envmap_probe(src, level, size, chart, st + float(i) * step);
        total += w;
    }
    return sum / total;
}

/// The half-pixel step \p h from \p uv in the remap's output, doubled, as a step across \p chart from \p st.
/**
    The smaller of the steps toward either side, so that a seam (lat-long wrap, disc rim, equal-area fold) on one side
    does not blow it up; a step reaching past the pixel could cross a seam on both sides.
*/
SHARED_FN vec2 envmap_pixel_step(int mapping, int chart, int dst, mat3 rotation, vec2 uv, vec2 h, vec2 st)
{
    vec2 fwd = envmap_chart_point(mapping, chart, envmap_remap_dir(dst, rotation, uv + h)) - st;
    vec2 bwd = st - envmap_chart_point(mapping, chart, envmap_remap_dir(dst, rotation, uv - h));
    return 2.0f * (dot(fwd, fwd) <= dot(bwd, bwd) ? fwd : bwd);
}

/// Output pixel \p pixel of a remap to \p dst at \p size, filtered over the part of the source it covers.
/**
    The filter is a Gaussian of half a pixel, carried into the source by the remap's local stretch: an ellipse there,
    whose covariance is a quarter of the outer products of the pixel's two steps. Its short axis is widened to at
    least a \p max_aniso th of the long one, which bounds the reads.

    A read at a mip level blurs by that level's variance in every direction (envmap_read_variance()), so the levels
    either side of the short axis's variance are blended by the weight that gives exactly it, and along the long axis
    a line of reads makes up the rest at each, the coarser level's chosen so that the blend has exactly the long
    axis's variance too.
*/
SHARED_FN EnvValue envmap_ewa(EnvSource src, int dst, mat3 rotation, ivec2 pixel, ivec2 size, int max_aniso)
{
    vec2  uv    = nearest_valid_envmap_uv(dst, vec2((float(pixel.x) + 0.5f) / float(size.x),
                                                    (float(pixel.y) + 0.5f) / float(size.y)));
    vec3  c     = envmap_locate(src.mapping, envmap_remap_dir(dst, rotation, uv));
    int   chart = int(c.z);
    vec2  st    = vec2(c.x, c.y);
    ivec2 base  = envmap_chart_size(src, 0);
    vec2  res   = vec2(float(base.x), float(base.y));

    vec2 a = envmap_pixel_step(src.mapping, chart, dst, rotation, uv, vec2(0.5f / float(size.x), 0.0f), st) * res;
    vec2 b = envmap_pixel_step(src.mapping, chart, dst, rotation, uv, vec2(0.0f, 0.5f / float(size.y)), st) * res;

    // the covariance, and its eigenvalues and major axis
    float sxx = 0.25f * (a.x * a.x + b.x * b.x), syy = 0.25f * (a.y * a.y + b.y * b.y);
    float sxy = 0.25f * (a.x * a.y + b.x * b.y);
    float m = 0.5f * (sxx + syy), d = sqrt(max(0.0f, 0.25f * (sxx - syy) * (sxx - syy) + sxy * sxy));
    float major = m + d, minor = max(0.0f, m - d);
    if (major <= 0.0f)
        return envmap_bilinear(src, 0, chart, base, st);

    vec2 axis = abs(sxy) > 1e-12f * major ? normalize(vec2(major - syy, sxy))
                : sxx >= syy             ? vec2(1.0f, 0.0f)
                                         : vec2(0.0f, 1.0f);
    float aniso = float(max(1, max_aniso));
    minor       = max(minor, major / (aniso * aniso));

    // the finest level whose reads blur by no more than the short axis wants, and the next, blended to make it up;
    // envmap_read_variance() is (k + 1/6) t2 - k, with t2 = ratio^2l, solved for the level
    float r2 = src.ratio * src.ratio, k = src.spread / (r2 - 1.0f);
    int   lo   = clamp(int(floor(log((minor + k) / (k + 1.0f / 6.0f)) / log(r2) + 1e-4f)), 0, src.levels - 1);
    int   hi   = min(lo + 1, src.levels - 1);
    float b_lo = envmap_read_variance(src, lo), b_hi = envmap_read_variance(src, hi);
    float w    = hi > lo ? clamp((minor - b_lo) / (b_hi - b_lo), 0.0f, 1.0f) : 0.0f;

    // along the long axis, what each level's reads leave to make up, the finer level's covering what the coarser
    // one overshoots
    float g_hi = max(0.0f, major - b_hi);
    float g_lo = w < 1.0f ? max(0.0f, (major - w * (b_hi + g_hi)) / (1.0f - w) - b_lo) : 0.0f;

    int      max_half = 3 * max(1, max_aniso);
    EnvValue result   = EnvValue(0.0f);
    if (w < 1.0f)
        result += (1.0f - w) * envmap_probe_line(src, lo, chart, st, axis, sqrt(g_lo), res, max_half);
    if (w > 0.0f)
        result += w * envmap_probe_line(src, hi, chart, st, axis, sqrt(g_hi), res, max_half);
    return result;
}

SHARED_END

#endif // HDRVIEW_SHARED_ENVMAP_SAMPLE_H
