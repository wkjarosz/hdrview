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

# Set the framework search order to last, so that we can find libraries in the system directories first. This is to
# prevent CMAKE from finding old versions of headers, e.g. libjpeg in /Library/Frameworks/Mono.framework/Headers, and
# first check other install locations like brew.
set(CMAKE_FIND_FRAMEWORK LAST)

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
set(MY_RESOURCE_FILES "assets/app_settings/icon.png" "assets/app_settings/icon-256.png")
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
get_cmake_property(IS_MULTI GENERATOR_IS_MULTI_CONFIG)
if(NOT CMAKE_BUILD_TYPE AND NOT IS_MULTI)
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
    GITHUB_REPOSITORY wkjarosz/glfw
    GIT_TAG 7a3f5df970fda02a1b6b6a4115eb9df30676a0dc
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
set(IMGUI_DISABLE_OBSOLETE_FUNCTIONS ON)

CPMAddPackage(
  NAME hello_imgui
  GITHUB_REPOSITORY pthom/hello_imgui
  VERSION 1.92.0
  GIT_TAG v1.92.0
  OPTIONS "HELLOIMGUI_EMSCRIPTEN_PTHREAD OFF" "HELLOIMGUI_EMSCRIPTEN_PTHREAD_ALLOW_MEMORY_GROWTH OFF"
          "IMGUI_DISABLE_OBSOLETE_FUNCTIONS ON"
)

if(hello_imgui_ADDED)
  message(STATUS "hello_imgui library added")
  if(HELLOIMGUI_EMSCRIPTEN_PTHREAD)
    add_compile_options(-pthread)
  endif()
endif()

CPMAddPackage("gh:epezent/implot#3da8bd34299965d3b0ab124df743fe3e076fa222")
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
  GIT_TAG cdc9d0bc2a0de2f0bc1bbc640914e2554e2bee35
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

  CPMAddPackage(
    NAME portable-file-dialogs
    GITHUB_REPOSITORY wkjarosz/portable-file-dialogs
    GIT_TAG 1d755d814b77e546f544266bc24ac76029b8763b
  )
  if(portable-file-dialogs_ADDED)
    message(STATUS "portable-file-dialogs library added")
    add_library(portable-file-dialogs INTERFACE IMPORTED)
    target_include_directories(portable-file-dialogs INTERFACE "${portable-file-dialogs_SOURCE_DIR}")
  endif()
  list(APPEND HDRVIEW_DEPENDENCIES "portable-file-dialogs")

endif()

CPMAddPackage(
  NAME exif
  GITHUB_REPOSITORY Tom94/libexif
  GIT_TAG f99967d09f1424170388cb2c0b65dcf67d6f5ac3
  OPTIONS "BUILD_SHARED_LIBS OFF"
)
if(exif_ADDED)
  message(STATUS "Will build exif library")
  list(APPEND HDRVIEW_DEPENDENCIES "exif_static")
else()
  message(FATAL_ERROR "libexif not found.")
endif()

