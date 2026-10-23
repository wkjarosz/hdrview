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

TEST_CASE("line helpers see the lines of a string_view, and nothing past its end")
{
    // Every length of view into one buffer: none of them is null-terminated where it ends, so a helper
    // reading through data() instead of the view's length would see the rest of the buffer.
    const string backing = "first\nsecond\nthird";
    for (size_t length = 0; length <= backing.size(); ++length)
    {
        const string_view view(backing.data(), length);
        CAPTURE(view);

        vector<string> lines;
        process_lines(view, [&lines](string_view l) { lines.emplace_back(l); });

        // the lines visited are the view's own, in order
        string joined, indented;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            joined += (i ? "\n" : "") + lines[i];
            indented += (i ? "\n  " : "  ") + lines[i];
        }
        const bool ends_in_newline = !view.empty() && view.back() == '\n';
        CHECK(joined + (ends_in_newline ? "\n" : "") == view);

        // indent adds its prefix to each of those lines and to nothing else
        CHECK(indent(view, true, 2) == indented + (ends_in_newline ? "\n" : ""));

        // add_line_numbers numbers each of them, once
        const string   numbered = add_line_numbers(view);
        vector<string> numbered_lines;
        process_lines(numbered, [&numbered_lines](string_view l) { numbered_lines.emplace_back(l); });
        REQUIRE(numbered_lines.size() == lines.size());
        for (size_t i = 0; i < lines.size(); ++i)
        {
            CAPTURE(i);
            CHECK(numbered_lines[i] == std::to_string(i + 1) + ": " + lines[i]);
        }
    }
}
