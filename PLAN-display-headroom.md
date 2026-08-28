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

- **headroom unknown (0)** -- draw nothing new; behave exactly as today. Note this is also the state
  during the first second on Wayland, before the compositor's preferred description arrives, so the
  band appearing slightly after launch is expected rather than a bug.
- **headroom <= 1 (SDR display)** -- no HDR band. Consider labeling the `[0,1]` band `SDR` anyway, or
  suppressing both labels; decide when it can be seen on a real display.

## Platform research

Headroom is `max_luminance / SDR_white_luminance` everywhere except macOS, which reports the ratio
directly. Proposed single accessor: `float display_headroom()`, returning a multiple of SDR white,
`0` for unknown.

### macOS -- done

`src/display_headroom_cocoa.mm` reads `NSScreen.maximumExtendedDynamicRangeColorComponentValue` on the
*window's* screen (`glfwGetCocoaWindow(w).screen`), already in units of SDR white. `display_headroom()`
calls it directly rather than going through `m_display_cs`, which on macOS never leaves its defaults --
`update_colorpass()` is empty there, its whole body being under `#if defined(HELLOIMGUI_HAS_OPENGL)`.

Nothing existing gives us this:

- GLFW's Cocoa backend is a stub -- `_glfwGetWindowMaxLuminanceCocoa()` returns `0.0f`,
  `_glfwGetWindowSdrWhiteLevelCocoa()` returns `80.0f`.
- hello_imgui's `hasEdrSupport()` is a bool, and derives it from
  `maximumPotentialExtendedDynamicRangeColorComponentValue` across *all* screens -- the static
  ceiling, not the current value, and not per-window.

Queried live on every call, not cached: measured at **34 ns** (46 ns wrapped in an autoreleasepool), so
the handful of calls `draw_histogram()` makes per frame cost well under a microsecond. That also sidesteps
having to invent a per-frame refresh hook on the one platform that has none.

Gated on `m_float_buffer`. Without it we are drawing into a plain 8-bit sRGB layer, so white is the
ceiling whatever the panel could reach. The framebuffer is decided once at startup across *all* attached
screens (`hasEdrSupport()`), while the headroom is the current screen's alone -- which is what makes
dragging the window between an XDR panel and an SDR monitor change the band.

Measured on an M1 Max, macOS 26.5, three displays:

```
screen                        current  reference  potential
LG UltraFine                   1.0000     0.0000     1.0000
Built-in Retina Display        5.3056     0.0000    16.0000
Kuycon G27P                    1.0000     0.0000     1.0000
```

- **An SDR display reports 1.0, never 0.** So the "unknown" state does not arise on macOS the way it does
  on Wayland, and `has_hdr` correctly reads false on the externals: SDR band only, dimmed above white,
  exactly as on master. The degenerate case the plan left open is therefore just fine unlabeled-HDR.
- **`maximumReference...` is 0 here** -- it is only populated in an HDR reference preset, so it is not a
  usable fallback.

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

### Wayland -- works today

The GLFW fork already does this correctly, and `query_display_colorspace()` already reads the result
into `m_display_cs.max_nits` / `.sdr_white_nits`. It takes the compositor's *preferred* image
description (`wp_color_management_surface_feedback_v1::get_preferred`) and reads the `luminances`
event. That is the right event: the protocol describes the alternative, `target_luminance`, as *"only
theoretical and [it] may not correspond to the luminance of light emitted on an actual display"* --
and the fork correctly leaves that handler empty.

Measured on KDE Plasma / KWin 6.7.4 against an HDR display whose peak brightness is overridden to
1850 nits:

```
22:14:41  ... 80 nits SDR white, 0.2-80 nits range   (does not support HDR)
22:14:42  ... 80 nits SDR white, 0-1850 nits range   (colorpass active)
```

`max_nits` matches the display's configured peak exactly, so headroom is 1850/80 = **23.1x**, about
4.5 stops.

Two things to know when testing:

- **The real values arrive asynchronously**, roughly a second after startup, once the compositor
  delivers the preferred image description. The first line above is a transient: before it arrives,
  max and reference are both 80 and the display reads as SDR. Anything that samples headroom once at
  startup will latch that wrong value -- another reason to read it per frame, as `update_colorpass()`
  already does.
