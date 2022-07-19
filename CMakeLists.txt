cmake_minimum_required(VERSION 3.3)
project(hdrview)

set(HDRVIEW_VERSION "1.2.0")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake/" "${CMAKE_SOURCE_DIR}/resources")
include(sanitizers)

# Set ourselves as the startup project in visual studio.
# Not available until cmake 3.6, but doesn't break older versions.
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

CPMAddPackage("gh:wkjarosz/filesystem#5e0eb2a6160201de38d076c085641742ee86a8f6")
if(filesystem_ADDED)
  add_library(filesystem INTERFACE IMPORTED)
  target_include_directories(filesystem INTERFACE "${filesystem_SOURCE_DIR}")
endif()

CPMAddPackage("gh:nothings/stb#a0a939058c579ddefd4c5671b046f29d12aeae01")
if(stb_ADDED)
  add_library(stb INTERFACE IMPORTED)
  target_include_directories(stb INTERFACE "${stb_SOURCE_DIR}")
endif()

CPMAddPackage("gh:cxong/tinydir#9f866c1ec09e62aa4df50d7209556ed705a4dd90")
if(tinydir_ADDED)
  add_library(tinydir INTERFACE IMPORTED)
  target_include_directories(tinydir INTERFACE "${tinydir_SOURCE_DIR}")
endif()

CPMAddPackage(
  NAME nlohmann_json
  VERSION 3.9.1 # the git repo is incredibly large, so we download the archived include directory
  URL https://github.com/nlohmann/json/releases/download/v3.9.1/include.zip
  URL_HASH SHA256=6bea5877b1541d353bd77bdfbdb2696333ae5ed8f9e8cc22df657192218cad91
  OPTIONS "JSON_MultipleHeaders ON"
)
if(nlohmann_json_ADDED)
  add_library(nlohmann_json INTERFACE IMPORTED)
  target_include_directories(nlohmann_json INTERFACE ${nlohmann_json_SOURCE_DIR}/include)
endif()

CPMAddPackage("gh:syoyo/tinydng#96eea08c8ff4b18cae2f10028acc1dcda76873a6")
if(tinydng_ADDED)
  add_library(tinydng INTERFACE IMPORTED)
  target_include_directories(tinydng INTERFACE "${tinydng_SOURCE_DIR}")
endif()

CPMAddPackage(
  NAME platform_folders
  GITHUB_REPOSITORY sago007/PlatformFolders
  GIT_TAG 4.2.0
)
if(platform_folders_ADDED)
  target_include_directories(platform_folders INTERFACE "${platform_folders_SOURCE_DIR}")
endif()

CPMAddPackage("gh:docopt/docopt.cpp#400e6dd8e59196c914dcc2c56caf7dae7efa5eb3")
if(docopt_ADDED)
  if(WIN32)
    CPMAddPackage(
      NAME Boost
      VERSION 1.73.0
      GITHUB_REPOSITORY "boostorg/boost"
      GIT_TAG "boost-1.73.0"
    )
    target_link_libraries(docopt INTERFACE Boost::regex)
  endif()
endif()


