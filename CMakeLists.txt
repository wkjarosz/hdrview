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
option(HDRVIEW_ENABLE_LIBPNG "Enable LIBPNG support" ON)
option(HDRVIEW_ENABLE_LIBJPEG "Enable libjpeg(-turbo) support" ON)
option(UHDR_BUILD_DEPS "Whether to build uhdr dependencies instead of using system-installed ones" ON)
option(HDRVIEW_ENABLE_LIBUHDR "Whether to build with Ultra HDR support" ON)
option(HDRVIEW_ENABLE_LIBJXL "Enable JPEG-XL support" ON)
option(HDRVIEW_ENABLE_J2K "Enable JPEG-2000 support" ON)
option(HDRVIEW_ENABLE_HTJ2K "Enable high-throughput JPEG2000 (HTJ2K) support" ON)
option(HDRVIEW_ENABLE_AVIF "Enable AVIF support" ON)
option(HDRVIEW_ENABLE_HEIC "Enable HEIC support" ON)
option(HDRVIEW_ENABLE_AVCI "Enable AVCI support" ON)
option(HDRVIEW_ENABLE_LIBWEBP "Enable LIBWEBP support" ON)
option(HDRVIEW_ENABLE_LIBTIFF "Enable LIBTIFF support" ON)
option(HDRVIEW_ENABLE_LIBRAW "Enable LibRaw support" ON)
option(HDRVIEW_ENABLE_X3F "Enable LibRaw X3F support" ON)

set(HDRVIEW_ENABLE_LIBHEIF OFF)
if(HDRVIEW_ENABLE_J2K
   OR HDRVIEW_ENABLE_HTJ2K
   OR HDRVIEW_ENABLE_AVIF
   OR HDRVIEW_ENABLE_HEIC
   OR HDRVIEW_ENABLE_AVCI
)
  set(HDRVIEW_ENABLE_LIBHEIF ON)
endif()

set(CMAKE_INCLUDE_DIRECTORIES_PROJECT_BEFORE ON)

# Set the framework search order to last, so that we can find libraries in the system directories first. This is to
# prevent CMAKE from finding old versions of headers, e.g. libjpeg in /Library/Frameworks/Mono.framework/Headers, and
# first check other install locations like brew.
set(CMAKE_FIND_FRAMEWORK LAST)

