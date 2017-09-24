//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "imageshader.h"
#include "common.h"
#include "dither-matrix256.h"
#include <random>

using namespace nanogui;
using namespace Eigen;
using namespace std;

namespace
{
std::mt19937 g_rand(53);
// Vertex shader
constexpr char const *const vertexShader =
R"(#version 330

    uniform vec2 imageScale;
    uniform vec2 imagePosition;

    uniform vec2 referenceScale;
    uniform vec2 referencePosition;

    in vec2 vertex;

    out vec2 imageUV;
	out vec2 referenceUV;

    void main()
    {
        imageUV = (vertex/2.0 - imagePosition + 0.5) / imageScale;
		referenceUV = (vertex/2.0 - referencePosition + 0.5) / referenceScale;
        gl_Position  = vec4(vertex.x, -vertex.y, 0.0, 1.0);
    }
)";

// Fragment shader
constexpr char const *const fragmentShader =
	R"(#version 330

	#ifndef saturate
	#define saturate(v) clamp(v, 0, 1)
	#endif

    uniform sampler2D ditherImg;
    uniform vec2 randomness;
    uniform bool hasDither;

    uniform sampler2D image;
	uniform bool hasImage;

    uniform sampler2D reference;
	uniform bool hasReference;

	uniform int blendMode;
    uniform float gain;
    uniform int channel;
    uniform float gamma;
    uniform bool sRGB;

    in vec2 imageUV;
	in vec2 referenceUV;
    in vec4 gl_FragCoord;

    out vec4 out_color;

	float linearToS(float a)
	{
		return a < 0.0031308 ? 12.92 * a : 1.055 * pow(a, 1.0/2.4) - 0.055;
	}

    vec3 linearToSRGB(vec3 color)
    {
       return vec3(linearToS(color.r), linearToS(color.g), linearToS(color.b));
    }

	float sToLinear(float a)
	{
		return a < 0.04045 ? (1.0 / 12.92) * a : pow((a + 0.055) * (1.0 / 1.055), 2.4);
	}

    vec3 sRGBToLinear(vec3 color)
    {
       return vec3(sToLinear(color.r), sToLinear(color.g), sToLinear(color.b));
    }

	vec3 tonemap(vec3 color)
	{
		return sRGB ? gain * linearToSRGB(color) : gain * pow(color, vec3(1.0/gamma));
	}

	vec3 inverseTonemap(vec3 color)
	{
		return sRGB ? sRGBToLinear(color/gain) : pow(color/gain, vec3(gamma));
	}

	// returns the luminance of a linear rgb color
	vec3 RGBToLuminance(vec3 rgb)
	{
		const vec3 RGB2Y = vec3(0.212671, 0.715160, 0.072169);
		return vec3(dot(RGB2Y, rgb));
	}

	// Converts a color from linear RGB to XYZ space
	vec3 RGBToXYZ(vec3 rgb)
	{
		const mat3 RGB2XYZ = mat3(
		    0.412453, 0.212671, 0.019334,
		    0.357580, 0.715160, 0.119193,
		    0.180423, 0.072169, 0.950227);
	    return RGB2XYZ * rgb;
	}

	// Converts a color from XYZ to linear RGB space
	vec3 XYZToRGB(vec3 xyz)
	{
		const mat3 XYZ2RGB = mat3(
		     3.240479, -0.969256,  0.055648,
		    -1.537150,  1.875992, -0.204043,
		    -0.498535,  0.041556,  1.057311);
	    return XYZ2RGB * xyz;
	}

	float labf(float t)
	{
		const float c1 = 0.008856451679;    // pow(6.0/29.0, 3.0);
		const float c2 = 7.787037037;       // pow(29.0/6.0, 2.0)/3;
		const float c3 = 0.1379310345;      // 16.0/116.0
		return (t > c1) ? pow(t, 1.0/3.0) : (c2*t) + c3;
	}

	vec3 XYZToLab(vec3 xyz)
	{
		// N=normalize for D65 white point
	    xyz /= vec3(.95047, 1.000, 1.08883);

	    vec3 v = vec3(labf(xyz.x), labf(xyz.y), labf(xyz.z));
	    return vec3((116.0 * v.y) - 16.0,
					500.0 * (v.x - v.y),
					200.0 * (v.y - v.z));
	}

	vec3 RGBToLab(vec3 rgb)
	{
		vec3 lab = XYZToLab(RGBToXYZ(rgb));

		// renormalize
		const vec3 minLab = vec3(0, -86.1846, -107.864);
		const vec3 maxLab = vec3(100, 98.2542, 94.4825);
	    return (lab-minLab)/(maxLab-minLab);
	}

    // note: uniformly distributed, normalized rand, [0;1[
    float nrand(vec2 n)
    {
        return fract(sin(dot(n.xy, vec2(12.9898, 78.233)))* 43758.5453);
    }

    float randZeroMeanUniform(vec2 xy)
    {
        // Result is in range [-0.5, 0.5]
        return texture(ditherImg, xy/vec2(256,256)).r/65536 - 0.5;
    }

    float randZeroMeanTriangle(vec2 xy)
    {
        float r = randZeroMeanUniform(xy);

        // Convert uniform distribution into triangle-shaped distribution
        // Result is in range [-1.0,1.0]
        float rp = sqrt(2*r);       // positive triangle
        float rn = sqrt(2*r+1)-1;   // negative triangle
        return (r < 0) ? rn : rp;
    }

	vec3 jetFalseColor(vec3 col)
	{
		float x = saturate(RGBToLuminance(col).r);

		float r = saturate((x < 0.7) ? 4.0 * x - 1.5 : -4.0 * x + 4.5);
	    float g = saturate((x < 0.5) ? 4.0 * x - 0.5 : -4.0 * x + 3.5);
	    float b = saturate((x < 0.3) ? 4.0 * x + 0.5 : -4.0 * x + 2.5);
	    return vec3(r, g, b);
	}

	vec3 positiveNegative(vec3 col)
	{
		float x = dot(col, vec3(1.0)/3.0);
		float r = saturate(mix(0.0, 1.0, max(x, 0.0)));
		float g = 0.0;
		float b = saturate(mix(0.0, 1.0, -min(x, 0.0)));
		return vec3(r, g, b);
	}

	vec3 chooseChannel(vec3 col)
	{
		switch (channel)
		{
			case CHANNEL_RED:           return col.rrr;
			case CHANNEL_GREEN:         return col.ggg;
			case CHANNEL_BLUE:          return col.bbb;
			case CHANNEL_LUMINANCE:     return RGBToLuminance(col);
			case CHANNEL_CIEL:          return RGBToLab(col).xxx;
			case CHANNEL_CIEa:          return RGBToLab(col).yyy;
			case CHANNEL_CIEb:          return RGBToLab(col).zzz;
			case CHANNEL_FALSE_COLOR:   return jetFalseColor(col);
			case CHANNEL_POSITIVE_NEGATIVE:       return positiveNegative(col);
		}
		return col.rgb;
	}

	vec4 blend(vec4 imageVal, vec4 referenceVal)
	{
		vec3 diff = imageVal.rgb - referenceVal.rgb;
		float alpha = imageVal.a + referenceVal.a*(1-imageVal.a);
        switch (blendMode)
		{
			case NORMAL_BLEND:              return vec4(imageVal.rgb*imageVal.a + referenceVal.rgb*referenceVal.a*(1-imageVal.a), alpha);
			case MULTIPLY_BLEND:            return vec4(imageVal.rgb * referenceVal.rgb, alpha);
			case DIVIDE_BLEND:              return vec4(imageVal.rgb / referenceVal.rgb, alpha);
			case ADD_BLEND:                 return vec4(imageVal.rgb + referenceVal.rgb, alpha);
			case AVERAGE_BLEND:             return 0.5*(imageVal + referenceVal);
			case SUBTRACT_BLEND:            return vec4(diff, alpha);
            case DIFFERENCE_BLEND:          return vec4(abs(diff), alpha);
            case RELATIVE_DIFFERENCE_BLEND: return vec4(abs(diff) / (referenceVal.rgb + vec3(0.01)), alpha);
        }
        return vec4(0.0);
    }

	vec3 dither(vec3 color)
	{
		if (hasDither)
			return color;

		return color + vec3(randZeroMeanTriangle(gl_FragCoord.xy + randomness)/255.0);
	}

    void main()
    {
        vec3 darkGray = vec3(0.1, 0.1, 0.1);
        vec3 lightGray = vec3(0.2, 0.2, 0.2);

        vec3 checker = mod(int(floor(gl_FragCoord.x / 8) + floor(gl_FragCoord.y / 8)), 2) == 0 ? darkGray : lightGray;

		out_color.a = 1.0;

		if (!hasImage)
		{
			out_color.rgb = tonemap(checker);
            return;
        }

        vec4 imageVal = texture(image, imageUV);

		if (hasReference)
		{
			vec4 referenceVal = texture(reference, referenceUV);
			imageVal = blend(imageVal, referenceVal);
		}

		out_color.rgb = mix(checker, dither(tonemap(chooseChannel(imageVal.rgb))), imageVal.a);
    }
)";

