//
// Created by Wojciech Jarosz on 9/8/17.
//

#pragma once

#include <nanogui/opengl.h>
#include <nanogui/glutil.h>
#include "common.h"

/*!
 * Draws an image to the screen, optionally with high-quality dithering.
 */
class ImageShader
{
public:
	ImageShader();
	virtual ~ImageShader();

	void draw(GLuint imageId,
	          const Eigen::Vector2f & scale,
	          const Eigen::Vector2f & position,
	          float gain, float gamma,
	          bool sRGB, bool dither,
	          EChannel channel, EBlendMode mode);

	void draw(GLuint imageId,
	          GLuint referenceId,
	          const Eigen::Vector2f & imageScale,
	          const Eigen::Vector2f & imagePosition,
	          const Eigen::Vector2f & referenceScale,
	          const Eigen::Vector2f & referencePosition,
	          float gain, float gamma,
	          bool sRGB, bool dither,
	          EChannel channel, EBlendMode mode);

private:
	nanogui::GLShader m_shader;
	GLuint m_ditherTexId = 0;
};