CPMAddPackage(
  NAME openexr
  GITHUB_REPOSITORY mitsuba-renderer/openexr
  GIT_TAG dbabb6f9500ee628c1faba21bb8add2649cc32a6
  OPTIONS
    "PYILMBASE_ENABLE OFF"
    "ILMBASE_BUILD_SHARED_LIBS OFF"
    "OPENEXR_BUILD_SHARED_LIBS OFF"
    "ILMBASE_NAMESPACE_VERSIONING OFF"
    "OPENEXR_NAMESPACE_VERSIONING OFF"
    "OPENEXR_BUILD_UTILS OFF"
    "INSTALL_OPENEXR_EXAMPLES OFF"
    "BUILD_TESTING OFF"
)
if(openexr_ADDED)
  set(ZLIB_LIBRARIES "")
  if(WIN32)
    CPMAddPackage(
      NAME zlibstatic
      GIT_REPOSITORY    https://github.com/mitsuba-renderer/zlib.git
      GIT_TAG           080a732c47e86444034c1f99355368d35c1e458a
      OPTIONS
        "ZLIB_BUILD_STATIC_LIBS TRUE"
        "ZLIB_BUILD_SHARED_LIBS FALSE"
    )
    if(zlibstatic_ADDED)
      set(ZLIB_INCLUDE_DIR ${zlibstatic_SOURCE_DIR} ${zlibstatic_BINARY_DIR})
      set(ZLIB_LIBRARIES zlibstatic)

      message("adding zlib include dirs: ${ZLIB_INCLUDE_DIR}")
      message("adding zlib target: ${ZLIB_LIBRARIES}")
      target_include_directories(IlmImf PRIVATE ${ZLIB_INCLUDE_DIR})
      add_dependencies(IlmImf ${ZLIB_LIBRARIES})
    endif()
  endif()

  add_library(openexr::openexr INTERFACE IMPORTED)
  target_include_directories(openexr::openexr SYSTEM INTERFACE "${openexr_SOURCE_DIR}/IlmBase/Imath;${openexr_SOURCE_DIR}/IlmBase/Iex;${openexr_SOURCE_DIR}/IlmBase/Half;${openexr_SOURCE_DIR}/OpenEXR/IlmImf;${openexr_BINARY_DIR}/IlmBase/config;${openexr_BINARY_DIR}/OpenEXR/config")
  target_link_libraries(openexr::openexr INTERFACE "${ZLIB_LIBRARIES};OpenEXR::IlmImf;IlmBase::Imath;IlmBase::Half")

  # set_target_properties(openexr::openexr PROPERTIES
  #   INTERFACE_INCLUDE_DIRECTORIES
  #   "${openexr_SOURCE_DIR}/IlmBase/Imath;${openexr_SOURCE_DIR}/IlmBase/Iex;${openexr_SOURCE_DIR}/IlmBase/Half;${openexr_SOURCE_DIR}/OpenEXR/IlmImf;${openexr_BINARY_DIR}/IlmBase/config;${openexr_BINARY_DIR}/OpenEXR/config"
  #   INTERFACE_LINK_LIBRARIES
  #   "${ZLIB_LIBRARIES};OpenEXR::IlmImf;IlmBase::Imath;IlmBase::Half"
  #   )
endif()

CPMAddPackage("gh:gabime/spdlog@1.9.2")


CPMAddPackage(
  NAME nanogui
  GITHUB_REPOSITORY Tom94/nanogui-1
  GIT_TAG 5ca2cf89756b5b545d530de02b2d88be2eee7a87
  OPTIONS
    "NANOGUI_BUILD_EXAMPLES OFF"
    "NANOGUI_BUILD_SHARED OFF"
    "NANOGUI_BUILD_PYTHON OFF"
    "NANOGUI_INSTALL OFF"
)
if(nanogui_ADDED)
  add_library(nanogui::nanogui INTERFACE IMPORTED)
  target_include_directories(nanogui::nanogui SYSTEM INTERFACE "${nanogui_SOURCE_DIR}/ext/glfw/include;${nanogui_SOURCE_DIR}/ext/nanovg/src;${nanogui_SOURCE_DIR}/include;${NANOGUI_EXTRA_INCS}")
  target_link_libraries(nanogui::nanogui INTERFACE "nanogui")
  target_compile_definitions(nanogui::nanogui INTERFACE "${NANOGUI_BACKEND_DEFS}")

  # set_target_properties(nanogui::nanogui PROPERTIES
  #   INTERFACE_INCLUDE_DIRECTORIES
  #   "${nanogui_SOURCE_DIR}/ext/glfw/include;${nanogui_SOURCE_DIR}/ext/nanovg/src;${nanogui_SOURCE_DIR}/include;${NANOGUI_EXTRA_INCS}"
  #   INTERFACE_LINK_LIBRARIES "OpenEXR::IlmImf;IlmBase::Imath;IlmBase::Half"
  #   INTERFACE_COMPILE_DEFINITIONS "${NANOGUI_BACKEND_DEFS}"
  #   )
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

