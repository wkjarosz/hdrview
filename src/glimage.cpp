/*! \file glimage.cpp
    \author Wojciech Jarosz
*/
#include "glimage.h"
#include <iostream>     // std::cout, std::fixed
#include <algorithm>    // std::transform
#include <exception>    // std::transform
#include <random>

using namespace nanogui;
using namespace Eigen;
using namespace std;

namespace
{
    std::mt19937 g_rand(53);
}

GLImage::GLImage() :
    m_filename()
{
    // empty
}


GLImage::~GLImage()
{
    clear();
}


void GLImage::init()
{
    m_shader = new GLShader();
    // Gamma/exposure tonemapper with dither as a GLSL shader
    m_shader->init(
        "Tonemapper",

        // Vertex shader
        R"(
            #version 330
            uniform mat4 modelViewProj;
            in vec2 position;
            out vec2 uv;
            void main()
            {
                gl_Position = modelViewProj * vec4(position.x, position.y, 0.0, 1.0);
                uv = vec2((position.x+1)/2, (-position.y+1)/2);
            }
        )",

        // Fragment shader
        R"(
            #version 330
            uniform sampler2D source;
            uniform sampler2D dither_texture;
            uniform vec2 randomness;
            uniform bool dither;
            uniform float gain;
            uniform vec3 channels;
            uniform float gamma;
            uniform bool sRGB;
            in vec2 uv;
            out vec4 out_color;
            in vec4 gl_FragCoord;
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
                return texture(dither_texture, xy/vec2(256,256)).r/65536 - 0.5;
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
            float checkerboard(float period, vec2 xy)
            {
                return step(0.0, sin(xy.x * 3.1415926 / period) *
                                 sin(xy.y * 3.1415926 / period));
            }
            void main()
            {
                vec4 fg = texture(source, uv);
                fg.rgb *= gain;
                vec4 bg;
                bg.rgb = vec3(checkerboard(8, gl_FragCoord.xy))*0.5 + 0.5;
                bg.a = 1.0;
                vec4 color = mix(bg, fg, fg.a);
                out_color.rgb = sRGB ? linearToSRGB(color.rgb) : pow(color.rgb, vec3(1.0/gamma));
                float dith = randZeroMeanTriangle(gl_FragCoord.xy + randomness);
                out_color.rgb += dither ? vec3(dith/255.0) : vec3(0.0);
                out_color.rgb = (channels.r == 0.0) ?
                                   (channels.g == 0.0 ? out_color.bbb : out_color.ggg) :
                                   (channels.g != 0.0 && channels.b != 0.0) ? out_color.rgb : out_color.rrr;
                out_color.a = 1.0;
            }
        )"
    );

    MatrixXu indices(3, 2); /* Draw 2 triangles */
    indices.col(0) << 0, 1, 2;
    indices.col(1) << 2, 3, 0;

    MatrixXf positions(2, 4);
    positions.col(0) << -1, -1;
    positions.col(1) << 1, -1;
    positions.col(2) << 1, 1;
    positions.col(3) << -1, 1;

    m_shader->bind();
    m_shader->uploadIndices(indices);
    m_shader->uploadAttrib("position", positions);

    // Allocate texture memory for the image
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    // glPixelStorei(GL_UNPACK_ROW_LENGTH, width());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width(), height(),
                 0, GL_RGBA, GL_FLOAT, (const GLvoid *) m_image.data());
}


void GLImage::clear()
{
    if (m_texture)
        glDeleteTextures(1, &m_texture);

    if (m_shader)
        m_shader->free();

    delete m_shader;
    m_shader = 0;
}


void GLImage::draw(const Matrix4f & mvp,
                     float gain, float gamma,
                     bool sRGB, bool dither,
                     const Vector3f & channels) const
{
    if (m_shader)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texture);

        m_shader->bind();
        m_shader->setUniform("gain", gain);
        m_shader->setUniform("gamma", gamma);
        m_shader->setUniform("channels", channels);
        m_shader->setUniform("source", 0);
        m_shader->setUniform("dither_texture", 1);
        m_shader->setUniform("dither", (int)dither);
        m_shader->setUniform("sRGB", (int)sRGB);
        Vector2f randomness(std::generate_canonical<float, 10>(g_rand)*255,
                            std::generate_canonical<float, 10>(g_rand)*255);
        m_shader->setUniform("randomness", randomness);
        m_shader->setUniform("modelViewProj", mvp);
        m_shader->drawIndexed(GL_TRIANGLES, 0, 2);
    }
}
