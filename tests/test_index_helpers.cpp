//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

/** \file test_index_helpers.cpp
    \author Wojciech Jarosz

    The index arithmetic that walks the image list, and the download-progress arithmetic the status bar reads.
*/

#include <doctest/doctest.h>

#include "common.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <numeric>
#include <vector>

TEST_CASE("next_matching_index holds its invariants over every small vector and starting index")
{
    // starting indices outside the vector are included: -1 for "nothing selected", and anything a stale
    // session file might name
    auto is_set = [](size_t, const bool &b) { return b; };

    for (int size = 0; size <= 6; ++size)
        for (int mask = 0; mask < (1 << size); ++mask)
        {
            std::vector<bool> v(size);
            int               num_matches = 0;
            for (int i = 0; i < size; ++i)
                if ((v[i] = (mask >> i) & 1))
                    ++num_matches;

            for (int start = -3; start <= size + 2; ++start)
                for (auto dir : {Direction_Forward, Direction_Backward})
                {
                    CAPTURE(size);
                    CAPTURE(mask);
                    CAPTURE(start);
                    CAPTURE(dir == Direction_Forward);

                    const int next = next_matching_index(v, start, is_set, dir);

                    if (num_matches == 0)
                    {
                        // nothing to move to, so the caller's own index comes back untouched
                        CHECK(next == start);
                        continue;
                    }

                    // any result is a real position holding a match
                    REQUIRE(next >= 0);
                    REQUIRE(next < size);
                    CHECK(v[next]);

                    // An index that is not a position in the vector is not a place to step from, so the
                    // walk begins at the near end instead, and that element is itself a candidate.
                    const int step = dir == Direction_Forward ? 1 : -1;
                    const int from = (start >= 0 && start < size) ? mod(start + step, size)
                                                                  : (dir == Direction_Forward ? 0 : size - 1);
                    // the first match from there in that direction, so nothing passed over may match
                    for (int i = from; i != next; i = mod(i + step, size)) CHECK_FALSE(v[i]);

                    // stepping repeatedly visits every match once per cycle and nothing else
                    std::vector<int> visited;
                    int              at = next;
                    for (int step = 0; step < num_matches; ++step)
                    {
                        visited.push_back(at);
                        at = next_matching_index(v, at, is_set, dir);
                    }
                    CHECK(at == next); // back where the walk started
                    std::sort(visited.begin(), visited.end());
                    CHECK(std::adjacent_find(visited.begin(), visited.end()) == visited.end());
                    CHECK((int)visited.size() == num_matches);
                }
        }
}

TEST_CASE("nth_matching_index agrees with a direct scan for every small vector")
{
    auto is_set = [](size_t, const bool &b) { return b; };

    for (int size = 0; size <= 6; ++size)
        for (int mask = 0; mask < (1 << size); ++mask)
        {
            std::vector<bool> v(size);
            std::vector<int>  matches;
            for (int i = 0; i < size; ++i)
                if ((v[i] = (mask >> i) & 1))
                    matches.push_back(i);

            // one past the last match too, which has to report past-the-end
            for (int n = 0; n <= (int)matches.size() + 1; ++n)
            {
                CAPTURE(size);
                CAPTURE(mask);
                CAPTURE(n);
                const int expected = n < (int)matches.size() ? matches[n] : (int)v.size();
                CHECK(nth_matching_index(v, n, is_set) == expected);
            }
        }
}

TEST_CASE("download_percent_remaining reports a usable percentage for any byte counts")
{
    // The status bar draws its bar only while this is above zero and fills it by (100 - remaining) / 100, so
    // the value has to stay inside [0, 100], fall as bytes arrive, and reach zero only when the transfer is.
    SUBCASE("a total the server has not reported yet cannot divide")
    {
        // Emscripten's progress callback can fire before the content length is known, and some servers
        // never send one
        CHECK(download_percent_remaining(0, 0) == 100);
        CHECK(download_percent_remaining(1234, 0) == 100);
        CHECK(download_percent_remaining(0, -1) == 100);
    }

    SUBCASE("it is within a point of the exact percentage, and zero only once every byte has arrived")
    {
        // Every byte count of every small total, including more bytes than were declared, which reads as
        // finished rather than negative; and a sample of large totals, where a nearly finished transfer
        // still shows and one past 32 bits must not overflow. Being within a point of a percentage keeps the
        // value inside [0, 100].
        std::vector<int64_t> totals;
        for (int64_t total = 1; total <= 64; ++total) totals.push_back(total);
        for (int64_t total : {int64_t(1000), int64_t(1000000), int64_t(8) << 30}) totals.push_back(total);

        for (int64_t total : totals)
        {
            std::vector<int64_t> counts;
            if (total <= 64)
                for (int64_t loaded = 0; loaded <= total + 2; ++loaded) counts.push_back(loaded);
            else
                counts = {0, 1, total / 4, total / 2, total - 1, total, total + total / 2};

            int previous = 101;
            for (int64_t loaded : counts)
            {
                const int    r     = download_percent_remaining(loaded, total);
                const double exact = 100.0 * double(std::max<int64_t>(0, total - loaded)) / double(total);
                INFO("loaded = ", loaded, " of ", total, " -> ", r);
                CHECK(std::abs(r - exact) < 1.0);
                CHECK(r <= previous); // never goes backwards as bytes arrive
                CHECK((r == 0) == (loaded >= total));
                previous = r;
            }
        }
    }
}
