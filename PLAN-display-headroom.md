# Display HDR headroom in the histogram

Working notes for `feature/display-headroom`. **Delete this file before merging** -- it is a handoff
document, not documentation.

## Goal

Adopt Lightroom's presentation of display headroom in the histogram:

- The range up to display value 1.0 is labeled **SDR**.
- The range from 1.0 up to the display's current headroom is labeled **HDR**, and grows or shrinks as
  the monitor's brightness (and hence its headroom) changes.
- The plot background is highlighted across SDR + HDR together, and dimmed beyond the headroom
  ceiling -- values the display cannot show at the current exposure.

HDRView renders in extended sRGB where **1.0 is the display's SDR reference white**, so headroom is
just a multiple of that: 1.0 means no headroom, 8.0 means three stops. That is the number to draw,
and on macOS it is also the number the OS hands us, with no unit conversion.

## Where this goes

`Image::draw_histogram()` in `src/image-gui.cpp` (starts ~line 193). The relevant part today is
~lines 443-481, which:

1. computes `xrange` -- display 0.0 and 1.0 mapped into plot space through the live exposure/offset,
2. draws two `ImPlot::DragRect`s that dim everything *outside* `[0, 1]`,
3. draws draggable black/white point lines at those two values, tagged `"0"` and `"1"`.

The change is to that second step, plus new labels:

- Dim from the left plot edge to display 0 (unchanged), and from **headroom** to the right plot edge
  (currently: from 1.0).
- Leave `[0, 1]` and `[1, headroom]` both undimmed; label them `SDR` and `HDR`.
- Draw a non-draggable line at `headroom`, tagged with its value.

The black/white drag lines and their `"0"` / `"1"` tags stay exactly as they are -- they still mark
the white point, which is now the SDR/HDR boundary rather than the edge of the highlighted region.

Everything is expressed in plot space via the same transform the existing code uses:

```cpp
plot_x = (display_value - offset_live) * pow(2, -exposure_live)
```

so it composes with all three x-axis scales (Linear / sRGB / Asinh) for free, since those are applied
by ImPlot's axis transform rather than by this code.

### Degenerate cases

- **headroom unknown (0)** -- draw nothing new; behave exactly as today.
- **headroom <= 1 (SDR display)** -- no HDR band. Consider labeling the `[0,1]` band `SDR` anyway, or
  suppressing both labels; decide when it can be seen on a real display.

## Platform research

Headroom is `max_luminance / SDR_white_luminance` everywhere except macOS, which reports the ratio
directly. Proposed single accessor: `float display_headroom()`, returning a multiple of SDR white,
`0` for unknown.

### macOS -- easiest, not yet wired up

`NSScreen.maximumExtendedDynamicRangeColorComponentValue` on the *window's* screen
(`glfwGetCocoaWindow(w).screen`). Already in units of SDR white.

Apple documents it as changing dynamically with brightness, thermal state, and power source, and
posts **no notification** for it -- so poll. That is fine: `update_colorpass()` already re-queries
display state once per frame.

Nothing existing gives us this:

- GLFW's Cocoa backend is a stub -- `_glfwGetWindowMaxLuminanceCocoa()` returns `0.0f`,
  `_glfwGetWindowSdrWhiteLevelCocoa()` returns `80.0f`.
- hello_imgui's `hasEdrSupport()` is a bool, and derives it from
  `maximumPotentialExtendedDynamicRangeColorComponentValue` across *all* screens -- the static
  ceiling, not the current value, and not per-window.

So: a new `src/display_headroom_cocoa.mm`. `GLFW_EXPOSE_NATIVE_COCOA` is already defined
(`src/app.cpp:26`), `enable_language(OBJC)` is already on, and `.mm` files are already in
`EXTRA_SOURCES` (`CMakeLists.txt:394`). Expect ~15 lines.

Also available: `maximumReferenceExtendedDynamicRangeColorComponentValue` (headroom for reference
rendering) and `maximumPotential...` (static ceiling). See the open question below.

### Windows -- looks plumbed, is not

`_glfwGetWindowMaxLuminanceWin32()` returns **only `0.0f` or `80.0f`**:

