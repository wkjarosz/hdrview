/** \file test_gui_display.cpp
    \author Wojciech Jarosz

    The display pipeline as the readouts reproduce it. HDRViewApp::tonemap_value() and the colormap lookup
    under it are a CPU re-implementation of what assets/shaders/image-shader.sglsl puts on screen; the pixel
    inspector, the pixel-value overlay and the statistics swatches are all built on them. Where the two
    drift, HDRView reports a color it is not showing.

    Both tests below transcribe the shader's own arithmetic and compare against it across a sweep, the same
    way "colorpass GLSL PQ constants match colorspace.h's inverse_EOTF_BT2100_PQ" does in
    tests/test_colorspace.cpp. If you edit either side, edit both.
*/

#include "app.h"
#include "colormap.h"
#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <cmath>

namespace
{

bool approx(float a, float b, float eps = 2e-3f) { return std::fabs(a - b) < eps; }
bool approx(float3 a, float3 b, float eps = 2e-3f) { return la::maxelem(la::abs(a - b)) < eps; }

//! Colormap::initialize()'s rule for which colormaps get a filtered texture; the qualitative ones are
//! sampled Nearest, and none of them is offered as a false-color map.
bool is_continuous(Colormap_ cmap) { return cmap > ImPlotColormap_Paired; }

/*!
    The colormap fetch as the fragment shader performs it: tonemap()'s texel-center remap of \p t, followed
    by a bilinear lookup into the N-texel, ClampToEdge texture Colormap::initialize() uploads. Returns the
    stored (sRGB-encoded) color, exactly what the shader hands to sRGBToLinear().
*/
float3 glsl_colormap_fetch(Colormap_ cmap, float t, bool reverse)
{
    if (reverse)
        t = 1.f - t;

    const auto &values = Colormap::values(cmap);
    const float n      = (float)values.size();
    // mix(0.5 / n, (n - 0.5) / n, t), converted from a uv to a texel coordinate, is just t * (n - 1); the
    // sampler's ClampToEdge is what bounds it once t leaves [0, 1].
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

//! Restores the tonemap controls the tests below drive directly, whatever the rest of the suite left set.
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
    // Colormap::sample() is the readouts' half of the shader's colormap texture fetch. It has to agree with
    // it over the whole of t, not just where the two parameterizations happen to cross.
    ImGuiTest *t = IM_REGISTER_TEST(engine, "display", "colormap_sample_matches_the_shaders_texture_fetch");
    t->TestFunc  = [](ImGuiTestContext *)
    {
        for (Colormap_ cmap = 0; cmap < Colormap_COUNT; ++cmap)
        {
            const auto &values = Colormap::values(cmap);
            IM_CHECK(!values.empty());

            // Both ends, for every colormap: t 0 is its first color and t 1 its last. This much holds
            // whether the texture is filtered or not, so the qualitative maps are included.
            const ImVec4 first = ImGui::ColorConvertU32ToFloat4(values.front());
            const ImVec4 last  = ImGui::ColorConvertU32ToFloat4(values.back());
            const ImVec4 lo    = Colormap::sample(cmap, 0.f);
            const ImVec4 hi    = Colormap::sample(cmap, 1.f);
            IM_CHECK(approx(float3{lo.x, lo.y, lo.z}, float3{first.x, first.y, first.z}, 1.f / 255.f));
            IM_CHECK(approx(float3{hi.x, hi.y, hi.z}, float3{last.x, last.y, last.z}, 1.f / 255.f));

            if (!is_continuous(cmap))
                continue;

            // And the interior, against the filtered fetch itself. ImPlot's table quantizes each step
            // between two colors into 255 of its own, so allow a couple of those.
            for (float x : {0.f, 0.01f, 0.1f, 0.25f, 1.f / 3.f, 0.5f, 0.625f, 0.75f, 0.9f, 0.99f, 1.f})
            {
                const ImVec4 got = Colormap::sample(cmap, x);
                IM_CHECK(approx(float3{got.x, got.y, got.z}, glsl_colormap_fetch(cmap, x, false), 3.f / 255.f));
            }

            // Out of range clamps rather than wrapping or extrapolating, as ClampToEdge does.
            const ImVec4 under = Colormap::sample(cmap, -0.5f);
            const ImVec4 over  = Colormap::sample(cmap, 1.5f);
            IM_CHECK(approx(float3{under.x, under.y, under.z}, float3{first.x, first.y, first.z}, 1.f / 255.f));
            IM_CHECK(approx(float3{over.x, over.y, over.z}, float3{last.x, last.y, last.z}, 1.f / 255.f));
        }
    };

    // tonemap_value() is the readouts' half of the shader's exposure/offset step and tonemap(). Swept over
    // both, every tonemap mode, and the translucent pixels where the premultiplied convention is the only
    // thing that distinguishes a right answer from a plausible one.
    t           = IM_REGISTER_TEST(engine, "display", "tonemap_value_matches_the_shader");
    t->TestFunc = [](ImGuiTestContext *)
    {
        TonemapState restore;

        // Transcribed from main() and tonemap() in assets/shaders/image-shader.sglsl.
        auto glsl_tonemap =
            [](float4 value, float exposure, float offset, float gamma, Tonemap_ mode, Colormap_ cmap, bool reverse)
        {
            const float3 rgb = std::pow(2.f, exposure) * value.xyz() + offset * value.w;
            if (mode == Tonemap_Gamma)
            {
                auto sign_pow = [](float a, float p) {
                    return (a > 0.f ? 1.f : a < 0.f ? -1.f : 0.f) * std::pow(std::fabs(a), p);
                };
                return float4{la::apply(sign_pow, rgb, float3{1.f / gamma}), value.w};
            }

            const float avg = la::dot(rgb, float3{1.f / 3.f});
            const float t   = (mode == Tonemap_FalseColor) ? avg : 0.5f * avg + 0.5f;
            return float4{sRGB_to_linear(glsl_colormap_fetch(cmap, t, reverse)) * value.w, value.w};
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
                                    glsl_tonemap(value, exposure, offset, gamma, mode, hdrview()->colormap(), reverse);
                                IM_CHECK(approx(got.xyz(), want.xyz(), 3.f / 255.f));
                                IM_CHECK(approx(got.w, want.w));
                            }
    };
}
