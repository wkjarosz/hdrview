#include "pfm.h"
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <cmath>

using namespace std;

float * load_pfm(const char * filename, int * width, int * height, int * numChannels)
{
	float * data = nullptr;
	FILE *f = 0;

	try
	{
		f = fopen(filename, "rb");

		if (!f)
			throw runtime_error("load_pfm: Error opening");

		char buffer[1024];
		fscanf(f, "%s\n", buffer);
	    if (strcmp(buffer, "Pf") == 0)
	        *numChannels = 1;
	    else if (strcmp(buffer, "PF") == 0)
	        *numChannels = 3;
	    else
			throw runtime_error("load_pfm: Cannot deduce number of channels from header");


		fscanf(f, "%d%d", width, height);
		if (*width <= 0 || *height <= 0)
	    	throw runtime_error("load_pfm: Invalid image width or height");

		float scale;
		fscanf(f, "%f", &scale);
		if (scale > 0.0f)
	    	throw runtime_error("loadPFM: Big-Endian files not supported");

		data = new float[(*width) * (*height) * 3];

		fread(data, 1, 1, f);
		fread(data, sizeof(float), (*width) * (*height) * (*numChannels), f);

		// multiply data by scale factor
		scale = fabsf(scale);
		if (scale != 1.f)
	        for (int i = 0; i < (*width) * (*height) * (*numChannels); ++i)
	            data[i] *= scale;

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

bool write_pfm(const char * filename, int width, int height, int numChannels, const float * data)
{
	FILE *f = fopen(filename, "wb");

	if (!f)
	{
		cerr << "writePFM: Error opening file '" << filename << "'" << endl;
		return false;
	}

	fprintf(f, numChannels == 1 ? "Pf\n" : "PF\n");
	fprintf(f, "%d %d\n", width, height);
	fprintf(f, "-1.0000000\n");

	if (numChannels == 3 || numChannels == 1)
	{
		fwrite(&data[0], width * height * sizeof(float) * numChannels,  1, f);
	}
	else if (numChannels == 4)
	{
		for (int i = 0; i < width * height * 4; i += 4)
			fwrite(&data[i], sizeof(float) * 3,  1, f);
	}
	else
	{
		fclose(f);
		cerr << "writePFM: Unsupported number of channels "
			 << numChannels << " when writing file '" << filename << "'" << endl;
		return false;
	}

	fclose(f);
	return true;
}