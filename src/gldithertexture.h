/*! \file gldithertexture.h
    \author Wojciech Jarosz
*/
#pragma once

#include <nanogui/opengl.h>
#include <nanogui/glutil.h>
#include "dither-matrix256.h"

/*!
    A simple utility class for uploading and binding the dither matrix to the
    GPU.
*/
class GLDitherTexture
{
public:
    ~GLDitherTexture()
    {
        if (texture)
            glDeleteTextures(1, &texture);
    }

    void init()
    {
        /* Allocate texture memory for the rendered image */
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        glPixelStorei(GL_UNPACK_ROW_LENGTH, 256);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 256, 256,
                     0, GL_RED, GL_FLOAT, (const GLvoid *) dither_matrix256);
    }

    void bind()
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texture);
    }

    uint32_t texture = 0;
};
