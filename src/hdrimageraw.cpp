//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "hdrimageraw.h"
#include "common.h"
#include "hdrimage.h"
#include "parallelfor.h"
#include <ImathMatrix.h>
#include <spdlog/spdlog.h>
#include <vector>

using namespace nanogui;
using namespace std;
using Imath::M33f;
using Imath::V2f;
using Imath::V3f;

namespace
{

inline int bayer_color(int x, int y)
{
    const int bayer[2][2] = {{0, 1}, {1, 2}};

    return bayer[y % 2][x % 2];
}

inline float clamp2(float value, float mn, float mx)
{
    if (mn > mx)
        std::swap(mn, mx);
    return std::clamp(value, mn, mx);
}

inline float clamp4(float value, float a, float b, float c, float d)
{
    float mn = std::min({a, b, c, d});
    float mx = std::max({a, b, c, d});
    return std::clamp(value, mn, mx);
}

inline float interp_green_h(const HDRImage &raw, int x, int y)
{
    float v = 0.50f * (raw(x - 1, y).g + raw(x + 1, y).g + raw(x, y).g) - 0.25f * (raw(x - 2, y).g + raw(x + 2, y).g);
    // Don't extrapolate past the neighboring green values
    return clamp2(v, raw(x - 1, y).g, raw(x + 1, y).g);
}

inline float interp_green_v(const HDRImage &raw, int x, int y)
{
    float v = 0.50f * (raw(x, y - 1).g + raw(x, y + 1).g + raw(x, y).g) - 0.25f * (raw(x, y - 2).g + raw(x, y + 2).g);
    // Don't extrapolate past the neighboring green values
    return clamp2(v, raw(x, y - 1).g, raw(x, y + 1).g);
}

inline float ghG(const Array2Df &G, int i, int j) { return fabs(G(i - 1, j) - G(i, j)) + fabs(G(i + 1, j) - G(i, j)); }

inline float gvG(const Array2Df &G, int i, int j) { return fabs(G(i, j - 1) - G(i, j)) + fabs(G(i, j + 1) - G(i, j)); }

/*!
 * \brief Compute the missing green pixels using a simple bilinear interpolation
 * from the 4 neighbors.
 *
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_green_linear(HDRImage &raw, const nanogui::Vector2i &red_offset)
{
    parallel_for(1, raw.height() - 1 - red_offset.y(), 2,
                 [&raw, &red_offset](int yy)
                 {
                     int t = yy + red_offset.y();
                     for (int xx = 1; xx < raw.width() - 1 - red_offset.x(); xx += 2)
                     {
                         int l = xx + red_offset.x();

                         // coordinates of the missing green pixels (red and blue) in
                         // this Bayer tile are: (l,t) and (r,b)
                         int r = l + 1;
                         int b = t + 1;

                         raw(l, t).g = 0.25f * (raw(l, t - 1).g + raw(l, t + 1).g + raw(l - 1, t).g + raw(l + 1, t).g);
                         raw(r, b).g = 0.25f * (raw(r, b - 1).g + raw(r, b + 1).g + raw(r - 1, b).g + raw(r + 1, b).g);
                     }
                 });
}

// takes as input a raw image and returns a single-channel
// 2D image corresponding to the red or blue channel using simple interpolation
void bilinear_red_blue(HDRImage &raw, int c, const nanogui::Vector2i &red_offset)
{
    // diagonal interpolation
    parallel_for(red_offset.y() + 1, raw.height() - 1, 2,
                 [&raw, c, &red_offset](int y)
                 {
                     for (int x = red_offset.x() + 1; x < raw.width() - 1; x += 2)
                         raw(x, y)[c] = 0.25f * (raw(x - 1, y - 1)[c] + raw(x + 1, y - 1)[c] + raw(x - 1, y + 1)[c] +
                                                 raw(x + 1, y + 1)[c]);
                 });

    // horizontal interpolation
    parallel_for(red_offset.y(), raw.height(), 2,
                 [&raw, c, &red_offset](int y)
                 {
                     for (int x = red_offset.x() + 1; x < raw.width() - 1; x += 2)
                         raw(x, y)[c] = 0.5f * (raw(x - 1, y)[c] + raw(x + 1, y)[c]);
                 });

    // vertical interpolation
    parallel_for(red_offset.y() + 1, raw.height() - 1, 2,
                 [&raw, c, &red_offset](int y)
                 {
                     for (int x = red_offset.x(); x < raw.width(); x += 2)
                         raw(x, y)[c] = 0.5f * (raw(x, y - 1)[c] + raw(x, y + 1)[c]);
                 });
}

/*!
 * \brief Interpolate the missing red and blue pixels using a simple linear or bilinear interpolation.
 *
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_red_blue_linear(HDRImage &raw, const nanogui::Vector2i &red_offset)
{
    bilinear_red_blue(raw, 0, red_offset);
    bilinear_red_blue(raw, 2, nanogui::Vector2i((red_offset.x() + 1) % 2, (red_offset.y() + 1) % 2));
}

// takes as input a raw image and returns a single-channel
// 2D image corresponding to the red or blue channel using green based interpolation
void green_based_red_or_blue(HDRImage &raw, int c, const nanogui::Vector2i &red_offset)
{
    // horizontal interpolation
    parallel_for(red_offset.y(), raw.height(), 2,
                 [&raw, c, &red_offset](int y)
                 {
                     for (int x = red_offset.x() + 1; x < raw.width() - 1; x += 2)
                         raw(x, y)[c] = std::max(
                             0.f, 0.5f * (raw(x - 1, y)[c] + raw(x + 1, y)[c] - raw(x - 1, y)[1] - raw(x + 1, y)[1]) +
                                      raw(x, y)[1]);
                 });

    // vertical interpolation
    parallel_for(red_offset.y() + 1, raw.height() - 1, 2,
                 [&raw, c, &red_offset](int y)
                 {
                     for (int x = red_offset.x(); x < raw.width(); x += 2)
                         raw(x, y)[c] = std::max(
                             0.f, 0.5f * (raw(x, y - 1)[c] + raw(x, y + 1)[c] - raw(x, y - 1)[1] - raw(x, y + 1)[1]) +
                                      raw(x, y)[1]);
                 });

    // diagonal interpolation
    parallel_for(red_offset.y() + 1, raw.height() - 1, 2,
                 [&raw, c, &red_offset](int y)
                 {
                     for (int x = red_offset.x() + 1; x < raw.width() - 1; x += 2)
                         raw(x, y)[c] =
                             std::max(0.f, 0.25f * (raw(x - 1, y - 1)[c] + raw(x + 1, y - 1)[c] + raw(x - 1, y + 1)[c] +
                                                    raw(x + 1, y + 1)[c] - raw(x - 1, y - 1)[1] - raw(x + 1, y - 1)[1] -
                                                    raw(x - 1, y + 1)[1] - raw(x + 1, y + 1)[1]) +
                                               raw(x, y)[1]);
                 });
}

/*!
 * \brief Interpolate the missing red and blue pixels using a linear or bilinear interpolation
 * guided by the green channel, which is assumed already demosaiced.
 *
 * The interpolation is equivalent to performing (bi)linear interpolation of the red-green and
 * blue-green differences, and then adding green back into the interpolated result. This inject
 * some of the higher resolution of the green channel, and reduces color fringing under the
 * assumption that the color channels in natural images are positively correlated.
 *
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_red_blue_green_guided_linear(HDRImage &raw, const nanogui::Vector2i &red_offset)
{
    green_based_red_or_blue(raw, 0, red_offset);
    green_based_red_or_blue(raw, 2, nanogui::Vector2i((red_offset.x() + 1) % 2, (red_offset.y() + 1) % 2));
}

void malvar_red_or_blue_at_green(HDRImage &raw, int c, const nanogui::Vector2i &red_offset, bool horizontal)
{
    int dx = (horizontal) ? 1 : 0;
    int dy = (horizontal) ? 0 : 1;
    // fill in half of the missing locations (R or B)
    parallel_for(2 + red_offset.y(), raw.height() - 2, 2,
                 [&raw, c, &red_offset, dx, dy](int y)
                 {
                     for (int x = 2 + red_offset.x(); x < raw.width() - 2; x += 2)
                     {
                         raw(x, y)[c] = (5.f * raw(x, y)[1] -
                                         1.f * (raw(x - 1, y - 1)[1] + raw(x + 1, y - 1)[1] + raw(x + 1, y + 1)[1] +
                                                raw(x - 1, y + 1)[1] + raw(x - 2, y)[1] + raw(x + 2, y)[1]) +
                                         .5f * (raw(x, y - 2)[1] + raw(x, y + 2)[1]) +
                                         4.f * (raw(x - dx, y - dy)[c] + raw(x + dx, y + dy)[c])) /
                                        8.f;
                     }
                 });
}

void malvar_red_or_blue(HDRImage &raw, int c1, int c2, const nanogui::Vector2i &red_offset)
{
    // fill in half of the missing locations (R or B)
    parallel_for(2 + red_offset.y(), raw.height() - 2, 2,
                 [&raw, c1, c2, &red_offset](int y)
                 {
                     for (int x = 2 + red_offset.x(); x < raw.width() - 2; x += 2)
                     {
                         raw(x, y)[c1] =
                             (6.f * raw(x, y)[c2] +
                              2.f * (raw(x - 1, y - 1)[c1] + raw(x + 1, y - 1)[c1] + raw(x + 1, y + 1)[c1] +
                                     raw(x - 1, y + 1)[c1]) -
                              3 / 2.f *
                                  (raw(x, y - 2)[c2] + raw(x, y + 2)[c2] + raw(x - 2, y)[c2] + raw(x + 2, y)[c2])) /
                             8.f;
                     }
                 });
}

/*!
 * \brief Interpolate the missing red and blue pixels using the method by Malvar et al. 2004.
 *
 * The interpolation for each channel is guided by the available information from all other
 * channels. The green channel is assumed to already be demosaiced.
 *
 * The method uses a 5x5 linear filter.
 *
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_red_blue_malvar(HDRImage &raw, const nanogui::Vector2i &red_offset)
{
    // fill in missing red horizontally
    malvar_red_or_blue_at_green(raw, 0, nanogui::Vector2i((red_offset.x() + 1) % 2, red_offset.y()), true);
    // fill in missing red vertically
    malvar_red_or_blue_at_green(raw, 0, nanogui::Vector2i(red_offset.x(), (red_offset.y() + 1) % 2), false);

    // fill in missing blue horizontally
    malvar_red_or_blue_at_green(raw, 2, nanogui::Vector2i(red_offset.x(), (red_offset.y() + 1) % 2), true);
    // fill in missing blue vertically
    malvar_red_or_blue_at_green(raw, 2, nanogui::Vector2i((red_offset.x() + 1) % 2, red_offset.y()), false);

    // fill in missing red at blue
    malvar_red_or_blue(raw, 0, 2, nanogui::Vector2i((red_offset.x() + 1) % 2, (red_offset.y() + 1) % 2));
    // fill in missing blue at red
    malvar_red_or_blue(raw, 2, 0, red_offset);
}

void malvar_green(HDRImage &raw, int c, const nanogui::Vector2i &red_offset)
{
    // fill in half of the missing locations (R or B)
    parallel_for(2, raw.height() - 2 - red_offset.y(), 2,
                 [&raw, c, &red_offset](int yy)
                 {
                     int y = yy + red_offset.y();
                     for (int xx = 2; xx < raw.width() - 2 - red_offset.x(); xx += 2)
                     {
                         int   x = xx + red_offset.x();
                         float v = (4.f * raw(x, y)[c] +
                                    2.f * (raw(x, y - 1)[1] + raw(x - 1, y)[1] + raw(x, y + 1)[1] + raw(x + 1, y)[1]) -
                                    1.f * (raw(x, y - 2)[c] + raw(x - 2, y)[c] + raw(x, y + 2)[c] + raw(x + 2, y)[c])) /
                                   8.f;
                         raw(x, y)[1] =
                             clamp4(v, raw(x, y - 1)[1], raw(x - 1, y)[1], raw(x, y + 1)[1], raw(x + 1, y)[1]);
                     }
                 });
}

/*!
 * \brief Interpolate the missing green pixels using the method by Malvar et al. 2004.
 *
 * The method uses a plus "+" shaped 5x5 filter, which is linear, except--to reduce
 * ringing/over-shooting--the interpolation is not allowed to extrapolate higher or
 * lower than the surrounding green pixels.
 *
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_green_malvar(HDRImage &raw, const nanogui::Vector2i &red_offset)
{
    // fill in missing green at red pixels
    malvar_green(raw, 0, red_offset);
    // fill in missing green at blue pixels
    malvar_green(raw, 2, nanogui::Vector2i((red_offset.x() + 1) % 2, (red_offset.y() + 1) % 2));
}

inline nanogui::Vector3f camera_to_Lab(const V3f &c, const M33f &camera_to_XYZ, const vector<float> &LUT)
{
    V3f xyz = c * camera_to_XYZ;

    for (int i = 0; i < 3; ++i) xyz[i] = LUT[::clamp((int)(xyz[i] * LUT.size()), 0, int(LUT.size() - 1))];

    return nanogui::Vector3f(116.0f * xyz[1] - 16, 500.0f * (xyz[0] - xyz[1]), 200.0f * (xyz[1] - xyz[2]));
}

/*!
 * \brief Compute the missing green pixels using vertical linear interpolation.
 *
 * @param raw       The source raw pixel data.
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_green_horizontal(HDRImage &res, const HDRImage &raw, const nanogui::Vector2i &red_offset)
{
    parallel_for(red_offset.y(), res.height(), 2,
                 [&res, &raw, &red_offset](int y)
                 {
                     for (int x = 2 + red_offset.x(); x < res.width() - 2; x += 2)
                     {
                         // populate the green channel into the red and blue pixels
                         res(x, y).g         = interp_green_h(raw, x, y);
                         res(x + 1, y + 1).g = interp_green_h(raw, x + 1, y + 1);
                     }
                 });
}

/*!
 * \brief Compute the missing green pixels using horizontal linear interpolation.
 *
 * @param raw       The source raw pixel data.
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_green_vertical(HDRImage &res, const HDRImage &raw, const nanogui::Vector2i &red_offset)
{
    parallel_for(2 + red_offset.y(), res.height() - 2, 2,
                 [&res, &raw, &red_offset](int y)
                 {
                     for (int x = red_offset.x(); x < res.width(); x += 2)
                     {
                         res(x, y).g         = interp_green_v(raw, x, y);
                         res(x + 1, y + 1).g = interp_green_v(raw, x + 1, y + 1);
                     }
                 });
}

} // namespace

void bayer_mosaic(HDRImage &raw, const nanogui::Vector2i &red_offset)
{
    Color4 mosaic[2][2] = {{Color4(1.f, 0.f, 0.f, 1.f), Color4(0.f, 1.f, 0.f, 1.f)},
                           {Color4(0.f, 1.f, 0.f, 1.f), Color4(0.f, 0.f, 1.f, 1.f)}};
    parallel_for(0, raw.height(), 1,
                 [&](int y)
                 {
                     int r = mod(y - red_offset.y(), 2);
                     for (int x = 0; x < raw.width(); ++x)
                     {
                         int c = mod(x - red_offset.x(), 2);
                         raw(x, y) *= mosaic[r][c];
                     }
                 });
}

void demosaic_linear(HDRImage &raw, const nanogui::Vector2i &red_offset)
{
    demosaic_green_linear(raw, red_offset);
    demosaic_red_blue_linear(raw, red_offset);
}

void demosaic_green_guided_linear(HDRImage &raw, const nanogui::Vector2i &red_offset)
{
    demosaic_green_linear(raw, red_offset);
    demosaic_red_blue_green_guided_linear(raw, red_offset);
}

void demosaic_Malvar(HDRImage &raw, const nanogui::Vector2i &red_offset)
{
    demosaic_green_malvar(raw, red_offset);
    demosaic_red_blue_malvar(raw, red_offset);
}

void demosaic_AHD(HDRImage &raw, const nanogui::Vector2i &red_offset, const M33f &camera_to_XYZ)
{
    using Image3f  = Array2D<nanogui::Vector3f>;
    using HomoMap  = Array2D<uint8_t>;
    HDRImage rgb_H = raw;
    HDRImage rgb_V = raw;
    Image3f  lab_H(raw.width(), raw.height(), nanogui::Vector3f(0.f));
    Image3f  lab_V(raw.width(), raw.height(), nanogui::Vector3f(0.f));
    HomoMap  homo_H = HomoMap(raw.width(), raw.height());
    HomoMap  homo_V = HomoMap(raw.width(), raw.height());

    // interpolate green channel both horizontally and vertically
    demosaic_green_horizontal(rgb_H, raw, red_offset);
    demosaic_green_vertical(rgb_V, raw, red_offset);

    // interpolate the red and blue using the green as a guide
    demosaic_red_blue_green_guided_linear(rgb_H, red_offset);
    demosaic_red_blue_green_guided_linear(rgb_V, red_offset);

    // Scale factor to push XYZ values to [0,1] range
    // FIXME
    float scale = 1.0f; // / (max().max() * camera_to_XYZ.max());

    // Precompute a table for the nonlinear part of the CIELab conversion
    vector<float> Lab_LUT;
    Lab_LUT.reserve(0xFFFF);
    parallel_for(0, Lab_LUT.size(),
                 [&Lab_LUT](int i)
                 {
                     float r    = i * 1.0f / (Lab_LUT.size() - 1);
                     Lab_LUT[i] = r > 0.008856 ? std::pow(r, 1.0f / 3.0f) : 7.787f * r + 4.0f / 29.0f;
                 });

    // convert both interpolated images to CIE L*a*b* so we can compute perceptual differences
    parallel_for(0, rgb_H.height(),
                 [&rgb_H, &lab_H, &camera_to_XYZ, &Lab_LUT, scale](int y)
                 {
                     for (int x = 0; x < rgb_H.width(); ++x)
                         lab_H(x, y) = camera_to_Lab(V3f(rgb_H(x, y)[0], rgb_H(x, y)[1], rgb_H(x, y)[2]) * scale,
                                                     camera_to_XYZ, Lab_LUT);
                 });
    parallel_for(0, rgb_V.height(),
                 [&rgb_V, &lab_V, &camera_to_XYZ, &Lab_LUT, scale](int y)
                 {
                     for (int x = 0; x < rgb_V.width(); ++x)
                         lab_V(x, y) = camera_to_Lab(V3f(rgb_V(x, y)[0], rgb_V(x, y)[1], rgb_V(x, y)[2]) * scale,
                                                     camera_to_XYZ, Lab_LUT);
                 });

    // Build homogeneity maps from the CIELab images which count, for each pixel,
    // the number of visually similar neighboring pixels
    static const int neighbor[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    parallel_for(1, lab_H.height() - 1,
                 [&homo_H, &homo_V, &lab_H, &lab_V](int y)
                 {
                     for (int x = 1; x < lab_H.width() - 1; ++x)
                     {
                         float ldiffH[4], ldiffV[4], abdiffH[4], abdiffV[4];

                         for (int i = 0; i < 4; i++)
                         {
                             int dx = neighbor[i][0];
                             int dy = neighbor[i][1];

                             // Local luminance and chromaticity differences to the 4 neighbors for both interpolations
                             // directions
                             ldiffH[i]  = std::abs(lab_H(x, y)[0] - lab_H(x + dx, y + dy)[0]);
                             ldiffV[i]  = std::abs(lab_V(x, y)[0] - lab_V(x + dx, y + dy)[0]);
                             abdiffH[i] = ::square(lab_H(x, y)[1] - lab_H(x + dx, y + dy)[1]) +
                                          ::square(lab_H(x, y)[2] - lab_H(x + dx, y + dy)[2]);
                             abdiffV[i] = ::square(lab_V(x, y)[1] - lab_V(x + dx, y + dy)[1]) +
                                          ::square(lab_V(x, y)[2] - lab_V(x + dx, y + dy)[2]);
                         }

                         float leps  = std::min(std::max(ldiffH[0], ldiffH[1]), std::max(ldiffV[2], ldiffV[3]));
                         float abeps = std::min(std::max(abdiffH[0], abdiffH[1]), std::max(abdiffV[2], abdiffV[3]));

                         // Count number of neighboring pixels that are visually similar
                         for (int i = 0; i < 4; i++)
                         {
                             if (ldiffH[i] <= leps && abdiffH[i] <= abeps)
                                 homo_H(x, y)++;
                             if (ldiffV[i] <= leps && abdiffV[i] <= abeps)
                                 homo_V(x, y)++;
                         }
                     }
                 });

    // Combine the most homogenous pixels for the final result
    parallel_for(1, raw.height() - 1,
                 [&raw, &homo_H, &homo_V, &rgb_H, &rgb_V](int y)
                 {
                     for (int x = 1; x < raw.width() - 1; ++x)
                     {
                         // Sum up the homogeneity of both images in a 3x3 window
                         int hmH = 0, hmV = 0;
                         for (int j = y - 1; j <= y + 1; j++)
                             for (int i = x - 1; i <= x + 1; i++)
                             {
                                 hmH += homo_H(i, j);
                                 hmV += homo_V(i, j);
                             }

                         if (hmH > hmV)
                         {
                             // horizontal interpolation is more homogeneous
                             raw(x, y) = rgb_H(x, y);
                         }
                         else if (hmV > hmH)
                         {
                             // vertical interpolation is more homogeneous
                             raw(x, y) = rgb_V(x, y);
                         }
                         else
                         {
                             // No clear winner, blend
                             Color4 blend = (rgb_H(x, y) + rgb_V(x, y)) * 0.5f;
                             raw(x, y)    = blend;
                         }
                     }
                 });

    // Now handle the boundary pixels
    demosaic_border(raw, 3);
}

void demosaic_green_Phelippeau(HDRImage &raw, const nanogui::Vector2i &red_offset)
{
    Array2Df Gh(raw.width(), raw.height());
    Array2Df Gv(raw.width(), raw.height());

    // populate horizontally interpolated green
    parallel_for(red_offset.y(), raw.height(), 2,
                 [&raw, &Gh, &red_offset](int y)
                 {
                     for (int x = 2 + red_offset.x(); x < raw.width() - 2; x += 2)
                     {
                         Gh(x, y)         = interp_green_h(raw, x, y);
                         Gh(x + 1, y + 1) = interp_green_h(raw, x + 1, y + 1);
                     }
                 });

    // populate vertically interpolated green
    parallel_for(2 + red_offset.y(), raw.height() - 2, 2,
                 [&raw, &Gv, &red_offset](int y)
                 {
                     for (int x = red_offset.x(); x < raw.width(); x += 2)
                     {
                         Gv(x, y)         = interp_green_v(raw, x, y);
                         Gv(x + 1, y + 1) = interp_green_v(raw, x + 1, y + 1);
                     }
                 });

    parallel_for(2 + red_offset.y(), raw.height() - 2, 2,
                 [&raw, &Gh, &Gv, red_offset](int y)
                 {
                     for (int x = 2 + red_offset.x(); x < raw.width() - 2; x += 2)
                     {
                         float ghGh = ghG(Gh, x, y);
                         float ghGv = ghG(Gv, x, y);
                         float gvGh = gvG(Gh, x, y);
                         float gvGv = gvG(Gv, x, y);

                         raw(x, y).g = (ghGh + gvGh <= gvGv + ghGv) ? Gh(x, y) : Gv(x, y);

                         x++;
                         y++;

                         ghGh = ghG(Gh, x, y);
                         ghGv = ghG(Gv, x, y);
                         gvGh = gvG(Gh, x, y);
                         gvGv = gvG(Gv, x, y);

                         raw(x, y).g = (ghGh + gvGh <= gvGv + ghGv) ? Gh(x, y) : Gv(x, y);
                     }
                 });
}

void demosaic_border(HDRImage &raw, size_t border)
{
    parallel_for(0, raw.height(),
                 [&](size_t y)
                 {
                     for (size_t x = 0; x < (size_t)raw.width(); ++x)
                     {
                         // skip the center of the image
                         if (x == border && y >= border && y < raw.height() - border)
                             x = raw.width() - border;

                         nanogui::Vector3f sum   = nanogui::Vector3f(0.f);
                         nanogui::Vector3i count = nanogui::Vector3i(0.f);

                         for (size_t ys = y - 1; ys <= y + 1; ++ys)
                         {
                             for (size_t xs = x - 1; xs <= x + 1; ++xs)
                             {
                                 // rely on xs and ys = -1 to wrap around to max value
                                 // since they are unsigned
                                 if (ys < (size_t)raw.height() && xs < (size_t)raw.width())
                                 {
                                     int c = bayer_color(xs, ys);
                                     sum[c] += raw(xs, ys)[c];
                                     ++count[c];
                                 }
                             }
                         }

                         int col = bayer_color(x, y);
                         for (int c = 0; c < 3; ++c)
                         {
                             if (col != c)
                                 raw(x, y)[c] = count[c] ? (sum[c] / count[c]) : 1.0f;
                         }
                     }
                 });
}
