//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "pfm.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <cstdint>

using namespace std;


namespace
{

float reinterpret_as_host_endian(float f, bool bigEndian)
{
	static_assert(sizeof(float) == sizeof(unsigned int), "Sizes must match");

	const auto* uchar = (const unsigned char *) &f;
	uint32_t i;
	if (bigEndian)
		i = (uchar[3]<<0) | (uchar[2]<<8) | (uchar[1]<<16) | (uchar[0]<<24);
	else
		i = (uchar[0]<<0) | (uchar[1]<<8) | (uchar[2]<<16) | (uchar[3]<<24);

	float ret;
	memcpy(&ret, &i, sizeof(float));
	return ret;
}

} // end namespace

bool is_pfm_image(const char *filename) noexcept
{
	FILE *f = nullptr;
	int numInputsRead = 0;

	try
	{
		f = fopen(filename, "rb");

		if (!f)
			throw runtime_error("load_pfm_image: Error opening");

		char buffer[1024];
		numInputsRead = fscanf(f, "%2s\n", buffer);
		if (numInputsRead != 1)
			throw runtime_error("load_pfm_image: Could not read number of channels in header");

	    if (!(strcmp(buffer, "Pf") == 0 || strcmp(buffer, "PF") == 0))
			throw runtime_error("load_pfm_image: Cannot deduce number of channels from header");

		int width, height;
		numInputsRead = fscanf(f, "%d%d", &width, &height);
		if (numInputsRead != 2 || width <= 0 || height <= 0)
	    	throw runtime_error("load_pfm_image: Invalid image width or height");

		float scale;
		numInputsRead = fscanf(f, "%f", &scale);
		if (numInputsRead != 1)
	    	throw runtime_error("load_pfm_image: Invalid file endianness. Big-Endian files not supported");

		fclose(f);
		return true;
	}
	catch (const exception & e)
	{
		fclose(f);
		return false;
	}
}

float * load_pfm_image(const char *filename, int *width, int *height, int *numChannels)
{
	float * data = nullptr;
	FILE *f = nullptr;
	int numInputsRead = 0;

	try
	{
		f = fopen(filename, "rb");

		if (!f)
			throw runtime_error("load_pfm_image: Error opening");

		char buffer[1024];
		numInputsRead = fscanf(f, "%2s\n", buffer);
		if (numInputsRead != 1)
			throw runtime_error("load_pfm_image: Could not read number of channels in header");

	    if (strcmp(buffer, "Pf") == 0)
	        *numChannels = 1;
	    else if (strcmp(buffer, "PF") == 0)
	        *numChannels = 3;
	    else
			throw runtime_error("load_pfm_image: Cannot deduce number of channels from header");


		numInputsRead = fscanf(f, "%d%d", width, height);
		if (numInputsRead != 2 || *width <= 0 || *height <= 0)
	    	throw runtime_error("load_pfm_image: Invalid image width or height");

		float scale;
		numInputsRead = fscanf(f, "%f", &scale);
		if (numInputsRead != 1)
	    	throw runtime_error("load_pfm_image: Invalid file endianness. Big-Endian files not supported");

	    bool bigEndian = scale > 0.0f;

		data = new float[(*width) * (*height) * 3];

		if (fread(data, 1, 1, f) != 1)
			throw runtime_error("load_pfm_image: Unknown error");

		size_t numFloats = static_cast<size_t>((*width) * (*height) * (*numChannels));
		if (fread(data, sizeof(float), numFloats, f) != numFloats)
			throw runtime_error("load_pfm_image: Could not read all pixel data");

		// multiply data by scale factor
		scale = fabsf(scale);
        for (size_t i = 0; i < numFloats; ++i)
            data[i] = scale*reinterpret_as_host_endian(data[i], bigEndian);

		fclose(f);
		return data;
	}
	catch (const runtime_error & e)
	{
		fclose(f);
		delete [] data;
		throw runtime_error(string(e.what()) + " in file '" + filename + "'");
	}
}

bool write_pfm_image(const char *filename, int width, int height, int numChannels, const float *data)
{
	FILE *f = fopen(filename, "wb");

	if (!f)
	{
		cerr << "write_pfm_image: Error opening file '" << filename << "'" << endl;
		return false;
	}

	fprintf(f, numChannels == 1 ? "Pf\n" : "PF\n");
	fprintf(f, "%d %d\n", width, height);

	// determine system endianness
	bool littleEndian = false;
	{
		int n = 1;
		// little endian if true
		if (*(char *)&n == 1)
			littleEndian = true;
	}

	fprintf(f, littleEndian ? "-1.0000000\n" : "1.0000000\n");

	if (numChannels == 3 || numChannels == 1)
	{
		fwrite(&data[0], width * height * sizeof(float) * numChannels, 1, f);
	}
	else if (numChannels == 4)
	{
		for (int i = 0; i < width * height * 4; i += 4)
			fwrite(&data[i], sizeof(float) * 3, 1, f);
	}
	else
	{
		fclose(f);
		cerr << "write_pfm_image: Unsupported number of channels "
			 << numChannels << " when writing file '" << filename << "'" << endl;
		return false;
	}

	fclose(f);
	return true;
}