void setDitherParams(GLShader & shader, GLuint textureId, bool hasDither)
{
	shader.setUniform("hasDither", (int)hasDither);
	if (!hasDither)
		return;

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, textureId);

	shader.setUniform("ditherImg", 0);
	Vector2f randomness(std::generate_canonical<float, 10>(g_rand)*255,
	                    std::generate_canonical<float, 10>(g_rand)*255);
	shader.setUniform("randomness", randomness);
}

void setImageParams(GLShader & shader,
                    GLuint imageId,
                    const Vector2f & scale,
                    const Vector2f & position,
                    float gain, float gamma, bool sRGB,
                    EChannel channel)
{
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, imageId);

	shader.setUniform("gain", gain);
	shader.setUniform("gamma", gamma);
	shader.setUniform("sRGB", (int)sRGB);
	shader.setUniform("channel", (int)channel);

	shader.setUniform("image", 1);
	shader.setUniform("imageScale", scale);
	shader.setUniform("imagePosition", position);
}

void setReferenceParams(GLShader & shader,
                        GLuint referenceId,
                        const Vector2f & scale,
                        const Vector2f & position,
                        EBlendMode blendMode)
{
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, referenceId);

	shader.setUniform("reference", 2);
	shader.setUniform("referenceScale", scale);
	shader.setUniform("referencePosition", position);
	shader.setUniform("blendMode", (int)blendMode);
}

} // namespace