```c
return window->bitsPerSample == 16 && _glfwGetWindowAdvancedColorEnabledWin32(window) ? 0.0f : 80.0f;
```

That is a flag, never a measurement -- and `0.0` means *unknown* in `DisplayColorSpace`'s vocabulary.
The SDR white level, by contrast, is real: the fork walks the display paths and calls
`DisplayConfigGetDeviceInfo` for it.

For a real ceiling: `IDXGIOutput6::GetDesc1` -> `DXGI_OUTPUT_DESC1`. No D3D device needed --
`CreateDXGIFactory1` -> `EnumAdapters` -> `EnumOutputs` -> QI for `IDXGIOutput6`, matching the
window's `HMONITOR`. Either in HDRView directly or upstreamed into the GLFW fork.

Two traps:

- `MaxLuminance` is *small-area peak*; `MaxFullFrameLuminance` is *sustained full-field*. On OLED
  these differ severalfold. Pick deliberately -- full-frame is the honest one for judging an image.
- Both come from EDID, which displays routinely overstate.

Note the ceiling is static but the SDR white level is user-adjustable, so headroom still moves live.

### Wayland -- plumbed, but currently reports nothing useful

The GLFW fork already does this correctly, and `query_display_colorspace()` already reads the result
into `m_display_cs.max_nits` / `.sdr_white_nits`. It takes the compositor's *preferred* image
description (`wp_color_management_surface_feedback_v1::get_preferred`) and reads the `luminances`
event. That is the right event: the protocol describes the alternative, `target_luminance`, as *"only
theoretical and [it] may not correspond to the luminance of light emitted on an actual display"* --
and the fork correctly leaves that handler empty.

**But measured on KDE Plasma / KWin 6.7.4, on an HDR-enabled display, it reports a headroom of 1.0:**

```
kscreen-doctor:  HDR: enabled -- SDR brightness: 80 nits
                 Peak brightness: 418 nits, overridden with: 1850 nits

HDRView:         Got a floating-point precision framebuffer.
                 Display color space is sRGB/BT.709 gamut with Linear transfer,
                 80 nits SDR white, 0.2-80 nits range (does not support HDR).
```

The fp16 buffer was granted and the transfer is `ext_linear`, so the extended path did engage. The
values are genuinely arriving from the compositor (a 0.2 nit minimum could not come from anywhere
else). KWin simply reports `max_lum == reference_lum == 80`.

Unresolved: whether KWin describes the preferred description this way by design, or whether GLFW is
asking the wrong question. Worth settling before trusting the Wayland number, but it does not block
the feature -- it degrades to "unknown/SDR" and draws nothing.

## Suggested order of work

Most of this is not platform-specific, and is testable on any machine.

1. **`display_headroom()` plus a manual override.** A `--headroom <x>` flag (and/or a debug control)
   that forces a value. Needed regardless: without it the feature cannot be exercised on Linux at all
   given the KWin finding above, and on macOS it is the only way to see the band move on demand
   rather than waiting for a thermal event.
2. **The histogram UI**, against that override. Pure ImPlot drawing -- fully verifiable without any
   HDR display.
3. **macOS `NSScreen` query.** Small and isolated; needs a Mac to compile and a real EDR display to
   confirm the number is sane and the band tracks the brightness slider.
4. **Windows DXGI query.** Independent of 3.
5. **Wayland investigation.** Independent of everything else.

## Open questions

- **Label format** for the headroom tag: multiplier (`8x`), stops (`+3`), or nits? The neighbouring
  tags read `"0"` and `"1"`, which argues for the multiplier.
- **Current vs. potential headroom on macOS.** The dynamic value is the truthful one, but it sags
  under thermal throttling and on battery, so the band will visibly drift while nothing about the
  image changed. A second, fainter tick at `maximumPotential...` would make that legible instead of
  mysterious. Needs a real display to judge.
- **Dim style beyond the ceiling.** The existing out-of-range dimming uses
  `DragRect(..., ImVec4(0,0,0,1.5), NoInputs | NoFit)`. Reusing it keeps the look consistent, but
  "beyond what the display can show" may warrant something distinct from "outside the exposure
  range".
