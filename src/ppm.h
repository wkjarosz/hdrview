/*!
    ppm.h -- Declaration of routines to read and write a PPM images

    \author Wojciech Jarosz

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/
#pragma once

bool is_ppm(const char * filename);
bool write_ppm(const char * filename, int width, int height, int numChannels, const unsigned char * data);
float * load_ppm(const char * filename, int * width, int * height, int * numChannels);