#define DEFINE_PARAMS(parent,name) m_shader.define(#name, to_string(parent::name))
#define DEFINE_PARAMS2(parent,name,prefix) m_shader.define(#prefix#name, to_string(parent::name))

ImageShader::ImageShader()
{
	DEFINE_PARAMS2(EChannel, RED, CHANNEL_);
	DEFINE_PARAMS2(EChannel, GREEN, CHANNEL_);
	DEFINE_PARAMS2(EChannel, BLUE, CHANNEL_);
	DEFINE_PARAMS2(EChannel, RGB, CHANNEL_);
	DEFINE_PARAMS2(EChannel, LUMINANCE, CHANNEL_);
	DEFINE_PARAMS2(EChannel, CIEL, CHANNEL_);
	DEFINE_PARAMS2(EChannel, CIEa, CHANNEL_);
	DEFINE_PARAMS2(EChannel, CIEb, CHANNEL_);
	DEFINE_PARAMS2(EChannel, FALSE_COLOR, CHANNEL_);
	DEFINE_PARAMS2(EChannel, POSITIVE_NEGATIVE, CHANNEL_);

	DEFINE_PARAMS(EBlendMode, NORMAL_BLEND);
	DEFINE_PARAMS(EBlendMode, MULTIPLY_BLEND);
	DEFINE_PARAMS(EBlendMode, DIVIDE_BLEND);
	DEFINE_PARAMS(EBlendMode, ADD_BLEND);
	DEFINE_PARAMS(EBlendMode, AVERAGE_BLEND);
	DEFINE_PARAMS(EBlendMode, SUBTRACT_BLEND);
	DEFINE_PARAMS(EBlendMode, DIFFERENCE_BLEND);
	DEFINE_PARAMS(EBlendMode, RELATIVE_DIFFERENCE_BLEND);

	// Gamma/exposure tonemapper with hasDither as a GLSL shader
	m_shader.init("Tonemapper", vertexShader, fragmentShader);

	// Draw 2 triangles
	MatrixXu indices(3, 2);
	indices.col(0) << 0, 1, 2;
	indices.col(1) << 2, 3, 1;

	MatrixXf vertices(2, 4);
	vertices.col(0) << -1, -1;
	vertices.col(1) <<  1, -1;
	vertices.col(2) << -1,  1;
	vertices.col(3) <<  1,  1;

	m_shader.bind();
	m_shader.uploadIndices(indices);
	m_shader.uploadAttrib("vertex", vertices);

	// Allocate texture memory for the hasDither image
	glGenTextures(1, &m_ditherTexId);
	glBindTexture(GL_TEXTURE_2D, m_ditherTexId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glPixelStorei(GL_UNPACK_ROW_LENGTH, 256);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 256, 256,
	             0, GL_RED, GL_FLOAT, (const GLvoid *) dither_matrix256);
}

ImageShader::~ImageShader()
{
	m_shader.free();
	if (m_ditherTexId)
		glDeleteTextures(1, &m_ditherTexId);
}

void ImageShader::draw(GLuint imageId,
						const Vector2f & imageScale, const Vector2f & imagePosition,
						float gain, float gamma, bool sRGB, bool hasDither,
						EChannel channel, EBlendMode mode)
{
	m_shader.bind();

	setDitherParams(m_shader, m_ditherTexId, hasDither);
	setImageParams(m_shader, imageId, imageScale, imagePosition, gain, gamma, sRGB, channel);
	m_shader.setUniform("hasImage", (int)true);
	m_shader.setUniform("hasReference", (int)false);

	m_shader.drawIndexed(GL_TRIANGLES, 0, 2);
}

void ImageShader::draw(GLuint imageId,
                       GLuint referenceId,
                       const Vector2f & imageScale, const Vector2f & imagePosition,
                       const Vector2f & referenceScale, const Vector2f & referencePosition,
                       float gain, float gamma, bool sRGB, bool hasDither,
                       EChannel channel, EBlendMode mode)
{
	m_shader.bind();

	setDitherParams(m_shader, m_ditherTexId, hasDither);
	setImageParams(m_shader, imageId, imageScale, imagePosition, gain, gamma, sRGB, channel);
	setReferenceParams(m_shader, referenceId, referenceScale, referencePosition, mode);
	m_shader.setUniform("hasImage", (int)true);
	m_shader.setUniform("hasReference", (int)true);

	m_shader.drawIndexed(GL_TRIANGLES, 0, 2);
}