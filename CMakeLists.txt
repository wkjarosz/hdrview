cmake_minimum_required (VERSION 2.8.3)
project(hdrview)

# prevent glfw from changing the directory on macOS bundles
SET(GLFW_USE_CHDIR OFF CACHE BOOL "Prevent glfwInit from chdir to Contents/Resources" FORCE)

# Set a default build configuration (Release)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
    "MinSizeRel" "RelWithDebInfo")
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" U_CMAKE_BUILD_TYPE)

if(NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/ext/nanogui/ext/glfw")
  message(FATAL_ERROR "The HDRView dependency repositories (NanoGUI, GLFW, etc.) are missing! "
    "You probably did not clone the project with --recursive. It is possible to recover "
    "by calling \"git submodule update --init --recursive\"")
endif()

# Enable folders for projects in Visual Studio
if (CMAKE_GENERATOR MATCHES "Visual Studio")
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

# Sanitize build environment for static build with C++11
if (MSVC)
  # Disable annoying secure CRT warnings
  add_definitions (/D "_CRT_SECURE_NO_WARNINGS")

  # Parallel build on MSVC (all targets)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")

  if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:SSE2")

    # Disable Eigen vectorization for Windows 32 bit builds (issues with unaligned access segfaults)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /DEIGEN_DONT_ALIGN")
  endif()

  # Static build
  set(CompilerFlags
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO
        CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
        CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO)
  foreach(CompilerFlag ${CompilerFlags})
    string(REPLACE "/MD" "/MT" ${CompilerFlag} "${${CompilerFlag}}")
  endforeach()
elseif(APPLE)
    # Try to auto-detect a suitable SDK
    # Commented out for now -- causes a too new SDK to be selected on AppVeyor
    #execute_process(COMMAND bash -c "xcodebuild -version -sdk | grep MacOSX | grep Path | head -n 1 | cut -f 2 -d ' '" OUTPUT_VARIABLE CMAKE_OSX_SYSROOT)
    #string(REGEX REPLACE "(\r?\n)+$" "" CMAKE_OSX_SYSROOT "${CMAKE_OSX_SYSROOT}")
    #string(REGEX REPLACE "^.*X([0-9.]*).sdk$" "\\1" CMAKE_OSX_DEPLOYMENT_TARGET "${CMAKE_OSX_SYSROOT}")
endif()

# Enable C++11 mode on GCC / Clang
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

if (WIN32)
  # Build zlib (only on Windows)
  set(ZLIB_BUILD_STATIC_LIBS ON CACHE BOOL " " FORCE)
  set(ZLIB_BUILD_SHARED_LIBS OFF CACHE BOOL " " FORCE)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ext/zlib ext_build/zlib)

  set(ZLIB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ext/zlib" CACHE PATH " " FORCE)
  set(ZLIB_LIBRARY
		optimized "${CMAKE_CURRENT_BINARY_DIR}/ext_build/zlib/$<CONFIG>/zlibstatic.lib"
    debug "${CMAKE_CURRENT_BINARY_DIR}/ext_build/zlib/$<CONFIG>/zlibstaticd.lib" CACHE FILEPATH " " FORCE)

  set_property(TARGET zlibstatic PROPERTY FOLDER "dependencies")
  include_directories(${ZLIB_INCLUDE_DIR} "${CMAKE_CURRENT_BINARY_DIR}/ext_build/zlib")
endif()

# Build OpenER
set(ILMBASE_BUILD_SHARED_LIBS OFF CACHE BOOL " " FORCE)
set(OPENEXR_BUILD_SHARED_LIBS OFF CACHE BOOL " " FORCE)
set(ILMBASE_NAMESPACE_VERSIONING OFF CACHE BOOL " " FORCE)
set(OPENEXR_NAMESPACE_VERSIONING OFF CACHE BOOL " " FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ext/openexr ext_build/openexr)
set_property(TARGET IexMath eLut toFloat b44ExpLogTable dwaLookups CopyIlmBaseLibs IlmThread Half Iex Imath IlmImf PROPERTY FOLDER "dependencies")

set(OPENEXR_INCLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/openexr/IlmBase/Imath
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/openexr/IlmBase/Iex
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/openexr/IlmBase/Half
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/openexr/OpenEXR/IlmImf
  ${CMAKE_CURRENT_BINARY_DIR}/ext_build/openexr/OpenEXR/config
  ${CMAKE_CURRENT_BINARY_DIR}/ext_build/openexr/IlmBase/config)

# Build NanoGUI
set(NANOGUI_BUILD_EXAMPLE OFF CACHE BOOL " " FORCE)
set(NANOGUI_BUILD_SHARED  OFF CACHE BOOL " " FORCE)
set(NANOGUI_BUILD_PYTHON  OFF CACHE BOOL " " FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ext/nanogui ext_build/nanogui)
set_property(TARGET glfw glfw_objects nanogui PROPERTY FOLDER "dependencies")

# Build docopt.cpp
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ext/docopt ext_build/docopt)
set_property(TARGET docopt PROPERTY FOLDER "dependencies")

