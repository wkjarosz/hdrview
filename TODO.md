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
- [x] native file dialogs on macOS that take keyboard focus: nativefiledialog-extended, in process, with xdg-desktop-portal on Linux
- [ ] write HEIF/AVIF gain maps (ISO 21496-1 `tmap`) on Export rendition, as Ultra HDR and JPEG XL do
  - libheif (1.23.3) has no API to write a gain map, or to read one: a `tmap` file needs a derived `tmap` item
    referencing the base and a hidden gain-map image item, and an `altr` entity group, none of which its API can
    create. Revisit when libheif adds one, rather than hand-building the boxes around its output.


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
