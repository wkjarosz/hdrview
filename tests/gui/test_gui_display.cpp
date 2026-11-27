/** \file test_gui_display.cpp
    \author Wojciech Jarosz

    The display pipeline as the readouts reproduce it: HDRViewApp::pixel_value() and tonemap_value() run the
    image shader's own steps (src/shared/pipeline.h), which test_colorspace.cpp holds to their definitions, except
    for the colormap lookup, which reads Colormap::sample() where the shader reads a texture. These tests check that
    lookup against the colormaps' own colors, and that the readouts follow the viewer's controls as the screen does.
*/

#include "app.h"
#include "colormap.h"
#include "test_gui_registry.h"
#include "test_gui_support.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <cmath>

using namespace hdrview_test;

namespace
{

bool approx(float a, float b, float eps = 2e-3f) { return std::fabs(a - b) < eps; }
bool approx(float3 a, float3 b, float eps = 2e-3f) { return la::maxelem(la::abs(a - b)) < eps; }

/// Colormap::initialize()'s rule for which colormaps get a filtered texture; the qualitative ones are Nearest.
bool is_continuous(Colormap_ cmap) { return cmap > ImPlotColormap_Paired; }

/// Restores the tonemap controls the tests below drive directly, whatever the rest of the suite left set.
struct TonemapState
{
    Tonemap_ tonemap  = hdrview()->tonemap();
    float    gamma    = hdrview()->gamma_live();
    float    exposure = hdrview()->exposure_live();
    float    offset   = hdrview()->offset_live();
    bool     reverse  = hdrview()->reverse_colormap();

    ~TonemapState()
    {
        hdrview()->tonemap()          = tonemap;
        hdrview()->gamma_live()       = gamma;
        hdrview()->exposure_live()    = exposure;
        hdrview()->offset_live()      = offset;
        hdrview()->reverse_colormap() = reverse;
    }
};

} // namespace

