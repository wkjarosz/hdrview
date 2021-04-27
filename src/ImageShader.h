//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <nanogui/opengl.h>
// #include <nanogui/glutil.h>
#include <nanogui/shader.h>
#include "Common.h"

/*!
 * Draws an image to the screen, optionally with high-quality dithering.
 */
class ImageShader
{
public:
	ImageShader();
	virtual ~ImageShader();

	void draw(GLuint imageId,
	          const nanogui::Vector2f & scale,
	          const nanogui::Vector2f & position,
	          float gain, float gamma,
	          bool sRGB, bool dither,
	          EChannel channel, EBlendMode mode);

	void draw(GLuint imageId,
	          GLuint referenceId,
	          const nanogui::Vector2f & imageScale,
	          const nanogui::Vector2f & imagePosition,
	          const nanogui::Vector2f & referenceScale,
	          const nanogui::Vector2f & referencePosition,
	          float gain, float gamma,
	          bool sRGB, bool dither,
	          EChannel channel, EBlendMode mode);

private:
	// nanogui::Shader m_shader;
	GLuint m_ditherTexId = 0;
};