include_directories(
  ${CMAKE_SOURCE_DIR}/ext/alphanum
  ${CMAKE_SOURCE_DIR}/src)

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

# Needed to generated files
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Add a custom command that produces hdrview_version.cpp, plus a dummy output that's not actually produced, in order to
# force version.cmake to always be re-run before the build
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hdrview_version.cpp # ${CMAKE_CURRENT_BINARY_DIR}/_hdrview_version.cpp
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/resources/version.cmake
  COMMENT "Generating timestamp and git version info")

# add_custom_target(version_info COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/resources/version.cmake
# ADD_DEPENDENCIES ${CMAKE_SOURCE_DIR}/resources/version.cmake COMMENT "Generating timestamp and git version info")

add_executable(
  HDRView
  hdrview_version.cpp
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
  src/json.h
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
  src/rasterdraw.cpp
  src/rasterdraw.h
  src/timer.h
  src/tool.cpp
  src/tool.h
  src/well.cpp
  src/well.h
  ${EXTRA_SOURCE})

# add_dependencies(HDRView version_info)

set(HDRVIEW_DEFINITIONS -DHDRVIEW_VERSION="${HDRVIEW_VERSION}")
if(WIN32)
  set(HDRVIEW_DEFINITIONS ${HDRVIEW_DEFINITIONS} -DNOMINMAX)
endif()

add_definitions(${HDRVIEW_DEFINITIONS})
# add_definitions(${HDRVIEW_DEFINITIONS} ${NANOGUI_EXTRA_DEFS})


set_target_properties(HDRView PROPERTIES OUTPUT_NAME "HDRView")

if(APPLE)
  # Build an application bundle on OSX
  set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE TRUE)
  set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "HDRView")
  set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_BUNDLE_VERSION ${HDRVIEW_VERSION})
  set_target_properties(
    HDRView PROPERTIES MACOSX_BUNDLE_INFO_STRING
                       "High dynamic range image viewer and comparison tool, version ${HDRVIEW_VERSION}.")
  string(TIMESTAMP YEAR "%Y")
  set_target_properties(
    HDRView PROPERTIES MACOSX_BUNDLE_COPYRIGHT
                       "Copyright 2023, Wojciech Jarosz. Freely available under the BSD license.")
  set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "com.im.HDRView")
  set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_ICON_FILE icon.icns)
  set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_SHORT_VERSION_STRING ${HDRVIEW_VERSION})
  set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_LONG_VERSION_STRING ${HDRVIEW_VERSION})
  set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_INFO_PLIST
                                           ${CMAKE_SOURCE_DIR}/resources/MacOSXBundleInfo.plist.in)
  set_target_properties(HDRView PROPERTIES RESOURCE resources/icon.icns)
  set_source_files_properties(resources/icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
elseif(NOT WIN32)
  # Insulate from a few types of ABI changes by statically linking against libgcc and libstdc++
  set_target_properties(HDRView PROPERTIES LINK_FLAGS "-static-libgcc")
endif()

if(UNIX AND NOT ${U_CMAKE_BUILD_TYPE} MATCHES DEBUG)
  add_custom_command(TARGET HDRView POST_BUILD COMMAND strip $<TARGET_FILE:HDRView>)
endif()

add_executable(
  hdrbatch
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

# add_dependencies(hdrbatch version_info)

add_executable(force-random-dither src/forced-random-dither.cpp)

# set_target_properties(force-random-dither PROPERTIES CXX_VISIBILITY_PRESET hidden)

target_link_libraries(HDRView PRIVATE stb nanogui::nanogui openexr::openexr docopt platform_folders filesystem tinydir nlohmann_json tinydng spdlog::spdlog)
target_link_libraries(hdrbatch PRIVATE stb nanogui::nanogui openexr::openexr docopt platform_folders filesystem tinydir nlohmann_json tinydng spdlog::spdlog)
target_link_libraries(force-random-dither PRIVATE stb nanogui::nanogui filesystem tinydir nlohmann_json tinydng spdlog::spdlog)

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