void RegisterTests_Display(ImGuiTestEngine *engine)
{
    // At each texel's center a colormap's own color, halfway between two of a filtered one their mean, and past
    // either end that end, as the shader's ClampToEdge texture reads them; ImPlot quantizes each step into 255.
    ImGuiTest *t = IM_REGISTER_TEST(engine, "display", "colormap_sample_reads_the_colormaps_own_colors");
    t->TestFunc  = [](ImGuiTestContext *)
    {
        auto rgb = [](ImVec4 c) { return float3{c.x, c.y, c.z}; };
        for (Colormap_ cmap = 0; cmap < Colormap_COUNT; ++cmap)
        {
            const auto &values = Colormap::values(cmap);
            const int   n      = int(values.size());
            IM_CHECK_RETV(n > 1, );
            auto color = [&](int i) { return rgb(ImGui::ColorConvertU32ToFloat4(values[size_t(i)])); };
            for (int i = 0; i < n; ++i)
            {
                IM_CHECK(approx(rgb(Colormap::sample(cmap, float(i) / float(n - 1))), color(i), 2.f / 255.f));
                if (is_continuous(cmap) && i + 1 < n)
                    IM_CHECK(approx(rgb(Colormap::sample(cmap, (i + 0.5f) / float(n - 1))),
                                    0.5f * (color(i) + color(i + 1)), 3.f / 255.f));
            }
            IM_CHECK(approx(rgb(Colormap::sample(cmap, -0.5f)), color(0), 1.f / 255.f));
            IM_CHECK(approx(rgb(Colormap::sample(cmap, 1.5f)), color(n - 1), 1.f / 255.f));
        }
    };

    // tonemap_value() follows the live controls, as relations between its outputs: neutral settings change
    // nothing, a stop more exposure doubles, the offset scales with coverage, the output raised to gamma gives back
    // what went in, and a reversed colormap shows at a mean what the forward one shows at its mirror.
    t           = IM_REGISTER_TEST(engine, "display", "tonemap_value_follows_the_live_controls");
    t->TestFunc = [](ImGuiTestContext *)
    {
        TonemapState restore;
        auto        *app   = hdrview();
        auto         shown = [app](float4 v, float exposure, float offset, float gamma, Tonemap_ mode = Tonemap_Gamma,
                                   bool reverse = false)
        {
            app->tonemap()          = mode;
            app->exposure_live()    = exposure;
            app->offset_live()      = offset;
            app->gamma_live()       = gamma;
            app->reverse_colormap() = reverse;
            return app->tonemap_value(v);
        };
        const float4 v{0.9f, 0.2f, 0.05f, 0.7f};
        IM_CHECK(approx(shown(v, 0.f, 0.f, 1.f).xyz(), v.xyz()) && shown(v, 0.f, 0.f, 1.f).w == v.w);
        IM_CHECK(approx(shown(v, 1.f, 0.f, 1.f).xyz(), 2.f * v.xyz()));
        IM_CHECK(approx(shown(v, 0.f, 0.25f, 1.f).xyz(), v.xyz() + float3{0.25f * v.w}));
        IM_CHECK(approx(shown(float4{0.f}, 0.f, 0.25f, 1.f).xyz(), float3{0.f}));
        for (int c = 0; c < 3; ++c) IM_CHECK(approx(std::pow(shown(v, 0.f, 0.f, 2.2f)[c], 2.2f), v[c]));
        IM_CHECK(approx(shown(float4{-v.xyz(), v.w}, 0.f, 0.f, 2.2f).xyz(), -shown(v, 0.f, 0.f, 2.2f).xyz()));
        for (Tonemap_ mode : {Tonemap_FalseColor, Tonemap_PositiveNegative})
            IM_CHECK(approx(
                shown(float4{0.3f, 0.3f, 0.3f, 1.f}, 0.f, 0.f, 1.f, mode, true).xyz(),
                shown(float4{float3{mode == Tonemap_FalseColor ? 0.7f : -0.3f}, 1.f}, 0.f, 0.f, 1.f, mode).xyz()));
    };

    // The status bar, the pixel inspector's watched pixels and the pixel grid all read pixel_value(), so they
    // show what the screen shows: each image's selected channels, isolated with its own alpha, and a
    // composite that blends only when there is a reference to blend with.
    t           = IM_REGISTER_TEST(engine, "display", "pixel_value_follows_the_channel_and_blend_controls");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        TonemapState    restore;
        const Channels_ channel = hdrview()->channel();
        const BlendMode blend   = hdrview()->blend_mode();

        reset_images(ctx);
        IM_CHECK_RETV(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE}) == 1, );
        hdrview()->set_current_image_index(0);
        auto img = hdrview()->current_image();
        IM_CHECK_RETV(img != nullptr, );
        const bool straight = img->transparency == TransparencyType_Straight;

        // a pixel whose channels all differ, so a readout showing the wrong one is caught
        int2   p{-1};
        float4 rgba;
        for (int y = 0; y < img->size().y && p.x < 0; ++y)
            for (int x = 0; x < img->size().x && p.x < 0; ++x)
            {
                rgba = img->rgba_pixel({x, y}, Target_Primary);
                if (rgba.w > 0.1f && std::fabs(rgba.x - rgba.y) > 0.05f && std::fabs(rgba.y - rgba.z) > 0.05f &&
                    std::fabs(rgba.x - rgba.z) > 0.05f)
                    p = {x, y};
            }
        IM_CHECK_RETV(p.x >= 0, );
        const float4 raw = hdrview()->pixel_value(p, true, 0);

        // With the tonemap the identity, the channel modes as relations between the readouts: a single channel is
        // the gray of the RGB mode's, which undoes alpha only where the file's is straight, alpha is its own gray, and
        // luminance weighs by the Y row of BT.709's RGB-to-XYZ matrix, from its primaries and D65 unrounded.
        hdrview()->tonemap()       = Tonemap_Gamma;
        hdrview()->gamma_live()    = 1.f;
        hdrview()->exposure_live() = 0.f;
        hdrview()->offset_live()   = 0.f;
        auto shown                 = [&](Channels_ ch)
        {
            hdrview()->channel() = ch;
            return hdrview()->pixel_value(p, false, 0);
        };
        const float4 color = shown(Channels_RGB);
        IM_CHECK(approx(shown(Channels_RGBA).xyz(), rgba.xyz(), 1e-5f) && shown(Channels_RGBA).w == rgba.w);
        IM_CHECK(approx(color.xyz() * (straight ? rgba.w : 1.f), rgba.xyz(), 1e-5f) && color.w == 1.f);
        for (int c = 0; c < 3; ++c) IM_CHECK((shown(Channels_(Channels_Red + c)) == float4{float3{color[c]}, 1.f}));
        IM_CHECK((shown(Channels_Alpha) == float4{float3{rgba.w}, 1.f}));
        IM_CHECK(approx(shown(Channels_Y).x, 0.2126390f * rgba.x + 0.7151687f * rgba.y + 0.0721923f * rgba.z, 1e-5f));
        for (int ch = 0; ch < Channels_COUNT; ++ch)
        {
            const float4 got = shown(Channels_(ch));
            // raw values are the file's own, whatever the viewer isolates
            IM_CHECK(hdrview()->pixel_value(p, true, 0) == raw);
            // with no reference there is nothing to blend, whatever the blend mode says
            for (int m = 0; m < BlendMode_COUNT; ++m)
            {
                hdrview()->blend_mode() = BlendMode_(m);
                IM_CHECK(hdrview()->pixel_value(p, false, 2) == got);
            }
        }

        // with a reference, the composite blends the two images' isolated channels: Normal lays the current one over
        // the reference, and Difference takes their absolute difference, either with the alpha of the two layered
        IM_CHECK_RETV(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE_2}) == 2, );
        const int ref = hdrview()->current_image_index() == 0 ? 1 : 0;
        hdrview()->set_current_image_index(1 - ref);
        hdrview()->set_reference_image_index(ref);
        auto cur = hdrview()->current_image(), other = hdrview()->reference_image();
        IM_CHECK_RETV(cur && other, );
        // a pixel where the current image is translucent over a reference that is there to a different degree,
        // which only source-over's own alpha gets right
        int2 q{-1};
        for (int y = 0; y < la::min(cur->size(), other->size()).y && q.x < 0; ++y)
            for (int x = 0; x < la::min(cur->size(), other->size()).x && q.x < 0; ++x)
            {
                const float a = cur->rgba_pixel({x, y}, Target_Primary).w;
                const float b = other->rgba_pixel({x, y}, Target_Secondary).w;
                if (a > 0.2f && a < 0.8f && b > 0.2f && std::fabs(a - b) > 0.2f)
                    q = {x, y};
            }
        IM_CHECK_RETV(q.x >= 0, );
        hdrview()->channel() = Channels_RGBA;
        const float4 top = cur->rgba_pixel(q, Target_Primary), bottom = other->rgba_pixel(q, Target_Secondary);
        const float  alpha = top.w + bottom.w * (1.f - top.w);
        for (BlendMode m : {BlendMode_Normal, BlendMode_Difference})
        {
            hdrview()->blend_mode() = m;
            const float4 got        = hdrview()->pixel_value(q, false, 2);
            const float3 want =
                m == BlendMode_Normal ? top.xyz() + bottom.xyz() * (1.f - top.w) : la::abs(top.xyz() - bottom.xyz());
            IM_CHECK(approx(got.xyz(), want, 1e-5f) && approx(got.w, alpha, 1e-5f));
        }

        hdrview()->channel()    = channel;
        hdrview()->blend_mode() = blend;
        reset_images(ctx);
    };
}
