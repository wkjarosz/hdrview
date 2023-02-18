cmake_minimum_required(VERSION 3.13)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/" "${CMAKE_SOURCE_DIR}/resources")

include(VersionFromGit)

string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M")
message(STATUS "Saving build timestamp: ${BUILD_TIME}")

version_from_git(LOG ON TIMESTAMP "%Y-%m-%d-%H:%M:%S")

project(
  hdrview
  DESCRIPTION
    "A simple research-oriented image viewer with an emphasis on examining and comparing high-dynamic range (HDR) images."
  VERSION ${VERSION}
  LANGUAGES C CXX)

set(HDRVIEW_VERSION "${GIT_DESCRIBE}")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(HDRVIEW_VERSION "${HDRVIEW_VERSION} (64 bit)")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(HDRVIEW_VERSION "${HDRVIEW_VERSION} (32 bit)")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/common.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/common.cpp @ONLY)

include(sanitizers)

# Set ourselves as the startup project in visual studio. Not available until cmake 3.6, but doesn't break older
# versions.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT HDRView)

set(USE_IWYU OFF CACHE BOOL "INCLUDE WHAT YOU USE")

include(CheckCXXCompilerFlag)

# ============================================================================
# Set a default build configuration (Release)
# ============================================================================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" U_CMAKE_BUILD_TYPE)

# ============================================================================
# Enable folders for projects in Visual Studio
# ============================================================================
if(CMAKE_GENERATOR MATCHES "Visual Studio")
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

