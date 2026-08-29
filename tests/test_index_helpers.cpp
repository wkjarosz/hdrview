//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "common.h"

#include <initializer_list>
#include <algorithm>
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
    // -1 is the "nothing selected" index (m_current with no image, or selected_group/reference_group after
    // update_visibility() hides every group). It is not a position to step from, and adding a step to it in
    // unsigned arithmetic would give a remainder of 2^64 % size rather than of the index, so the search
    // starts at the near end instead.
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
        // A session file can name a group index the image doesn't have.
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
    // The hand-picked cases above name the behaviors worth reading; this pins them over the whole
    // small-input space, where the interesting starting indices are the ones outside the vector -- -1
    // for "nothing selected", and anything a stale session file might name.
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
                        // Nothing to move to, so the caller's own index comes back untouched -- never an
                        // index into a vector that has no match to offer.
                        CHECK(next == start);
                        continue;
                    }

                    // Any result is a real position holding a match.
                    REQUIRE(next >= 0);
                    REQUIRE(next < size);
                    CHECK(v[next]);

                    // Stepping repeatedly visits every match and nothing else, so no match is
                    // unreachable and none is visited twice per cycle.
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

            // One past the last match too, which has to report past-the-end rather than an index.
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
