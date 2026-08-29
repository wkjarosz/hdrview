/** \file test_gui_viewport.cpp
    \author Wojciech Jarosz

    The viewport transforms: pixel <-> viewport <-> app position, and their agreement with the quad the
    image shader is actually asked to draw. Everything the app says about where a pixel is -- the mouse
    readout, the pixel-value overlay, the window borders, the watched-pixel crosshairs -- is one of these
    functions, so they and the shader have to describe the same place.
*/

#include "app.h"
#include "image.h"
#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <cmath>

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif
#ifndef HDRVIEW_GUI_TEST_IMAGE_2
#error "HDRVIEW_GUI_TEST_IMAGE_2 must be defined by CMake to a second fixture image path"
#endif

namespace
{

//! Loads `paths` and waits for all of them to arrive.
void load_and_wait(ImGuiTestContext *ctx, const std::vector<std::string> &paths)
{
    hdrview()->close_all_images();
    for (int frame = 0; frame < 60 && hdrview()->num_images() != 0; ++frame) ctx->Yield();
    hdrview()->load_images(paths);
    for (int frame = 0; frame < 240 && hdrview()->num_images() < (int)paths.size(); ++frame) ctx->Yield();
    IM_CHECK_EQ(hdrview()->num_images(), (int)paths.size());
}

bool approx(float2 a, float2 b, float eps = 1e-2f) { return la::maxelem(la::abs(a - b)) < eps; }

/*!
    Gives an image the display window an OpenEXR file can carry -- offset from the origin and a different
    size from the data window -- for as long as it is in scope.

    The fixtures are PNGs, whose two windows always coincide at the origin, which hides every place the two
    are confused for one another. Restored on destruction, and nothing between here and there yields, so no
    frame is ever drawn against the substitute.
*/
struct ScopedDisplayWindow
{
    ImagePtr img;
    Box2i    saved;

    ScopedDisplayWindow(ImagePtr i, const Box2i &substitute) : img(i), saved(i->display_window)
    {
        img->display_window = substitute;
    }
    ~ScopedDisplayWindow() { img->display_window = saved; }
};

//! Every combination of the two flip axes, so no test below checks only the unflipped case.
struct FlipState
{
    bool &h       = *hdrview()->action("Flip horizontally").p_selected;
    bool &v       = *hdrview()->action("Flip vertically").p_selected;
    bool  saved_h = h, saved_v = v;

    void set(int f)
    {
        h = (f & 1) != 0;
        v = (f & 2) != 0;
    }
    ~FlipState()
    {
        h = saved_h;
        v = saved_v;
    }
};

} // namespace

