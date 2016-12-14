cmake_minimum_required (VERSION 2.8.3)
project(hdrview)

# prevent glfw from changing the directory on macOS bundles
SET(GLFW_USE_CHDIR OFF CACHE BOOL "Prevent glfwInit from chdir to Contents/Resources" FORCE)

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

# Resource file (icons etc.)
set(EXTRA_SOURCE "")
if (APPLE)
  set(EXTRA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.icns")
endif()

# The following lines build the main executable. If you add a source
# code file to Nori, be sure to include it in this list.
add_executable(HDRView MACOSX_BUNDLE
  src/ImageQuad.cpp
  src/ImageQuad.h
  src/pfm.h
  src/pfm.cpp
  src/ppm.h
  src/ppm.cpp
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
  ${EXTRA_SOURCE}
)

target_link_libraries(HDRView IlmImf nanogui ${NANOGUI_EXTRA_LIBS})
set_target_properties(HDRView PROPERTIES OUTPUT_NAME "HDRView")

if (APPLE)
  # Build an application bundle on OSX
  set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "HDRView")
  set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_BUNDLE_GUI_IDENTIFIER "com.im.HDRView")
  set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_ICON_FILE icon.icns)
  set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/MacOSXBundleInfo.plist.in)
  set_source_files_properties(resources/icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
else()
  # Insulate from a few types of ABI changes by statically linking against libgcc and libstdc++
  set_target_properties(HDRView PROPERTIES LINK_FLAGS "-static-libgcc")
endif()

# The following lines build the warping test application
add_executable(force-random-dither
  src/forced-random-dither.cpp
)

target_link_libraries(force-random-dither nanogui ${NANOGUI_EXTRA_LIBS})