# Compile remainder of the codebase with compiler warnings turned on
if(MSVC)
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  endif()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-gnu-anonymous-struct -Wno-c99-extensions -Wno-nested-anon-types -Wno-deprecated-register")
  endif()
endif()

if (WIN32)
    add_dependencies(IlmImf zlibstatic)
endif()


include_directories(
    # NanoGUI
    ${CMAKE_CURRENT_SOURCE_DIR}/ext/nanogui/include
    # NanoGUI dependencies (Eigen, GLFW, NanoVG, GLAD?)
    ${NANOGUI_EXTRA_INCS}
    # OpenEXR high dynamic range bitmap library
    ${OPENEXR_INCLUDE_DIRS}
    # tinyfiles
    ${CMAKE_CURRENT_SOURCE_DIR}/ext/tinyfiles
    # tinydir
    ${CMAKE_CURRENT_SOURCE_DIR}/ext/tinydir
    # stb
    ${CMAKE_CURRENT_SOURCE_DIR}/ext/stb
    # docopt
    ${CMAKE_CURRENT_SOURCE_DIR}/ext/docopt
    # spdlog
    ${CMAKE_CURRENT_SOURCE_DIR}/ext/spdlog/include
)

# Resource file (icons etc.)
set(EXTRA_SOURCE "")
if (APPLE)
  set(EXTRA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.icns")
elseif(WIN32)
  set(EXTRA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.rc")
endif()

# The following lines build the main executable. If you add a source
# code file to Nori, be sure to include it in this list.
add_executable(HDRView MACOSX_BUNDLE
    src/common.cpp
    src/common.h
    src/glimage.cpp
    src/glimage.h
    src/gldithertexture.h
    src/envmap.cpp
    src/envmap.h
    src/pfm.h
    src/pfm.cpp
    src/ppm.h
    src/ppm.cpp
    src/hdrviewer.h
    src/hdrviewer.cpp
    src/dither-matrix256.h
    src/color.h
    src/hdrimage.h
    src/hdrimage.cpp
    src/main.cpp
    src/widgets.h
    src/widgets.cpp
    ${EXTRA_SOURCE})

add_definitions(${NANOGUI_EXTRA_DEFS})
target_link_libraries(HDRView IlmImf nanogui docopt_s ${NANOGUI_EXTRA_LIBS})

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

if (UNIX AND NOT ${U_CMAKE_BUILD_TYPE} MATCHES DEBUG)
  add_custom_command(TARGET HDRView POST_BUILD COMMAND strip $<TARGET_FILE:HDRView>)
endif()


add_executable(force-random-dither
  src/forced-random-dither.cpp
)

target_link_libraries(force-random-dither nanogui ${NANOGUI_EXTRA_LIBS})