option(HDRVIEW_ENABLE_LIBPNG "Enable LIBPNG support" ON)
if(HDRVIEW_ENABLE_LIBPNG)
  CPMAddPackage(
    NAME zlib-cmake
    URL https://github.com/jimmy-park/zlib-cmake/archive/main.tar.gz
    OPTIONS "ZLIB_TEST OFF" "ZLIB_INSTALL ON"
  )
  if(zlib-cmake_ADDED)
    message(STATUS "Will build zlib library")
    set_target_properties(ZLIB PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
  elseif(ZLIB_FOUND)
    message(STATUS "Using system-installed zlib library")
  else()
    message(FATAL_ERROR "zlib not found.")
  endif()
  CPMAddPackage(
    NAME PNG
    GITHUB_REPOSITORY pnggroup/libpng
    GIT_TAG 7e69b66b58434f6e99af4d03378eb4e5ff762047
    # VERSION 1.6.50 URL https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.50.zip
    OPTIONS "PNG_SHARED OFF"
            "PNG_STATIC ON"
            "PNG_FRAMEWORK OFF"
            "PNG_TESTS OFF"
            "PNG_TOOLS OFF"
            "SKIP_INSTALL_ALL ON"
            "EXCLUDE_FROM_ALL YES"
  )
  if(PNG_ADDED)
    message(STATUS "Will build libpng library")
    # set_target_properties(png_shared PROPERTIES POSITION_INDEPENDENT_CODE TRUE) set_target_properties(png_static
    # PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
    list(APPEND HDRVIEW_DEPENDENCIES "png_static")
  elseif(PNG_FOUND)
    message(STATUS "Using system-installed libpng library")
    list(APPEND HDRVIEW_DEPENDENCIES "PNG::PNG")
  else()
    message(FATAL_ERROR "libpng not found. Please install it or turn off HDRVIEW_ENABLE_LIBPNG.")
  endif()
  list(APPEND HDRVIEW_DEFINITIONS HDRVIEW_ENABLE_LIBPNG)
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
  GIT_TAG v3.3.4
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

# cmake-format: off
# libjpeg-turbo
option(HDRVIEW_ENABLE_LIBJPEG "Enable libjpeg(-turbo) support" ON)
if(HDRVIEW_ENABLE_LIBJPEG)
  find_package(JPEG)
  if(JPEG_FOUND)
    list(APPEND HDRVIEW_DEFINITIONS HDRVIEW_ENABLE_LIBJPEG)

    include(CheckCXXSourceCompiles)

    # this is needed for CheckCXXSourceCompiles
    set(CMAKE_REQUIRED_LIBRARIES ${JPEG_LIBRARIES})
    set(CMAKE_REQUIRED_INCLUDES ${JPEG_INCLUDE_DIRS})
    check_cxx_source_compiles(
      "
  #include <stddef.h>
  #include <stdio.h>
  #include <jpeglib.h>

  int main() {
    jpeg_write_icc_profile(NULL, NULL, 0);
    return 0;
  }
  "
      HAVE_JPEG_WRITE_ICC_PROFILE
    )
    unset(CMAKE_REQUIRED_LIBRARIES)
    unset(CMAKE_REQUIRED_INCLUDES)

    message(STATUS "Found libjpeg-turbo with HAVE_JPEG_WRITE_ICC_PROFILE=${HAVE_JPEG_WRITE_ICC_PROFILE}")

    set(JPEG_HEADER_FOUND FALSE)
    foreach(JPEG_DIR ${JPEG_INCLUDE_DIRS})
      if(EXISTS "${JPEG_DIR}/jpeglib.h")
      file(READ "${JPEG_DIR}/jpeglib.h" JPEGLIB_H_CONTENTS)
      message(STATUS "========== ${JPEG_DIR}/jpeglib.h ==========")
      message(STATUS "${JPEGLIB_H_CONTENTS}")
      set(JPEG_HEADER_FOUND TRUE)
      endif()
    endforeach()
    if(NOT JPEG_HEADER_FOUND)
      message(WARNING "Could not find jpeglib.h in any of: ${JPEG_INCLUDE_DIRS}")
    endif()

    list(APPEND HDRVIEW_DEPENDENCIES JPEG::JPEG)
  else()
    message(STATUS "libjpeg library not found, will build libjpeg-turbo")

    set(JPEGTURBO_TARGET_NAME turbojpeg-hdrview)
    set(JPEGTURBO_PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/${JPEGTURBO_TARGET_NAME})
    set(JPEGTURBO_SOURCE_DIR ${JPEGTURBO_PREFIX_DIR}/source)
    set(JPEGTURBO_BINARY_DIR ${JPEGTURBO_PREFIX_DIR}/build)

    file(MAKE_DIRECTORY ${JPEGTURBO_SOURCE_DIR}/src)

    set(JPEG_INCLUDE_DIRS ${JPEGTURBO_BINARY_DIR} ${JPEGTURBO_SOURCE_DIR}/src)
    if(MSVC)
      set(JPEG_LIB ${CMAKE_STATIC_LIBRARY_PREFIX}jpeg-static${CMAKE_STATIC_LIBRARY_SUFFIX})
    else()
      set(JPEG_LIB ${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX})
    endif()
    if(IS_MULTI)
      set(JPEG_LIB_PREFIX ${JPEGTURBO_BINARY_DIR}/$<CONFIG>/)
    else()
      set(JPEG_LIB_PREFIX ${JPEGTURBO_BINARY_DIR}/)
    endif()
    set(JPEG_LIBRARIES ${JPEG_LIB_PREFIX}${JPEG_LIB})

    message(STATUS "JPEGTURBO_BINARY_DIR: ${JPEGTURBO_BINARY_DIR}")
    message(STATUS "JPEGTURBO_SOURCE_DIR: ${JPEGTURBO_SOURCE_DIR}")

    include(ExternalProject)
    if(EMSCRIPTEN)
      ExternalProject_Add(
        ${JPEGTURBO_TARGET_NAME}
        GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git
        GIT_TAG 3.1.0
        PREFIX ${JPEGTURBO_PREFIX_DIR}
        SOURCE_DIR ${JPEGTURBO_SOURCE_DIR}
        BINARY_DIR ${JPEGTURBO_BINARY_DIR}
        CONFIGURE_COMMAND emcmake cmake ${JPEGTURBO_SOURCE_DIR} -DENABLE_SHARED=0 -DWITH_SIMD=0
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> --target jpeg-static
        BUILD_BYPRODUCTS ${JPEG_LIBRARIES}
        INSTALL_COMMAND ""
      )
    else()
      ExternalProject_Add(
        ${JPEGTURBO_TARGET_NAME}
        GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git
        GIT_TAG 3.1.0
        PREFIX ${JPEGTURBO_PREFIX_DIR}
        SOURCE_DIR ${JPEGTURBO_SOURCE_DIR}
        BINARY_DIR ${JPEGTURBO_BINARY_DIR}
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> --target jpeg-static
        CMAKE_ARGS
            -DENABLE_SHARED=0
            -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
            -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
            -DWITH_JPEG8=1
            -DWITH_TURBOJPEG=1
        BUILD_BYPRODUCTS ${JPEG_LIBRARIES}
        INSTALL_COMMAND ""
      )

      ExternalProject_Add_Step(${JPEGTURBO_TARGET_NAME} print_jpeglib_h
        COMMAND ${CMAKE_COMMAND} -E echo "========== src/jpeglib.h =========="
        COMMAND ${CMAKE_COMMAND} -E cat ${JPEGTURBO_SOURCE_DIR}/src/jpeglib.h
        DEPENDEES download
        ALWAYS 1
      )
    endif()

    # The following variables ensure that find_package can find libjpeg-turbo
    set(JPEG_LIBRARY ${JPEG_LIBRARIES})
    set(JPEG_INCLUDE_DIR ${JPEGTURBO_SOURCE_DIR}/src)

    set(HAVE_JPEG_WRITE_ICC_PROFILE TRUE)

    add_library(jpeg-turbo INTERFACE IMPORTED GLOBAL)
    add_dependencies(jpeg-turbo ${JPEGTURBO_TARGET_NAME})
    set_target_properties(
      jpeg-turbo PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${JPEG_INCLUDE_DIRS}" INTERFACE_LINK_LIBRARIES
                                                                                 "${JPEG_LIBRARIES}"
    )
    add_library(JPEG::JPEG ALIAS jpeg-turbo)

    list(APPEND HDRVIEW_DEPENDENCIES jpeg-turbo)
    list(APPEND HDRVIEW_DEFINITIONS HDRVIEW_ENABLE_LIBJPEG)
  endif()
  if (HAVE_JPEG_WRITE_ICC_PROFILE)
    add_definitions(-DHAVE_JPEG_WRITE_ICC_PROFILE=1)
  endif ()
  message(STATUS "JPEG_LIBRARIES: ${JPEG_LIBRARIES}")
  message(STATUS "JPEG_INCLUDE_DIRS: ${JPEG_INCLUDE_DIRS}")
endif()
# cmake-format: on

# cmake-format: off
option(HDRVIEW_ENABLE_UHDR "Whether to build with Ultra HDR support" ON)
if(HDRVIEW_ENABLE_UHDR)
  CPMAddPackage(
    NAME libuhdr
    GITHUB_REPOSITORY google/libultrahdr
    # VERSION 1.4.0
    GIT_TAG 5fa99b5271a3c80a13c78062d7adc6310222dd8e
    OPTIONS "UHDR_BUILD_BENCHMARK OFF"
            "UHDR_BUILD_FUZZERS OFF"
            "JPEG_FOUND TRUE"
            "BUILD_SHARED_LIBS OFF"
            "UHDR_BUILD_DEPS ON"
            "UHDR_BUILD_EXAMPLES OFF"
            "UHDR_BUILD_TESTS OFF"
            "UHDR_ENABLE_INSTALL OFF"
  )
  if(libuhdr_ADDED)
    message(STATUS "Building uhdr library with UHDR_BUILD_DEPS=${UHDR_BUILD_DEPS}")
  elseif(TARGET core)
    message(STATUS "Will link against system-installed uhdr library")
  else()
    message(FATAL_ERROR "uhdr library not found. Please install it or turn off HDRVIEW_ENABLE_UHDR.")
  endif()
  message(STATUS "setting uhdr dependent on ${JPEGTURBO_TARGET_NAME}")
  add_dependencies(core JPEG::JPEG)
  add_dependencies(uhdr JPEG::JPEG)

  list(APPEND HDRVIEW_DEPENDENCIES "uhdr")
  list(APPEND HDRVIEW_DEFINITIONS HDRVIEW_ENABLE_UHDR)
endif()
# cmake-format: on

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
            "JPEGXL_BUNDLE_LIBPNG OFF"
            "JPEGXL_ENABLE_SJPEG OFF"
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
  message(STATUS "Using lcms2 target already provided by libjxl")
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

option(HDRVIEW_ENABLE_HEIF "Enable HEIF/AVIF support" ON)
if(HDRVIEW_ENABLE_HEIF)
  CPMAddPackage(
    NAME Libheif
    GITHUB_REPOSITORY strukturag/libheif
    VERSION 1.17.6 # set minimum version to the one in ubuntu 24.04
    # URL https://github.com/strukturag/libheif/archive/refs/tags/v1.19.5.zip
    GIT_TAG 95428fdf22b2e0ec3ad4515c882fa0d6dd663f0a
    OPTIONS "BUILD_SHARED_LIBS OFF"
            "BUILD_TESTING OFF"
            "WITH_EXAMPLES OFF"
            "WITH_EXAMPLE_HEIF_VIEW OFF"
            "WITH_FUZZERS OFF"
            "WITH_GDK_PIXBUF OFF"
            "WITH_INSTALL OFF"
            "CMAKE_COMPILE_WARNING_AS_ERROR OFF"
            "EXCLUDE_FROM_ALL YES"
            # codecs
            "WITH_LIBSHARPYUV OFF"
            "WITH_LIBDE265 ON"
            "WITH_X265 ON"
            "WITH_OpenH264_ENCODER OFF"
            "WITH_OpenH264_DECODER ON"
            "WITH_JPEG_ENCODER OFF"
            "WITH_JPEG_DECODER ON"
            "WITH_AOM_ENCODER OFF"
            "WITH_AOM_DECODER ON"
            "HAVE_JPEG_WRITE_ICC_PROFILE ${HAVE_JPEG_WRITE_ICC_PROFILE}"
  )
  if(Libheif_ADDED)
    message(STATUS "Will build libheif library")
    # Remove the doc_doxygen target created with 'ALL'
    if(TARGET doc_doxygen)
      set_target_properties(doc_doxygen PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
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
  src/imageio/image_loader.cpp
  src/imageio/exr_header.cpp
  src/imageio/exif.cpp
  src/imageio/exr.cpp
  src/imageio/heif.cpp
  src/imageio/icc.cpp
  src/imageio/jpg.cpp
  src/imageio/jxl.cpp
  src/imageio/pfm.cpp
  src/imageio/png.cpp
  src/imageio/stb.cpp
  src/imageio/qoi.cpp
  src/imageio/uhdr.cpp
  src/dear_widgets.cpp
  src/dear_widgets.h
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
    -sEXPORTED_RUNTIME_METHODS=[ccall,HEAPU8]
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
