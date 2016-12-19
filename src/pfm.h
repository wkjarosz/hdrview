/*!
    pfm.h -- Declaration of routines to read and write a PFM images

    \author Wojciech Jarosz

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/
#pragma once

bool is_pfm(const char * filename);
bool write_pfm(const char * filename, int width, int height, int numChannels, const float * data);
float * load_pfm(const char * filename, int * width, int * height, int * numChannels);
