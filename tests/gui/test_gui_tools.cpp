/** \file test_gui_tools.cpp
    \author Wojciech Jarosz

    Mouse-mode actions, driven from the Tools menu and the floating tool palette: one of
    Pan/Rectangular-select/Pixel-inspector is active at a time, and the palette stays a compact box inside
    the image viewport, snaps to the corner it is dropped nearest, and can be collapsed and hidden.
*/

#include "app.h"
#include "test_gui_registry.h"

#include "imgui_internal.h"
#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <cstring>
#include <string>

using std::string;

static bool is_selected(const char *name) { return *hdrview()->action(name).p_selected; }

static void assert_only(const char *active_name)
{
    for (int i = 0; i < MouseMode_COUNT; ++i)
        IM_CHECK_EQ(is_selected(mouse_mode_action_name(i)), strcmp(mouse_mode_action_name(i), active_name) == 0);
}

#define PALETTE "//##ToolPalette"

static const char *palette_window_ref = PALETTE;

//! Test-engine ref of one of the palette's tool buttons, whose label ImGui::IconButton() builds as
//! "<icon>##<action name>". A literal '/' in the name has to be escaped, or the engine reads it as a
//! path separator.
static string palette_tool_ref(const char *action_name)
{
    string escaped = action_name;
    for (size_t i = escaped.find('/'); i != string::npos; i = escaped.find('/', i + 2)) escaped.insert(i, 1, '\\');
    return PALETTE "/" + hdrview()->action(action_name).icon + "##" + escaped;
}

//! True when the palette is expanded; collapsed, ImGui submits its title bar and nothing else.
static bool palette_expanded(ImGuiTestContext *ctx)
{
    return ctx->ItemExists(palette_tool_ref("Rectangular select").c_str());
}

//! Puts the palette in a known state: its visibility and collapsed state come from the user's settings.
static void show_and_expand_palette(ImGuiTestContext *ctx)
{
    ctx->SetRef("##MainMenuBar");
    if (!*hdrview()->action("Show tool palette").p_selected)
        ctx->MenuClick("Windows/Show tool palette");

    ctx->SetRef("");
    ctx->WindowCollapse(palette_window_ref, false);
    ctx->Yield(4);
    IM_CHECK(palette_expanded(ctx));
}

//! A point on the palette's title bar clear of its collapse arrow. GetWindowTitlebarPoint() returns the
//! bar's center, which on a palette only as wide as its buttons is the arrow itself.
static float2 palette_grab_point(ImGuiTestContext *ctx)
{
    ImGuiWindow *w = ctx->WindowInfo(palette_window_ref).Window;
    IM_CHECK_SILENT_RETV(w != nullptr, float2{0.f});

    const float arrow_right = ImGui::GetStyle().FramePadding.x + ImGui::GetFontSize();
    return float2{w->Pos} + float2{0.5f * (arrow_right + w->Size.x), 0.5f * w->TitleBarHeight};
}

//! Drag the palette by its title bar to \p target, where the snapping logic then takes over.
//! Not ImGuiTestContext::WindowMove(), which force-expands the window and skips the drag when the target
//! is where the window already sits.
static void drag_palette_to(ImGuiTestContext *ctx, float2 target)
{
    ctx->MouseMoveToPos(palette_grab_point(ctx));
    ctx->MouseDown(ImGuiMouseButton_Left);
    ctx->MouseMoveToPos(target);
    ctx->MouseUp(ImGuiMouseButton_Left);
    ctx->Yield(3);
}

