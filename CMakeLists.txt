cmake_minimum_required (VERSION 3.2)
project(hdrview)

set(HDRVIEW_VERSION "0.3")

# Set ourselves as the startup project in visual studio.
# Not available until cmake 3.6, but doesn't break older versions.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT hdrview)

add_subdirectory(ext ext_build)

include_directories(
  # tinyformat string formatting library
  ${TFM_INCLUDE_DIR}
  # Eigen linear algebra library
  SYSTEM ${EIGEN_INCLUDE_DIR}
  # OpenEXR high dynamic range bitmap library
  SYSTEM ${OPENEXR_INCLUDE_DIRS}
  # Pseudorandom number generator
  ${PCG32_INCLUDE_DIR}
  # Helper functions for statistical hypothesis tests
  ${HYPOTHESIS_INCLUDE_DIR}
  # GLFW library for OpenGL context creation
  SYSTEM ${GLFW_INCLUDE_DIR}
  # GLEW library for accessing OpenGL functions
  SYSTEM ${GLEW_INCLUDE_DIR}
  # NanoVG drawing library
  SYSTEM ${NANOVG_INCLUDE_DIR}
  # NanoGUI user interface library
  SYSTEM ${NANOGUI_INCLUDE_DIR}
  SYSTEM ${NANOGUI_EXTRA_INCS}
  # tinydir
  ${TINYDIR_INCLUDE_DIR}
  # docopt
  ${DOCOPT_INCLUDE_DIR}
  # spdlog
  ${SPDLOG_INCLUDE_DIR}
  # boost REGEX
  ${Boost_INCLUDE_DIRS}
  # stb
  ${STB_INCLUDE_DIR}
  # tinydngloader
  ${TINYDNG_INCLUDE_DIR}
)

