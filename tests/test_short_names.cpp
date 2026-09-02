//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "common.h"

#include <string>
#include <vector>

using std::string;
using std::vector;

TEST_CASE("shorten_names returns one name per input")
{
    vector<string> names{"/dir/render_0001.exr", "/dir/render_0002.exr", "/dir/render_0003.exr"};
    auto           shortened = shorten_names(names);
    REQUIRE(shortened.size() == names.size());
    for (const auto &s : shortened) CHECK_FALSE(s.empty());
}

TEST_CASE("shorten_names trims the shared prefix and suffix")
{
    vector<string> names{"/some/long/dir/render_0001.exr", "/some/long/dir/render_0002.exr"};
    auto           shortened = shorten_names(names);
    REQUIRE(shortened.size() == 2);

    // The unique part is the frame number; the directory and extension are shared and elided.
    CHECK(shortened[0].find("0001") != string::npos);
    CHECK(shortened[1].find("0002") != string::npos);
    CHECK(shortened[0].find("/some/long/dir/") == string::npos);
    CHECK(shortened[0] != shortened[1]);
}

TEST_CASE("shorten_names handles a name that is entirely the common suffix")
{
    // "a.exr" is entirely shared, leaving an empty range whose preceding byte is at unsigned index -1
    vector<string> names{"a.exr", "xa.exr"};
    auto           shortened = shorten_names(names);
    REQUIRE(shortened.size() == 2);

    // with nothing unique left, a name falls back to its own file name
    CHECK(shortened[0] == "a.exr");
    CHECK_FALSE(shortened[1].empty());
    CHECK(shortened[1].find("x") != string::npos);
}

TEST_CASE("shorten_names handles identical names")
{
    // Loading the same file twice: every character is both a common prefix and a common suffix.
    vector<string> names{"/dir/image.exr", "/dir/image.exr"};
    auto           shortened = shorten_names(names);
    REQUIRE(shortened.size() == 2);
    CHECK(shortened[0] == "image.exr");
    CHECK(shortened[1] == "image.exr");
}

TEST_CASE("shorten_names handles degenerate inputs")
{
    CHECK(shorten_names({}).empty());

    SUBCASE("a single name")
    {
        auto shortened = shorten_names({"/dir/only.exr"});
        REQUIRE(shortened.size() == 1);
        CHECK_FALSE(shortened[0].empty());
    }

    SUBCASE("empty strings")
    {
        auto shortened = shorten_names({"", ""});
        REQUIRE(shortened.size() == 2);
    }

    SUBCASE("one empty string among real names")
    {
        auto shortened = shorten_names({"", "/dir/image.exr"});
        REQUIRE(shortened.size() == 2);
    }

    SUBCASE("names that share everything but their length")
    {
        auto shortened = shorten_names({"aaaaaaaa", "aaaaaaaaaaaa"});
        REQUIRE(shortened.size() == 2);
    }
}

TEST_CASE("shorten_names accepts non-ASCII names")
{
    // isalnum() is undefined for a negative char, which is what every continuation byte of a multi-byte
    // UTF-8 character is.
    vector<string> names{"/dir/\xc3\xa9t\xc3\xa9_0001.exr", "/dir/\xc3\xa9t\xc3\xa9_0002.exr"};
    auto           shortened = shorten_names(names);
    REQUIRE(shortened.size() == 2);
    CHECK(shortened[0] != shortened[1]);
}
