/*!
    ppm.cpp -- Routines to read and write a PPM images

    \author Wojciech Jarosz

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/
#include "ppm.h"
#include <cstdio>
#include <string>
#include <iostream>
#include <stdexcept>
#include <cmath>

using namespace std;

namespace
{

struct RGB
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
};

} // end namespace

bool is_ppm(const char * filename)
{
    FILE *infile = 0;
    int numInputsRead = 0;
    char buffer[256];

    try
    {
        infile = fopen(filename, "rb");

        if (!infile)
            throw std::runtime_error("cannot open file.");

        if ((fgets(buffer, 256, infile) == 0) || (buffer[0] != 'P') || (buffer[1] != '6'))
            throw std::runtime_error("image is not a binary PPM file.");

        // skip comments
        do {fgets(buffer, sizeof(buffer), infile);} while(buffer[0] == '#');

        // read image size
        int width, height;
        numInputsRead = sscanf(buffer, "%d %d", &width, &height);
        if (numInputsRead != 2)
            throw runtime_error("could not read number of channels in header.");

        // skip comments
        do {fgets(buffer, sizeof(buffer), infile);} while(buffer[0] == '#');

        // read maximum pixel value (usually 255)
        int colors;
        numInputsRead = sscanf(buffer, "%d", &colors);
        if (numInputsRead != 1)
            throw runtime_error("could not read max color value.");

        if (colors != 255)
            throw std::runtime_error("max color value must be 255.");

        fclose(infile);
        return true;
    }
    catch (const std::exception &e)
    {
        if (infile)
            fclose(infile);
        return false;
    }
}


float * load_ppm(const char * filename, int * width, int * height, int * numChannels)
{
    FILE *infile = 0;
    float * img = 0;
    int colors;
    int numInputsRead = 0;
    float invColors;
    char buffer[256];
    RGB *buf = 0;

    try
    {
        infile = fopen(filename, "rb");

        if (!infile)
            throw std::runtime_error("cannot open file.");

        if ((fgets(buffer, 256, infile) == 0) || (buffer[0] != 'P') || (buffer[1] != '6'))
            throw std::runtime_error("image is not a binary PPM file.");

        *numChannels = 3;

        // skip comments
        do {fgets(buffer, sizeof(buffer), infile);} while(buffer[0] == '#');

        // read image size
        numInputsRead = sscanf(buffer, "%d %d", width, height);
        if (numInputsRead != 2)
            throw runtime_error("could not read number of channels in header.");

        // skip comments
        do {fgets(buffer, sizeof(buffer), infile);} while(buffer[0] == '#');

        // read maximum pixel value (usually 255)
        numInputsRead = sscanf(buffer, "%d", &colors);
        if (numInputsRead != 1)
            throw runtime_error("could not read max color value.");
        invColors = 1.0f/colors;

        if (colors != 255)
            throw std::runtime_error("max color value must be 255.");

        img = new float [*width * *height * 3];

        buf = new RGB[*width];
        for (int y = 0; y < *height; ++y)
        {
            if (fread(buf, *width * sizeof(RGB), 1, infile) != 1)
                throw std::runtime_error("cannot read pixel data.");

            RGB *cur = buf;
            float * curLine = &img[y * *width * 3];
            for (int x = 0; x < *width; x++)
            {
                curLine[3*x + 0] = cur->r * invColors;
                curLine[3*x + 1] = cur->g * invColors;
                curLine[3*x + 2] = cur->b * invColors;
                cur++;
            }
        }
        delete [] buf;

        fclose(infile);
        return img;
    }
    catch (const std::exception &e)
    {
        delete [] buf;
        delete [] img;
        if (infile)
            fclose(infile);
        throw std::runtime_error(string("ERROR in load_ppm: ") +
                                 string(e.what()) +
                                 string(" Unable to read PPM file '") +
                                 filename + "'");
    }
}


bool write_ppm(const char * filename, int width, int height, int numChannels, const unsigned char * data)
{
    FILE *outfile = 0;

    try
    {
        outfile = fopen(filename, "wb");
        if (!outfile)
            throw std::runtime_error("cannot open file.");

        // write header
        fprintf(outfile, "P6\n");
        fprintf(outfile, "%d %d\n", width, height);
        fprintf(outfile, "255\n");

        size_t numChars = numChannels*width*height;
        if (fwrite(data, sizeof(unsigned char), numChars, outfile) != numChars)
            throw std::runtime_error("cannot write pixel data.");

        fclose (outfile);
        return true;
    }
    catch (const std::exception &e)
    {
        if (outfile)
            fclose (outfile);
        throw std::runtime_error(string("ERROR in write_ppm: ") +
                                 string(e.what()) +
                                 string(" Unable to write PPM file '") +
                                 string(filename) + "'");
    }
}