# Resource file (icons etc.)
set(EXTRA_SOURCE "")
if (APPLE)
    set(EXTRA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.icns")
elseif(WIN32)
    set(EXTRA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.rc")
endif()


add_executable(HDRView
               src/async.h
               src/color.cpp
               src/color.h
               src/colorspace.cpp
               src/colorspace.h
               src/commandhistory.h
               src/common.cpp
               src/common.h
               src/dithermatrix256.h
               src/editimagepanel.cpp
               src/editimagepanel.h
               src/envmap.cpp
               src/envmap.h
               src/filmictonecurve.cpp
               src/filmictonecurve.h
               src/fwd.h
               src/xpuimage.cpp
               src/xpuimage.h
               src/hdrimage.cpp
               src/hdrimage.h
               src/hdrimageio.cpp
               src/hdrview.cpp
               src/hdrviewscreen.cpp
               src/hdrviewscreen.h
               src/helpwindow.cpp
               src/helpwindow.h
               src/hslgradient.cpp
               src/hslgradient.h
               src/imagebutton.cpp
               src/imagebutton.h
               src/imagelistpanel.cpp
               src/imagelistpanel.h
               src/imageview.cpp
               src/imageview.h
               src/multigraph.cpp
               src/multigraph.h
               src/parallelfor.cpp
               src/parallelfor.h
               src/pfm.h
               src/pfm.cpp
               src/ppm.h
               src/ppm.cpp
               src/progress.cpp
               src/progress.h
               src/range.h
               src/timer.h
               src/well.cpp
               src/well.h
               ${EXTRA_SOURCE})

set(HDRVIEW_DEFINITIONS -DHDRVIEW_VERSION="${HDRVIEW_VERSION}")
if (APPLE)
    # HDRVIEW is unlikely to switch away from openGL anytime soon
    set(HDRVIEW_DEFINITIONS ${HDRVIEW_DEFINITIONS} -DGL_SILENCE_DEPRECATION)
endif()

add_definitions(${HDRVIEW_DEFINITIONS} ${NANOGUI_EXTRA_DEFS})

set_target_properties(HDRView PROPERTIES OUTPUT_NAME "HDRView")

if (APPLE)
    # Build an application bundle on OSX
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE TRUE)
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "HDRView")
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_INFO_STRING "High dynamic range image viewer and comparison tool, version ${HDRVIEW_VERSION}.")
    STRING(TIMESTAMP YEAR "%Y")
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_COPYRIGHT "Copyright ${YEAR}, Wojciech Jarosz. Freely available under the BSD license.")
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_BUNDLE_GUI_IDENTIFIER "com.im.HDRView")
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_ICON_FILE icon.icns)
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_BUNDLE_VERSION ${HDRVIEW_VERSION})
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_SHORT_VERSION_STRING ${HDRVIEW_VERSION})
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_LONG_VERSION_STRING ${HDRVIEW_VERSION})
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/MacOSXBundleInfo.plist.in)
    set_target_properties(HDRView PROPERTIES RESOURCE resources/icon.icns)
    set_source_files_properties(resources/icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
elseif (NOT WIN32)
    # Insulate from a few types of ABI changes by statically linking against libgcc and libstdc++
    set_target_properties(HDRView PROPERTIES LINK_FLAGS "-static-libgcc")
endif()

if (UNIX AND NOT ${U_CMAKE_BUILD_TYPE} MATCHES DEBUG)
    add_custom_command(TARGET HDRView POST_BUILD COMMAND strip $<TARGET_FILE:HDRView>)
endif()


add_executable(hdrbatch
               src/color.cpp
               src/color.h
               src/colorspace.cpp
               src/colorspace.h
               src/common.cpp
               src/common.h
               src/envmap.cpp
               src/envmap.h
               src/dithermatrix256.h
               src/fwd.h
               src/hdrimage.cpp
               src/hdrimage.h
               src/hdrimageio.cpp
               src/hdrbatch.cpp
               src/parallelfor.cpp
               src/parallelfor.h
               src/pfm.cpp
               src/pfm.h
               src/ppm.cpp
               src/ppm.h
               src/progress.cpp
               src/progress.h
               src/range.h
               src/timer.h
               )


add_executable(force-random-dither
    src/forced-random-dither.cpp)               

target_link_libraries(HDRView ${OPENEXR_LIBS} nanogui docopt_s ${NANOGUI_EXTRA_LIBS} ${Boost_REGEX_LIBRARY})
target_link_libraries(hdrbatch ${OPENEXR_LIBS} docopt_s ${Boost_REGEX_LIBRARY})
target_link_libraries(force-random-dither nanogui ${NANOGUI_EXTRA_LIBS})

if (NOT ${CMAKE_VERSION} VERSION_LESS 3.3 AND IWYU)
    find_program(iwyu_path NAMES include-what-you-use iwyu)
    if (iwyu_path)
        set_property(TARGET HDRView hdrbatch force-random-dither PROPERTY CXX_INCLUDE_WHAT_YOU_USE ${iwyu_path})
    endif()
endif()

if (APPLE)
    install(TARGETS HDRView BUNDLE DESTINATION "/Applications")
    install(SCRIPT resources/osx-post-install.cmake)
elseif(WIN32)
    install(TARGETS HDRView RUNTIME DESTINATION "bin")
else()
    install(TARGETS HDRView RUNTIME DESTINATION "bin")
    install(FILES resources/hdrview.desktop DESTINATION "/usr/share/applications")
    install(FILES resources/icon-512.png DESTINATION "/usr/share/icons/hicolor/1024x1024/apps" RENAME hdrview.png)
    install(FILES resources/icon-512.png DESTINATION "/usr/share/icons/hicolor/512x512/apps" RENAME hdrview.png)
    install(FILES resources/icon-256.png DESTINATION "/usr/share/icons/hicolor/256x256/apps" RENAME hdrview.png)
    install(FILES resources/icon-128.png DESTINATION "/usr/share/icons/hicolor/128x128/apps" RENAME hdrview.png)
    install(FILES resources/icon-96.png DESTINATION "/usr/share/icons/hicolor/96x96/apps" RENAME hdrview.png)
    install(FILES resources/icon-64.png DESTINATION "/usr/share/icons/hicolor/64x64/apps" RENAME hdrview.png)
    install(FILES resources/icon-48.png DESTINATION "/usr/share/icons/hicolor/48x48/apps" RENAME hdrview.png)
    install(FILES resources/icon-32.png DESTINATION "/usr/share/icons/hicolor/32x32/apps" RENAME hdrview.png)
    install(FILES resources/icon-24.png DESTINATION "/usr/share/icons/hicolor/24x24/apps" RENAME hdrview.png)
    install(FILES resources/icon-16.png DESTINATION "/usr/share/icons/hicolor/16x16/apps" RENAME hdrview.png)
endif()


# Force colored output for the ninja generator
if (CMAKE_GENERATOR STREQUAL "Ninja")
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fcolor-diagnostics")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcolor-diagnostics")
  elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fdiagnostics-color=always")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always")
  endif()
endif()

target_compile_features(HDRView PRIVATE cxx_std_17)
# target_compile_features(hdrbatch PRIVATE cxx_std_17)