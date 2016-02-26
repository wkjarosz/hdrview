/*! \file FullScreenDitherer.h
    \author Wojciech Jarosz
*/
#pragma once

#include <nanogui/opengl.h>
#include <nanogui/glutil.h>
#include "dither-matrix256.h"

/*!
    A class which draws a full-screen quad with a dither matrix
    with values in the range [-0.5,0.5]/255.0
*/
class FullScreenDitherer
{
public:
    ~FullScreenDitherer()
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
                     0, GL_RED, GL_FLOAT, (uint8_t *) dither_matrix256);
    }

    void bind()
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texture);
    }

    uint32_t texture = 0;
};
