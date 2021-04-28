//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
// This file is a modified version of Filmic Worlds' FilmicToneCurve class:
// https://github.com/johnhable/fw-public
// which was released into the public domain through the CC0 Universal license

#pragma once

#include "common.h"

class FilmicToneCurve
{
public:
	struct CurveParamsUser
	{
		float toeStrength = .25f;           ///< as a ratio in [0,1]
		float toeLength = .25f;             ///< as a ratio in [0,1]
		float shoulderStrength = 4.0f;      ///< white point, in F stops
		float shoulderLength = 0.5f;        ///< as a ratio in [0,1]
		float shoulderAngle = 0.5f;         ///< as a ratio in [0,1]
		float gamma = 1.0f;
	};

	struct CurveParamsDirect
	{
		CurveParamsDirect()
		{
			reset();
		}

		void reset()
		{
			x0 = .25f;
			y0 = .25f;
			x1 = .75f;
			y1 = .75f;
			W = 1.0f;

			gamma = 1.0f;

			overshootX = 0.0f;
			overshootY = 0.0f;
		}

		float x0;
		float y0;
		float x1;
		float y1;
		float W;

		float overshootX;
		float overshootY;

		float gamma;
	};

	struct CurveSegment
	{
		CurveSegment()
		{
			reset();
		}

		void reset()
		{
			offsetX = 0.0f;
			offsetY = 0.0f;
			scaleX = 1.0f; // always 1 or -1
			lnA = 0.0f;
			B = 1.0f;
		}

		float eval(float x) const;
		float evalInv(float y) const;

		float offsetX;
		float offsetY;
		float scaleX; // always 1 or -1
		float scaleY;
		float lnA;
		float B;
	};

	struct FullCurve
	{
		FullCurve()
		{
			reset();
		}

		void reset()
		{
			W = 1.0f;
			invW = 1.0f;

			x0 = .25f;
			y0 = .25f;
			x1 = .75f;
			y1 = .75f;


			for (int i = 0; i < 3; i++)
			{
				m_segments[i].reset();
				m_invSegments[i].reset();
			}
		}

		float eval(float x) const;
		float evalInv(float x) const;

		float W;
		float invW;

		float x0;
		float x1;
		float y0;
		float y1;

		CurveSegment m_segments[3];
		CurveSegment m_invSegments[3];
	};

	static void createCurve(FullCurve& dstCurve, const CurveParamsDirect& srcParams);
	static void calcDirectParamsFromUser(CurveParamsDirect& dstParams, const CurveParamsUser& srcParams);
};
