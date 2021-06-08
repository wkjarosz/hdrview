//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

bool   is_pfm_image(const char *filename) noexcept;
bool   write_pfm_image(const char *filename, int width, int height, int numChannels, const float *data);
float *load_pfm_image(const char *filename, int *width, int *height, int *numChannels);
