//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "common.h"

#include <string>
#include <string_view>
#include <vector>

using std::string;
using std::string_view;
using std::vector;

// A string_view carries its own length and need not be null-terminated. These three helpers take one, so a
// view over the front of a larger buffer has to stop at the view's end -- reading through data() instead
// runs on to whatever follows, which for a view into a std::string is the rest of that string.
TEST_CASE("line helpers respect the length of a non-terminated string_view")
{
    // Everything from the newline on is outside the view, and must not be seen.
    const string      backing = "first\nsecond\nthird";
    const string_view view(backing.data(), 5); // exactly "first"
    REQUIRE(view == "first");

    SUBCASE("process_lines visits only the lines inside the view")
    {
        vector<string> lines;
        process_lines(view, [&lines](string_view l) { lines.emplace_back(l); });
        REQUIRE(lines.size() == 1);
        CHECK(lines[0] == "first");
    }

    SUBCASE("add_line_numbers numbers only the lines inside the view")
    {
        const auto numbered = add_line_numbers(view);
        CHECK(numbered.find("second") == string::npos);
        CHECK(numbered.find("third") == string::npos);
        CHECK(numbered.find("first") != string::npos);
    }

    SUBCASE("indent indents only the lines inside the view")
    {
        const auto indented = indent(view, true, 2);
        CHECK(indented == "  first");
    }
}

// The ordinary null-terminated case has to keep working unchanged.
TEST_CASE("line helpers still handle multi-line input")
{
    const string input = "alpha\nbeta";

    vector<string> lines;
    process_lines(input, [&lines](string_view l) { lines.emplace_back(l); });
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "alpha");
    CHECK(lines[1] == "beta");

    CHECK(indent(input, true, 2) == "  alpha\n  beta");
}
