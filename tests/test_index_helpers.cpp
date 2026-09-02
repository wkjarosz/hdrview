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
#include <initializer_list>
#include <numeric>
#include <vector>

TEST_CASE("next_matching_index steps through a vector in both directions")
{
    std::vector<int> v{0, 1, 2, 3};
    auto             all = [](size_t, const int &) { return true; };

    CHECK(next_matching_index(v, 0, all, Direction_Forward) == 1);
    CHECK(next_matching_index(v, 3, all, Direction_Forward) == 0); // wraps
    CHECK(next_matching_index(v, 3, all, Direction_Backward) == 2);
    CHECK(next_matching_index(v, 0, all, Direction_Backward) == 3); // wraps
}

TEST_CASE("next_matching_index starts at the near end when nothing is selected")
{
    // -1 is the "nothing selected" index (m_current with no image, or selected_group/reference_group once
    // update_visibility() has hidden every group), not a position to step from
    auto all = [](size_t, const int &) { return true; };

    for (int size = 1; size <= 8; ++size)
    {
        CAPTURE(size);
        std::vector<int> v(size);
        std::iota(v.begin(), v.end(), 0);

        CHECK(next_matching_index(v, -1, all, Direction_Forward) == 0);
        CHECK(next_matching_index(v, -1, all, Direction_Backward) == size - 1);
    }
}

TEST_CASE("next_matching_index skips elements that don't match")
{
    std::vector<bool> visible{true, false, false, true, false};
    auto              is_visible = [](size_t, const bool &b) { return b; };

    CHECK(next_matching_index(visible, 0, is_visible, Direction_Forward) == 3);
    CHECK(next_matching_index(visible, 3, is_visible, Direction_Forward) == 0);
    CHECK(next_matching_index(visible, 3, is_visible, Direction_Backward) == 0);
    CHECK(next_matching_index(visible, 0, is_visible, Direction_Backward) == 3);

    SUBCASE("nothing matches at all")
    {
        std::vector<bool> none{false, false, false};
        auto              never = [](size_t, const bool &b) { return b; };
        CHECK(next_matching_index(none, 1, never, Direction_Forward) == 1); // stays put
        CHECK(next_matching_index(none, -1, never, Direction_Backward) == -1);
    }

    SUBCASE("an empty vector returns the index it was given")
    {
        std::vector<bool> empty;
        auto              never = [](size_t, const bool &b) { return b; };
        CHECK(next_matching_index(empty, -1, never, Direction_Forward) == -1);
        CHECK(next_matching_index(empty, 7, never, Direction_Backward) == 7);
    }

    SUBCASE("an out-of-range index still lands on a match")
    {
        // a session file can name a group index the image doesn't have
        std::vector<bool> v{false, true, false};
        auto              is_set = [](size_t, const bool &b) { return b; };
        CHECK(next_matching_index(v, 9999, is_set, Direction_Forward) == 1);
        CHECK(next_matching_index(v, 9999, is_set, Direction_Backward) == 1);
    }
}

TEST_CASE("nth_matching_index finds the nth match or reports past-the-end")
{
    std::vector<bool> visible{false, true, false, true, true};
    auto              is_visible = [](size_t, const bool &b) { return b; };

    CHECK(nth_matching_index(visible, 0, is_visible) == 1);
    CHECK(nth_matching_index(visible, 1, is_visible) == 3);
    CHECK(nth_matching_index(visible, 2, is_visible) == 4);
    CHECK(nth_matching_index(visible, 3, is_visible) == visible.size()); // no 4th match
}

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

    SUBCASE("the endpoints are exact")
    {
        CHECK(download_percent_remaining(0, 1000) == 100);
        CHECK(download_percent_remaining(1000, 1000) == 0);
        // over-reporting more bytes than the declared total still reads as finished, not negative
        CHECK(download_percent_remaining(1500, 1000) == 0);
    }

    SUBCASE("a partial transfer is neither 0 nor 100")
    {
        // a plain integer division would send all of these to zero
        CHECK(download_percent_remaining(500, 1000) == 50);
        CHECK(download_percent_remaining(250, 1000) == 75);
        CHECK(download_percent_remaining(999, 1000) == 1);
        // rounded up, so a nearly-finished transfer still shows
        CHECK(download_percent_remaining(999999, 1000000) == 1);
    }

    SUBCASE("the invariants hold across every small total and byte count")
    {
        for (int64_t total = 1; total <= 64; ++total)
        {
            int64_t previous = 101;
            for (int64_t loaded = 0; loaded <= total; ++loaded)
            {
                const int r = download_percent_remaining(loaded, total);
                INFO("loaded = ", loaded, " of ", total, " -> ", r);
                CHECK(r >= 0);
                CHECK(r <= 100);
                CHECK(r <= previous); // never goes backwards as bytes arrive
                CHECK((r == 0) == (loaded == total));
                previous = r;
            }
        }
    }

    SUBCASE("a transfer larger than 32 bits does not overflow the percentage")
    {
        const int64_t huge = 8ll << 30;
        CHECK(download_percent_remaining(0, huge) == 100);
        CHECK(download_percent_remaining(huge / 2, huge) == 50);
        CHECK(download_percent_remaining(huge, huge) == 0);
    }
}
