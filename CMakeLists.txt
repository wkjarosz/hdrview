cmake_minimum_required (VERSION 2.8.3)
project(HDRView)

option(HDR_VIEW_DEV_MODE "HDRView developer mode" OFF)

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
  add_definitions (/D "_CRT_SECURE_NO_WARNINGS")
  add_definitions (/D "__TBB_NO_IMPLICIT_LINKAGE")

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
  execute_process(COMMAND bash -c "xcodebuild -version -sdk | grep MacOSX | grep Path | head -n 1 | cut -f 2 -d ' '" OUTPUT_VARIABLE CMAKE_OSX_SYSROOT)
  string(REGEX REPLACE "(\r?\n)+$" "" CMAKE_OSX_SYSROOT "${CMAKE_OSX_SYSROOT}")
  string(REGEX REPLACE "^.*X([0-9.]*).sdk$" "\\1" CMAKE_OSX_DEPLOYMENT_TARGET "${CMAKE_OSX_SYSROOT}")
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  # Enable C++11 mode on GCC / Clang
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

# Compile with compiler warnings
if(MSVC)
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  endif()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-gnu-anonymous-struct -Wno-c99-extensions -Wno-nested-anon-types")
  endif()
endif()

# Build OpenEXR
set(OPENEXR_BUILD_TESTS OFF CACHE BOOL " " FORCE)
set(OPENEXR_BUILD_UTILS  OFF CACHE BOOL " " FORCE)
set(OPENEXR_BUILD_EXAMPLES  OFF CACHE BOOL " " FORCE)
set(OPENEXR_INSTALL_EXAMPLES  OFF CACHE BOOL " " FORCE)
set(OPENEXR_INSTALL_DOCS  OFF CACHE BOOL " " FORCE)
set(OPENEXR_BUILD_SHARED_LIBS  OFF CACHE BOOL " " FORCE)
set(OPENEXR_NAMESPACE_VERSIONING  OFF CACHE BOOL " " FORCE)
set(ILMIMF_BUILD_TESTS OFF CACHE BOOL " " FORCE)
set(ILMIMF_CREATE_LIBTOOL_FILE OFF CACHE BOOL " " FORCE)
set(ILMIMF_INSTALL_PKGCONFIG OFF CACHE BOOL " " FORCE)
set(ILMBASE_BUILD_SHARED_LIBS OFF CACHE BOOL " " FORCE)
set(ILMBASE_NAMESPACE_VERSIONING OFF CACHE BOOL " " FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ext/openexr ext_build/openexr)
set(OPENEXR_LIBRARIES IlmImf IlmThread Iex IexMath Imath Half)

# Build NanoGUI
set(NANOGUI_BUILD_EXAMPLE OFF CACHE BOOL " " FORCE)
set(NANOGUI_BUILD_SHARED  OFF CACHE BOOL " " FORCE)
set(NANOGUI_BUILD_PYTHON  OFF CACHE BOOL " " FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ext/nanogui ext_build/nanogui)

set_property(TARGET IlmImf IlmThread Iex IexMath Imath Half nanogui glfw glfw_objects bin2c PROPERTY FOLDER "dependencies")


# Compile against & link to previously compiled external projects
# Header file directories of dependency libraries
link_directories(${CMAKE_BINARY_DIR}/ext_build/dist/lib)
include_directories(
  # Eigen
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/nanogui/ext/eigen
  # GLFW
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/nanogui/ext/glfw/include
  # GLEW
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/nanogui/ext/glew/include
  # NanoVG
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/nanogui/ext/nanovg/src
  # NanoGUI
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/nanogui/include
  # OpenEXR
  ${CMAKE_BINARY_DIR}/ext_build/openexr/OpenEXR/config/
  ${CMAKE_BINARY_DIR}/ext_build/openexr/IlmBase/config/
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/openexr/OpenEXR/IlmImf/
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/openexr/IlmBase/IlmThread/
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/openexr/IlmBase/Iex/
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/openexr/IlmBase/IexMath/
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/openexr/IlmBase/Imath/
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/openexr/IlmBase/Half/
  # Resource file
  ${CMAKE_CURRENT_BINARY_DIR}
)

if (HDR_VIEW_DEV_MODE)
  add_definitions(-DDEV_MODE)
endif()

add_executable(HDRView #MACOSX_BUNDLE
  src/ImageQuad.cpp
  src/ImageQuad.h
  src/stb_image.h
  src/stb_image_write.h
  src/HDRViewer.h
  src/HDRViewer.cpp
  src/dither-matrix256.h
  src/FullScreenDitherer.h
  src/main.cpp)

target_link_libraries(HDRView nanogui ${NANOGUI_EXTRA_LIBS} ${OPENEXR_LIBRARIES})
set_target_properties(HDRView PROPERTIES OUTPUT_NAME "HDRView")


add_executable(force-random-dither
  src/forced-random-dither.cpp)

target_link_libraries(force-random-dither nanogui ${NANOGUI_EXTRA_LIBS})

# if (APPLE)
#   # Build an application bundle on OSX
#   set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "HDRView")
#   set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_BUNDLE_GUI_IDENTIFIER "com.im.HDRView")
#   set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_ICON_FILE hdrview.icns)
#   set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/MacOSXBundleInfo.plist.in)
#   set_source_files_properties(resources/hdrview.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
# else()
#   # Insulate from a few types of ABI changes by statically linking against libgcc and libstdc++
#   set_target_properties(HDRView PROPERTIES LINK_FLAGS "-static-libgcc")
#   #set_target_properties(HDRView PROPERTIES LINK_FLAGS "-static-libgcc -static-libstdc++")
# endif()

if (UNIX AND NOT ${U_CMAKE_BUILD_TYPE} MATCHES DEBUG)
  add_custom_command(TARGET HDRView POST_BUILD COMMAND strip $<TARGET_FILE:HDRView>)
endif()
