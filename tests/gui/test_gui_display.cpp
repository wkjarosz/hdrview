/** \file test_gui_display.cpp
    \author Wojciech Jarosz

    The display pipeline as the readouts reproduce it: HDRViewApp::tonemap_value() and the colormap lookup
    under it are a CPU re-implementation of assets/shaders/image-shader.sglsl. Both tests transcribe the
    shader's arithmetic and sweep it against theirs; if you edit either side, edit both.
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
//! sampled Nearest.
bool is_continuous(Colormap_ cmap) { return cmap > ImPlotColormap_Paired; }

//! The colormap fetch as the fragment shader performs it: tonemap()'s texel-center remap of \p t, then a
//! bilinear lookup into the N-texel ClampToEdge texture. Returns the stored, sRGB-encoded color.
float3 glsl_colormap_fetch(Colormap_ cmap, float t, bool reverse)
{
    if (reverse)
        t = 1.f - t;

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
                IM_CHECK(approx(float3{got.x, got.y, got.z}, glsl_colormap_fetch(cmap, x, false), 3.f / 255.f));
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

        // transcribed from main() and tonemap() in assets/shaders/image-shader.sglsl
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
