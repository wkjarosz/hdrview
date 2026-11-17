//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// The highlight rolloff's settings as app settings and session files store them.

#include <doctest/doctest.h>

#include "app.h"

using Settings = HDRViewApp::RolloffSettings;

TEST_CASE("Highlight rolloff settings survive JSON, and bad values cannot get in")
{
    // every field away from its default, so a field written or read under the wrong key is caught
    Settings s;
    s.enabled               = true;
    s.preset                = HDRViewApp::RolloffPreset_Custom;
    s.custom.ceiling        = HDRViewApp::RolloffCeiling_Custom;
    s.custom.custom_ceiling = 1.25f;
    s.custom.peak_source    = HDRViewApp::PeakSource_Measured;
    s.custom.custom_peak    = 5.5f;
    s.custom.preserve_hue   = 0.3f;

    Settings back;
    back.from_json(s.to_json());
    CHECK(back.to_json() == s.to_json());
    CHECK(back.enabled);
    CHECK(back.preset == s.preset);
    CHECK(back.custom.ceiling == s.custom.ceiling);
    CHECK(back.custom.custom_ceiling == s.custom.custom_ceiling);
    CHECK(back.custom.peak_source == s.custom.peak_source);
    CHECK(back.custom.custom_peak == s.custom.custom_peak);
    CHECK(back.custom.preserve_hue == s.custom.preserve_hue);

    // unknown ids, wrong types and out-of-range numbers leave or clamp the values rather than take them
    Settings odd = s;
    odd.from_json({{"enabled", "yes"},
                   {"preset", "brightest"},
                   {"custom",
                    {{"ceiling", 2},
                     {"custom_ceiling", -3.f},
                     {"peak_source", "none"},
                     {"custom_peak", "high"},
                     {"preserve_hue", 4.f}}}});
    CHECK(odd.enabled == s.enabled);
    CHECK(odd.preset == s.preset);
    CHECK(odd.custom.ceiling == s.custom.ceiling);
    CHECK(odd.custom.custom_ceiling == 0.f);
    CHECK(odd.custom.peak_source == s.custom.peak_source);
    CHECK(odd.custom.custom_peak == s.custom.custom_peak);
    CHECK(odd.custom.preserve_hue == 1.f);

    // and anything that is not an object changes nothing
    Settings same = s;
    same.from_json(json::array());
    CHECK(same.to_json() == s.to_json());
}