if(APPLE)
  set(CMAKE_MACOSX_RPATH ON)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  if("cxx_std_17" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    message(STATUS "Using C++17 standard")
    set(CMAKE_CXX_STANDARD 17 CACHE STRING "The C++ standard to use")
  elseif("cxx_std_14" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    message(STATUS "Using C++14 standard")
    set(CMAKE_CXX_STANDARD 14 CACHE STRING "The C++ standard to use")
  elseif("cxx_std_11" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    message(STATUS "Using C++11 standard")
    set(CMAKE_CXX_STANDARD 11 CACHE STRING "The C++ standard to use")
  else()
    message(FATAL_ERROR "Unsupported compiler -- HDRView requires at least C++11!")
  endif()

  # Prefer libc++ in conjunction with Clang
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    check_cxx_compiler_flag("-stdlib=libc++" HAS_LIBCPP)
    if(HAS_LIBCPP)
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -D_LIBCPP_VERSION")
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
      set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -stdlib=libc++")
      message(STATUS "HDRView: using libc++.")
    else()
      message(
        WARNING
          "libc++ is recommended in conjunction with clang. Please insteall the libc++ development headers, provided e.g. by the packages 'libc++-dev' and 'libc++abi-dev' on Debian/Ubuntu."
      )
    endif()
  endif()

  # Enable link time optimization and set the default symbol visibility to hidden (very important to obtain small
  # binaries)
  if(NOT ${U_CMAKE_BUILD_TYPE} MATCHES DEBUG)
    # Default symbol visibility
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
    # set(CMAKE_CXX_VISIBILITY_PRESET hidden)
  endif()

  # Disable specific GCC 7 warnings
  if(CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 7.0)
    set(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations -Wno-misleading-indentation -Wformat-truncation=0 -Wno-int-in-bool-context -Wimplicit-fallthrough=0"
    )
  endif()
endif()

# ============================================================================
# Sanitize build environment for static build with C++11/17
# ============================================================================
if(MSVC)
  # Disable annoying secure CRT warnings
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /D_CRT_SECURE_NO_WARNINGS")

  # Parallel build on MSVC (all targets)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")

  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:SSE2")
  endif()

  # Static build
  set(CompilerFlags
      CMAKE_CXX_FLAGS
      CMAKE_CXX_FLAGS_DEBUG
      CMAKE_CXX_FLAGS_RELEASE
      CMAKE_CXX_FLAGS_MINSIZEREL
      CMAKE_CXX_FLAGS_RELWITHDEBINFO
      CMAKE_C_FLAGS
      CMAKE_C_FLAGS_DEBUG
      CMAKE_C_FLAGS_RELEASE
      CMAKE_C_FLAGS_MINSIZEREL
      CMAKE_C_FLAGS_RELWITHDEBINFO)
  foreach(CompilerFlag ${CompilerFlags})
    string(REPLACE "/MD" "/MT" ${CompilerFlag} "${${CompilerFlag}}")
  endforeach()
endif()

# ============================================================================
# Add dependencies via CPM (cmake/tools.cmake includes cmake/CPM.cmake)
#
# see https://github.com/TheLartians/CPM.cmake for more info
# ============================================================================
include(cmake/CPM.cmake)

cpmaddpackage("gh:wkjarosz/filesystem#5e0eb2a6160201de38d076c085641742ee86a8f6")
if(filesystem_ADDED)
  add_library(filesystem INTERFACE IMPORTED)
  target_include_directories(filesystem INTERFACE "${filesystem_SOURCE_DIR}")
endif()

cpmaddpackage("gh:nothings/stb#a0a939058c579ddefd4c5671b046f29d12aeae01")
if(stb_ADDED)
  add_library(stb INTERFACE IMPORTED)
  target_include_directories(stb INTERFACE "${stb_SOURCE_DIR}")
endif()

cpmaddpackage("gh:cxong/tinydir#9f866c1ec09e62aa4df50d7209556ed705a4dd90")
if(tinydir_ADDED)
  add_library(tinydir INTERFACE IMPORTED)
  target_include_directories(tinydir INTERFACE "${tinydir_SOURCE_DIR}")
endif()

cpmaddpackage(
  NAME
  nlohmann_json
  VERSION
  3.9.1 # the git repo is incredibly large, so we download the archived include directory
  URL
  https://github.com/nlohmann/json/releases/download/v3.9.1/include.zip
  URL_HASH
  SHA256=6bea5877b1541d353bd77bdfbdb2696333ae5ed8f9e8cc22df657192218cad91
  OPTIONS
  "JSON_MultipleHeaders ON")
if(nlohmann_json_ADDED)
  add_library(nlohmann_json INTERFACE IMPORTED)
  target_include_directories(nlohmann_json INTERFACE ${nlohmann_json_SOURCE_DIR}/include)
endif()

cpmaddpackage("gh:syoyo/tinydng#96eea08c8ff4b18cae2f10028acc1dcda76873a6")
if(tinydng_ADDED)
  add_library(tinydng INTERFACE IMPORTED)
  target_include_directories(tinydng INTERFACE "${tinydng_SOURCE_DIR}")
endif()

cpmaddpackage(NAME platform_folders GITHUB_REPOSITORY sago007/PlatformFolders GIT_TAG 4.2.0)
if(platform_folders_ADDED)
  target_include_directories(platform_folders INTERFACE "${platform_folders_SOURCE_DIR}")
endif()

cpmaddpackage(NAME cli11 URL https://github.com/CLIUtils/CLI11/archive/v2.2.0.zip DOWNLOAD_ONLY YES)
if(cli11_ADDED)
  add_library(cli11 INTERFACE IMPORTED)
  target_include_directories(cli11 INTERFACE "${cli11_SOURCE_DIR}/include")
endif()

cpmaddpackage("gh:gabime/spdlog@1.9.2")

cpmaddpackage(
  NAME
  Imath
  VERSION
  3.1.6 # the git repo is incredibly large, so we download the archived include directory
  URL
  https://github.com/AcademySoftwareFoundation/Imath/archive/refs/tags/v3.1.6.zip
  OPTIONS
  "BUILD_SHARED_LIBS OFF"
  "BUILD_TESTING OFF"
  "EXCLUDE_FROM_ALL YES")

cpmaddpackage(
  NAME
  openexr
  GITHUB_REPOSITORY
  AcademySoftwareFoundation/openexr
  GIT_TAG
  71bffa3d7d89e46a37108732f5e63d554c10e018
  OPTIONS
  "OPENEXR_BUILD_TOOLS OFF"
  "BUILD_SHARED_LIBS OFF"
  "OPENEXR_INSTALL_EXAMPLES OFF"
  "BUILD_TESTING OFF"
  "EXCLUDE_FROM_ALL YES")

cpmaddpackage(
  NAME
  nanogui
  GITHUB_REPOSITORY
  Tom94/nanogui-1
  GIT_TAG
  cf8baf7c2a02a0e8443379a6b80f6709139f84a0
  OPTIONS
  "NANOGUI_BUILD_EXAMPLES OFF"
  "NANOGUI_BUILD_SHARED OFF"
  "NANOGUI_BUILD_PYTHON OFF"
  "NANOGUI_INSTALL OFF"
  "EXCLUDE_FROM_ALL YES")
if(nanogui_ADDED)
  add_library(nanogui::nanogui INTERFACE IMPORTED)
  target_include_directories(
    nanogui::nanogui SYSTEM INTERFACE ${nanogui_SOURCE_DIR}/ext/glfw/include ${nanogui_SOURCE_DIR}/ext/nanovg/src
                                      ${nanogui_SOURCE_DIR}/include ${NANOGUI_EXTRA_INCS})
  target_link_libraries(nanogui::nanogui INTERFACE "nanogui")
  target_compile_definitions(nanogui::nanogui INTERFACE "${NANOGUI_BACKEND_DEFS}")
endif()

# ============================================================================
# Compile remainder of the codebase with compiler warnings turned on
# ============================================================================
if(MSVC)
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} -Wno-gnu-anonymous-struct -Wno-c99-extensions -Wno-nested-anon-types -Wno-deprecated-register"
    )
  endif()
endif()

include_directories(${CMAKE_SOURCE_DIR}/ext/alphanum ${CMAKE_SOURCE_DIR}/src)

# Resource file (icons etc.)
set(EXTRA_SOURCE "")
if(APPLE)
  set(EXTRA_SOURCE "${CMAKE_SOURCE_DIR}/resources/icon.icns")
elseif(WIN32)
  set(EXTRA_SOURCE "${CMAKE_SOURCE_DIR}/resources/icon.rc")
endif()

if(NANOGUI_BACKEND STREQUAL "OpenGL")
  set(RESOURCE_GLOB "resources/*.gl")
elseif(NANOGUI_BACKEND STREQUAL "GLES 2")
  set(RESOURCE_GLOB "resources/*.gles")
elseif(NANOGUI_BACKEND STREQUAL "GLES 3")
  set(RESOURCE_GLOB "resources/*.gles")
elseif(NANOGUI_BACKEND STREQUAL "Metal")
  set(RESOURCE_GLOB "resources/*.metal")
endif()

# Run simple cmake converter to put shader files into the data segment

# Glob up resource files
file(GLOB resources "${RESOURCE_GLOB}")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/resources")

# Precompile .metal shaders to .metallib files
foreach(fname_in IN LISTS resources)
  if(NANOGUI_BACKEND STREQUAL "Metal" AND fname_in MATCHES "\\.metal")
    get_filename_component(fname_out ${fname_in} NAME)
    set(fname_out "${CMAKE_CURRENT_BINARY_DIR}/resources/${fname_out}lib")
    add_custom_command(OUTPUT ${fname_out} DEPENDS ${fname_in} COMMAND xcrun -sdk macosx metal -std=osx-metal2.0 -O3
                                                                       "${fname_in}" -o "${fname_out}" VERBATIM)
  else()
    set(fname_out "${fname_in}")
  endif()
  list(APPEND resources_processed ${fname_out})
endforeach()

file(GLOB patterns "${CMAKE_SOURCE_DIR}/resources/patterns/*.png")
list(APPEND resources_processed ${patterns})

# Concatenate resource files into a comma separated string
string(REGEX REPLACE "([^\\]|^);" "\\1," resources_string "${resources_processed}")
string(REGEX REPLACE "[\\](.)" "\\1" resources_string "${resources_string}")

# Create command line for running bin2c cmake script
set(bin2c_cmdline -DOUTPUT_C=hdrview_resources.cpp -DOUTPUT_H=hdrview_resources.h "-DINPUT_FILES=${resources_string}"
                  -P "${CMAKE_SOURCE_DIR}/resources/bin2c.cmake")

# Run bin2c on resource files
add_custom_command(
  OUTPUT hdrview_resources.cpp hdrview_resources.h
  COMMAND ${CMAKE_COMMAND} ARGS ${bin2c_cmdline}
  DEPENDS ${resources} ${resources_processed}
  COMMENT "Running bin2c" PRE_BUILD
  VERBATIM)

# Needed to access generated files
include_directories(${CMAKE_CURRENT_BINARY_DIR})

if(WIN32)
  set(HDRVIEW_DEFINITIONS ${HDRVIEW_DEFINITIONS} -DNOMINMAX)
endif()

add_definitions(${HDRVIEW_DEFINITIONS})

add_executable(
  HDRView
  hdrview_resources.cpp
  src/filters/bilateral.cpp
  src/filters/box_blur.cpp
  src/filters/brightness_contrast.cpp
  src/filters/canvas_size.cpp
  src/filters/channel_mixer.cpp
  src/filters/colorspace.cpp
  src/filters/exposure.cpp
  src/filters/fill.cpp
  src/filters/filmic.cpp
  src/filters/filters.cpp
  src/filters/filters.h
  src/filters/flatten.cpp
  src/filters/free_xform.cpp
  src/filters/gaussian.cpp
  src/filters/hsl.cpp
  src/filters/median.cpp
  src/filters/remap.cpp
  src/filters/resize.cpp
  src/filters/shift.cpp
  src/filters/unsharp_mask.cpp
  src/filters/zap_gremlins.cpp
  src/async.h
  src/box.h
  src/brush.cpp
  src/brush.h
  src/cliformatter.h
  src/color.h
  src/colorslider.cpp
  src/colorslider.h
  src/colorspace.cpp
  src/colorspace.h
  src/colorwheel.cpp
  src/colorwheel.h
  src/commandhistory.h
  common.cpp
  src/common.h
  src/dialog.cpp
  src/dialog.h
  src/dithermatrix256.h
  src/envmap.cpp
  src/envmap.h
  src/filmictonecurve.cpp
  src/filmictonecurve.h
  src/fwd.h
  src/hdrcolorpicker.cpp
  src/hdrcolorpicker.h
  src/hdrimage.cpp
  src/hdrimage.h
  src/hdrimagedngio.cpp
  src/hdrimageio.cpp
  src/hdrimageraw.cpp
  src/hdrimageraw.h
  src/hdrimageview.cpp
  src/hdrimageview.h
  src/hdrview.cpp
  src/hdrviewscreen.cpp
  src/hdrviewscreen.h
  src/helpwindow.cpp
  src/helpwindow.h
  src/hscrollpanel.cpp
  src/hscrollpanel.h
  src/hslgradient.cpp
  src/hslgradient.h
  src/imagebutton.cpp
  src/imagebutton.h
  src/imagelistpanel.cpp
  src/imagelistpanel.h
  src/json.h
  src/menu.cpp
  src/menu.h
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
  src/rasterdraw.cpp
  src/rasterdraw.h
  src/timer.h
  src/searchbox.cpp
  src/searchbox.h
  src/tool.cpp
  src/tool.h
  src/well.cpp
  src/well.h
  src/xpuimage.cpp
  src/xpuimage.h
  ${EXTRA_SOURCE})

set_target_properties(HDRView PROPERTIES OUTPUT_NAME "HDRView")

add_executable(
  hdrbatch
  src/cliformatter.h
  src/color.h
  src/colorspace.cpp
  src/colorspace.h
  common.cpp
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
  src/json.h
  src/parallelfor.cpp
  src/parallelfor.h
  src/pfm.cpp
  src/pfm.h
  src/ppm.cpp
  src/ppm.h
  src/progress.cpp
  src/progress.h
  src/range.h
  src/timer.h)

add_executable(force-random-dither src/forced-random-dither.cpp)

target_link_libraries(
  HDRView
  PRIVATE stb
          nanogui::nanogui
          OpenEXR::OpenEXR
          Imath::Imath
          cli11
          platform_folders
          filesystem
          tinydir
          nlohmann_json
          tinydng
          spdlog::spdlog)
target_link_libraries(
  hdrbatch
  PRIVATE stb
          nanogui::nanogui
          OpenEXR::OpenEXR
          Imath::Imath
          cli11
          platform_folders
          filesystem
          tinydir
          nlohmann_json
          tinydng
          spdlog::spdlog)
target_link_libraries(
  force-random-dither
  PRIVATE stb
          nanogui::nanogui
          filesystem
          tinydir
          nlohmann_json
          tinydng
          spdlog::spdlog)

if(NOT ${CMAKE_VERSION} VERSION_LESS 3.3 AND USE_IWYU)
  find_program(iwyu_path NAMES include-what-you-use iwyu)
  message("Found iwyu: ${iwyu_path}")
  if(iwyu_path)
    set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE ${IWYU_PATH})
    set(CMAKE_C_INCLUDE_WHAT_YOU_USE ${IWYU_PATH})
    set_property(TARGET HDRView hdrbatch force-random-dither PROPERTY CXX_INCLUDE_WHAT_YOU_USE ${iwyu_path})
  endif()
endif()

if(APPLE)
  string(TIMESTAMP YEAR "%Y")
  # Build an application bundle on OSX
  set_target_properties(
    HDRView
    PROPERTIES MACOSX_BUNDLE TRUE
               MACOSX_BUNDLE_BUNDLE_NAME "HDRView"
               MACOSX_BUNDLE_BUNDLE_VERSION ${HDRVIEW_VERSION}
               MACOSX_BUNDLE_INFO_STRING
               "High dynamic range image viewer and comparison tool, version ${HDRVIEW_VERSION}."
               MACOSX_BUNDLE_COPYRIGHT "Copyright ${YEAR}, Wojciech Jarosz. Freely available under the BSD license."
               MACOSX_BUNDLE_GUI_IDENTIFIER "com.im.HDRView"
               MACOSX_BUNDLE_ICON_FILE icon.icns
               MACOSX_BUNDLE_SHORT_VERSION_STRING ${HDRVIEW_VERSION}
               MACOSX_BUNDLE_LONG_VERSION_STRING ${HDRVIEW_VERSION}
               MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/MacOSXBundleInfo.plist.in
               RESOURCE resources/icon.icns)
  set_source_files_properties(resources/icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

  # build HDRView first so we can build the hdrbatch executable directly into the HDRView bundle on macOS
  add_dependencies(hdrbatch HDRView)

  # put the hdrbatch executable into the HDRView bundle
  add_custom_command(
    TARGET hdrbatch
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/HDRView.app/Contents/Resources/bin/
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:hdrbatch> ${PROJECT_BINARY_DIR}/HDRView.app/Contents/Resources/bin/
    COMMENT "Adding hdrbatch into HDRView bundle")
  # set_target_properties(hdrbatch PROPERTIES RUNTIME_OUTPUT_DIRECTORY
  # ${PROJECT_BINARY_DIR}/HDRView.app/Contents/Resources/bin/)
elseif(NOT WIN32)
  # Insulate from a few types of ABI changes by statically linking against libgcc and libstdc++
  set_target_properties(HDRView PROPERTIES LINK_FLAGS "-static-libgcc")
  set_target_properties(hdrbatch PROPERTIES LINK_FLAGS "-static-libgcc")
endif()

if(UNIX AND NOT ${U_CMAKE_BUILD_TYPE} MATCHES DEBUG)
  add_custom_command(TARGET HDRView POST_BUILD COMMAND strip $<TARGET_FILE:HDRView>)
endif()

if(APPLE)
  install(TARGETS HDRView BUNDLE DESTINATION "/Applications")
  install(SCRIPT cmake/osx-post-install.cmake)
elseif(WIN32)
  install(TARGETS HDRView RUNTIME DESTINATION "bin")
  install(TARGETS hdrbatch RUNTIME DESTINATION "bin")
else()
  install(TARGETS HDRView RUNTIME DESTINATION "bin")
  install(TARGETS hdrbatch RUNTIME DESTINATION "bin")
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
if(CMAKE_GENERATOR STREQUAL "Ninja")
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fcolor-diagnostics")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcolor-diagnostics")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fdiagnostics-color=always")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always")
  endif()
endif()

target_compile_features(HDRView PRIVATE cxx_std_17)
target_compile_features(hdrbatch PRIVATE cxx_std_17)