- **`max_nits` is fixed; `sdr_white_nits` moves with the monitor's brightness control.** So headroom
  changes via the denominator: turning brightness down raises it. This is exactly the behavior the
  band is meant to visualize, and it makes Wayland the most convenient platform to develop against --
  the band can be made to expand and contract with a brightness key.

## Suggested order of work

Most of this is not platform-specific, and is testable on any machine.

1. ~~**`display_headroom()`**~~ -- done, in `app-colorpass.cpp`, from `m_display_cs`.
2. ~~**The histogram UI.**~~ -- done, verified on Wayland against a live headroom.
3. ~~**macOS `NSScreen` query.**~~ -- done, in `display_headroom_cocoa.mm`, verified on an XDR panel
   and on two SDR externals.
4. **Windows DXGI query.** Independent of 3.

Steps 1 and 2 are fully verifiable on Wayland against a real, live-changing headroom, so the
platform-specific steps reduce to making macOS and Windows report the same number.

## Settled

- **Label format**: a multiplier, `6.25x`, in grey rather than white so it does not read as a third
  handle beside the black and white points. Band labels sit in a strip *above* the data rectangle,
  mirroring the tick labels below it -- inside the plot they were hard to read against the histogram,
  and the plot's top edge is already owned by the legend (`ImPlotLocation_North`) and the clip-warning
  toggles. ImPlot's `PlotPadding` covers all but ~2.5 px of that strip, which `draw_histogram()`
  reserves before `BeginPlot()`; the labels are drawn under a widened clip rect, since they reach past
  the frame ImPlot clips to.
- **Axis range**: `x_limits()` capped the asinh axis at 4x display white, so any real headroom put the
  ceiling permanently off the plot. Asinh now reaches `max(4x, headroom * 1.15)`; linear and sRGB are
  unchanged.
- **Dim style beyond the ceiling**: one style for both dimmed regions, the existing
  `DragRect(..., ImVec4(0,0,0,1.5), NoInputs | NoFit)`. The two edges do now mean different things --
  the left one is the black point the exposure put there, the right one is a limit of the hardware
  that no drag will move -- but a second dim style would cost more in visual noise than the
  distinction is worth, and the ceiling's own tag already marks that edge as something else. Revisit
  only if the shared appearance actually misleads in use.
- **Trusting the ceiling**: it is only as good as the peak the display reports. KDE writes a
  `maxPeakBrightnessOverride` in `~/.config/kwinoutputconfig.json` from its HDR calibration wizard
  (`/usr/bin/hdrcalibrator`), and a careless run leaves a value the panel cannot reach -- 1850 nits
  against an EDID peak of 418, here, until the wizard was re-run and it became 500. A ceiling far from
  where values visibly clip is a reason to suspect that key, not this code.

## Open questions

- **Current vs. potential headroom on macOS -- the drift is much bigger than expected.** The current
  value was measured collapsing from **5.31x to 1.0x in a smooth ~2 s ramp**, unprompted, on AC power
  with no system thermal warning (`pmset -g therm` clean, `UserIsActive 1`), while
  `maximumPotential...` sat rock-steady at 16.0 throughout. It then *stayed* at 1.0 across an app quit
  and relaunch and several minutes of polling. Brightness alone does not explain it: the panel was at
  381.8 nits SDR white against a 1600 nit peak (`ioreg -c AppleARMBacklight` ->
  `BrightnessMilliNits`), a ratio of 4.19, so EDR appears to be gated off outright rather than merely
  reduced.

  So on macOS the HDR band does not just drift -- it can vanish entirely and stay vanished, on hardware
  that is nominally 16x capable. Reporting the current value is still the *truthful* choice (when it
  reads 1.0 the display genuinely will not show you anything above white), but it is worth deciding
  whether a band that disappears for minutes at a time reads as informative or as broken. The obvious
  mitigations, in increasing order of dishonesty: a faint second tick at `maximumPotential...`; holding
  the band at a recent maximum with a decay; or using `maximumPotential...` outright.

  Root cause is unidentified and is Apple's; what is established is that it is real, large, abrupt, and
  sticky. Reproduce by putting an EDR window on an XDR panel and polling
  `maximumExtendedDynamicRangeColorComponentValue`.
