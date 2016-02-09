/*! \file ImageQuad.h
    \author Wojciech Jarosz
*/
#ifndef IMAGE_QUAD_H
#define IMAGE_QUAD_H

#include <string>  
#include <nanogui/opengl.h>
#include <nanogui/glutil.h>


/*!
    A class which encapulates a single image which is draw as a
    textured GL quad to the screen.
*/
class ImageQuad
{
public:
    ImageQuad();
    ~ImageQuad();
    void clear();


    void init();

    std::string filename() const {return m_filename;}
    const float * rgba() const {return m_image;}
    nanogui::Color pixel(int x, int y) const;
    const Eigen::Vector2i & size() const {return m_size;}
    int width() const {return m_size.x();}
    int height() const {return m_size.y();}

    void draw(const Eigen::Matrix4f & mvp,
              float gain, float gamma,
              bool sRGB, bool dither,
              const Eigen::Vector3f & channels);
    bool load(const std::string & filename);
    bool save(const std::string & filename,
              float gain, float gamma,
              bool sRGB, bool dither);

private:
    nanogui::GLShader * m_shader = nullptr;
    uint32_t m_texture = 0;
    float * m_image = nullptr;
    Eigen::Vector2i m_size = Eigen::Vector2i::Zero();
    std::string m_filename;
};

#endif
