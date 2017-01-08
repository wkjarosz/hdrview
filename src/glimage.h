/*! \file glimage.h
    \author Wojciech Jarosz
*/
#pragma once

#include <string>
#include <nanogui/opengl.h>
#include <nanogui/glutil.h>
#include "hdrimage.h"

/*!
    A class which encapulates a single image which is draw as a
    textured GL quad to the screen.
*/
class GLImage
{
public:
    GLImage();
    ~GLImage();
    void clear();

    void init();

    std::string filename() const {return m_filename;}
    const HDRImage & image() const {return m_image;}
    const Color4 & pixel(int x, int y) const {return m_image(x, y);}
    Color4 & pixel(int x, int y) {return m_image(x, y);}
    Eigen::Vector2i size() const {return Eigen::Vector2i(width(), height());}
    int width() const {return m_image.width();}
    int height() const {return m_image.height();}

    void draw(const Eigen::Matrix4f & mvp,
              float gain, float gamma,
              bool sRGB, bool dither,
              const Eigen::Vector3f & channels);
    bool load(const std::string & filename)
    {
        m_filename = filename;
        return m_image.load(filename);
    }
    bool save(const std::string & filename,
              float gain, float gamma,
              bool sRGB, bool dither)
    {
        return m_image.save(filename, gain, gamma, sRGB, dither);
    }

private:
    nanogui::GLShader * m_shader = nullptr;
    uint32_t m_texture = 0;
    HDRImage m_image;
    std::string m_filename;
};
