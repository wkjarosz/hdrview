# prevent glfw from changing the directory on macOS bundles
SET(GLFW_USE_CHDIR OFF CACHE BOOL "Prevent glfwInit from chdir to Contents/Resources" FORCE)

#============================================================================
# Check if we have all the submodules checked out
#============================================================================
if(NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/nanogui/ext/glfw")
    message(FATAL_ERROR "The HDRView dependency repositories (NanoGUI, GLFW, etc.) are missing! "
        "You probably did not clone the project with --recursive. It is possible to recover "
        "by calling \"git submodule update --init --recursive\"")
endif()

#============================================================================
# Build ZLIB on Windows (needed for OpenEXR)
#============================================================================
if (WIN32)
    # Build zlib (only on Windows)
    set(ZLIB_BUILD_STATIC_LIBS ON CACHE BOOL " " FORCE)
    set(ZLIB_BUILD_SHARED_LIBS OFF CACHE BOOL " " FORCE)
    add_subdirectory(zlib)

    set(ZLIB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/zlib" CACHE PATH " " FORCE)
    set(ZLIB_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/zlib/$<CONFIGURATION>/zlibstatic.lib" CACHE FILEPATH " " FORCE)

    set_property(TARGET zlibstatic PROPERTY FOLDER "dependencies")
    include_directories(${ZLIB_INCLUDE_DIR} "${CMAKE_CURRENT_BINARY_DIR}/zlib")
endif()

#============================================================================
# Build OpenEXR
#============================================================================
set(ILMBASE_BUILD_SHARED_LIBS OFF CACHE BOOL " " FORCE)
set(OPENEXR_BUILD_SHARED_LIBS OFF CACHE BOOL " " FORCE)
set(ILMBASE_NAMESPACE_VERSIONING OFF CACHE BOOL " " FORCE)
set(OPENEXR_NAMESPACE_VERSIONING OFF CACHE BOOL " " FORCE)
add_subdirectory(openexr EXCLUDE_FROM_ALL)
set_property(TARGET IexMath eLut toFloat b44ExpLogTable dwaLookups CopyIlmBaseLibs IlmThread Half Iex Imath IlmImf PROPERTY FOLDER "dependencies")

#============================================================================
# Build NanoGUI
#============================================================================
set(NANOGUI_BUILD_EXAMPLE OFF CACHE BOOL " " FORCE)
set(NANOGUI_BUILD_SHARED  OFF CACHE BOOL " " FORCE)
set(NANOGUI_BUILD_PYTHON  OFF CACHE BOOL " " FORCE)
set(NANOGUI_INSTALL OFF CACHE BOOL " " FORCE)
add_subdirectory(nanogui)
set_property(TARGET glfw glfw_objects nanogui nanogui-obj PROPERTY FOLDER "dependencies")

#============================================================================
# Build docopt.cpp
#============================================================================
add_subdirectory(docopt EXCLUDE_FROM_ALL)
if (WIN32)
	add_definitions("-DDOCTOPT_USE_BOOST_REGEX")
	set(Boost_USE_STATIC_LIBS ON)
	set(Boost_USE_STATIC_RUNTIME ON)
    find_package(Boost 1.53 REQUIRED COMPONENTS regex)
endif()
set_property(TARGET docopt PROPERTY FOLDER "dependencies")

#============================================================================
# Handle zlib and boost dependencies on windows
#============================================================================
if (WIN32)
    add_dependencies(IlmImf zlibstatic)
endif()

#============================================================================
# Define variables for the include directories for the various dependencies
# and export them to the parent scope
#============================================================================
set(OPENEXR_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/openexr/IlmBase/Imath
    ${CMAKE_CURRENT_SOURCE_DIR}/openexr/IlmBase/Iex
    ${CMAKE_CURRENT_SOURCE_DIR}/openexr/IlmBase/Half
    ${CMAKE_CURRENT_SOURCE_DIR}/openexr/OpenEXR/IlmImf
    ${CMAKE_CURRENT_BINARY_DIR}/openexr/OpenEXR/config
    ${CMAKE_CURRENT_BINARY_DIR}/openexr/IlmBase/config PARENT_SCOPE)
set(GLFW_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/nanogui/ext/glfw/include PARENT_SCOPE)
set(GLEW_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/nanogui/ext/glew/include PARENT_SCOPE)
set(NANOVG_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/nanogui/ext/nanovg/src PARENT_SCOPE)
set(NANOGUI_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/nanogui/include PARENT_SCOPE)
set(NANOGUI_EXTRA_INCS ${NANOGUI_EXTRA_INCS} PARENT_SCOPE)
set(NANOGUI_EXTRA_DEFS ${NANOGUI_EXTRA_DEFS} PARENT_SCOPE)
set(NANOGUI_EXTRA_LIBS ${NANOGUI_EXTRA_LIBS} PARENT_SCOPE)
set(EIGEN_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/nanogui/ext/eigen PARENT_SCOPE)
set(DOCOPT_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/docopt PARENT_SCOPE)
set(TINYDIR_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/tinydir PARENT_SCOPE)
set(TINYDNG_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/tinydngloader PARENT_SCOPE)
set(STB_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/stb PARENT_SCOPE)
set(SPDLOG_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/spdlog/include PARENT_SCOPE)
