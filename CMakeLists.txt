cmake_minimum_required (VERSION 2.8.3)
project(hdrview)

add_subdirectory(ext ext_build)

include_directories(
  # Eigen linear algebra library
  ${EIGEN_INCLUDE_DIR}
  # OpenEXR high dynamic range bitmap library
  ${OPENEXR_INCLUDE_DIRS}
  # GLFW library for OpenGL context creation
  ${GLFW_INCLUDE_DIR}
  # GLEW library for accessing OpenGL functions
  ${GLEW_INCLUDE_DIR}
  # NanoVG drawing library
  ${NANOVG_INCLUDE_DIR}
  # NanoGUI user interface library
  ${NANOGUI_INCLUDE_DIR}
)

# The following lines build the main executable. If you add a source
# code file to Nori, be sure to include it in this list.
add_executable(hdrview #MACOSX_BUNDLE
  src/ImageQuad.cpp
  src/ImageQuad.h
  src/pfm.h
  src/pfm.cpp
  src/stb_image.h
  src/stb_image_write.h
  src/HDRViewer.h
  src/HDRViewer.cpp
  src/dither-matrix256.h
  src/FullScreenDitherer.h
  src/Color.h
  src/FloatImage.h
  src/FloatImage.cpp
  src/main.cpp
)

# The following lines build the warping test application
add_executable(force-random-dither
  src/forced-random-dither.cpp
)

target_link_libraries(hdrview IlmImf nanogui ${NANOGUI_EXTRA_LIBS})
target_link_libraries(force-random-dither nanogui ${NANOGUI_EXTRA_LIBS})
