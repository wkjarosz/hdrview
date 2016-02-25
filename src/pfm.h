#pragma once

bool write_pfm(const char * filename, int width, int height, int numChannels, const float * data);
float * load_pfm(const char * filename, int * width, int * height, int * numChannels);