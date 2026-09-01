/** \file test_gui_viewport.cpp
    \author Wojciech Jarosz

    The viewport transforms: pixel <-> viewport <-> app position, and their agreement with the quad the
    image shader is actually asked to draw. Everything the app says about where a pixel is -- the mouse
    readout, the pixel-value overlay, the window borders, the watched-pixel crosshairs -- is one of these
    functions, so they and the shader have to describe the same place.

    And what moves that transform: every way of zooming promises to hold one point still while it does, and
    the scroll wheel and a two-finger gesture are the two ways of asking for a zoom by hand.
*/

#include "app.h"
#include "image.h"
#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include "test_gui_support.h"

using namespace hdrview_test;

#include <cmath>

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif
#ifndef HDRVIEW_GUI_TEST_IMAGE_2
#error "HDRVIEW_GUI_TEST_IMAGE_2 must be defined by CMake to a second fixture image path"
#endif

namespace
{

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
        reset_images(ctx);
        IM_CHECK_EQ(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE}), 1);

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
        reset_view_controls(ctx);
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
        reset_images(ctx);
        IM_CHECK_EQ(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE, HDRVIEW_GUI_TEST_IMAGE_2}), 2);
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
        reset_view_controls(ctx);
    };

    /*
        Every way of zooming does it about some point, and that point is the promise the user is relying on:
        whatever pixel was under it is still under it afterwards, so the thing being looked at does not slide
        away as the zoom changes. Checked through each entry point that makes that promise, since each picks
        its own focus and each could pick the wrong one:

          - zoom_at_vp_pos(), what the scroll wheel calls, about an arbitrary point -- and a step of `amount`
            has to be exactly `amount` on the zoom_level() scale the zoom readout and the zoom slider share.
          - zoom_in()/zoom_out(), the menu's power-of-two steps, about the viewport center.
          - touch_gesture(), a two-finger pinch, whose focus is the one that also *moves*: the pixel under
            the fingers' midpoint follows that midpoint, and the zoom changes by exactly the ratio their
            separation did. That is what makes two fingers moved together pan, and what makes the image grow
            by as much as the fingers spread rather than by some fraction of it.

        Swept over zoom, pan and both flip axes.
    */
    ImGuiTest *t3 = IM_REGISTER_TEST(engine, "viewport", "zooming_pins_the_pixel_under_its_focus");
    t3->TestFunc  = [](ImGuiTestContext *ctx)
    {
        reset_images(ctx);
        IM_CHECK_EQ(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE}), 1);

        FlipState flip;

        // Puts the viewport in a known state before each case below.
        auto set_view = [](float zoom, float2 pan)
        {
            hdrview()->set_zoom(zoom);
            hdrview()->center();
            hdrview()->reposition_pixel_to_vp_pos(pan, float2{0.f, 0.f});
        };

        for (int f = 0; f < 4; ++f)
        {
            flip.set(f);
            for (float zoom : {0.125f, 1.f, 3.f, 64.f})
                for (float2 pan : {float2{0.f, 0.f}, float2{37.f, -19.f}})
                {
                    // Zooming about a point, by a step measured on the zoom_level() scale.
                    for (float amount : {-2.5f, 0.7f, 2.5f})
                        for (float2 focus : {float2{101.f, 83.f}, float2{0.f, 0.f}})
                        {
                            set_view(zoom, pan);
                            const float2 pinned = hdrview()->pixel_at_vp_pos(focus);
                            const float  level  = hdrview()->zoom_level();

                            hdrview()->zoom_at_vp_pos(amount, focus);

                            IM_CHECK_LT(std::abs(hdrview()->zoom_level() - (level + amount)), 1e-3f);
                            // A viewport unit is 1/zoom of a pixel, so the tolerance has to be too.
                            IM_CHECK(approx(hdrview()->pixel_at_vp_pos(focus), pinned, 1e-2f / hdrview()->zoom()));
                        }

                    // The menu's steps, which hold the viewport center rather than the mouse.
                    for (bool in : {true, false})
                    {
                        set_view(zoom, pan);
                        const float2 center = 0.5f * hdrview()->viewport_size();
                        const float2 pinned = hdrview()->pixel_at_vp_pos(center);

                        if (in)
                            hdrview()->zoom_in();
                        else
                            hdrview()->zoom_out();

                        IM_CHECK(approx(hdrview()->pixel_at_vp_pos(center), pinned, 1e-2f / hdrview()->zoom()));
                    }

                    // A two-finger gesture: pinch only, pan only, and both at once.
                    for (float scale : {1.f, 1.0f / 1.03f, 1.4f, 0.5f})
                        for (float2 travel : {float2{0.f, 0.f}, float2{53.f, -27.f}})
                        {
                            set_view(zoom, pan);
                            const float2 from   = float2{101.f, 83.f};
                            const float2 to     = from + travel;
                            const float2 pinned = hdrview()->pixel_at_vp_pos(from);

                            hdrview()->touch_gesture(2, scale, hdrview()->app_pos_at_vp_pos(from),
                                                     hdrview()->app_pos_at_vp_pos(to));

                            const float zoomed = zoom * scale;
                            IM_CHECK_LT(std::abs(hdrview()->zoom() - zoomed), 1e-3f * zoomed);
                            IM_CHECK(approx(hdrview()->pixel_at_vp_pos(to), pinned, 1e-2f / zoomed));
                        }
                }
        }

        // Leave no fingers down: while any are, the viewport suppresses drag-panning.
        hdrview()->touch_gesture(0, 1.f, float2{0.f}, float2{0.f});
        reset_view_controls(ctx);
    };

    /*
        The scroll wheel, driven the way a user drives it, through the input path rather than the transform
        it ends up calling. Two things are worth stating there and nowhere else:

        Rate. A notch of a discrete wheel arrives as one whole unit, while a trackpad reports the same travel
        as a stream of small fractions adding up to many units -- so the two are only comparable once the
        notch has been scaled up, and a notch left on the trackpad's scale moves the viewport by a fraction
        of what it should, which reads as the wheel doing nothing. Stated as bounds and as a comparison
        between the devices, since the exact step is a tuning choice and the fractions are what it is tuned
        against.

        Mode. The wheel zooms about the mouse, and shift makes the same wheel pan instead -- neither of which
        the functions it calls can be asked about, since they cannot know what was passed to them.
    */
    ImGuiTest *t4 = IM_REGISTER_TEST(engine, "viewport", "scrolling_zooms_or_pans_alike_from_either_device");
    t4->TestFunc  = [](ImGuiTestContext *ctx)
    {
        reset_images(ctx);
        IM_CHECK_EQ(load_and_wait(ctx, {HDRVIEW_GUI_TEST_IMAGE}), 1);

        ctx->MouseMoveToPos(hdrview()->app_pos_at_vp_pos(0.25f * hdrview()->viewport_size()));
        // Where the pointer actually landed, which is what the viewport anchors a wheel zoom on. Asking for
        // a position and assuming the pointer is at it leaves a sub-pixel difference, and the drift that
        // difference causes is larger than the anchoring being checked below.
        const float2 mouse = float2{ImGui::GetIO().MousePos};

        // Scrolls `events` wheel events of `wheel` units each over the viewport, from a known starting view.
        // The idle frames make each call a gesture of its own: which kind of device is scrolling is latched
        // until scrolling stops, so without them a call would inherit the previous call's device.
        auto scroll = [ctx](float wheel, int events)
        {
            ctx->Yield(30);
            hdrview()->set_zoom(1.f);
            hdrview()->center();
            for (int i = 0; i < events; ++i) ctx->MouseWheelY(wheel);
        };

        // One notch of zoom: enough to see, and not so much that the image leaps past what was being
        // looked at. And it happens about the mouse, not about the center or the corner.
        scroll(1.f, 1);
        const float notch = hdrview()->zoom();
        IM_CHECK_GT(notch, 1.05f);
        IM_CHECK_LT(notch, 2.f);
        {
            const float2 pinned = hdrview()->pixel_at_app_pos(mouse);
            ctx->MouseWheelY(1.f);
            IM_CHECK(approx(hdrview()->pixel_at_app_pos(mouse), pinned, 1e-2f / hdrview()->zoom()));
        }

        // The same notch the other way is its inverse.
        scroll(-1.f, 1);
        IM_CHECK_LT(std::abs(notch * hdrview()->zoom() - 1.f), 1e-3f);

        // The travel a trackpad spreads over many fractional events does what the notch it adds up to does,
        // rather than a tenth of it.
        scroll(0.5f, 20);
        IM_CHECK_LT(std::abs(hdrview()->zoom() - notch), 0.05f * notch);

        // Held shift turns the same wheel into a pan: the zoom stays put and the image translates, by an
        // amount a user can see, and again at one rate for both devices.
        auto shift_scroll = [ctx, &scroll](float wheel, int events)
        {
            ctx->KeyDown(ImGuiMod_Shift);
            scroll(wheel, events);
            const float2 moved = hdrview()->vp_pos_at_pixel(float2{0.f, 0.f});
            ctx->KeyUp(ImGuiMod_Shift);
            return moved;
        };

        // Where the origin sits with the view reset and nothing scrolled, to measure the translation from.
        scroll(0.f, 0);
        const float2 unmoved = hdrview()->vp_pos_at_pixel(float2{0.f, 0.f});

        const float2 panned = shift_scroll(1.f, 1);
        IM_CHECK_LT(std::abs(hdrview()->zoom() - 1.f), 1e-4f);
        IM_CHECK_GT(la::length(panned - unmoved), 10.f);
        IM_CHECK(approx(shift_scroll(0.5f, 20), panned, 1.f));

        reset_view_controls(ctx);
    };
}
