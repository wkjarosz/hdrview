# TODO

##

- [x] sort image list
- [x] left vs. right align images in image list
- [x] have spdlog output to the log window
- [x] add image and channel list filtering
- [x] load images asynchronously
- [x] load multiple images/entire directories
  - [x] drop event to load files for GLFW
  - [x] multi-select on desktop
- [x] fix reference image index when closing image
- [ ] render pixel grid in the shader
- [x] improve statistics
  - [x] compute min/max/avg ignoring infinities, and report # of infinities
  - [x] account for blend mode when computing pixel hover (and statistics)
- [x] figure our threading while considering emscripten
- [x] selection support
  - [x] compute statistics also over selection
- [x] command palette/improved hotkey support
- [x] save as support for OpenEXR
- [ ] handle multi-view EXRs better
- [x] pinch-zoom and pan support in emscripten with SDL
- [ ] move shader, texture, renderpass and others into separate library
- [x] merge metal/glsl shaders + C++ colorspace stuff?
- [ ] express app/viewport/image coordinate transforms using 2x2 matrices
- [x] flip and mirror image view
- [ ] native file dialogs on macOS that take keyboard focus
  - portable-file-dialogs runs `osascript -e 'choose file name …'` in a child process, whose panel never becomes
    the active app, so keys typed right after it opens reach HDRView's shortcuts (a Right arrow switches the
    channel group). Affects every open/save/folder dialog.
  - keep this in a small library, not in HDRView. A fork tweak (`tell me to activate`, then refocusing HDRView) is
    only a workaround, since pfd's macOS design is the out-of-process AppleScript.
  - candidate replacement: [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended), which uses
    in-process NSOpenPanel/NSSavePanel on macOS and IFileDialog on Windows. On Linux it uses GTK3 or, with
    `NFD_PORTAL`, xdg-desktop-portal over D-Bus, a build dependency that pfd's zenity/kdialog child process avoids.
    The web build keeps emscripten-browser-file.


## Old

- [x] Improve responsiveness during long operations
   - [x] Add progress bars
   - [x] Run them in a separate thread and avoid freezing the main application
   - [x] Send texture data to GL in smaller tiles, across several re-draws to avoid stalling main app
   - [x] Allow canceling/aborting long operations
- [ ] Improved DNG/demosaicing pipeline
   - [ ] Improve DNG color correction
   - [x] Allow skipping DNG demosaicing during load (load only the "CFA" part via the channel selector)
   - [ ] Add demosaicing/color correction/white balancing post-load filters
   - [ ] Will require storing DNG metadata to apply correct color-correction matrix
- [x] Selection support
- [ ] More image filters/transformations/adjustments 
   - [x] Canvas size/cropping
   - [ ] White balance adjustment
   - [x] Brightness/contrast
   - [ ] Luminance/chromaticity denoising 
   - [ ] Levels
   - [x] Hue/Saturation
   - [x] Convert to grayscale/desaturate
   - [x] Invert
   - [ ] Equalize/normalize histogram
   - [ ] Match color/histogram matching
   - [ ] FFT-based convolution/blur
   - [ ] Motion blur
   - [x] Merge down/flatten layers
- [ ] Enable processing/filtering images passed on command-line even in GUI mode (e.g. load many images, blur them, and then display them in the GUI, possibly without saving)
- [ ] HDR merging
- [ ] HDR tonemapping
- [x] General image editing
   - [x] Clone stamp
   - [x] Airbrush
   - [x] Cropping
- [x] GUI improvements
   - [x] Add support for resizing side panel
   - [x] Allow error logging to output to a debug status panel in the GUI.
   - [x] Improved drop-down menus
   - [x] Save all
   - [x] Close all
   - [x] Show command history and allow undoing/redoing multiple steps at once