void RegisterTests_Viewport(ImGuiTestEngine *engine)
{
    /*
        The pixel <-> viewport map is an invertible affine one: a pixel step is exactly `zoom` viewport
        units, positive or negative with the flip, and converting either way and back returns what went in.
        Swept over zoom, pan and both flip axes, with a display window that neither starts at the origin nor
        matches the data window, since that is what an EXR carries and what the flip mirrors about.
    */
    ImGuiTest *t = IM_REGISTER_TEST(engine, "viewport", "pixel_transform_is_affine_and_invertible");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE});

        auto      img = hdrview()->current_image();
        FlipState flip;

        for (const Box2i &display_window : {img->display_window, Box2i{{-13, 7}, {img->data_window.max + int2{5, -3}}}})
        {
            ScopedDisplayWindow scoped{img, display_window};
            for (int f = 0; f < 4; ++f)
            {
                flip.set(f);
                for (float zoom : {0.125f, 1.f, 3.f, 64.f})
                    for (float2 pan : {float2{0.f, 0.f}, float2{37.f, -19.f}})
                    {
                        hdrview()->set_zoom(zoom);
                        hdrview()->center();
                        hdrview()->reposition_pixel_to_vp_pos(pan, float2{0.f, 0.f});

                        for (float2 pixel : {float2{0.f, 0.f}, float2{0.5f, 0.5f}, float2{-4.25f, 11.75f},
                                             float2{img->data_window.max}, float2{display_window.min}})
                        {
                            // Round trip, in both directions.
                            IM_CHECK(approx(hdrview()->pixel_at_vp_pos(hdrview()->vp_pos_at_pixel(pixel)), pixel));
                            IM_CHECK(approx(hdrview()->vp_pos_at_pixel(hdrview()->pixel_at_vp_pos(pixel)), pixel));

                            // One pixel across is exactly `zoom` across, mirrored along the flipped axes.
                            const float2 step =
                                hdrview()->vp_pos_at_pixel(pixel + 1.f) - hdrview()->vp_pos_at_pixel(pixel);
                            IM_CHECK(approx(step, zoom * la::select(bool2{flip.h, flip.v}, float2{-1.f}, float2{1.f}),
                                            1e-2f * zoom));

                            // And repositioning puts the pixel where it was asked to go.
                            hdrview()->reposition_pixel_to_vp_pos(float2{31.f, 17.f}, pixel);
                            IM_CHECK(approx(hdrview()->vp_pos_at_pixel(pixel), float2{31.f, 17.f}));
                        }
                    }
            }
        }
    };

    /*
        Where the image actually lands on screen, against the specification written out below rather than
        against any other part of the transform.

        This has to be absolute. image_position()/image_scale() are derived from vp_pos_at_pixel(), so the
        two agree by construction and comparing them cannot detect a change in the transform they share;
        likewise, asking vp_pos_at_pixel() where the image is and then asking it which pixel is there just
        has it grade itself. What is *not* free is the placement: the current image's display window is
        centered in the viewport, and a flipped axis mirrors within it -- and the reference image is placed
        by that same mapping, so the two stay overlaid whatever their own windows are.
    */
    ImGuiTest *t2 = IM_REGISTER_TEST(engine, "viewport", "images_land_where_the_viewport_specifies");
    t2->TestFunc  = [](ImGuiTestContext *ctx)
    {
        // Two images of different sizes, so a reference whose windows differ from the current image's is
        // exercised rather than the coincidence of them matching.
        load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE, HDRVIEW_GUI_TEST_IMAGE_2});
        hdrview()->set_current_image_index(0);
        hdrview()->set_reference_image_index(1);
        auto img = hdrview()->current_image();
        auto ref = hdrview()->reference_image();
        IM_CHECK(img && ref);
        IM_CHECK(img->display_window != ref->display_window);

        FlipState flip;

        // A display window that neither starts at the origin nor matches the data window, since that is
        // what an OpenEXR file carries and what a flip mirrors about; the fixtures are PNGs, whose two
        // windows always coincide at the origin and so hide any confusion between them.
        for (const Box2i &display_window : {img->display_window, Box2i{{-9, 4}, {700, 990}}})
        {
            ScopedDisplayWindow scoped{img, display_window};
            for (int f = 0; f < 4; ++f)
            {
                flip.set(f);
                for (float zoom : {0.3f, 1.f, 2.f, 8.f})
                {
                    hdrview()->set_zoom(zoom);
                    hdrview()->center();

                    // The specification, independent of everything under test.
                    const float2 span            = zoom * float2{display_window.size()};
                    const float2 origin          = (hdrview()->viewport_size() - span) / 2.f;
                    auto         expected_vp_pos = [&](float2 pixel)
                    {
                        const float2 within = zoom * (pixel - float2{display_window.min});
                        return origin + la::select(bool2{flip.h, flip.v}, span - within, within);
                    };

                    for (float2 pixel : {float2{display_window.min}, float2{display_window.max}, float2{0.f, 0.f},
                                         float2{-6.5f, 123.25f}})
                        IM_CHECK(approx(hdrview()->vp_pos_at_pixel(pixel), expected_vp_pos(pixel), 1e-2f * zoom));

                    // The quad the shader samples each image over: uv 0 at its data window's min corner,
                    // uv 1 at the max one, both placed by that same mapping. The reference included -- it
                    // is mirrored about the *current* image's display window, not its own.
                    for (ConstImagePtr which : {ConstImagePtr{img}, ConstImagePtr{ref}})
                    {
                        const float2 vp    = hdrview()->viewport_size();
                        const float2 pos   = hdrview()->image_position(which) * vp;
                        const float2 scale = hdrview()->image_scale(which) * vp;
                        IM_CHECK(approx(pos, expected_vp_pos(float2{which->data_window.min}), 1e-2f * zoom));
                        IM_CHECK(approx(pos + scale, expected_vp_pos(float2{which->data_window.max}), 1e-2f * zoom));
                    }

                    // The same thing in the terms a user would check it in: the pixel reported half a pixel
                    // inside the leading corner of the drawn image. Flipped horizontally, the leftmost
                    // column on screen is the image's rightmost, not its neighbor.
                    const Box2i  dw = img->data_window;
                    const Box2f  drawn{expected_vp_pos(float2{dw.min}), expected_vp_pos(float2{dw.max})};
                    const Box2f  rect = Box2f{drawn}.make_valid();
                    const float2 half{0.5f * zoom};
                    const int2   near_pixel = int2{hdrview()->pixel_at_vp_pos(rect.min + half)};
                    const int2   far_pixel  = int2{hdrview()->pixel_at_vp_pos(rect.max - half)};
                    IM_CHECK_EQ(near_pixel.x, flip.h ? dw.max.x - 1 : dw.min.x);
                    IM_CHECK_EQ(near_pixel.y, flip.v ? dw.max.y - 1 : dw.min.y);
                    IM_CHECK_EQ(far_pixel.x, flip.h ? dw.min.x : dw.max.x - 1);
                    IM_CHECK_EQ(far_pixel.y, flip.v ? dw.min.y : dw.max.y - 1);
                }
            }
        }

        hdrview()->set_reference_image_index(-1, true);
    };
}