# cmake-format: off
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
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/*.png
  )
  list(APPEND MY_RESOURCE_FILES ${ADDITIONAL_RESOURCE_FILES})
elseif(APPLE)
  file(
    GLOB_RECURSE ADDITIONAL_RESOURCE_FILES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Roboto/**.* ${CMAKE_CURRENT_SOURCE_DIR}/assets/app_settings/apple/**.*
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/*.metal ${CMAKE_CURRENT_SOURCE_DIR}/assets/*.png
  )
  list(APPEND MY_RESOURCE_FILES ${ADDITIONAL_RESOURCE_FILES})
elseif(WIN32)
  file(
    GLOB_RECURSE ADDITIONAL_RESOURCE_FILES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Roboto/**.* ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/*.glsl
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/*.png
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

# Function to mark a library's includes as SYSTEM to suppress compiler warnings
# Usage: mark_as_system_library(target_name)
function(mark_as_system_library target_name)
  if(TARGET ${target_name})
    # Resolve the actual target if this is an alias
    get_target_property(aliased_target ${target_name} ALIASED_TARGET)
    if(aliased_target)
      set(actual_target ${aliased_target})
    else()
      set(actual_target ${target_name})
    endif()
    
    # Get the library's existing interface includes
    get_target_property(lib_includes ${actual_target} INTERFACE_INCLUDE_DIRECTORIES)
    
    if(lib_includes)
      # Get existing system includes (if any) to avoid overwriting
      get_target_property(existing_system_includes ${actual_target} INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
      
      # Combine existing system includes with the library's includes
      if(existing_system_includes)
        set(new_system_includes "${existing_system_includes};${lib_includes}")
      else()
        set(new_system_includes "${lib_includes}")
      endif()
      
      # Update the target to use SYSTEM includes
      set_target_properties(${actual_target} PROPERTIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${new_system_includes}"
      )
      message(STATUS "Marked ${target_name} includes as SYSTEM (actual target: ${actual_target})")
    endif()
  else()
    message(WARNING "Target ${target_name} does not exist, cannot mark as SYSTEM")
  endif()
endfunction()

# Function to disable warnings when compiling a third-party library target
# Usage: disable_target_warnings(target_name)
function(disable_target_warnings target_name)
  if(TARGET ${target_name})
    # Resolve the actual target if this is an alias
    get_target_property(aliased_target ${target_name} ALIASED_TARGET)
    if(aliased_target)
      set(actual_target ${aliased_target})
    else()
      set(actual_target ${target_name})
    endif()
    # Determine the target type so we can set appropriate compile option scope.
    get_target_property(_target_type ${actual_target} TYPE)
    if(NOT _target_type)
      set(_target_type "")
    endif()

    if(MSVC)
      # Remove any existing warning flags set by the target's own CMakeLists
      get_target_property(existing_flags ${actual_target} COMPILE_OPTIONS)
      if(existing_flags)
        list(FILTER existing_flags EXCLUDE REGEX "^/[Ww][0-4]?")
        set_target_properties(${actual_target} PROPERTIES COMPILE_OPTIONS "${existing_flags}")
      endif()
      # Use /w to disable all warnings, add it before other flags to avoid D9025 warnings
      if(_target_type STREQUAL "INTERFACE_LIBRARY")
        target_compile_options(${actual_target} BEFORE INTERFACE /w)
      else()
        target_compile_options(${actual_target} BEFORE PRIVATE /w)
      endif()
    else()
      # For INTERFACE targets we must set INTERFACE properties; for others use PRIVATE
      if(_target_type STREQUAL "INTERFACE_LIBRARY")
        target_compile_options(${actual_target} INTERFACE -w)
      else()
        target_compile_options(${actual_target} PRIVATE -w)
      endif()
    endif()
    message(STATUS "Disabled warnings for ${target_name} (actual target: ${actual_target})")
  else()
    message(WARNING "Target ${target_name} does not exist, cannot disable warnings")
  endif()
endfunction()

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

  if(NOT glfw_ADDED)
    message(FATAL_ERROR "Could not download custom fork of glfw library.")
  endif()
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

set(HELLOIMGUI_EMSCRIPTEN_PTHREAD OFF)
set(HELLOIMGUI_EMSCRIPTEN_PTHREAD_ALLOW_MEMORY_GROWTH OFF)
set(IMGUI_DISABLE_OBSOLETE_FUNCTIONS ON)

CPMAddPackage(
  NAME hello_imgui
  GITHUB_REPOSITORY pthom/hello_imgui
  # URL https://github.com/pthom/hello_imgui/archive/refs/tags/v1.92.5.zip
  VERSION 1.92.5
  GIT_TAG v1.92.5
  OPTIONS "HELLOIMGUI_EMSCRIPTEN_PTHREAD OFF" "HELLOIMGUI_EMSCRIPTEN_PTHREAD_ALLOW_MEMORY_GROWTH OFF"
          "IMGUI_DISABLE_OBSOLETE_FUNCTIONS ON"
  SYSTEM YES
)
if(hello_imgui_ADDED)
  message(STATUS "Will build hello_imgui library")
  if(HELLOIMGUI_EMSCRIPTEN_PTHREAD)
    add_compile_options(-pthread)
  endif()
  # Disable warnings for third-party compiled targets
  disable_target_warnings(imgui)
  disable_target_warnings(hello_imgui)
  disable_target_warnings(stb_hello_imgui)
  disable_target_warnings(plutosvg)
elseif(hello_imgui_FOUND OR TARGET HELLOIMGUI::hello_imgui)
  message(STATUS "Using system-installed hello_imgui library")
else()
  message(FATAL_ERROR "Could not find or download hello_imgui library.")
endif()

CPMAddPackage("gh:epezent/implot#3da8bd34299965d3b0ab124df743fe3e076fa222")
if(NOT implot_ADDED)
  message(FATAL_ERROR "Could not download implot library.")
endif()
message(STATUS "implot library added")
# implot has no CMake support, so we create our own target
add_library(
  implot STATIC ${implot_SOURCE_DIR}/implot.h ${implot_SOURCE_DIR}/implot_internal.h ${implot_SOURCE_DIR}/implot.cpp
                ${implot_SOURCE_DIR}/implot_items.cpp ${implot_SOURCE_DIR}/implot_demo.cpp
)
target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot PUBLIC imgui)
set_target_properties(implot PROPERTIES CXX_STANDARD 17)
disable_target_warnings(implot)
mark_as_system_library(implot)
list(APPEND HDRVIEW_DEPENDENCIES "implot")

CPMAddPackage(
  NAME my_imgui_command_palette
  GITHUB_REPOSITORY wkjarosz/imgui-command-palette
  GIT_TAG 425e8c8be56970b3d2173cf1d764614c9c978345
  DOWNLOAD_ONLY YES
  SYSTEM YES
)
if(NOT my_imgui_command_palette_ADDED)
  message(FATAL_ERROR "Could not download imgui-command-palette library.")
endif()
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
disable_target_warnings(my_imgui_command_palette)
mark_as_system_library(my_imgui_command_palette)
list(APPEND HDRVIEW_DEPENDENCIES "my_imgui_command_palette")

CPMAddPackage("gh:sgorsten/linalg@2.2")
if(NOT linalg_ADDED)
  message(FATAL_ERROR "Could not download linalg library.")
endif()
message(STATUS "linalg library added")
add_library(linalg INTERFACE IMPORTED)
target_include_directories(linalg INTERFACE "${linalg_SOURCE_DIR}")
list(APPEND HDRVIEW_DEPENDENCIES "linalg")

CPMAddPackage(
  NAME spdlog
  VERSION 1.14.0 # this is the first version that has MDC support, unfortunately ubuntu 22.04 has 1.12.0
  URL https://github.com/gabime/spdlog/archive/refs/tags/v1.16.0.zip
  OPTIONS "SPDLOG_INSTALL NO" # don't create an installable target
  SYSTEM YES
)
if(spdlog_ADDED)
  message(STATUS "Will build spdlog library")
elseif(spdlog_FOUND OR TARGET spdlog::spdlog)
  message(STATUS "Using system-installed spdlog library")
else()
  message(FATAL_ERROR "Could not find or download spdlog library.")
endif()
list(APPEND HDRVIEW_DEPENDENCIES "spdlog::spdlog")

CPMAddPackage("gh:wkjarosz/smallthreadpool#386dc5d7d5eaf60cb00b34345170548a0c0f3ef9")
if(NOT smallthreadpool_ADDED)
  message(FATAL_ERROR "Could not download smallthreadpool library.")
endif()
message(STATUS "smallthreadpool library added")
# Create implementation library with wrapper
configure_file("${CMAKE_SOURCE_DIR}/cmake/stp_impl.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/stp_impl.cpp" COPYONLY)
add_library(stp STATIC "${CMAKE_CURRENT_BINARY_DIR}/stp_impl.cpp")
target_include_directories(stp PUBLIC "${smallthreadpool_SOURCE_DIR}")
target_link_libraries(stp PRIVATE spdlog::spdlog)
target_compile_features(stp PUBLIC cxx_std_17)
# add_library(stp INTERFACE IMPORTED)
# target_include_directories(stp INTERFACE "${smallthreadpool_SOURCE_DIR}")
list(APPEND HDRVIEW_DEPENDENCIES "stp")

CPMAddPackage("gh:wkjarosz/stb#491e9c3ac2add5bda0fffb5a8cb99868c483506b")
if(NOT stb_ADDED)
  message(FATAL_ERROR "Could not download stb library.")
endif()
message(STATUS "stb library added")
add_library(stb INTERFACE IMPORTED)
target_include_directories(stb INTERFACE "${stb_SOURCE_DIR}")
list(APPEND HDRVIEW_DEPENDENCIES "stb")

CPMAddPackage("gh:phoboslab/qoi#b0b926ee70108b0113dd2718d13d2207c474ed2e")
if(NOT qoi_ADDED)
  message(FATAL_ERROR "Could not download qoi library.")
endif()
message(STATUS "qoi library added")
# Create implementation library with wrapper
configure_file("${CMAKE_SOURCE_DIR}/cmake/qoi_impl.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/qoi_impl.cpp" COPYONLY)
add_library(qoi STATIC "${CMAKE_CURRENT_BINARY_DIR}/qoi_impl.cpp")
target_include_directories(qoi PUBLIC "${qoi_SOURCE_DIR}")
disable_target_warnings(qoi)
mark_as_system_library(qoi)
list(APPEND HDRVIEW_DEPENDENCIES "qoi")

if(EMSCRIPTEN)

  CPMAddPackage("gh:Armchair-Software/emscripten-browser-file#d21dec772050d581c02aa9820e4d877fdb060dc8")
  if(NOT emscripten-browser-file_ADDED)
    message(FATAL_ERROR "Could not download emscripten-browser-file library.")
  endif()
  message(STATUS "emscripten-browser-file library added")
  add_library(emscripten-browser-file INTERFACE IMPORTED)
  target_include_directories(emscripten-browser-file INTERFACE "${emscripten-browser-file_SOURCE_DIR}")
  list(APPEND HDRVIEW_DEPENDENCIES "emscripten-browser-file")

  CPMAddPackage("gh:Armchair-Software/emscripten-browser-clipboard#bcba48fee22551b458fe5e1e513f6402c810d886")
  if(NOT emscripten-browser-clipboard_ADDED)
    message(FATAL_ERROR "Could not download emscripten-browser-clipboard library.")
  endif()
  message(STATUS "emscripten-browser-clipboard library added")
  add_library(emscripten-browser-clipboard INTERFACE IMPORTED)
  target_include_directories(emscripten-browser-clipboard INTERFACE "${emscripten-browser-clipboard_SOURCE_DIR}")
  list(APPEND HDRVIEW_DEPENDENCIES "emscripten-browser-clipboard")

else()

  CPMAddPackage(
    NAME portable-file-dialogs
    GITHUB_REPOSITORY wkjarosz/portable-file-dialogs
    GIT_TAG 1d755d814b77e546f544266bc24ac76029b8763b
    SYSTEM YES
  )
  if(NOT portable-file-dialogs_ADDED)
    message(FATAL_ERROR "Could not download portable-file-dialogs library.")
  endif()
  message(STATUS "portable-file-dialogs library added")
  add_library(portable-file-dialogs INTERFACE IMPORTED)
  target_include_directories(portable-file-dialogs INTERFACE "${portable-file-dialogs_SOURCE_DIR}")
  list(APPEND HDRVIEW_DEPENDENCIES "portable-file-dialogs")

endif()

CPMAddPackage(
  NAME libexif
  GITHUB_REPOSITORY Tom94/libexif
  GIT_TAG f99967d09f1424170388cb2c0b65dcf67d6f5ac3
  FIND_PACKAGE_ARGUMENTS "QUIET"
  OPTIONS "BUILD_SHARED_LIBS OFF"
  VERSION 0.6.24
  SYSTEM YES
)

if(libexif_ADDED AND TARGET exif_static)
  message(STATUS "Will build exif library")
  disable_target_warnings(exif_static)
  list(APPEND HDRVIEW_DEPENDENCIES "exif_static")
elseif(libexif_FOUND AND TARGET exif)
  message(STATUS "Using system-installed exif library (version ${LIBEXIF_VERSION})")
  list(APPEND HDRVIEW_DEPENDENCIES "exif")
else()
  message(FATAL_ERROR "Could not find or download exif library.")
endif()


if(HDRVIEW_ENABLE_LIBPNG OR HDRVIEW_ENABLE_LIBHEIF)
  # cmake-format: off
  # Build zlib ourselves if:
  # - On Windows/Emscripten (system zlib may not be available)
  # - Building a universal binary on macOS (need both architectures)
  # - Cross-compiling on macOS (CMAKE_OSX_ARCHITECTURES is set and differs from host)
  set(NEED_CUSTOM_ZLIB FALSE)
  if(WIN32 OR MSVC OR EMSCRIPTEN)
    set(NEED_CUSTOM_ZLIB TRUE)
  elseif(APPLE AND CMAKE_OSX_ARCHITECTURES)
    # Check if building universal binary
    if(CMAKE_OSX_ARCHITECTURES MATCHES ".*arm64.*" AND CMAKE_OSX_ARCHITECTURES MATCHES ".*x86_64.*")
      set(NEED_CUSTOM_ZLIB TRUE)
    else()
      # Check if cross-compiling (target arch differs from host)
      if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "${CMAKE_HOST_SYSTEM_PROCESSOR}")
        set(NEED_CUSTOM_ZLIB TRUE)
      endif()
    endif()
  endif()
  
  if(NEED_CUSTOM_ZLIB)
    CPMAddPackage(
      NAME zlib-cmake
      VERSION 1.2.12
      URL https://github.com/jimmy-park/zlib-cmake/archive/main.tar.gz
      #FIND_PACKAGE_ARGUMENTS "NAMES zlib ZLIB"
      OPTIONS "ZLIB_TEST OFF" "ZLIB_INSTALL ON"
      SYSTEM YES
    )
    if(zlib-cmake_ADDED)
      message(STATUS "Will build zlib library")
      set_target_properties(ZLIB PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
      disable_target_warnings(ZLIB)
    elseif(TARGET ZLIB::ZLIB)
      # Target exists but wasn't built by CPM, so it was found by find_package()
      if(DEFINED ZLIB_VERSION_STRING)
        message(STATUS "Using system-installed zlib library (version ${ZLIB_VERSION_STRING})")
      else()
        message(STATUS "Using system-installed zlib library")
      endif()
    else()
      message(FATAL_ERROR "zlib not found.")
    endif()
  endif()
  # cmake-format: on
endif()

if(HDRVIEW_ENABLE_LIBPNG)
  CPMAddPackage(
    NAME PNG
    GITHUB_REPOSITORY pnggroup/libpng
    GIT_TAG 62954d33fe729566e9d17b78ddaf013896a1f3c5
    VERSION 1.6.0
    # URL https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.51.zip
    OPTIONS "PNG_SHARED OFF"
            "PNG_STATIC ON"
            "PNG_FRAMEWORK OFF"
            "PNG_TESTS OFF"
            "PNG_TOOLS OFF"
            "PNG_ARM_NEON off" # for now we disable NEON to avoid build issues on some CI machines
            "SKIP_INSTALL_ALL ON"
            "EXCLUDE_FROM_ALL YES"
    SYSTEM YES
  )
  if(PNG_ADDED)
    message(STATUS "Will build libpng library")
    # set_target_properties(png_shared PROPERTIES POSITION_INDEPENDENT_CODE TRUE) set_target_properties(png_static
    # PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
    # mark_as_system_library(png_static)
    disable_target_warnings(png_static)
    list(APPEND HDRVIEW_DEPENDENCIES "png_static")
  elseif(TARGET PNG::PNG)
    message(STATUS "Using system-installed libpng library")
    list(APPEND HDRVIEW_DEPENDENCIES "PNG::PNG")
  else()
    message(FATAL_ERROR "libpng not found. Please install it or turn off HDRVIEW_ENABLE_LIBPNG.")
  endif()
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
  message(STATUS "Will build Imath library")
elseif(TARGET Imath::Imath)
  message(STATUS "Will link against system-installed Imath library")
else()
  message(FATAL_ERROR "Imath library not found. Please install it.")
endif()

if(EMSCRIPTEN AND NOT HELLOIMGUI_EMSCRIPTEN_PTHREAD)
  set(OPENEXR_THREADING_OPTIONS "OPENEXR_ENABLE_THREADING OFF")
else()
  set(OPENEXR_THREADING_OPTIONS "OPENEXR_ENABLE_THREADING ON")
endif()

CPMAddPackage(
  NAME OpenEXR
  # GITHUB_REPOSITORY AcademySoftwareFoundation/openexr 
  FIND_PACKAGE_ARGUMENTS "REQUIRED"
  # NOTE: OpenEXR v3.3.0 broke subsampled channel loading, but it seems to be fixed starting with v3.4
  VERSION 3.1.5 # this is the version available in ubuntu 24.04
  GIT_TAG v3.4.4
  URL https://github.com/AcademySoftwareFoundation/openexr/archive/refs/tags/v3.4.4.zip
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
  SYSTEM YES
)
if(OpenEXR_ADDED)
  message(STATUS "Will build OpenEXR library with threading options: ${OPENEXR_THREADING_OPTIONS}")
  # Disable warnings for all OpenEXR component targets
  disable_target_warnings(OpenEXR)
  disable_target_warnings(Iex)
  disable_target_warnings(IlmThread)
  disable_target_warnings(OpenEXRCore)
  disable_target_warnings(OpenEXRUtil)
  disable_target_warnings(Imath)
elseif(TARGET OpenEXR::OpenEXR)
  message(STATUS "Will link against system-installed OpenEXR library")
else()
  message(FATAL_ERROR "OpenEXR library not found. Please install it.")
endif()
list(APPEND HDRVIEW_DEPENDENCIES "OpenEXR::OpenEXR")

# cmake-format: off
# libjpeg-turbo
if(HDRVIEW_ENABLE_LIBJPEG)
  find_package(JPEG)
  if(JPEG_FOUND)
    list(APPEND HDRVIEW_DEFINITIONS "HDRVIEW_ENABLE_LIBJPEG=1")

    # Test whether libjpeg provides jpeg_write_icc_profile
    include(CheckCXXSourceCompiles)

    set(JPEG_ICC_TEST_SOURCE
      "
      #include <stddef.h>
      #include <stdio.h>
      #include <jpeglib.h>

      int main() {
        jpeg_write_icc_profile((j_compress_ptr)0, (const JOCTET*)0, 0);
        return 0;
      }
      "
    )

    # Prefer linking via imported target if available
    if(TARGET JPEG::JPEG)
      # write source and run try_compile linking the imported target
      file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cmake_check_jpeg_icc.cpp" "${JPEG_ICC_TEST_SOURCE}")
      try_compile(HAVE_JPEG_WRITE_ICC_PROFILE
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_check_jpeg_icc"
        SOURCES "${CMAKE_CURRENT_BINARY_DIR}/cmake_check_jpeg_icc.cpp"
        LINK_LIBRARIES JPEG::JPEG
      )
    else()
      # fallback: use the existing check with required includes/libs
      set(CMAKE_REQUIRED_LIBRARIES ${JPEG_LIBRARIES})
      set(CMAKE_REQUIRED_INCLUDES ${JPEG_INCLUDE_DIRS})
      check_cxx_source_compiles("${JPEG_ICC_TEST_SOURCE}" HAVE_JPEG_WRITE_ICC_PROFILE)
      unset(CMAKE_REQUIRED_LIBRARIES)
      unset(CMAKE_REQUIRED_INCLUDES)
    endif()

    message(STATUS "Found libjpeg with HAVE_JPEG_WRITE_ICC_PROFILE=${HAVE_JPEG_WRITE_ICC_PROFILE}")

    find_package(TurboJpeg QUIET)
    message(STATUS "JPEG is libjpeg-turbo = ${TurboJpeg_FOUND}")

    mark_as_system_library(JPEG::JPEG)
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

      # ExternalProject_Add_Step(${JPEGTURBO_TARGET_NAME} print_jpeglib_h
      #   COMMAND ${CMAKE_COMMAND} -E echo "========== src/jpeglib.h =========="
      #   COMMAND ${CMAKE_COMMAND} -E cat ${JPEGTURBO_SOURCE_DIR}/src/jpeglib.h
      #   DEPENDEES download
      #   ALWAYS 1
      # )
    endif()

    # add_custom_command(
    #   TARGET ${JPEGTURBO_TARGET_NAME}
    #   POST_BUILD
    #   COMMAND ${CMAKE_COMMAND} -E copy_if_different
    #           ${JPEGTURBO_SOURCE_DIR}/src/jconfig.h
    #           ${JPEGTURBO_BINARY_DIR}/jconfig.h
    #   COMMENT "Copying jconfig.h to build directory"
    # )

    # The following variables ensure that find_package can find libjpeg-turbo
    set(JPEG_LIBRARY ${JPEG_LIBRARIES})
    # Expose both the libjpeg-turbo build/include (binary dir) and the
    # source include directory so external projects (CPM/ExternalProject)
    # that look for `JPEG_INCLUDE_DIR`/`JPEG_INCLUDE_DIRS` will find
    # the generated `jconfig.h` as well as the public headers.
    # Provide a path that some CMake Find modules expect to be a file
    # (e.g. FindJPEG may read the file with file(STRINGS ...)). Point it
    # at the primary header so modules don't accidentally try to read a
    # directory as a file on Windows.
    set(JPEG_INCLUDE_DIR ${JPEGTURBO_SOURCE_DIR}/src/jpeglib.h CACHE INTERNAL "JPEG include dir")
    set(JPEG_INCLUDE_DIRS "${JPEGTURBO_BINARY_DIR};${JPEGTURBO_SOURCE_DIR}/src" CACHE INTERNAL "JPEG include dirs")

    set(HAVE_JPEG_WRITE_ICC_PROFILE TRUE)

    add_library(jpeg-turbo INTERFACE IMPORTED GLOBAL)
    add_dependencies(jpeg-turbo ${JPEGTURBO_TARGET_NAME})
    set_target_properties(
      jpeg-turbo PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${JPEG_INCLUDE_DIRS}" INTERFACE_LINK_LIBRARIES
                                                                                 "${JPEG_LIBRARIES}"
    )
    add_library(JPEG::JPEG ALIAS jpeg-turbo)

    # disable_target_warnings(jpeg-turbo)
    mark_as_system_library(jpeg-turbo)
    list(APPEND HDRVIEW_DEPENDENCIES jpeg-turbo)

    # Pre-populate JPEG variables so other dependency's find_package(JPEG) will use this one
    set(JPEG_FOUND TRUE CACHE INTERNAL "JPEG found")
    set(JPEG_LIBRARIES JPEG::JPEG CACHE INTERNAL "JPEG library")
    get_target_property(JPEG_INCLUDE_DIRS_TMP JPEG::JPEG INTERFACE_INCLUDE_DIRECTORIES)
    if(JPEG_INCLUDE_DIRS_TMP)
      # Try to find a usable header path from the interface include directories.
      # Prefer an explicit file entry, then a directory that contains jpeglib.h.
      set(_JPEG_HEADER_FOUND "")
      foreach(_inc IN LISTS JPEG_INCLUDE_DIRS_TMP)
        if(EXISTS "${_inc}" AND NOT IS_DIRECTORY "${_inc}")
          # _inc is a file (some packages export a direct header path)
          set(_JPEG_HEADER_FOUND "${_inc}")
          break()
        endif()
        if(EXISTS "${_inc}/jpeglib.h")
          set(_JPEG_HEADER_FOUND "${_inc}/jpeglib.h")
          break()
        endif()
      endforeach()

      if(_JPEG_HEADER_FOUND)
        set(JPEG_INCLUDE_DIR "${_JPEG_HEADER_FOUND}" CACHE INTERNAL "JPEG include dir" FORCE)
      else()
        # Fallback: use the source header so FindJPEG won't try to read a directory on Windows.
        set(JPEG_INCLUDE_DIR "${JPEGTURBO_SOURCE_DIR}/src/jpeglib.h" CACHE INTERNAL "JPEG include dir" FORCE)
      endif()
      unset(_JPEG_HEADER_FOUND)
    endif()
    set(JPEG_VERSION TRUE CACHE INTERNAL 80)
    message(STATUS "Providing JPEG target to other dependencies")
  endif()
  if (HAVE_JPEG_WRITE_ICC_PROFILE)
    add_definitions(-DHAVE_JPEG_WRITE_ICC_PROFILE=1)
  endif ()
  message(STATUS "JPEG_LIBRARIES: ${JPEG_LIBRARIES}")
  message(STATUS "JPEG_INCLUDE_DIR: ${JPEG_INCLUDE_DIR}")
  message(STATUS "JPEG_INCLUDE_DIRS: ${JPEG_INCLUDE_DIRS}")
endif()
# cmake-format: on

# cmake-format: off
if(HDRVIEW_ENABLE_LIBUHDR)
  CPMAddPackage(
    NAME libuhdr
    GITHUB_REPOSITORY google/libultrahdr
    # VERSION 1.4.0
    GIT_TAG 5ed39d67cd31d254e84ebf76b03d4b7bcc12e2f7
    OPTIONS "UHDR_BUILD_BENCHMARK OFF"
            "UHDR_BUILD_FUZZERS OFF"
            "JPEG_FOUND TRUE"
            "BUILD_SHARED_LIBS OFF"
            "UHDR_BUILD_DEPS ${UHDR_BUILD_DEPS}"
            "UHDR_BUILD_EXAMPLES OFF"
            "UHDR_BUILD_TESTS OFF"
            "UHDR_ENABLE_INSTALL OFF"
    SYSTEM YES
  )
  if(libuhdr_ADDED)
    message(STATUS "Will build uhdr library with UHDR_BUILD_DEPS=${UHDR_BUILD_DEPS}")
    disable_target_warnings(uhdr)
  elseif(TARGET core)
    message(STATUS "Will link against system-installed uhdr library")
  else()
    message(FATAL_ERROR "uhdr library not found. Please install it or turn off HDRVIEW_ENABLE_LIBUHDR.")
  endif()
  message(STATUS "setting uhdr dependent on ${JPEGTURBO_TARGET_NAME}")

  if (TARGET core)
    add_dependencies(core JPEG::JPEG)
    list(APPEND HDRVIEW_DEPENDENCIES "core")
  endif()
  if (TARGET uhdr)
    add_dependencies(uhdr JPEG::JPEG)
    list(APPEND HDRVIEW_DEPENDENCIES "uhdr")
  endif()
endif()
# cmake-format: on

if(HDRVIEW_ENABLE_LIBJXL)
  CPMAddPackage(
    NAME JXL
    GITHUB_REPOSITORY libjxl/libjxl
    VERSION 0.7.0 # version in ubuntu 24.04
    URL https://github.com/libjxl/libjxl/archive/refs/tags/v0.11.1.zip
    # GIT_TAG 6e83d97e73c68ff7d3374f939971bab617be4c3c
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
            "JPEGXL_ENABLE_VIEWERS OFF"
            "JPEGXL_ENABLE_PLUGINS OFF"
            "JPEGXL_BUNDLE_LIBPNG OFF"
            "JPEGXL_ENABLE_SJPEG OFF"
            # "JPEGXL_FORCE_SYSTEM_LCMS2 ON"
            "HWY_ENABLE_TESTS OFF"
            "HWY_ENABLE_INSTALL OFF"
            "HWY_ENABLE_EXAMPLES OFF"
            "HWY_ENABLE_CONTRIB OFF"
            "EXCLUDE_FROM_ALL YES"
    SYSTEM YES
  )
  if(JXL_ADDED)
    message(STATUS "Will build JPEG-XL library")
    # make the lcms2 target available for us to link against
    # target_link_libraries(jxl PUBLIC lcms2)
    # Disable warnings for all jxl targets
    disable_target_warnings(jxl)
    disable_target_warnings(jxl_dec)
    # disable_target_warnings(jxl_enc)
    disable_target_warnings(jxl_threads)
    disable_target_warnings(jxl_dec-obj)
    disable_target_warnings(jxl_enc-obj)
    disable_target_warnings(jxl_dec-internal)
    disable_target_warnings(jxl-internal)
    disable_target_warnings(jxl_cms)
    disable_target_warnings(enc_fast_lossless)
    disable_target_warnings(hwy)
    # After CPM adds JXL, use the source directory variable
    if(JXL_SOURCE_DIR)
      message(STATUS "Adding include directory for bundled Highway: ${JXL_SOURCE_DIR}/third_party/highway")
      # Prioritize the bundled Highway over system installation
      include_directories(BEFORE SYSTEM 
        ${JXL_SOURCE_DIR}/third_party/highway
      )
    else()
      message(STATUS "Adding include directory for bundled Highway: ${CMAKE_BINARY_DIR}/cpm_modules/jxl/39d3/third_party/highway")
      include_directories(BEFORE SYSTEM 
        ${CMAKE_BINARY_DIR}/cpm_modules/jxl/39d3/third_party/highway
        )
    endif()
  elseif(TARGET jxl)
    message(STATUS "Using system-installed JPEG-XL library")
  else()
    message(FATAL_ERROR "JPEG-XL library not found. Please install it or turn off HDRVIEW_ENABLE_LIBJXL.")
  endif()
  list(APPEND HDRVIEW_DEPENDENCIES "jxl" "jxl_threads")
endif()

if(TARGET lcms2)
  message(STATUS "Using lcms2 target already provided by libjxl")
  # list(APPEND HDRVIEW_DEPENDENCIES lcms2)
  list(APPEND HDRVIEW_DEFINITIONS "HDRVIEW_ENABLE_LCMS2=1")

  # Set cache variables for so other dependency's find_package(LCMS2) finds this one
  # Get the include directories from the lcms2 target
  get_target_property(LCMS2_INCLUDE_DIRS lcms2 INTERFACE_INCLUDE_DIRECTORIES)
  if(NOT LCMS2_INCLUDE_DIRS)
    get_target_property(LCMS2_INCLUDE_DIRS lcms2 INCLUDE_DIRECTORIES)
  endif()
  # Note: FindLCMS2 checks for LCMS2_INCLUDE_DIR (singular) not LCMS2_INCLUDE_DIRS (plural)
  set(LCMS2_INCLUDE_DIR "${LCMS2_INCLUDE_DIRS}" CACHE PATH "LCMS2 include dir from jxl" FORCE)
  set(LCMS2_LIBRARIES lcms2 CACHE STRING "LCMS2 library from jxl" FORCE)
  set(LCMS2_FOUND TRUE CACHE BOOL "LCMS2 found via jxl" FORCE)
  message(STATUS "Providing lcms2 target (from libjxl) to other dependencies: ${LCMS2_INCLUDE_DIR}")
else()
  find_package(LCMS2 REQUIRED)
  if(LCMS2_FOUND)
    message(STATUS "Using system-installed lcms2 library")
    mark_as_system_library(lcms2)
    list(APPEND HDRVIEW_DEPENDENCIES "lcms2")
    list(APPEND HDRVIEW_DEFINITIONS "HDRVIEW_ENABLE_LCMS2=1")
  else()
    message(FATAL_ERROR "lcms2 library not found.")
  endif()
endif()

if(HDRVIEW_ENABLE_LIBHEIF)
  CPMAddPackage(
    NAME Libheif
    # GITHUB_REPOSITORY strukturag/libheif
    VERSION 1.17.6 # set minimum version to the one in ubuntu 24.04
    URL https://github.com/strukturag/libheif/archive/refs/tags/v1.20.2.zip
    # GIT_TAG 95428fdf22b2e0ec3ad4515c882fa0d6dd663f0a
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
            "ENABLE_PLUGIN_LOADING OFF"
            "WITH_LIBSHARPYUV OFF"
            "WITH_LIBDE265 ${HDRVIEW_ENABLE_HEIC}"
            "WITH_X265 ${HDRVIEW_ENABLE_HEIC}"
            "WITH_OpenJPEG_ENCODER ${HDRVIEW_ENABLE_J2K}"
            "WITH_OpenJPEG_DECODER ${HDRVIEW_ENABLE_J2K}"
            "WITH_OPENJPH_ENCODER ${HDRVIEW_ENABLE_HTJ2K}"
            "WITH_OpenH264_ENCODER ${HDRVIEW_ENABLE_AVCI}"
            "WITH_OpenH264_DECODER ${HDRVIEW_ENABLE_AVCI}"
            "WITH_JPEG_ENCODER ${HDRVIEW_ENABLE_LIBJPEG}"
            "WITH_JPEG_DECODER ${HDRVIEW_ENABLE_LIBJPEG}"
            "WITH_AOM_ENCODER ${HDRVIEW_ENABLE_AVIF}"
            "WITH_AOM_DECODER ${HDRVIEW_ENABLE_AVIF}"
            "HAVE_JPEG_WRITE_ICC_PROFILE ${HAVE_JPEG_WRITE_ICC_PROFILE}"
    SYSTEM YES
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
    # Ensure heif waits for turbojpeg to be built
    if(TARGET ${JPEGTURBO_TARGET_NAME})
      add_dependencies(heif ${JPEGTURBO_TARGET_NAME})
    endif()
    # Ensure heif has the JPEG include paths and link information so its plugins can find jpeglib.h
    if(TARGET JPEG::JPEG)
      target_link_libraries(heif PRIVATE JPEG::JPEG)
    elseif(JPEG_INCLUDE_DIRS)
      target_include_directories(heif PRIVATE ${JPEG_INCLUDE_DIRS})
    endif()
    mark_as_system_library(heif)
    disable_target_warnings(heif)
  elseif(TARGET heif)
    message(STATUS "Using system-installed libheif library")
  else()
    message(
      FATAL_ERROR
        "libheif not found. Please install it or turn off HDRVIEW_ENABLE_J2K, HDRVIEW_ENABLE_HTJ2K, HDRVIEW_ENABLE_AVIF, HDRVIEW_ENABLE_HEIC, and HDRVIEW_ENABLE_AVCI."
    )
  endif()
  list(APPEND HDRVIEW_DEPENDENCIES "heif")
endif()

if(HDRVIEW_ENABLE_LIBWEBP)
  # Disable SIMD for Emscripten to avoid AVX2 pointer type issues
  if(EMSCRIPTEN)
    set(WEBP_SIMD_ENABLED OFF)
  else()
    set(WEBP_SIMD_ENABLED ON)
  endif()
  CPMAddPackage(
    NAME libwebp
    VERSION 1.3.2 # version in ubuntu 24.04
    URL https://github.com/webmproject/libwebp/archive/refs/tags/v1.6.0.zip
    FIND_PACKAGE_ARGUMENTS "QUIET"
    # DOWNLOAD_ONLY YES
    OPTIONS "WEBP_LINK_STATIC ON"
            "WEBP_ENABLE_SIMD ${WEBP_SIMD_ENABLED}"
            "WEBP_BUILD_ANIM_UTILS OFF"
            "WEBP_BUILD_CWEBP OFF"
            "WEBP_BUILD_DWEBP OFF"
            "WEBP_BUILD_GIF2WEBP OFF"
            "WEBP_BUILD_IMG2WEBP OFF"
            "WEBP_BUILD_VWEBP OFF"
            "WEBP_BUILD_WEBPINFO OFF"
            "WEBP_BUILD_LIBWEBPMUX OFF"
            "WEBP_BUILD_WEBPMUX ON"
            "WEBP_BUILD_EXTRAS OFF"
            "WEBP_BUILD_WEBP_JS OFF"
            "WEBP_BUILD_FUZZTEST OFF"
            "WEBP_USE_THREAD ON"
            "WEBP_NEAR_LOSSLESS ON"
            "WEBP_ENABLE_SWAP_16BIT_CSP ON"
    SYSTEM YES
  )
  if(libwebp_ADDED)
    message(STATUS "Will build libwebp library")
  elseif(libwebp_FOUND AND TARGET webp AND TARGET webpdemux)
    message(STATUS "Using system-installed libwebp library (version ${WEBP_VERSION})")
  elseif(TARGET webp AND TARGET webpdemux)
    message(STATUS "Using previously built libwebp library")
  else()
    message(FATAL_ERROR "libwebp library not found. Please install it.")
  endif()
  # Link both decoder and encoder libraries, plus demux for metadata extraction
  list(APPEND HDRVIEW_DEPENDENCIES "webp" "webpdemux")
endif()

if(HDRVIEW_ENABLE_LIBTIFF)
  CPMAddPackage(
    NAME TIFF
    VERSION 4.5.1 # version in ubuntu 24.04
    GITHUB_REPOSITORY Tom94/libtiff
    GIT_TAG b6446af165a5a4177eb9a4f6834bb948d4f89d39
    OPTIONS "BUILD_SHARED_LIBS OFF"
            # "HAVE_JPEGTURBO_DUAL_MODE_8_12 TRUE"
            "tiff-tools OFF"
            "tiff-tests OFF"
            "tiff-contrib OFF"
            "tiff-docs OFF"
            "tiff-deprecated OFF"
            "tiff-install OFF"
            "jbig OFF"
            "lerc OFF"
            "lzma OFF"
            "zstd OFF" 
    SYSTEM YES
  )
  if(TIFF_ADDED)
    message(STATUS "Will build libtiff library")
    disable_target_warnings(tiff)
    list(APPEND HDRVIEW_DEPENDENCIES "tiff")
  elseif(TARGET TIFF::TIFF)
    message(STATUS "Will link against system-installed libtiff library")
    list(APPEND HDRVIEW_DEPENDENCIES "TIFF::TIFF")
  else()
    message(FATAL_ERROR "libtiff library not found. Please install it.")
  endif()
endif()


if(HDRVIEW_ENABLE_LIBRAW)
  CPMAddPackage(
      NAME LibRaw_src
      GITHUB_REPOSITORY LibRaw/LibRaw
      GIT_TAG 0.21.4
      VERSION 0.21.4
      DOWNLOAD_ONLY YES
      SYSTEM YES
  )

  # Before the LibRaw CPMAddPackage: 
  if(WIN32 OR MSVC OR EMSCRIPTEN)
    set(MATH_LIBRARY "" CACHE STRING "Math library not needed on Windows/MSVC/Emscripten" FORCE)
    message(STATUS "No math library needed on Windows/MSVC/Emscripten for LibRaw")
  endif()

  CPMAddPackage(
      NAME LibRaw
      GITHUB_REPOSITORY LibRaw/LibRaw-cmake
      GIT_TAG eb98e4325aef2ce85d2eb031c2ff18640ca616d3
      VERSION 0.21.0
      FIND_PACKAGE_ARGUMENTS "QUIET"
      OPTIONS "LIBRAW_PATH ${LibRaw_src_SOURCE_DIR}"
              "BUILD_SHARED_LIBS OFF" 
              "LIBRAW_BUILD_TESTS OFF"
              "LIBRAW_BUILD_EXAMPLES OFF"
              "LIBRAW_INSTALL OFF"
              "ENABLE_X3FTOOLS ${HDRVIEW_ENABLE_X3F}"
      SYSTEM YES
  )
  if(LibRaw_ADDED AND TARGET libraw::libraw_r)
      message(STATUS "Will build libraw library")
      mark_as_system_library(libraw::libraw_r)
      disable_target_warnings(libraw::libraw_r)
  elseif(LibRaw_FOUND AND TARGET libraw::libraw_r)
    message(STATUS "Using system-installed libraw library (version ${LIBRAW_VERSION})")
  else()
    message(FATAL_ERROR "libraw_r library not found. Please install it.")
  endif()
  list(APPEND HDRVIEW_DEPENDENCIES "libraw::libraw_r")
endif()
    
CPMAddPackage(
  NAME CLI11
  VERSION 2.4.1
  URL https://github.com/CLIUtils/CLI11/archive/v2.4.2.zip
  DOWNLOAD_ONLY YES
  SYSTEM YES
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

CPMAddPackage("gh:wkjarosz/smalldds#cece9a892e5dbbd0603b1863cc1b2dbf21cf331d")
if(NOT smalldds_ADDED)
  message(FATAL_ERROR "Could not download smalldds library.")
endif()
message(STATUS "smalldds library added")
# Create implementation library with wrapper
configure_file("${CMAKE_SOURCE_DIR}/cmake/smalldds_impl.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/smalldds_impl.cpp" COPYONLY)
add_library(smalldds STATIC "${CMAKE_CURRENT_BINARY_DIR}/smalldds_impl.cpp")
target_include_directories(smalldds PUBLIC "${smalldds_SOURCE_DIR}")
target_compile_features(smalldds PUBLIC cxx_std_17)
disable_target_warnings(smalldds)
mark_as_system_library(smalldds)
list(APPEND HDRVIEW_DEPENDENCIES "smalldds")

CPMAddPackage(
  NAME bcdec
  GITHUB_REPOSITORY iOrange/bcdec
  GIT_TAG 963c5e56b7a335e066cff7d16a3de75f4e8ad366
  DOWNLOAD_ONLY YES
)
if(NOT bcdec_ADDED)
  message(FATAL_ERROR "Could not download bcdec library.")
endif()
message(STATUS "bcdec library added")
# Create implementation library with wrapper
configure_file("${CMAKE_SOURCE_DIR}/cmake/bcdec_impl.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/bcdec_impl.cpp" COPYONLY)
add_library(bcdec STATIC "${CMAKE_CURRENT_BINARY_DIR}/bcdec_impl.cpp")
target_include_directories(bcdec PUBLIC "${bcdec_SOURCE_DIR}")
disable_target_warnings(bcdec)
mark_as_system_library(bcdec)
list(APPEND HDRVIEW_DEPENDENCIES "bcdec")

CPMAddPackage(
  NAME astc_dec
  GITHUB_REPOSITORY wkjarosz/astc_dec
  GIT_TAG 0b3a1decbcf1d49b9af65b3afb7499d7486252d2
  DOWNLOAD_ONLY YES
)
if(NOT astc_dec_ADDED)
  message(FATAL_ERROR "Could not download astc_dec library.")
  endif()
message(STATUS "astc_dec library added")
add_library(astc_dec "${astc_dec_SOURCE_DIR}/astc_decomp.cpp" "${astc_dec_SOURCE_DIR}/astc_decomp.h")
target_compile_features(astc_dec PUBLIC cxx_std_11)
target_include_directories(astc_dec PUBLIC ${astc_dec_SOURCE_DIR})
disable_target_warnings(astc_dec)
mark_as_system_library(astc_dec)
list(APPEND HDRVIEW_DEPENDENCIES astc_dec)

CPMAddPackage(
  NAME miniz
  # GITHUB_REPOSITORY richgel999/miniz
  VERSION 3.0.2
  URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
  DOWNLOAD_ONLY YES
)
if(NOT miniz_ADDED AND NOT miniz_FOUND)
  message(FATAL_ERROR "Could not find or download miniz library.")
endif()
message(STATUS "miniz library added")
add_library(miniz "${miniz_SOURCE_DIR}/miniz.c" "${miniz_SOURCE_DIR}/miniz.h")
target_include_directories(miniz PUBLIC ${miniz_SOURCE_DIR})
disable_target_warnings(miniz)
mark_as_system_library(miniz)
list(APPEND HDRVIEW_DEPENDENCIES miniz)



# Helper function to add XXX_ENABLED=0 or XXX_ENABLED=1 definitions based on HDRVIEW_ENABLE_XXX option
# Also adds HDRVIEW_ENABLE_XXX itself when the option is ON
function(add_enabled_definition option_name)
  # Extract the feature name (e.g., "J2K" from "HDRVIEW_ENABLE_J2K")
  string(REPLACE "HDRVIEW_ENABLE_" "" feature_name "${option_name}")
  if(${option_name})
    list(APPEND HDRVIEW_DEFINITIONS "${option_name}=1")
  else()
    list(APPEND HDRVIEW_DEFINITIONS "${option_name}=0")
  endif()
  set(HDRVIEW_DEFINITIONS ${HDRVIEW_DEFINITIONS} PARENT_SCOPE)
endfunction()

# Add XXX_ENABLED=0 or XXX_ENABLED=1 definitions for all configurable options
add_enabled_definition(HDRVIEW_ENABLE_LIBPNG)
add_enabled_definition(HDRVIEW_ENABLE_LIBJPEG)
add_enabled_definition(HDRVIEW_ENABLE_LIBUHDR)
add_enabled_definition(HDRVIEW_ENABLE_LIBJXL)
add_enabled_definition(HDRVIEW_ENABLE_LIBHEIF)
add_enabled_definition(HDRVIEW_ENABLE_J2K)
add_enabled_definition(HDRVIEW_ENABLE_HTJ2K)
add_enabled_definition(HDRVIEW_ENABLE_AVIF)
add_enabled_definition(HDRVIEW_ENABLE_HEIC)
add_enabled_definition(HDRVIEW_ENABLE_AVCI)
add_enabled_definition(HDRVIEW_ENABLE_LIBWEBP)
add_enabled_definition(HDRVIEW_ENABLE_LIBTIFF)
add_enabled_definition(HDRVIEW_ENABLE_LIBRAW)
add_enabled_definition(HDRVIEW_ENABLE_X3F)

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
  src/imageio/dds.cpp
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
  src/imageio/raw.cpp
  src/imageio/tiff.cpp
  src/imageio/uhdr.cpp
  src/imageio/webp.cpp
  src/app-draw.cpp
  src/app-file-io.cpp
  src/app-gui.cpp
  src/app-windows.cpp
  src/app-zoom.cpp
  src/app.cpp
  src/colormap.cpp
  src/colorspace.cpp
  src/common.cpp
  src/dithermatrix256.cpp
  src/platform_utils.cpp
  src/fonts.cpp
  src/hdrview.cpp
  src/image.cpp
  src/imgui_ext.cpp
  src/image_gui.cpp
  src/opengl_check.cpp
  src/shader.cpp
  src/shader_gl.cpp
  src/renderpass_gl.cpp
  src/texture_gl.cpp
  src/texture.cpp
  src/theme.cpp
  ${EXTRA_SOURCES}
  ASSETS_LOCATION
  ${CMAKE_CURRENT_BINARY_DIR}/assets
)
target_include_directories(HDRView PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(HDRView PROPERTIES OUTPUT_NAME ${output_name} CXX_STANDARD 17)
target_compile_definitions(HDRView PRIVATE ${HDRVIEW_ICONSET})

if(DEFINED CMAKE_CONFIGURATION_TYPES)
  list(APPEND HDRVIEW_DEFINITIONS "HDRVIEW_BUILD_TYPE=\"$<CONFIG>\"")
else()
  list(APPEND HDRVIEW_DEFINITIONS "HDRVIEW_BUILD_TYPE=\"${CMAKE_BUILD_TYPE}\"")
endif()

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

if(UNIX AND NOT IS_MULTI AND NOT ${U_CMAKE_BUILD_TYPE} MATCHES DEBUG)
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
  install(TARGETS HDRView BUNDLE DESTINATION ${CMAKE_INSTALL_PREFIX})
  set(CPACK_GENERATOR "DragNDrop")
endif()

include(CPack)
