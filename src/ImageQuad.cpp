/*! \file ImageQuad.cpp
    \author Wojciech Jarosz
*/
#include "ImageQuad.h"
#include <iostream>     // std::cout, std::fixed
#include <algorithm>    // std::transform
#include <exception>    // std::transform

using namespace nanogui;
using namespace Eigen;
using namespace std;

ImageQuad::ImageQuad() :
    m_filename()
{
    // empty
}


ImageQuad::~ImageQuad()
{
    clear();
}


void ImageQuad::init()
{
    m_shader = new GLShader();
    /* Simple gamma tonemapper as a GLSL shader */
    m_shader->init(
        "Tonemapper",

        /* Vertex shader */
        "#version 330\n"
        "uniform mat4 modelViewProj;\n"
        "in vec2 position;\n"
        "out vec2 uv;\n"
        "void main() {\n"
        "    gl_Position = modelViewProj * vec4(position.x, position.y, 0.0, 1.0);\n"
        "    uv = vec2((position.x+1)/2, (-position.y+1)/2);\n"
        "}",

        /* Fragment shader */
        "#version 330\n"
        "uniform sampler2D source;\n"
        "uniform sampler2D dither_texture;\n"
        "uniform bool dither;\n"
        "uniform float gain;\n"
        "uniform vec3 channels;\n"
        "uniform float gamma;\n"
        "uniform bool sRGB;\n"
        "in vec2 uv;\n"
        "out vec4 out_color;\n"
        "in vec4 gl_FragCoord;\n"
        "float toSRGB(float value) {\n"
        "    if (value < 0.0031308)\n"
        "        return 12.92 * value;\n"
        "    return 1.055 * pow(value, 0.41666) - 0.055;\n"
        "}\n"
        "void main() {\n"
        "    vec4 color = texture(source, uv);\n"
        "    color.rgb *= gain;\n"
        "    if (sRGB)\n"
        "       out_color.rgb = vec3(toSRGB(color.r), toSRGB(color.g), toSRGB(color.b));\n"
        "    else\n"
        "       out_color.rgb = pow(color.rgb, vec3(1.0/gamma));\n"
        "    float dith = texture(dither_texture, gl_FragCoord.xy/vec2(256,256)).r/65536 - 0.5;\n"
        "    out_color.rgb += dither ? vec3(dith/255.0) : vec3(0.0);\n"
        "    out_color.rgb = (channels.r == 0.0) ? \n"
        "                       (channels.g == 0.0 ? out_color.bbb : out_color.ggg) :\n"
        "                       (channels.g != 0.0 && channels.b != 0.0) ? out_color.rgb : out_color.rrr;"
        "    out_color.a = color.a;\n"
        "}"
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

    /* Allocate texture memory for the rendered exr */
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    // glPixelStorei(GL_UNPACK_ROW_LENGTH, width());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width(), height(),
                 0, GL_RGBA, GL_FLOAT, (uint8_t *) m_image.data());
}


void ImageQuad::clear()
{
    if (m_texture)
        glDeleteTextures(1, &m_texture);

    if (m_shader)
        m_shader->free();

    delete m_shader;
    m_shader = 0;
}


void ImageQuad::draw(const Matrix4f & mvp,
                     float gain, float gamma,
                     bool sRGB, bool dither,
                     const Vector3f & channels)
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
        m_shader->setUniform("dither", dither);
        m_shader->setUniform("sRGB", sRGB);
        m_shader->setUniform("modelViewProj", mvp);
        m_shader->drawIndexed(GL_TRIANGLES, 0, 2);
    }
}
