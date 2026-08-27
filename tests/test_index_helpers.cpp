//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "common.h"

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
    // update_visibility() hides every group). Adding the backward step to it in size_t arithmetic wrapped to
    // near 2^64, so the starting index came out as 2^64 % size -- an arbitrary element that happened to be
    // right for some sizes and wrong for others.
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
