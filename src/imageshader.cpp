//
// Created by Wojciech Jarosz on 9/8/17.
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


	const mat3 RGB2XYZ = (mat3(
	    0.412453, 0.357580, 0.180423,
	    0.212671, 0.715160, 0.072169,
	    0.019334, 0.119193, 0.950227
	));

	const mat3 XYZ2RGB = (mat3(
	     3.240479, -1.537150, -0.498535,
	    -0.969256,  1.875992,  0.041556,
	     0.055648, -0.204043,  1.057311
	));

	const vec3 RGB2Y = vec3(0.212671, 0.715160, 0.072169);

	// returns the luminance of a linear rgb color
	vec3 luminance(vec3 rgb)
	{
		return vec3(dot(RGB2Y, rgb));
	}

	// Converts a color from linear RGB to XYZ space
	vec3 rgb2xyz(vec3 rgb)
	{
	    return RGB2XYZ * rgb;
	}

	// Converts a color from XYZ to linear RGB space
	vec3 xyz2rgb(vec3 xyz)
	{
	    return XYZ2RGB * xyz;
	}

	vec3 xyz2lab(vec3 xyz)
	{
		// N=normalize for D65 white point
	    vec3 n = xyz / vec3(.95047, 1.0, 1.08883);
	    vec3 v;
		const float c1 = 0.008856451679;    // pow(6.0/29.0, 3.0);
		const float c2 = 7.787037037;       // pow(29.0/6.0, 2.0)/3;
		const float c3 = 0.1379310345;      // 16.0/116.0
	    v.x = (n.x > c1) ? pow(n.x, 1.0/3.0) : c2*n.x + c3;
	    v.y = (n.y > c1) ? pow(n.y, 1.0/3.0) : c2*n.y + c3;
	    v.z = (n.z > c1) ? pow(n.z, 1.0/3.0) : c2*n.z + c3;
	    return vec3((116.0 * v.y) - 16.0,
					500.0 * (v.x - v.y),
					200.0 * (v.y - v.z));
	}

	vec3 rgb2lab(vec3 rgb)
	{
		vec3 lab = xyz2lab(rgb2xyz(rgb));

		// renormalize from [0, 100],  [-86.185, 98.254], [-107.863, 94.482] to [0, 1]
		vec2 aRange = vec2(-86.185, 98.254);
		vec2 bRange = vec2(-107.863, 94.482);
	    return vec3(lab.x / 100.0, max(0.0, (lab.y-aRange.x)/(aRange.y-aRange.x)), max(0.0, (lab.z-bRange.x)/(bRange.y-bRange.x)));
	}


	// Converts from pure Hue to linear RGB
	vec3 hue2rgb(float hue)
	{
	    float R = abs(hue * 6 - 3) - 1;
	    float G = 2 - abs(hue * 6 - 2);
	    float B = 2 - abs(hue * 6 - 4);
	    return saturate(vec3(R,G,B));
	}

    vec3 linearToSRGB(vec3 color)
    {
       float r = color.r < 0.0031308 ? 12.92 * color.r : 1.055 * pow(color.r, 1.0/2.4) - 0.055;
       float g = color.g < 0.0031308 ? 12.92 * color.g : 1.055 * pow(color.g, 1.0/2.4) - 0.055;
       float b = color.b < 0.0031308 ? 12.92 * color.b : 1.055 * pow(color.b, 1.0/2.4) - 0.055;
       return vec3(r, g, b);
    }

    vec3 sRGBToLinear(vec3 color)
    {
       float r = color.r < 0.04045 ? (1.0 / 12.92) * color.r : pow((color.r + 0.055) * (1.0 / 1.055), 2.4);
       float g = color.g < 0.04045 ? (1.0 / 12.92) * color.g : pow((color.g + 0.055) * (1.0 / 1.055), 2.4);
       float b = color.b < 0.04045 ? (1.0 / 12.92) * color.b : pow((color.b + 0.055) * (1.0 / 1.055), 2.4);
       return vec3(r, g, b);
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
		float x = saturate(luminance(col).r);

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
			case CHANNEL_LUMINANCE:     return vec3(luminance(col));
			case CHANNEL_A:             return rgb2lab(col).yyy;
			case CHANNEL_B:             return rgb2lab(col).zzz;
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

	vec3 tonemap(vec3 color)
	{
		return sRGB ? gain * linearToSRGB(color) : gain * pow(color, vec3(1.0/gamma));
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
	DEFINE_PARAMS2(EChannel, A, CHANNEL_);
	DEFINE_PARAMS2(EChannel, B, CHANNEL_);
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