void RegisterTests_Tools(ImGuiTestEngine *engine)
{
    ImGuiTest *t = IM_REGISTER_TEST(engine, "tools", "mouse_mode_mutually_exclusive");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        ctx->SetRef("##MainMenuBar");
        assert_only("Pan and zoom"); // default at startup

        ctx->MenuClick("Tools/Rectangular select");
        assert_only("Rectangular select");

        // the action's name contains a literal '/' ("Pixel/color inspector"), which MenuClick would parse
        // as a submenu separator: escape it, as Dear ImGui's demo does for "Tools/Metrics\\/Debugger"
        ctx->MenuClick("Tools/Pixel\\/color inspector");
        assert_only("Pixel/color inspector");

        ctx->MenuClick("Tools/Pan and zoom"); // restore the default the other tests expect
        assert_only("Pan and zoom");
    };

    t           = IM_REGISTER_TEST(engine, "tools", "palette_selects_mode");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        show_and_expand_palette(ctx);

        for (int i = 0; i < MouseMode_COUNT; ++i)
        {
            ctx->ItemClick(palette_tool_ref(mouse_mode_action_name(i)).c_str());
            assert_only(mouse_mode_action_name(i));
            IM_CHECK_EQ(hdrview()->mouse_mode(), i);
        }

        // the palette and the menu are two views of the same actions
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("Tools/Rectangular select");
        ctx->SetRef("");
        IM_CHECK(ctx->ItemExists(palette_tool_ref("Rectangular select").c_str()));
        assert_only("Rectangular select");

        ctx->ItemClick(palette_tool_ref("Pan and zoom").c_str());
        assert_only("Pan and zoom");
    };

    t           = IM_REGISTER_TEST(engine, "tools", "palette_is_compact_and_snaps_to_corners");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        show_and_expand_palette(ctx);

        const float2 vp_min  = hdrview()->app_pos_at_vp_pos(float2{0.f});
        const float2 vp_size = hdrview()->viewport_size();

        ImGuiWindow *w = ctx->WindowInfo(palette_window_ref).Window;
        IM_CHECK(w != nullptr);

        // the buttons sit centered in the window's padding; ImGui truncates the layout cursor to whole
        // pixels, so fractional padding or spacing shows up as a bottom edge tighter than the top one
        {
            ImGuiTestItemInfo first = ctx->ItemInfo(palette_tool_ref(mouse_mode_action_name(0)).c_str());
            ImGuiTestItemInfo last =
                ctx->ItemInfo(palette_tool_ref(mouse_mode_action_name(MouseMode_COUNT - 1)).c_str());
            IM_CHECK_EQ(first.RectFull.Min.y - (w->Pos.y + w->TitleBarHeight),
                        (w->Pos.y + w->Size.y) - last.RectFull.Max.y);
            IM_CHECK_EQ(first.RectFull.Min.x - w->Pos.x, (w->Pos.x + w->Size.x) - first.RectFull.Max.x);
        }

        // the palette hugs its buttons; it is not an edge toolbar spanning the image view
        IM_CHECK_LT(w->Size.x, 0.25f * vp_size.x);
        IM_CHECK_LT(w->Size.y, 0.5f * vp_size.y);
        // a vertical palette is one button wide plus padding, and the title bar must not widen it
        IM_CHECK_LT(w->Size.x, 2.f * ImGui::IconButtonSize().x);

        // dropped anywhere, it snaps to the nearest corner of the central node and ends up inside it
        for (int corner = 0; corner < 4; ++corner)
        {
            float2 frac{(corner & 1) ? 0.8f : 0.2f, (corner & 2) ? 0.8f : 0.2f};
            drag_palette_to(ctx, vp_min + frac * vp_size);

            w = ctx->WindowInfo(palette_window_ref).Window;
            IM_CHECK(w != nullptr);
            float2 rel = float2{w->Pos} - vp_min;
            IM_CHECK_GE(rel.x, 0.f);
            IM_CHECK_GE(rel.y, 0.f);
            IM_CHECK_LE(rel.x + w->Size.x, vp_size.x);
            IM_CHECK_LE(rel.y + w->Size.y, vp_size.y);
            IM_CHECK_EQ(rel.x + 0.5f * w->Size.x > 0.5f * vp_size.x, (corner & 1) != 0);
            IM_CHECK_EQ(rel.y + 0.5f * w->Size.y > 0.5f * vp_size.y, (corner & 2) != 0);
        }

        drag_palette_to(ctx, vp_min + 0.2f * vp_size); // back to the top-left the other tests expect
    };

    t           = IM_REGISTER_TEST(engine, "tools", "palette_collapse_and_hide");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        show_and_expand_palette(ctx);

        // the collapse arrow hides the buttons and leaves the bar; the engine hands back item info gathered
        // a couple of frames ago, so let the vanished items age out before asserting
        ctx->WindowCollapse(palette_window_ref, true);
        ctx->Yield(4);
        IM_CHECK(!palette_expanded(ctx));
        IM_CHECK(*hdrview()->action("Show tool palette").p_selected); // collapsed, not closed

        ctx->WindowCollapse(palette_window_ref, false);
        ctx->Yield(4);
        IM_CHECK(palette_expanded(ctx));

        // there is no close button: the Windows menu hides the palette and brings it back
        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("Windows/Show tool palette");
        ctx->SetRef("");
        ctx->Yield(4);
        IM_CHECK(!*hdrview()->action("Show tool palette").p_selected);
        IM_CHECK(ctx->WindowInfo(palette_window_ref, ImGuiTestOpFlags_NoError).Window == nullptr);

        ctx->SetRef("##MainMenuBar");
        ctx->MenuClick("Windows/Show tool palette");
        ctx->SetRef("");
        ctx->Yield(4);
        IM_CHECK(palette_expanded(ctx));
    };
}
