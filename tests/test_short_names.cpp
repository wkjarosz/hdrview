//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "common.h"

#include <algorithm>
#include <string>
#include <vector>

using std::string;
using std::vector;

TEST_CASE("shorten_names keeps a collection distinguishable, whatever its names share")
{
    const vector<vector<string>> cases = {
        {},
        {"/dir/only.exr"},
        {"/dir/render_0001.exr", "/dir/render_0002.exr", "/dir/render_0003.exr"},
        {"/some/long/dir/render_0001.exr", "/some/long/dir/render_0002.exr"},
        {"a.exr", "xa.exr"},                  // one name is entirely the other's common suffix
        {"/dir/image.exr", "/dir/image.exr"}, // the same file loaded twice: everything is shared
        {"", ""},
        {"", "/dir/image.exr"},
        {"aaaaaaaa", "aaaaaaaaaaaa"}, // sharing everything but their length
        // isalnum() is undefined for a negative char, which every UTF-8 continuation byte is
        {"/dir/\xc3\xa9t\xc3\xa9_0001.exr", "/dir/\xc3\xa9t\xc3\xa9_0002.exr"},
    };

    for (const auto &names : cases)
    {
        auto shortened = shorten_names(names);
        REQUIRE(shortened.size() == names.size());

        // whatever every name begins with is elided, once it is long enough to be worth an ellipsis
        size_t common = 0;
        while (!names.empty() && std::all_of(names.begin(), names.end(), [&](const string &n)
                                             { return n.size() > common && n[common] == names[0][common]; }))
            ++common;

        for (size_t i = 0; i < names.size(); ++i)
        {
            CAPTURE(names[i]);
            if (common > 8)
                CHECK(shortened[i].compare(0, common, names[i], 0, common) != 0);

            // a name with nothing unique left falls back to its own file name, so only an empty input
            // can shorten to nothing
            CHECK(shortened[i].empty() == names[i].empty());

            for (size_t j = 0; j < i; ++j)
            {
                CAPTURE(names[j]);
                if (names[i] != names[j])
                    CHECK(shortened[i] != shortened[j]);
            }
        }
    }
}

TEST_CASE("shorten_names trims what every name shares")
{
    auto shortened = shorten_names({"/some/long/dir/render_0001.exr", "/some/long/dir/render_0002.exr"});
    REQUIRE(shortened.size() == 2);

    // the frame number is the unique part; the directory and extension are shared and elided
    CHECK(shortened[0].find("0001") != string::npos);
    CHECK(shortened[1].find("0002") != string::npos);
    CHECK(shortened[0].find("/some/long/dir/") == string::npos);

    SUBCASE("and falls back to a file name when that leaves nothing")
    {
        CHECK(shorten_names({"a.exr", "xa.exr"})[0] == "a.exr");

        auto identical = shorten_names({"/dir/image.exr", "/dir/image.exr"});
        CHECK(identical[0] == "image.exr");
        CHECK(identical[1] == "image.exr");
    }
}
