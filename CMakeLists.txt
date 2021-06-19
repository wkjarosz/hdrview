cmake_minimum_required (VERSION 3.3)
project(hdrview)

set(HDRVIEW_VERSION "1.2.0")

# set(USE_SANITIZER "Address")
# set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/ext/cmake" ${CMAKE_MODULE_PATH})
# include(sanitizers)

# Set ourselves as the startup project in visual studio.
# Not available until cmake 3.6, but doesn't break older versions.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT hdrview)

add_subdirectory(ext ext_build)

include_directories(
  # tinyformat string formatting library
  ${TFM_INCLUDE_DIR}
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
  # cfgpath
  ${CFGPATH_INCLUDE_DIR}
  # stb
  ${STB_INCLUDE_DIR}
  # tinydngloader
  ${TINYDNG_INCLUDE_DIR}
  # alphanum
  ${ALPHANUM_INCLUDE_DIR}
)

# Resource file (icons etc.)
set(EXTRA_SOURCE "")
if (APPLE)
    set(EXTRA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.icns")
elseif(WIN32)
    set(EXTRA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.rc")
endif()


if (NANOGUI_BACKEND STREQUAL "OpenGL")
  set(RESOURCE_GLOB "resources/*.gl")
  message(STATUS "Using OpenGL backend.")
elseif (NANOGUI_BACKEND STREQUAL "GLES 2")
  set(RESOURCE_GLOB "resources/*.gles")
  message(STATUS "Using GLES 2 backend.")
elseif (NANOGUI_BACKEND STREQUAL "GLES 3")
  set(RESOURCE_GLOB "resources/*.gles")
  message(STATUS "Using GLES 3 backend.")
elseif (NANOGUI_BACKEND STREQUAL "Metal")
  set(RESOURCE_GLOB "resources/*.metal")
  message(STATUS "Using Metal backend.")
endif()

# Run simple cmake converter to put shader files into the data segment

# Glob up resource files
file(GLOB resources "${RESOURCE_GLOB}")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/resources")

# Precompile .metal shaders to .metallib files
foreach(fname_in IN LISTS resources)
  if (NANOGUI_BACKEND STREQUAL "Metal" AND fname_in MATCHES "\\.metal")
    get_filename_component(fname_out ${fname_in} NAME)
    set(fname_out "${CMAKE_CURRENT_BINARY_DIR}/resources/${fname_out}lib")
    add_custom_command(
        OUTPUT ${fname_out}
        DEPENDS ${fname_in}
        COMMAND xcrun -sdk macosx metal -std=osx-metal2.0 -O3 "${fname_in}" -o "${fname_out}"
        VERBATIM
    )
  else()
    set(fname_out "${fname_in}")
  endif()
  list(APPEND resources_processed ${fname_out})
endforeach()


file(GLOB patterns "${CMAKE_CURRENT_SOURCE_DIR}/resources/patterns/*.png")
list (APPEND resources_processed ${patterns})

# Concatenate resource files into a comma separated string
string(REGEX REPLACE "([^\\]|^);" "\\1," resources_string "${resources_processed}")
string(REGEX REPLACE "[\\](.)" "\\1" resources_string "${resources_string}")

# Create command line for running bin2c cmake script
set(bin2c_cmdline
  -DOUTPUT_C=hdrview_resources.cpp
  -DOUTPUT_H=hdrview_resources.h
  "-DINPUT_FILES=${resources_string}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/resources/bin2c.cmake")


# Run bin2c on resource files
add_custom_command(
  OUTPUT hdrview_resources.cpp hdrview_resources.h
  COMMAND ${CMAKE_COMMAND} ARGS ${bin2c_cmdline}
  DEPENDS ${resources} ${resources_processed}
  COMMENT "Running bin2c"
  PRE_BUILD VERBATIM)

# Needed to generated files
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Add a custom command that produces hdrview_version.cpp, plus
# a dummy output that's not actually produced, in order
# to force version.cmake to always be re-run before the build
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hdrview_version.cpp
          #  ${CMAKE_CURRENT_BINARY_DIR}/_hdrview_version.cpp
    COMMAND ${CMAKE_COMMAND} -P
          ${CMAKE_CURRENT_SOURCE_DIR}/resources/version.cmake
    COMMENT "Generating timestamp and git version info")

# add_custom_target(version_info
#   COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/resources/version.cmake
#   ADD_DEPENDENCIES ${CMAKE_CURRENT_SOURCE_DIR}/resources/version.cmake
#   COMMENT "Generating timestamp and git version info")

add_executable(HDRView
  hdrview_version.cpp
  hdrview_resources.cpp
  src/async.h
  src/box.h
  src/brush.cpp
  src/brush.h
  src/color.h
  src/colorslider.cpp
  src/colorslider.h
  src/colorspace.cpp
  src/colorspace.h
  src/colorwheel.cpp
  src/colorwheel.h
  src/commandhistory.h
  src/common.cpp
  src/common.h
  src/dialog.cpp
  src/dialog.h
  src/dithermatrix256.h
  src/drawline.cpp
  src/drawline.h
  src/dropdown.cpp
  src/dropdown.h
  src/editimagepanel.cpp
  src/editimagepanel.h
  src/envmap.cpp
  src/envmap.h
  src/filmictonecurve.cpp
  src/filmictonecurve.h
  src/fwd.h
  src/xpuimage.cpp
  src/xpuimage.h
  src/hdrcolorpicker.cpp
  src/hdrcolorpicker.h
  src/hdrimage.cpp
  src/hdrimage.h
  src/hdrimagedngio.cpp
  src/hdrimageio.cpp
  src/hdrimageraw.cpp
  src/hdrimageraw.h
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
  src/hdrimageview.cpp
  src/hdrimageview.h
  src/multigraph.cpp
  src/multigraph.h
  src/parallelfor.cpp
  src/parallelfor.h
  src/pfm.h
  src/pfm.cpp
  src/popupmenu.cpp
  src/popupmenu.h
  src/ppm.h
  src/ppm.cpp
  src/progress.cpp
  src/progress.h
  src/range.h
  src/timer.h
  src/tool.cpp
  src/tool.h
  src/well.cpp
  src/well.h
  ${EXTRA_SOURCE})

# add_dependencies(HDRView version_info)


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
  hdrview_version.cpp
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
  src/hdrimagedngio.cpp
  src/hdrimageio.cpp
  src/hdrimageraw.cpp
  src/hdrimageraw.h
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

# add_dependencies(hdrbatch version_info)

add_executable(force-random-dither
    src/forced-random-dither.cpp)          
    

# set_target_properties(force-random-dither PROPERTIES CXX_VISIBILITY_PRESET hidden)

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
    install(TARGETS HDRView BUNDLE DESTINATION "/Applications" RESOURCE DESTINATION "/Applications")
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
target_compile_features(hdrbatch PRIVATE cxx_std_17)
