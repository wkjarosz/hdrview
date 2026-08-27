//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "imageio/psd.h"

#include <string>

TEST_CASE("PSDMetadata::color_mode_name is defined for every 16-bit value")
{
    // color_mode is whatever uint16 the file's header held -- including PSDMetadata's own NotSet default of
    // 0xFFFF -- and the names table has ten entries. Indexing it directly walked off the end and handed a
    // wild const char* to std::string.
    CHECK(std::string(PSDMetadata::color_mode_name(PSDMetadata::RGB)) == "RGB");
    CHECK(std::string(PSDMetadata::color_mode_name(PSDMetadata::Bitmap)) == "Bitmap");
    CHECK(std::string(PSDMetadata::color_mode_name(PSDMetadata::Lab)) == "Lab");
    CHECK(std::string(PSDMetadata::color_mode_name(PSDMetadata::NotSet)) == "Unknown");

    for (int m = 0; m <= 0xFFFF; ++m)
    {
        auto name = PSDMetadata::color_mode_name((PSDMetadata::ColorMode)m);
        REQUIRE(name != nullptr);
        CHECK_FALSE(std::string(name).empty());
    }
}
