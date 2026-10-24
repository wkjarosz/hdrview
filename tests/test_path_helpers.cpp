//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "common.h"

#include <utility>

TEST_CASE("is_url recognizes http and https, and not paths that merely look like one")
{
    const std::pair<const char *, bool> cases[] = {
        {"http://example.com/a.exr", true},
        {"https://example.com/a.exr", true},
        {"HTTPS://EXAMPLE.COM/A.EXR", true},
        {"", false},
        {"a.exr", false},
        {"/tmp/http/a.exr", false},
        {"C:\\images\\https.exr", false},
        {"ftp://example.com/a.exr", false},
        // one slash short of a scheme, and shorter than any scheme: neither may read past the end
        {"https:/example.com/a.exr", false},
        {"http", false},
    };

    for (const auto &c : cases)
    {
        CAPTURE(c.first);
        CHECK(is_url(c.first) == c.second);
    }
}
