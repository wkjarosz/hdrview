//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <string>

int         version_major();
int         version_minor();
int         version_patch();
int         version_combined();
std::string version();
std::string git_hash();
std::string git_describe();
std::string build_timestamp();
std::string backend();