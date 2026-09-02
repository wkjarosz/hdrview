//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "common.h"

// doctest only forward-declares std::basic_ostream; MSVC's operator<<(ostream&, string_view) needs the
// complete type
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

using std::string;
using std::string_view;
using std::vector;

TEST_CASE("line helpers respect the length of a non-terminated string_view")
{
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
