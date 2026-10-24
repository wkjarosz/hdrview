//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "common.h"
#include "imgui_ext.h"

#include <imgui.h>

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

        // whatever every name begins or ends with is elided, once it is long enough to be worth an ellipsis
        size_t common = 0, common_end = 0;
        while (!names.empty() && std::all_of(names.begin(), names.end(), [&](const string &n)
                                             { return n.size() > common && n[common] == names[0][common]; }))
            ++common;
        while (!names.empty() && std::all_of(names.begin(), names.end(),
                                             [&](const string &n)
                                             {
                                                 return n.size() > common_end &&
                                                        n[n.size() - 1 - common_end] ==
                                                            names[0][names[0].size() - 1 - common_end];
                                             }))
            ++common_end;

        for (size_t i = 0; i < names.size(); ++i)
        {
            CAPTURE(names[i]);
            if (common > 8)
                CHECK(shortened[i].compare(0, common, names[i], 0, common) != 0);

            // A name that is all shared prefix or suffix has nothing unique left, and falls back to its own
            // file name, so only an empty input can shorten to nothing.
            if (names[i].size() > 4 && (names[i].size() == common || names[i].size() == common_end))
                CHECK(shortened[i] == get_filename(names[i]));
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

namespace
{

/// A headless ImGui frame with the default font, enough to lay text out without a window or a renderer.
struct HeadlessFrame
{
    ImGuiContext *context = ImGui::CreateContext();

    HeadlessFrame()
    {
        ImGuiIO &io    = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1024.f, 768.f);
        // the atlas then builds its glyphs on demand instead of waiting for a renderer to upload them
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
        ImGui::SetNextWindowSize(ImVec2(600.f, 100.f));
        ImGui::Begin("TruncatedText", nullptr, ImGuiWindowFlags_NoDecoration);
    }
    ~HeadlessFrame()
    {
        ImGui::End();
        ImGui::EndFrame();
        ImGui::DestroyContext(context);
    }
};

bool is_continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

size_t codepoints(const string &s)
{
    return size_t(std::count_if(s.begin(), s.end(), [](char c) { return !is_continuation(c); }));
}

bool starts_with(const string &s, const string &prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

bool ends_with(const string &s, const string &x) { return s.size() >= x.size() && s.substr(s.size() - x.size()) == x; }

} // namespace

TEST_CASE("TruncatedText fits a name beside its icon, cutting only the end it was asked to")
{
    HeadlessFrame frame;

    const string         icon      = "[]";
    const vector<string> names     = {"", "a", "render_0001.exr", "/some/long/dir/render_0001.exr:part.layer.(R,G,B)",
                                      "/dir/\xc3\xa9t\xc3\xa9/\xc3\xa9t\xc3\xa9_0001.exr"};
    const float          start     = ImGui::GetCursorPosX();
    const float          max_avail = ImGui::GetContentRegionAvail().x;

    // every width from nothing to more than any name needs, narrowing the line by moving the cursor along it
    for (float offset = max_avail; offset >= 0.f; offset -= 1.f)
    {
        ImGui::SetCursorPosX(start + offset);
        const float avail = ImGui::GetContentRegionAvail().x;
        auto        width = [&](const string &s) { return ImGui::CalcTextSize((icon + s).c_str()).x; };

        for (bool keep_tail : {true, false})
            for (const string suffix : {"", " *"})
                for (const auto &text : names)
                {
                    CAPTURE(avail);
                    CAPTURE(keep_tail);
                    CAPTURE(suffix);
                    CAPTURE(text);
                    const string lead   = keep_tail ? "" : " ";
                    const string result = ImGui::TruncatedText(text, icon, keep_tail, suffix);

                    REQUIRE(ends_with(result, suffix));
                    REQUIRE(starts_with(result, lead));
                    const string body = result.substr(lead.size(), result.size() - lead.size() - suffix.size());

                    // left whole exactly when it fits, or is too short to cut at all
                    const bool fits = width(lead + text + suffix) <= avail;
                    if (body == text)
                    {
                        CHECK((fits || codepoints(text) <= 1));
                        continue;
                    }
                    CHECK(!fits);

                    // only the far end is cut, at a character boundary, and an ellipsis marks where
                    string kept, wider;
                    if (keep_tail)
                    {
                        REQUIRE(starts_with(body, " ..."));
                        kept = body.substr(4);
                        REQUIRE(ends_with(text, kept));
                        CHECK(!is_continuation(text[text.size() - kept.size()]));
                        size_t from = text.size() - kept.size() - 1;
                        while (from > 0 && is_continuation(text[from])) --from;
                        wider = " ..." + text.substr(from) + suffix;
                    }
                    else
                    {
                        REQUIRE(ends_with(body, "..."));
                        kept = body.substr(0, body.size() - 3);
                        REQUIRE(starts_with(text, kept));
                        CHECK(!is_continuation(text[kept.size()]));
                        size_t to = kept.size() + 1;
                        while (to < text.size() && is_continuation(text[to])) ++to;
                        wider = lead + text.substr(0, to) + "..." + suffix;
                    }
                    CHECK(!kept.empty());

                    // as much as fits: one character more would not, unless one is all there is room for
                    CHECK((width(result) <= avail || codepoints(kept) == 1));
                    CHECK(width(wider) > avail);
                }
    }
}
