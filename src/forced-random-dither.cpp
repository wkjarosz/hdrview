/*!
    force-random-dither.cpp -- Generate a dither matrix using the force-random-dither method

    \author Wojciech Jarosz

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/

#include <iostream>
#include <Eigen/Dense>
#include <vector>
#include <algorithm>    // std::random_shuffle
#include <ctime>        // std::time
#include <cstdlib>      // std::rand, std::srand

#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

using Eigen::MatrixXd;
using Eigen::ArrayXXd;
using Eigen::Vector2i;
using namespace std;

const int Sm = 128;
const int Smk = Sm*Sm;


double toroidalMinimumDistance(const Vector2i & a, const Vector2i & b)
{
	int x0 = min(a.x(), b.x());
	int x1 = max(a.x(), b.x());
	int y0 = min(a.y(), b.y());
	int y1 = max(a.y(), b.y());
	double deltaX = min(x1-x0, x0+Sm-x1);
	double deltaY = min(y1-y0, y0+Sm-y1);
	return sqrt(deltaX*deltaX + deltaY*deltaY);
}

double force(double r)
{
	return exp(-sqrt(2*r));
}


int writeEXR(string name, const ArrayXXd & M)
{
	EXRImage image;
	InitEXRImage(&image);

	image.num_channels = 3;

	int width = Sm, height = Sm;

	// Must be BGR(A) order, since most of EXR viewers expect this channel order.
	const char* channel_names[] = {"B", "G", "R"}; // "B", "G", "R", "A" for RGBA image

	std::vector<float> images[3];
	images[0].resize(width * height);
	images[1].resize(width * height);
	images[2].resize(width * height);

	for (int i = 0; i < width * height; i++)
		images[0][i] = images[1][i] = images[2][i] = M(i);

	float* image_ptr[3];
	image_ptr[0] = &(images[2].at(0)); // B
	image_ptr[1] = &(images[1].at(0)); // G
	image_ptr[2] = &(images[0].at(0)); // R

	image.channel_names = channel_names;
	image.images = (unsigned char**)image_ptr;
	image.width = width;
	image.height = height;

	image.pixel_types = (int *)malloc(sizeof(int) * image.num_channels);
	image.requested_pixel_types = (int *)malloc(sizeof(int) * image.num_channels);
	for (int i = 0; i < image.num_channels; i++) {
		image.pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT; // pixel type of input image
		image.requested_pixel_types[i] = TINYEXR_PIXELTYPE_HALF; // pixel type of output image to be stored in .EXR
	}

	const char* err;
	int ret = SaveMultiChannelEXRToFile(&image, name.c_str(), &err);
	if (ret != 0)
	{
		fprintf(stderr, "Save EXR err: %s\n", err);
		return ret;
	}
	printf("Saved exr file. [ %s ] \n", name.c_str());
}

int main(int argc, char **argv)
{
	srand(unsigned ( std::time(0) ) );

	vector<Vector2i> freeLocations;
	ArrayXXd M = ArrayXXd::Zero(Sm,Sm);
	ArrayXXd forceField = ArrayXXd::Zero(Sm,Sm);

	// initialize free locations
	for (int y = 0; y < Sm; ++y)
		for (int x = 0; x < Sm; ++x)
			freeLocations.push_back(Vector2i(x,y));

	
	for (int ditherValue = 0; ditherValue < Smk; ++ditherValue)
	{
  		random_shuffle(freeLocations.begin(), freeLocations.end());
		
		double minimum = 1e20f;
		Vector2i minimumLocation(0,0);

		// int halfP = freeLocations.size();
		int halfP = min(max(1, (int)sqrt(freeLocations.size()*3/4)), (int)freeLocations.size());
		// int halfP = min(10, (int)freeLocations.size());
		for (int i = 0; i < halfP; ++i)
		{
			const Vector2i & location = freeLocations[i];
			if (forceField(location.x(), location.y()) < minimum)
			{
				minimum = forceField(location.x(), location.y());
				minimumLocation = location;
			}
		}

		Vector2i cell(0,0);
		for (cell.y() = 0; cell.y() < Sm; ++cell.y())
			for (cell.x() = 0; cell.x() < Sm; ++cell.x())
			{
				double r = toroidalMinimumDistance(cell, minimumLocation);
				forceField(cell.x(), cell.y()) += force(r);
			}

		freeLocations.erase(remove(freeLocations.begin(), freeLocations.end(), minimumLocation), freeLocations.end());
		M(minimumLocation.x(), minimumLocation.y()) = ditherValue;

		// if (ditherValue % 16 == 0)
		// {
		// 	std::stringstream ss;
  //           ss << ditherValue;
		// 	writeEXR("forceField-" + ss.str() + ".exr", forceField/(ditherValue+1));
		// }
	}

	std::cout << M << std::endl;

	cout << "unsigned dither_matrix[" << Smk << "] = \n{\n ";
	printf("%5d", (int)M(0));
	for (int i = 1; i < M.size(); ++i)
	{
		cout << ", ";
		if (i % Sm == 0)
			cout << endl << " ";
		printf("%5d", (int)M(i));
	}

	cout << "\n};" << endl;

	writeEXR(argv[1], M/Smk);

	return 0;
}