//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// The sections of the app's settings that read and write themselves as JSON: the highlight rolloff's, which
// session files store too, and the chromaticity diagram's legend.

#include <doctest/doctest.h>

#include "app.h"
#include "chromaticity.h"

#include <array>
#include <utility>

using Settings = HDRViewApp::RolloffSettings;

TEST_CASE("Each settings section survives JSON, and bad values cannot get in")
{
    // the highlight rolloff's
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

    // the chromaticity diagram's legend
    {
        using Legend = ChromaticityLegend;
        // the keys saved settings files hold, each flag's own
        const std::array<std::pair<const char *, bool Legend::*>, 7> flags = {
            {{"pixel density", &Legend::density},
             {"image gamut", &Legend::image_gamut},
             {"display gamut", &Legend::display_gamut},
             {"wavelengths", &Legend::wavelengths},
             {"daylight locus", &Legend::daylight},
             {"hovered pixel", &Legend::hovered},
             {"legend collapsed", &Legend::collapsed}}};
        const Legend defaults;
        CHECK(defaults.to_json().size() == flags.size() + 1); // the flags and the opacity
        CHECK(defaults.to_json()["pixel density opacity"] == defaults.density_opacity);

        // each flag away from its default alone, under its own key, and back in place
        for (size_t i = 0; i < flags.size(); ++i)
        {
            auto [key, flag] = flags[i];
            CAPTURE(key);
            Legend changed;
            changed.*flag = !(changed.*flag);
            CHECK(changed.to_json()[key] == changed.*flag);
            Legend back;
            back.from_json(changed.to_json());
            for (auto [other, f] : flags) CHECK(back.*f == changed.*f);
            CHECK(back.density_opacity == defaults.density_opacity);
        }
        Legend dimmer;
        dimmer.density_opacity = 0.25f;
        Legend back;
        back.from_json(dimmer.to_json());
        CHECK(back.density_opacity == 0.25f);

        // values of the wrong type leave the settings as they were, and the opacity is clamped
        Legend odd;
        odd.from_json({{"pixel density", "no"}, {"legend collapsed", 1}, {"pixel density opacity", 3.f}});
        CHECK(odd.density == defaults.density);
        CHECK(odd.collapsed == defaults.collapsed);
        CHECK(odd.density_opacity == 1.f);
        odd.from_json({{"pixel density opacity", "half"}});
        CHECK(odd.density_opacity == 1.f);

        // and anything that is not an object changes nothing
        Legend same;
        same.from_json(json::array());
        CHECK(same.to_json() == defaults.to_json());
    }
}
