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

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(VERSION_LONG "${GIT_DESCRIBE} (64 bit)")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(VERSION_LONG "${GIT_DESCRIBE} (32 bit)")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/version.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/src/version.cpp @ONLY)

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
  set(CMAKE_BUILD_TYPE ${DEFAULT_BUILD_TYPE} CACHE STRING "Choose the type of build." FORCE)
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

cpmaddpackage("gh:sgorsten/linalg@2.2")
if(linalg_ADDED)
  message(STATUS "linalg library added")
  add_library(linalg INTERFACE IMPORTED)
  target_include_directories(linalg INTERFACE "${linalg_SOURCE_DIR}")
endif()

cpmaddpackage("gh:fmtlib/fmt#10.1.1")
if(fmt_ADDED)
  message(STATUS "fmt library added")
endif()

cpmaddpackage(
  NAME
  spdlog
  URL
  https://github.com/gabime/spdlog/archive/refs/tags/v1.13.0.zip
  OPTIONS
  "SPDLOG_INSTALL NO" # don't create an installable target
  "SPDLOG_FMT_EXTERNAL YES" # use the fmt library we added above instead of the bundled one
)
if(spdlog_ADDED)
  message(STATUS "spdlog library added")
endif()

cpmaddpackage("gh:nothings/stb#a0a939058c579ddefd4c5671b046f29d12aeae01")
if(stb_ADDED)
  message(STATUS "stb library added")
  add_library(stb INTERFACE IMPORTED)
  target_include_directories(stb INTERFACE "${stb_SOURCE_DIR}")
endif()

cpmaddpackage("gh:Armchair-Software/emscripten-browser-file#2ff8f8f791c0e7177b40e621ee3f758e03b31bf5")
if(emscripten-browser-file_ADDED)
  message(STATUS "emscripten-browser-file library added")
  add_library(emscripten-browser-file INTERFACE IMPORTED)
  target_include_directories(emscripten-browser-file INTERFACE "${emscripten-browser-file_SOURCE_DIR}")
endif()

cpmaddpackage("gh:samhocevar/portable-file-dialogs#7f852d88a480020d7f91957cbcefe514fc95000c")
if(portable-file-dialogs_ADDED)
  message(STATUS "portable-file-dialogs library added")
  add_library(portable-file-dialogs INTERFACE IMPORTED)
  target_include_directories(portable-file-dialogs INTERFACE "${portable-file-dialogs_SOURCE_DIR}")
endif()

if(APPLE AND NOT EMSCRIPTEN)
  set(HELLOIMGUI_USE_GLFW_METAL ON)

  # set(HELLOIMGUI_USE_GLFW_OPENGL3 ON)

  # set(HELLOIMGUI_USE_SDL_OPENGL3 ON)

  # set(HELLOIMGUI_USE_SDL_METAL ON)
endif()

# copy asset directory to the build tree file
file(GLOB_RECURSE MY_RESOURCE_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} # CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/assets/**.*)

if(APPLE AND (HELLOIMGUI_USE_GLFW_METAL OR HELLOIMGUI_USE_SDL_METAL))

  # ~~~
  # # Precompile .metal shaders to .metallib files
  # # exclude metal files from the copy (we will compile them to metallib files below)
  # list(FILTER MY_RESOURCE_FILES EXCLUDE REGEX ".*\\.metal$")
  # # Glob up metal shaders
  # file(
  #   GLOB_RECURSE metal_shaders
  #   RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  #   ${CMAKE_CURRENT_SOURCE_DIR}/assets/**.metal
  # )
  # foreach(shader ${metal_shaders})
  #   set(shader_source "${CMAKE_CURRENT_SOURCE_DIR}/${shader}")
  #   set(shader_lib "${CMAKE_CURRENT_BINARY_DIR}/${shader}lib")
  #   # message(STATUS "Compiling Metal shader \"${shader_source}\" to MLTB library \"${shader_lib}\".")
  #   add_custom_command(
  #     OUTPUT ${shader_lib}
  #     DEPENDS ${shader_source}
  #     COMMAND xcrun -sdk macosx metal -std=osx-metal2.0 -O3 "${shader_source}" -o "${shader_lib}"
  #     WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  #     COMMENT "Compiling Metal shader \"${shader}\" to MLTB library \"${shader}lib\"."
  #     VERBATIM
  #   )
  #   set_property(
  #     SOURCE src/app.cpp
  #     APPEND
  #     PROPERTY OBJECT_DEPENDS ${shader_lib}
  #   )
  #   list(APPEND EXTRA_SOURCES ${shader_lib})
  # endforeach()
  # ~~~

  list(APPEND EXTRA_SOURCES src/renderpass_metal.mm src/shader_metal.mm src/texture_metal.mm)
endif()

foreach(MY_RESOURCE_FILE ${MY_RESOURCE_FILES})
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${MY_RESOURCE_FILE} ${CMAKE_CURRENT_BINARY_DIR}/${MY_RESOURCE_FILE}
                 COPYONLY)
endforeach()

cpmaddpackage("gh:pthom/hello_imgui#ed91f72da875b32ba96ef36d6633b9d7c0d95bee")
if(hello_imgui_ADDED)
  message(STATUS "hello_imgui library added")
endif()

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

hello_imgui_add_app(
  HDRView
  ${CMAKE_CURRENT_BINARY_DIR}/src/version.cpp
  src/app.cpp
  src/colorspace.cpp
  src/common.cpp
  src/image.cpp
  src/imageio.cpp
  src/imgui_ext.cpp
  src/image_gui.cpp
  src/pfm.cpp
  src/parallelfor.cpp
  src/opengl_check.cpp
  src/shader.cpp
  src/shader_gl.cpp
  src/renderpass_gl.cpp
  src/texture.cpp
  src/texture_gl.cpp
  ${EXTRA_SOURCES}
  ASSETS_LOCATION
  ${CMAKE_CURRENT_BINARY_DIR}/assets)

set_target_properties(HDRView PROPERTIES OUTPUT_NAME ${output_name} CXX_STANDARD 17)
if(APPLE)
  target_compile_options(HDRView PRIVATE "-fobjc-arc")
endif(APPLE)
target_link_libraries(HDRView PRIVATE linalg fmt::fmt spdlog::spdlog OpenEXR::OpenEXR Imath::Imath stb)
target_compile_definitions(HDRView PRIVATE HELLOIMGUI_NO_FONT_AWESOME4)

if(EMSCRIPTEN)
  target_link_libraries(HDRView PRIVATE emscripten-browser-file)
  target_link_options(
    HDRView
    PRIVATE
    -sEXPORTED_RUNTIME_METHODS=[ccall]
    -sEXPORTED_FUNCTIONS=[_main,_malloc,_free]
    -sNO_DISABLE_EXCEPTION_CATCHING
    -sMAX_WEBGL_VERSION=2
    -sMIN_WEBGL_VERSION=2)
  hello_imgui_set_emscripten_target_initial_memory_megabytes(HDRView 240)
else()
  target_link_libraries(HDRView PRIVATE portable-file-dialogs)
endif()

if(UNIX AND NOT ${U_CMAKE_BUILD_TYPE} MATCHES DEBUG)
  add_custom_command(TARGET HDRView POST_BUILD COMMAND strip $<TARGET_FILE:HDRView>)
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
