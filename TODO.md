# TODO

##

- [x] have spdlog output to the log window
- [ ] load multiplate images/entire directories
- [ ] fix reference image index when closing image
- [ ] render pixel grid in the shader
- [ ] figure out how to better handle channels dropdown with channel groups and arbitrary channels
- [ ] improve statistics
  - [x] compute min/max/avg ignoring infinities, and report # of infinities
  - [ ] compute statistics also over selection
  - [ ] account for blend mode when computing pixel hover (and statistics)
- [ ] figure our threading while considering emscripten
- [ ] selection support
- [ ] command palette/improved hotkey support
- [ ] save as support for OpenEXR
- [ ] handle multi-view EXRs better
- [ ] pinch-zoom and pan support in emscripten with SDL
- [ ] move shader, texture, renderpass and others into separate library
- [ ] merge metal/glsl shaders + C++ colorspace stuff?


## Old

- [ ] Improve responsiveness during long operations
   - [x] Add progress bars
   - [x] Run them in a separate thread and avoid freezing the main application
   - [x] Send texture data to GL in smaller tiles, across several re-draws to avoid stalling main app
   - [ ] Allow canceling/aborting long operations
- [ ] Improved DNG/demosaicing pipeline
   - [ ] Improve DNG color correction
   - [ ] Allow skipping DNG demosaicing during load
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
- [ ] GUI improvements
   - [x] Add support for resizing side panel
   - [ ] Allow error logging to output to a debug status panel in the GUI.
   - [x] Improved drop-down menus
   - [x] Save all
   - [x] Close all
   - [ ] Show command history and allow undoing/redoing multiple steps at once
