cmake_minimum_required(VERSION 3.13)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules" "${CMAKE_SOURCE_DIR}/cmake/")

include(VersionFromGit)

version_from_git(LOG ON TIMESTAMP "%Y-%m-%d-%H:%M:%S")

project(
  hdrview
  DESCRIPTION
    "A simple research-oriented image viewer with an emphasis on examining and comparing high-dynamic range (HDR) images."
  VERSION ${VERSION}
  LANGUAGES C CXX
)

# By default, we install in a portable way (i.e. assets and executable are in the same folder)
option(HDRVIEW_PORTABLE_INSTALL "Install in a portable way" ON)

option(HDRVIEW_IGNORE_BREW_PORTS "Ignore Homebrew and MacPorts directories" OFF)
if(HDRVIEW_IGNORE_BREW_PORTS)
  set(CMAKE_IGNORE_PATH "/usr/local;/opt/local;/opt/homebrew/lib;/opt/homebrew/include;/opt/local/bin")
endif()

# cmake-format: off
set(HDRVIEW_DEFINITIONS HELLOIMGUI_NO_FONT_AWESOME4)
set(HDRVIEW_ICONSET
    "HDRVIEW_ICONSET_MS"
    CACHE
      STRING
      "Choose the icon set: HDRVIEW_ICONSET_FA6, HDRVIEW_ICONSET_LC, HDRVIEW_ICONSET_MS, HDRVIEW_ICONSET_MD, HDRVIEW_ICONSET_MDI"
)

# Ensure the icon set is one of the allowed values
set(ALLOWED_ICONSETS "HDRVIEW_ICONSET_FA6" "HDRVIEW_ICONSET_LC" "HDRVIEW_ICONSET_MS" "HDRVIEW_ICONSET_MD"
                     "HDRVIEW_ICONSET_MDI"
)
list(FIND ALLOWED_ICONSETS ${HDRVIEW_ICONSET} ICONSET_INDEX)
if(ICONSET_INDEX EQUAL -1)
  message(FATAL_ERROR "Invalid icon set: ${HDRVIEW_ICONSET}. Allowed values are: ${ALLOWED_ICONSETS}")
endif()
# cmake-format: on

