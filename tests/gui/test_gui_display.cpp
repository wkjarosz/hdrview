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
    t           = IM_REGISTER_TEST(engine, "display", "tonemap_value_matches_the_shader");
    t->TestFunc = [](ImGuiTestContext *)
    {
        TonemapState restore;

        // main() in image-shader.sglsl: the shared steps, with the colormap fetched as the shader fetches it
        auto shader_tonemap =
            [](float4 value, float exposure, float offset, float gamma, Tonemap_ mode, Colormap_ cmap, bool reverse)
        {
            const float4 exposed = shader::expose(value, std::pow(2.f, exposure), offset);
            const float3 colormap_sRGB =
                mode == Tonemap_Gamma
                    ? float3{0.f}
                    : glsl_colormap_fetch(cmap, shader::colormap_coordinate(exposed.xyz(), mode, reverse));
            return shader::tonemap(exposed, mode, gamma, colormap_sRGB);
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
                                const float4 want = shader_tonemap(value, exposure, offset, gamma, mode,
                                                                   hdrview()->colormap(), reverse);
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

        for (int ch = 0; ch < Channels_COUNT; ++ch)
        {
            hdrview()->channel() = Channels_(ch);
            const float4 want    = hdrview()->tonemap_value(shader::choose_channel(rgba, Channels_(ch), straight));
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

        // isolating one channel shows it as an opaque gray, independently of how the shared code computes it
        hdrview()->channel()       = Channels_Red;
        hdrview()->tonemap()       = Tonemap_Gamma;
        hdrview()->gamma_live()    = 1.f;
        hdrview()->exposure_live() = 0.f;
        hdrview()->offset_live()   = 0.f;
        const float4 red           = hdrview()->pixel_value(p, false, 0);
        const float  want_red      = straight ? rgba.x / rgba.w : rgba.x;
        IM_CHECK(approx(red.xyz(), float3{want_red}, 1e-5f) && red.w == 1.f);

        // with a reference, the composite blends the two images' isolated channels
        IM_CHECK_RETV(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE_2}) == 2, );
        const int ref = hdrview()->current_image_index() == 0 ? 1 : 0;
        hdrview()->set_current_image_index(1 - ref);
        hdrview()->set_reference_image_index(ref);
        auto cur = hdrview()->current_image(), other = hdrview()->reference_image();
        IM_CHECK_RETV(cur && other, );
        const int2 q = la::min(cur->size(), other->size()) / 2;
        for (Channels_ c : {Channels_RGBA, Channels_Green})
            for (BlendMode_ m : {BlendMode_Normal, BlendMode_Difference})
            {
                hdrview()->channel()    = c;
                hdrview()->blend_mode() = m;
                const float4 want       = hdrview()->tonemap_value(
                    shader::blend(shader::choose_channel(cur->rgba_pixel(q, Target_Primary), c,
                                                         cur->transparency == TransparencyType_Straight),
                                  shader::choose_channel(other->rgba_pixel(q, Target_Secondary), c,
                                                         other->transparency == TransparencyType_Straight),
                                  m));
                const float4 got = hdrview()->pixel_value(q, false, 2);
                IM_CHECK(approx(got.xyz(), want.xyz(), 1e-5f) && approx(got.w, want.w, 1e-5f));
            }

        hdrview()->channel()    = channel;
        hdrview()->blend_mode() = blend;
        reset_images(ctx);
    };
}
