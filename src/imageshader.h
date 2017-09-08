//
// Created by Wojciech Jarosz on 9/8/17.
//

#pragma once

#include <nanogui/opengl.h>
#include <nanogui/glutil.h>

/*!
 * Draws an image to the screen, optionally with high-quality dithering.
 */
class ImageShader
{
public:
	ImageShader();
	virtual ~ImageShader();

	void draw(GLuint textureId,
	          const Eigen::Vector2f & scale,
	          const Eigen::Vector2f & position,
	          float gain, float gamma,
	          bool sRGB, bool dither,
	          const Eigen::Vector3f & channels);

private:
	nanogui::GLShader m_shader;
	GLuint m_ditherTexId = 0;
};