# copy assets to the build directory
set(MY_RESOURCE_FILES "assets/app_settings/icon.png")
if(EMSCRIPTEN)
  file(
    GLOB_RECURSE ADDITIONAL_RESOURCE_FILES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Roboto/**.*
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/app_settings/emscripten/**.* ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/*.glsl
  )
  list(APPEND MY_RESOURCE_FILES ${ADDITIONAL_RESOURCE_FILES})
elseif(APPLE)
  file(
    GLOB_RECURSE ADDITIONAL_RESOURCE_FILES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Roboto/**.* ${CMAKE_CURRENT_SOURCE_DIR}/assets/app_settings/apple/**.*
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/*.metal
  )
  list(APPEND MY_RESOURCE_FILES ${ADDITIONAL_RESOURCE_FILES})
elseif(WIN32)
  file(
    GLOB_RECURSE ADDITIONAL_RESOURCE_FILES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Roboto/**.* ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/*.glsl
  )
  list(APPEND MY_RESOURCE_FILES ${ADDITIONAL_RESOURCE_FILES})
endif()

# add our chosen icon font
if(HDRVIEW_ICONSET STREQUAL "HDRVIEW_ICONSET_FA6")
  # this file is included by hello imgui
  #
  # list(APPEND MY_RESOURCE_FILES "assets/fonts/Font_Awesome_6_Free-Solid-900.otf")
elseif(HDRVIEW_ICONSET STREQUAL "HDRVIEW_ICONSET_LC")
  list(APPEND MY_RESOURCE_FILES "assets/fonts/lucide.ttf")
elseif(HDRVIEW_ICONSET STREQUAL "HDRVIEW_ICONSET_MS")
  list(APPEND MY_RESOURCE_FILES "assets/fonts/MaterialSymbolsRounded_Filled-Regular.ttf")
elseif(HDRVIEW_ICONSET STREQUAL "HDRVIEW_ICONSET_MD")
  list(APPEND MY_RESOURCE_FILES "assets/fonts/MaterialIcons-Regular.ttf")
elseif(HDRVIEW_ICONSET STREQUAL "HDRVIEW_ICONSET_MDI")
  list(APPEND MY_RESOURCE_FILES "assets/fonts/materialdesignicons-webfont.ttf")
endif()

foreach(MY_RESOURCE_FILE ${MY_RESOURCE_FILES})
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/${MY_RESOURCE_FILE} ${CMAKE_CURRENT_BINARY_DIR}/${MY_RESOURCE_FILE} COPYONLY
  )
endforeach()

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/src/version.cpp ${CMAKE_CURRENT_BINARY_DIR}/src/_version.cpp
  COMMAND ${CMAKE_COMMAND} -D SRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/src -D BIN_DIR=${CMAKE_CURRENT_BINARY_DIR}/src -D
          CMAKE_SIZEOF_VOID_P=${CMAKE_SIZEOF_VOID_P} -P ${CMAKE_SOURCE_DIR}/cmake/generate_version.cmake
  COMMENT "Generating git version file"
)

include(sanitizers)

# Set ourselves as the startup project in visual studio. Not available until cmake 3.6, but doesn't break older
# versions.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT HDRView)

# ============================================================================
# Set a default build configuration (Release)
# ============================================================================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(DEFAULT_BUILD_TYPE Release)
  if(EMSCRIPTEN)
    set(DEFAULT_BUILD_TYPE MinSizeRel)
  endif()
  message(STATUS "Setting build type to '${DEFAULT_BUILD_TYPE}' as none was specified.")
  set(CMAKE_BUILD_TYPE
      ${DEFAULT_BUILD_TYPE}
      CACHE STRING "Choose the type of build." FORCE
  )
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" U_CMAKE_BUILD_TYPE)

# ============================================================================
# Enable folders for projects in Visual Studio
# ============================================================================
if(CMAKE_GENERATOR MATCHES "Visual Studio")
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  # Prefer libc++ in conjunction with Clang
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-stdlib=libc++" HAS_LIBCPP)
    if(HAS_LIBCPP)
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -D_LIBCPP_VERSION")
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
      set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -stdlib=libc++")
      message(STATUS "HDRView: using libc++.")
    else()
      message(
        WARNING
          "libc++ is recommended in conjunction with clang. Please install the libc++ development headers, provided e.g. by the packages 'libc++-dev' and 'libc++abi-dev' on Debian/Ubuntu."
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
      CMAKE_C_FLAGS_RELWITHDEBINFO
  )
  foreach(CompilerFlag ${CompilerFlags})
    string(REPLACE "/MD" "/MT" ${CompilerFlag} "${${CompilerFlag}}")
  endforeach()
endif()

include(FindPkgConfig)

# =====================================================================================
# Add dependencies via CPM (see https://github.com/TheLartians/CPM.cmake for more info)
# =====================================================================================
include(cmake/CPM.cmake)

if(APPLE AND NOT EMSCRIPTEN)
  # default to GLFW+Metal on macOS
  set(HELLOIMGUI_HAS_METAL ON)
  set(HELLOIMGUI_USE_GLFW3 ON)

  list(APPEND EXTRA_SOURCES src/renderpass_metal.mm src/shader_metal.mm src/texture_metal.mm)

  # on macOS, download a fork of glfw that allows dragging files onto app icon.

  # need to enable OBJC language for the custom glfw fork
  enable_language(OBJC)
  set(HELLOIMGUI_DOWNLOAD_GLFW_IF_NEEDED OFF)
  CPMAddPackage(
    NAME glfw
    GITHUB_REPOSITORY Tom94/glfw
    GIT_TAG 71eb7036b47e2733c2f7b4c0010e2ce62557009d
    OPTIONS "GLFW_BUILD_EXAMPLES OFF"
            "GLFW_BUILD_TESTS OFF"
            "GLFW_BUILD_DOCS OFF"
            "GLFW_BUILD_INSTALL OFF"
            "GLFW_INSTALL OFF"
            "GLFW_USE_CHDIR OFF"
            "BUILD_SHARED_LIBS OFF"
  )

  if(glfw_ADDED)
    message(STATUS "Using custom fork of glfw on macOS")

    mark_as_advanced(
      GLFW_BUILD_DOCS
      GLFW_BUILD_EXAMPLES
      GLFW_BUILD_INSTALL
      GLFW_BUILD_TESTS
      GLFW_DOCUMENT_INTERNALS
      GLFW_INSTALL
      GLFW_USE_CHDIR
      GLFW_USE_MENUBAR
      GLFW_USE_OSMESA
      GLFW_VULKAN_STATIC
      GLFW_USE_RETINA
      GLFW_USE_MIR
    )
  endif()
endif()

set(HELLOIMGUI_EMSCRIPTEN_PTHREAD OFF)
set(HELLOIMGUI_EMSCRIPTEN_PTHREAD_ALLOW_MEMORY_GROWTH OFF)

CPMAddPackage("gh:pthom/hello_imgui#v1.6.0")
if(hello_imgui_ADDED)
  message(STATUS "hello_imgui library added")
  if(HELLOIMGUI_EMSCRIPTEN_PTHREAD)
    add_compile_options(-pthread)
  endif()
endif()

CPMAddPackage("gh:epezent/implot#77674d270e851d3f3718aad00234201af2b76ac9")
if(implot_ADDED)
  message(STATUS "implot library added")

  # implot has no CMake support, so we create our own target
  add_library(
    implot STATIC ${implot_SOURCE_DIR}/implot.h ${implot_SOURCE_DIR}/implot_internal.h ${implot_SOURCE_DIR}/implot.cpp
                  ${implot_SOURCE_DIR}/implot_items.cpp
  )
  target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
  target_link_libraries(implot PUBLIC imgui)
  set_target_properties(implot PROPERTIES CXX_STANDARD 17)
endif()
list(APPEND HDRVIEW_DEPENDENCIES "implot")

CPMAddPackage(
  NAME my_imgui_command_palette
  GITHUB_REPOSITORY wkjarosz/imgui-command-palette
  GIT_TAG 5699254a9de9a6448360844696fb501bc5141353
  DOWNLOAD_ONLY YES
)
if(my_imgui_command_palette_ADDED)
  message(STATUS "imgui-command-palette library added")
  add_library(
    my_imgui_command_palette
    "${my_imgui_command_palette_SOURCE_DIR}/imcmd_command_palette.h"
    "${my_imgui_command_palette_SOURCE_DIR}/imcmd_command_palette.cpp"
    "${my_imgui_command_palette_SOURCE_DIR}/imcmd_fuzzy_search.h"
    "${my_imgui_command_palette_SOURCE_DIR}/imcmd_fuzzy_search.cpp"
  )
  target_compile_features(my_imgui_command_palette PUBLIC cxx_std_11)
  target_include_directories(my_imgui_command_palette PUBLIC ${my_imgui_command_palette_SOURCE_DIR})
  target_link_libraries(my_imgui_command_palette PUBLIC imgui)
endif()
list(APPEND HDRVIEW_DEPENDENCIES "my_imgui_command_palette")

CPMAddPackage("gh:sgorsten/linalg@2.2")
if(linalg_ADDED)
  message(STATUS "linalg library added")
  add_library(linalg INTERFACE IMPORTED)
  target_include_directories(linalg INTERFACE "${linalg_SOURCE_DIR}")
endif()
list(APPEND HDRVIEW_DEPENDENCIES "linalg")

CPMAddPackage("gh:fmtlib/fmt#9.1.0")
if(fmt_ADDED)
  message(STATUS "fmt library added")
endif()
list(APPEND HDRVIEW_DEPENDENCIES "fmt::fmt")

CPMAddPackage(
  NAME spdlog
  VERSION 1.12.0 # we set the minimum version to the one included in ubuntu 24.04
  URL https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.zip
  OPTIONS "SPDLOG_INSTALL NO" # don't create an installable target
          "SPDLOG_FMT_EXTERNAL YES" # use the fmt library we added above instead of the bundled one
)
if(spdlog_ADDED)
  message(STATUS "spdlog library added")
endif()
list(APPEND HDRVIEW_DEPENDENCIES "spdlog::spdlog")

CPMAddPackage("gh:nothings/stb#a0a939058c579ddefd4c5671b046f29d12aeae01")
if(stb_ADDED)
  message(STATUS "stb library added")
  add_library(stb INTERFACE IMPORTED)
  target_include_directories(stb INTERFACE "${stb_SOURCE_DIR}")
endif()
list(APPEND HDRVIEW_DEPENDENCIES "stb")

CPMAddPackage("gh:phoboslab/qoi#b0b926ee70108b0113dd2718d13d2207c474ed2e")
if(qoi_ADDED)
  message(STATUS "qoi library added")
  add_library(qoi INTERFACE IMPORTED)
  target_include_directories(qoi INTERFACE "${qoi_SOURCE_DIR}")
endif()
list(APPEND HDRVIEW_DEPENDENCIES "qoi")

if(EMSCRIPTEN)

  CPMAddPackage("gh:Armchair-Software/emscripten-browser-file#d21dec772050d581c02aa9820e4d877fdb060dc8")
  if(emscripten-browser-file_ADDED)
    message(STATUS "emscripten-browser-file library added")
    add_library(emscripten-browser-file INTERFACE IMPORTED)
    target_include_directories(emscripten-browser-file INTERFACE "${emscripten-browser-file_SOURCE_DIR}")
  endif()
  list(APPEND HDRVIEW_DEPENDENCIES "emscripten-browser-file")

  CPMAddPackage("gh:Armchair-Software/emscripten-browser-clipboard#bcba48fee22551b458fe5e1e513f6402c810d886")
  if(emscripten-browser-clipboard_ADDED)
    message(STATUS "emscripten-browser-clipboard library added")
    add_library(emscripten-browser-clipboard INTERFACE IMPORTED)
    target_include_directories(emscripten-browser-clipboard INTERFACE "${emscripten-browser-clipboard_SOURCE_DIR}")
  endif()
  list(APPEND HDRVIEW_DEPENDENCIES "emscripten-browser-clipboard")

else()

  CPMAddPackage("gh:samhocevar/portable-file-dialogs#7f852d88a480020d7f91957cbcefe514fc95000c")
  if(portable-file-dialogs_ADDED)
    message(STATUS "portable-file-dialogs library added")
    add_library(portable-file-dialogs INTERFACE IMPORTED)
    target_include_directories(portable-file-dialogs INTERFACE "${portable-file-dialogs_SOURCE_DIR}")
  endif()
  list(APPEND HDRVIEW_DEPENDENCIES "portable-file-dialogs")

endif()

CPMAddPackage(
  NAME Imath
  VERSION 3.1.9 # we set the minimium version to 3.1.9, but if manually building use a more recent version
  FIND_PACKAGE_ARGUMENTS "REQUIRED"
  # the git repo is incredibly large, so we download the archived include directory
  URL https://github.com/AcademySoftwareFoundation/Imath/archive/refs/tags/v3.1.12.zip
  OPTIONS "BUILD_SHARED_LIBS OFF" "IMATH_INSTALL OFF" "IMATH_INSTALL_PKG_CONFIG OFF" "BUILD_TESTING OFF"
          "EXCLUDE_FROM_ALL YES"
)
if(Imath_ADDED)
  message(STATUS "Building Imath library")
elseif(TARGET Imath::Imath)
  message(STATUS "Will link against system-installed Imath library")
else()
  message(FATAL_ERROR "Imath library not found. Please install it.")
endif()
# list(APPEND HDRVIEW_DEPENDENCIES "Imath::Imath")

if(EMSCRIPTEN AND NOT HELLOIMGUI_EMSCRIPTEN_PTHREAD)
  set(OPENEXR_THREADING_OPTIONS "OPENEXR_ENABLE_THREADING OFF")
else()
  set(OPENEXR_THREADING_OPTIONS "OPENEXR_ENABLE_THREADING ON")
endif()

CPMAddPackage(
  NAME OpenEXR
  GITHUB_REPOSITORY AcademySoftwareFoundation/openexr
  # FIXME: OpenEXR v3.3.0 and above seems to break our subsample channel loading
  FIND_PACKAGE_ARGUMENTS "REQUIRED"
  VERSION 3.1.5
  GIT_TAG v3.2.4
  OPTIONS "BUILD_SHARED_LIBS OFF"
          "BUILD_TESTING OFF"
          "OPENEXR_FORCE_INTERNAL_DEFLATE ON"
          "OPENEXR_BUILD_TOOLS OFF"
          "OPENEXR_BUILD_EXAMPLES OFF"
          "OPENEXR_INSTALL OFF"
          "OPENEXR_INSTALL_TOOLS OFF"
          "OPENEXR_INSTALL_EXAMPLES OFF"
          "OPENEXR_INSTALL_PKG_CONFIG OFF"
          "EXCLUDE_FROM_ALL YES"
          ${OPENEXR_THREADING_OPTIONS}
)
if(openexr_ADDED)
  message(STATUS "Building OpenEXR library with threading options: ${OPENEXR_THREADING_OPTIONS}")
elseif(TARGET OpenEXR::OpenEXR)
  message(STATUS "Will link against system-installed OpenEXR library")
else()
  message(FATAL_ERROR "OpenEXR library not found. Please install it.")
endif()
list(APPEND HDRVIEW_DEPENDENCIES "OpenEXR::OpenEXR")

option(HDRVIEW_ENABLE_UHDR "Whether to build with Ultra HDR support" ON)
if(HDRVIEW_ENABLE_UHDR)
  CPMAddPackage(
    NAME libuhdr
    GITHUB_REPOSITORY google/libultrahdr
    VERSION 1.4.0
    # GIT_TAG bf2aa439eea9ad5da483003fa44182f990f74091
    OPTIONS "BUILD_SHARED_LIBS OFF"
            "UHDR_BUILD_EXAMPLES OFF"
            "UHDR_BUILD_TESTS OFF"
            "UHDR_BUILD_BENCHMARK OFF"
            "UHDR_BUILD_FUZZERS OFF"
            "UHDR_ENABLE_INSTALL OFF"
            "UHDR_ENABLE_INTRINSICS OFF"
  )
  if(libuhdr_ADDED)
    message(STATUS "Building uhdr library with UHDR_BUILD_DEPS=${UHDR_BUILD_DEPS}")
  elseif(TARGET core)
    message(STATUS "Will link against system-installed uhdr library")
  else()
    message(FATAL_ERROR "uhdr library not found. Please install it or turn off HDRVIEW_ENABLE_UHDR.")
  endif()
  list(APPEND HDRVIEW_DEPENDENCIES "core")
  list(APPEND HDRVIEW_DEFINITIONS HDRVIEW_ENABLE_UHDR)
endif()

option(HDRVIEW_ENABLE_JPEGXL "Enable JPEG-XL support" ON)
if(HDRVIEW_ENABLE_JPEGXL)
  CPMAddPackage(
    NAME JXL
    GITHUB_REPOSITORY libjxl/libjxl
    VERSION 0.11.1
    OPTIONS "BUILD_SHARED_LIBS OFF"
            "BUILD_TESTING OFF"
            "JPEGXL_ENABLE_FUZZERS OFF"
            "JPEGXL_ENABLE_DEVTOOLS OFF"
            "JPEGXL_ENABLE_TOOLS OFF"
            "JPEGXL_ENABLE_BENCHMARK OFF"
            "JPEGXL_ENABLE_EXAMPLES OFF"
            "JPEGXL_ENABLE_JNI OFF"
            "JPEGXL_ENABLE_OPENEXR OFF"
            "JPEGXL_ENABLE_DOXYGEN OFF"
            "JPEGXL_ENABLE_MANPAGES OFF"
            "JPEGXL_ENABLE_JPEGLI_LIBJPEG OFF"
            "JPEGXL_ENABLE_SKCMS OFF"
            "JPEGXL_ENABLE_VIEWERS=OFF"
            "JPEGXL_ENABLE_PLUGINS=OFF"
            "EXCLUDE_FROM_ALL YES"
  )
  if(JXL_ADDED)
    message(STATUS "Building JPEG-XL library")
  elseif(TARGET jxl)
    message(STATUS "Using system-installed JPEG-XL library")
  else()
    message(FATAL_ERROR "JPEG-XL library not found. Please install it or turn off HDRVIEW_ENABLE_JPEGXL.")
  endif()
  list(APPEND HDRVIEW_DEPENDENCIES "jxl" "jxl_threads")
  list(APPEND HDRVIEW_DEFINITIONS HDRVIEW_ENABLE_JPEGXL)
endif()

if(TARGET lcms2)
  message(STATUS "Using lcms2 target already provided by one of the other dependencies")
  list(APPEND HDRVIEW_DEPENDENCIES "lcms2")
  list(APPEND HDRVIEW_DEFINITIONS HDRVIEW_ENABLE_LCMS2)
else()
  find_package(LCMS2 REQUIRED)
  if(LCMS2_FOUND)
    message(STATUS "Using system-installed lcms2 library")
    list(APPEND HDRVIEW_DEPENDENCIES "lcms2")
    list(APPEND HDRVIEW_DEFINITIONS HDRVIEW_ENABLE_LCMS2)
  else()
    message(FATAL_ERROR "lcms2 library not found.")
  endif()
endif()

option(HDRVIEW_ENABLE_HEIF "Enable HEIF/AVIF support" OFF)
if(HDRVIEW_ENABLE_HEIF)
  CPMAddPackage(
    NAME Libheif
    GITHUB_REPOSITORY strukturag/libheif
    VERSION 1.17.6 # set minimum version to the one in ubuntu 24.04
    URL https://github.com/strukturag/libheif/archive/refs/tags/v1.19.5.zip
    OPTIONS "BUILD_SHARED_LIBS OFF" "BUILD_TESTING OFF" "WITH_EXAMPLES OFF" "WITH_GDK_PIXBUF OFF"
            "CMAKE_COMPILE_WARNING_AS_ERROR OFF" "EXCLUDE_FROM_ALL YES"
  )
  if(Libheif_ADDED)
    message(STATUS "Will build libheif library")
    target_include_directories(
      heif INTERFACE $<BUILD_INTERFACE:${Libheif_SOURCE_DIR}> $<BUILD_INTERFACE:${Libheif_SOURCE_DIR}/libheif/api/>
                     $<BUILD_INTERFACE:${Libheif_BINARY_DIR}>
    )
  elseif(TARGET heif)
    message(STATUS "Using system-installed libheif library")
  else()
    message(FATAL_ERROR "libheif not found. Please install it or turn off HDRVIEW_ENABLE_HEIF.")
  endif()
  list(APPEND HDRVIEW_DEPENDENCIES "heif")
  list(APPEND HDRVIEW_DEFINITIONS HDRVIEW_ENABLE_HEIF)
endif()

CPMAddPackage(
  NAME CLI11
  VERSION 2.4.1
  URL https://github.com/CLIUtils/CLI11/archive/v2.4.2.zip
  DOWNLOAD_ONLY YES
)
if(CLI11_ADDED)
  message(STATUS "Will build CLI11 library")
  add_library(CLI11::CLI11 INTERFACE IMPORTED)
  target_include_directories(CLI11::CLI11 INTERFACE "${CLI11_SOURCE_DIR}/include")
elseif(TARGET CLI11::CLI11)
  message(STATUS "Will link against system-installed CLI11 library")
else()
  message(FATAL_ERROR "CLI11 library not found. Please install it.")
endif()
list(APPEND HDRVIEW_DEPENDENCIES "CLI11::CLI11")

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

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Now build the HDRView viewer app
string(TIMESTAMP YEAR "%Y")

set(output_name "HDRView")
set(HELLO_IMGUI_BUNDLE_IDENTIFIER_URL_PART "com.im.HDRView")
set(HELLO_IMGUI_BUNDLE_IDENTIFIER_NAME_PART ${app_name})
set(HELLO_IMGUI_ICON_DISPLAY_NAME ${output_name})
set(HELLO_IMGUI_BUNDLE_NAME ${output_name})
set(HELLO_IMGUI_BUNDLE_COPYRIGHT "© Wojciech Jarosz, ${YEAR}")
set(HELLO_IMGUI_BUNDLE_EXECUTABLE ${output_name})
set(HELLO_IMGUI_BUNDLE_VERSION ${VERSION})
set(HELLO_IMGUI_BUNDLE_SHORT_VERSION ${VERSION})
set(HELLO_IMGUI_BUNDLE_ICON_FILE icon.icns)

if(NOT HDRVIEW_PORTABLE_INSTALL)
  # Disable HelloImGui default install behavior
  set(HELLOIMGUI_ADD_APP_WITH_INSTALL
      OFF
      CACHE BOOL "" FORCE
  )
endif()

hello_imgui_add_app(
  HDRView
  ${CMAKE_CURRENT_BINARY_DIR}/src/version.cpp
  src/imageio/exr.cpp
  src/imageio/heif.cpp
  src/imageio/icc.cpp
  src/imageio/jxl.cpp
  src/imageio/pfm.cpp
  src/imageio/stb.cpp
  src/imageio/qoi.cpp
  src/imageio/uhdr.cpp
  src/app.cpp
  src/colormap.cpp
  src/colorspace.cpp
  src/common.cpp
  src/emscripten_utils.cpp
  src/fonts.cpp
  src/hdrview.cpp
  src/image.cpp
  src/imageio.cpp
  src/imgui_ext.cpp
  src/image_gui.cpp
  src/progress.cpp
  src/opengl_check.cpp
  src/scheduler.cpp
  src/shader.cpp
  src/shader_gl.cpp
  src/renderpass_gl.cpp
  src/texture.cpp
  src/texture_gl.cpp
  ${EXTRA_SOURCES}
  ASSETS_LOCATION
  ${CMAKE_CURRENT_BINARY_DIR}/assets
)
target_include_directories(HDRView PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(HDRView PROPERTIES OUTPUT_NAME ${output_name} CXX_STANDARD 17)
target_compile_definitions(HDRView PRIVATE ${HDRVIEW_ICONSET})

message(STATUS "HDRView build dependencies: ${HDRVIEW_DEPENDENCIES}")
message(STATUS "HDRView build definitions: ${HDRVIEW_DEFINITIONS}")

target_link_libraries(HDRView PRIVATE ${HDRVIEW_DEPENDENCIES})
target_compile_definitions(HDRView PRIVATE ${HDRVIEW_DEFINITIONS})

if(APPLE)
  target_compile_options(HDRView PRIVATE "-fobjc-arc")
  target_link_libraries(HDRView PRIVATE "-framework ApplicationServices")
  target_compile_definitions(HDRView PRIVATE IMGUI_ENABLE_OSX_DEFAULT_CLIPBOARD_FUNCTIONS)
endif(APPLE)

if(EMSCRIPTEN)
  target_link_options(
    HDRView
    PRIVATE
    -fexceptions
    -sEXPORTED_RUNTIME_METHODS=[ccall]
    -sEXPORTED_FUNCTIONS=[_main,_malloc,_free]
    -sNO_DISABLE_EXCEPTION_CATCHING
    -sMAX_WEBGL_VERSION=2
    -sMIN_WEBGL_VERSION=2
  )
  target_compile_options(HDRView PRIVATE -fexceptions -sNO_DISABLE_EXCEPTION_CATCHING)
  if(HELLOIMGUI_EMSCRIPTEN_PTHREAD)
    target_link_options(HDRView PRIVATE -pthread)
    target_link_options(HDRView PRIVATE -sPTHREAD_POOL_SIZE=3)
  endif()
  hello_imgui_set_emscripten_target_initial_memory_megabytes(HDRView 120)
endif()

if(UNIX AND NOT ${U_CMAKE_BUILD_TYPE} MATCHES DEBUG)
  add_custom_command(
    TARGET HDRView
    POST_BUILD
    COMMAND strip $<TARGET_FILE:HDRView>
  )
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

# Install and packaging support

if(NOT HDRVIEW_PORTABLE_INSTALL)
  # cmake-format: off
  # If not portable, we have to
  # - install the app manually
  # - install the assets manually
  # - pass the assets location to the app, and set this assets location in main() via the compile definition ASSETS_LOCATION:
  #     #ifdef ASSETS_LOCATION \n HelloImGui::SetAssetsFolder(ASSETS_LOCATION); \n #endif
  # cmake-format: on
  include(GNUInstallDirs)
  install(TARGETS HDRView DESTINATION ${CMAKE_INSTALL_BINDIR})
  set(assets_install_dir ${CMAKE_INSTALL_DATADIR}/HDRView)
  if(NOT IS_ABSOLUTE ${assets_install_dir})
    set(assets_install_dir ${CMAKE_INSTALL_PREFIX}/${assets_install_dir})
  endif()
  install(DIRECTORY assets DESTINATION ${assets_install_dir})
  target_compile_definitions(HDRView PRIVATE ASSETS_LOCATION="${assets_install_dir}/assets")
endif()

set(CPACK_PACKAGE_VENDOR "Wojciech Jarosz")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/wkjarosz/hdrview")
set(CPACK_PACKAGE_VERSION_MAJOR ${VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${VERSION_PATCH})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME}")

if(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
endif()

include(CPack)
