//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once
#include "json.h"
#include <ImfHeader.h>

json exr_header_to_json(const Imf::Header &header);
