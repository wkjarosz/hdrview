/** \file test_gui_display.cpp
    \author Wojciech Jarosz

    The display pipeline as the readouts reproduce it: HDRViewApp::pixel_value() and tonemap_value() run the
    image shader's own steps (src/shared/pipeline.h), except for the colormap lookup, which reads
    Colormap::sample() where the shader reads a texture. These tests transcribe that texture fetch, check the
    CPU lookup and the whole composition against it, and check that the readouts follow the viewer's channel
    and blend controls as the screen does.
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

/// The colormap fetch as the fragment shader performs it, returning the stored, sRGB-encoded color.
/**
    apply_tonemap()'s texel-center remap of \p t, then a bilinear lookup into the N-texel ClampToEdge texture.
*/
float3 glsl_colormap_fetch(Colormap_ cmap, float t)
{
    const auto &values = Colormap::values(cmap);
    const float n      = (float)values.size();
    // mix(0.5 / n, (n - 0.5) / n, t) in texel coordinates is t * (n - 1); ClampToEdge bounds it outside [0, 1]
    const float coord = std::min(std::max(t * (n - 1.f), 0.f), n - 1.f);
    const int   i0    = (int)std::floor(coord);
    const int   i1    = std::min(i0 + 1, (int)values.size() - 1);

    auto texel = [&values](int i)
    {
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(values[i]);
        return float3{c.x, c.y, c.z};
    };
    return la::lerp(texel(i0), texel(i1), coord - (float)i0);
}

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
    ImGuiTest *t = IM_REGISTER_TEST(engine, "display", "colormap_sample_matches_the_shaders_texture_fetch");
    t->TestFunc  = [](ImGuiTestContext *)
    {
        for (Colormap_ cmap = 0; cmap < Colormap_COUNT; ++cmap)
        {
            const auto &values = Colormap::values(cmap);
            IM_CHECK(!values.empty());

            // both ends, for every colormap: this holds whether or not the texture is filtered
            const ImVec4 first = ImGui::ColorConvertU32ToFloat4(values.front());
            const ImVec4 last  = ImGui::ColorConvertU32ToFloat4(values.back());
            const ImVec4 lo    = Colormap::sample(cmap, 0.f);
            const ImVec4 hi    = Colormap::sample(cmap, 1.f);
            IM_CHECK(approx(float3{lo.x, lo.y, lo.z}, float3{first.x, first.y, first.z}, 1.f / 255.f));
            IM_CHECK(approx(float3{hi.x, hi.y, hi.z}, float3{last.x, last.y, last.z}, 1.f / 255.f));

            if (!is_continuous(cmap))
                continue;

            // the interior, against the filtered fetch; ImPlot quantizes each step into 255 of its own
            for (float x : {0.f, 0.01f, 0.1f, 0.25f, 1.f / 3.f, 0.5f, 0.625f, 0.75f, 0.9f, 0.99f, 1.f})
            {
                const ImVec4 got = Colormap::sample(cmap, x);
                IM_CHECK(approx(float3{got.x, got.y, got.z}, glsl_colormap_fetch(cmap, x), 3.f / 255.f));
            }

            // out of range clamps, as ClampToEdge does
            const ImVec4 under = Colormap::sample(cmap, -0.5f);
            const ImVec4 over  = Colormap::sample(cmap, 1.5f);
            IM_CHECK(approx(float3{under.x, under.y, under.z}, float3{first.x, first.y, first.z}, 1.f / 255.f));
            IM_CHECK(approx(float3{over.x, over.y, over.z}, float3{last.x, last.y, last.z}, 1.f / 255.f));
        }
    };

    // swept over exposure, offset, every tonemap mode, and the translucent pixels where only the
    // premultiplied convention distinguishes a right answer from a plausible one
    t           = IM_REGISTER_TEST(engine, "display", "tonemap_value_applies_each_setting_as_defined");
    t->TestFunc = [](ImGuiTestContext *)
    {
        TonemapState restore;

        // What the view shows of a premultiplied value, written out from what each setting means: exposure scales
        // the color and the offset adds to it in proportion to alpha; the gamma mode raises each channel to one over
        // gamma, mirrored about zero; a colormap mode shows the colormap's color at the channels' mean, mapped from
        // [-1, 1] for positive/negative and turned around when reversed, premultiplied by alpha.
        auto shown =
            [](float4 v, float exposure, float offset, float gamma, Tonemap_ mode, Colormap_ cmap, bool reverse)
        {
            const float3 x = v.xyz() * std::exp2(exposure) + float3{offset * v.w};
            if (mode == Tonemap_Gamma)
            {
                float3 out;
                for (int c = 0; c < 3; ++c) out[c] = std::copysign(std::pow(std::fabs(x[c]), 1.f / gamma), x[c]);
                return float4{out, v.w};
            }
            float t = (x.x + x.y + x.z) / 3.f;
            if (mode == Tonemap_PositiveNegative)
                t = 0.5f * t + 0.5f;
            if (reverse)
                t = 1.f - t;
            const ImVec4 c = Colormap::sample(cmap, t);
            return float4{sRGB_to_linear(c.x) * v.w, sRGB_to_linear(c.y) * v.w, sRGB_to_linear(c.z) * v.w, v.w};
        };

        const float4 samples[] = {{0.4f, 0.4f, 0.4f, 1.f},   {0.4f, 0.4f, 0.4f, 0.5f}, {0.f, 0.f, 0.f, 0.f},
                                  {0.9f, 0.2f, 0.05f, 0.7f}, {1.5f, 0.3f, 2.75f, 1.f}, {-0.2f, 0.6f, -0.05f, 0.25f}};

        for (Tonemap_ mode : {Tonemap_Gamma, Tonemap_FalseColor, Tonemap_PositiveNegative})
            for (bool reverse : {false, true})
                for (float exposure : {-2.f, 0.f, 1.f})
                    for (float offset : {0.f, 0.25f, -0.3f})
                        for (float gamma : {1.f, 2.2f})
                            for (const float4 &value : samples)
                            {
                                hdrview()->tonemap()          = mode;
                                hdrview()->exposure_live()    = exposure;
                                hdrview()->offset_live()      = offset;
                                hdrview()->gamma_live()       = gamma;
                                hdrview()->reverse_colormap() = reverse;

                                const float4 got = hdrview()->tonemap_value(value);
                                const float4 want =
                                    shown(value, exposure, offset, gamma, mode, hdrview()->colormap(), reverse);
                                IM_CHECK(approx(got.xyz(), want.xyz(), 3.f / 255.f));
                                IM_CHECK(approx(got.w, want.w));
                            }
    };

    // The status bar, the pixel inspector's watched pixels and the pixel grid all read pixel_value(), so they
    // show what the screen shows: each image's selected channels, isolated with its own alpha, and a
    // composite that blends only when there is a reference to blend with.
    t           = IM_REGISTER_TEST(engine, "display", "pixel_value_follows_the_channel_and_blend_controls");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        TonemapState     restore;
        const Channels_  channel = hdrview()->channel();
        const BlendMode_ blend   = hdrview()->blend_mode();

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

        // with the tonemap the identity, which tonemap_value_applies_each_setting_as_defined holds it to, what each
        // channel mode shows written out: an isolated channel is gray and opaque, undone by alpha when the file's is
        // straight, and luminance weighs by BT.709's
        hdrview()->tonemap()       = Tonemap_Gamma;
        hdrview()->gamma_live()    = 1.f;
        hdrview()->exposure_live() = 0.f;
        hdrview()->offset_live()   = 0.f;
        auto isolated              = [](float4 v, int ch, bool straight_alpha) -> float4
        {
            const float d = straight_alpha ? v.w : 1.f;
            switch (ch)
            {
            case Channels_RGBA: return v;
            case Channels_RGB: return {v.x / d, v.y / d, v.z / d, 1.f};
            case Channels_Red: return {float3{v.x / d}, 1.f};
            case Channels_Green: return {float3{v.y / d}, 1.f};
            case Channels_Blue: return {float3{v.z / d}, 1.f};
            case Channels_Alpha: return {float3{v.w}, 1.f};
            // Channels_Y, by the Y row of BT.709's RGB-to-XYZ matrix, from its primaries and D65 unrounded
            default: return {float3{0.2126390f * v.x + 0.7151687f * v.y + 0.0721923f * v.z}, v.w};
            }
        };
        static_assert(Channels_COUNT == 7, "a new channel mode needs what it shows written out above");

        for (int ch = 0; ch < Channels_COUNT; ++ch)
        {
            hdrview()->channel() = Channels_(ch);
            const float4 want    = isolated(rgba, ch, straight);
            const float4 got     = hdrview()->pixel_value(p, false, 0);
            IM_CHECK(approx(got.xyz(), want.xyz(), 1e-5f) && approx(got.w, want.w, 1e-5f));

            // raw values are the file's own, whatever the viewer isolates
            IM_CHECK(hdrview()->pixel_value(p, true, 0) == raw);

            // with no reference there is nothing to blend, whatever the blend mode says
            for (int m = 0; m < BlendMode_COUNT; ++m)
            {
                hdrview()->blend_mode() = BlendMode_(m);
                const float4 composite  = hdrview()->pixel_value(p, false, 2);
                IM_CHECK(approx(composite.xyz(), got.xyz(), 1e-5f) && approx(composite.w, got.w, 1e-5f));
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
        for (Channels_ c : {Channels_RGBA, Channels_Green})
            for (BlendMode_ m : {BlendMode_Normal, BlendMode_Difference})
            {
                hdrview()->channel()    = c;
                hdrview()->blend_mode() = m;
                const float4 top =
                    isolated(cur->rgba_pixel(q, Target_Primary), c, cur->transparency == TransparencyType_Straight);
                const float4 bottom = isolated(other->rgba_pixel(q, Target_Secondary), c,
                                               other->transparency == TransparencyType_Straight);
                const float  alpha  = top.w + bottom.w * (1.f - top.w);
                const float3 color  = m == BlendMode_Normal ? top.xyz() + bottom.xyz() * (1.f - top.w)
                                                            : la::abs(top.xyz() - bottom.xyz());
                const float4 want{color, alpha};
                const float4 got = hdrview()->pixel_value(q, false, 2);
                IM_CHECK(approx(got.xyz(), want.xyz(), 1e-5f) && approx(got.w, want.w, 1e-5f));
            }

        hdrview()->channel()    = channel;
        hdrview()->blend_mode() = blend;
        reset_images(ctx);
    };
}
