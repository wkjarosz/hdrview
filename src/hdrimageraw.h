//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"
#include <ImathForward.h>

/*!
 * \brief Multiplies a raw image by the Bayer mosaic pattern so that only a single
 * R, G, or B channel is non-zero for each pixel.
 *
 * We assume the canonical Bayer pattern looks like:
 *
 * \rst
 * +---+---+
 * | R | G |
 * +---+---+
 * | G | B |
 * +---+---+
 *
 * \endrst
 *
 * and the pattern is tiled across the entire image.
 *
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern
 */
void bayer_mosaic(HDRImage &raw, const nanogui::Vector2i &red_offset);

/*!
 * \brief Demosaic the image using simple bilinear interpolation.
 *
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_linear(HDRImage &raw, const nanogui::Vector2i &red_offset);

/*!
 * \brief First interpolate the green channel linearly, then use this channel to guide the
 *          interpolation of the red-green and blue-green differences.
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_green_guided_linear(HDRImage &raw, const nanogui::Vector2i &red_offset);

/*!
 * \brief Demosaic using the method by Malvar et al. 2004.
 *
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_Malvar(HDRImage &raw, const nanogui::Vector2i &red_offset);

/*!
 * \brief Demosaic the image using the "Adaptive Homogeneity-Directed" interpolation
 * approach proposed by Hirakawa et al. 2004.
 *
 * The approach is fairly expensive, but produces the best results.
 *
 * The method first creates two competing full-demosaiced images: one where the
 * green channel is interpolated vertically, and the other horizontally. In both
 * images the red and green are demosaiced using the corresponding green channel
 * as a guide.
 *
 * The two candidate images are converted to XYZ (using the supplied \a camera_to_XYZ
 * matrix) subsequently to CIE L*a*b* space in order to determine how perceptually
 * "homogeneous" each pixel neighborhood is.
 *
 * "Homogeneity maps" are created for the two candidate imates which count, for each
 * pixel, the number of perceptually similar pixels among the 4 neighbors in the
 * cardinal directions.
 *
 * Finally, the output image is formed by choosing for each pixel the demosaiced
 * result which has the most homogeneous "votes" in the surrounding 3x3 neighborhood.
 *
 * @param red_offset     The x,y offset to the first red pixel in the Bayer pattern.
 * @param camera_to_XYZ   The matrix that transforms from sensor values to XYZ with
 *                      D65 white point.
 */
void demosaic_AHD(HDRImage &raw, const nanogui::Vector2i &red_offset, const Imath::Matrix33<float> &camera_to_XYZ);

/*!
 * \brief Interpolate the missing green pixels using the method by Phelippeau et al. 2009.
 *
 * @param red_offset The x,y offset to the first red pixel in the Bayer pattern.
 */
void demosaic_green_Phelippeau(HDRImage &raw, const nanogui::Vector2i &red_offset);

/*!
 * \brief   Demosaic the border of the image using naive averaging.
 *
 * Provides a results for all border pixels using a straight averge of the available pixels
 * in the 3x3 neighborhood. Useful in combination with more sophisticated methods which
 * require a larger window, and therefore cannot produce results at the image boundary.
 *
 * @param border    The size of the border in pixels.
 */
void demosaic_border(HDRImage &raw, size_t border);
