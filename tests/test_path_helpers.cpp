//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "common.h"

TEST_CASE("is_url recognizes http and https")
{
    CHECK(is_url("http://example.com/a.exr"));
    CHECK(is_url("https://example.com/a.exr"));
    CHECK(is_url("HTTPS://EXAMPLE.COM/A.EXR"));
}

TEST_CASE("is_url rejects paths that merely look like one")
{
    CHECK_FALSE(is_url(""));
    CHECK_FALSE(is_url("a.exr"));
    CHECK_FALSE(is_url("/tmp/http/a.exr"));
    CHECK_FALSE(is_url("C:\\images\\https.exr"));
    CHECK_FALSE(is_url("ftp://example.com/a.exr"));
    // one slash short of a scheme, and shorter than any scheme: neither may read past the end
    CHECK_FALSE(is_url("https:/example.com/a.exr"));
    CHECK_FALSE(is_url("http"));
}
