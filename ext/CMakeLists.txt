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
# Set a default build configuration (Release)
#============================================================================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
        "MinSizeRel" "RelWithDebInfo")
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" U_CMAKE_BUILD_TYPE)

#============================================================================
# Enable folders for projects in Visual Studio
#============================================================================
if (CMAKE_GENERATOR MATCHES "Visual Studio")
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

if (APPLE)
    set(CMAKE_MACOSX_RPATH ON)
endif()

#============================================================================
# Set C++ standard
#============================================================================
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_CXX_STANDARD OR CMAKE_CXX_STANDARD LESS 11)
    set(CMAKE_CXX_STANDARD 11)
endif()

#============================================================================
# Enable link time optimization and set the default symbol
# visibility to hidden (very important to obtain small binaries)
#============================================================================
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    if (NOT ${U_CMAKE_BUILD_TYPE} MATCHES DEBUG)
        # Default symbol visibility
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
    endif()
endif()

#============================================================================
# Sanitize build environment for static build with C++11
#============================================================================
if (MSVC)
    # Disable annoying secure CRT warnings
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /D_CRT_SECURE_NO_WARNINGS")

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

#============================================================================
# Build ZLIB on Windows (needed for OpenEXR)
#============================================================================
if (WIN32)
    # Build zlib (only on Windows)
    set(ZLIB_BUILD_STATIC_LIBS ON CACHE BOOL " " FORCE)
    set(ZLIB_BUILD_SHARED_LIBS OFF CACHE BOOL " " FORCE)
    add_subdirectory(zlib)

  set(ZLIB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/zlib" CACHE PATH " " FORCE)
  set(ZLIB_LIBRARY
		optimized "${CMAKE_CURRENT_BINARY_DIR}/zlib/$<CONFIG>/zlibstatic.lib"
		debug "${CMAKE_CURRENT_BINARY_DIR}/zlib/$<CONFIG>/zlibstaticd.lib" CACHE FILEPATH " " FORCE)

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
add_subdirectory(openexr)
set_property(TARGET IexMath eLut toFloat b44ExpLogTable dwaLookups CopyIlmBaseLibs IlmThread Half Iex Imath IlmImf PROPERTY FOLDER "dependencies")

#============================================================================
# Build NanoGUI
#============================================================================
set(NANOGUI_BUILD_EXAMPLE OFF CACHE BOOL " " FORCE)
set(NANOGUI_BUILD_SHARED  OFF CACHE BOOL " " FORCE)
set(NANOGUI_BUILD_PYTHON  OFF CACHE BOOL " " FORCE)
add_subdirectory(nanogui)
set_property(TARGET glfw glfw_objects nanogui nanogui-obj PROPERTY FOLDER "dependencies")

#============================================================================
# Build docopt.cpp
#============================================================================
add_subdirectory(docopt)
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
#============================================================================
set(OPENEXR_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/openexr/IlmBase/Imath
    ${CMAKE_CURRENT_SOURCE_DIR}/openexr/IlmBase/Iex
    ${CMAKE_CURRENT_SOURCE_DIR}/openexr/IlmBase/Half
    ${CMAKE_CURRENT_SOURCE_DIR}/openexr/OpenEXR/IlmImf
    ${CMAKE_CURRENT_BINARY_DIR}/openexr/OpenEXR/config
    ${CMAKE_CURRENT_BINARY_DIR}/openexr/IlmBase/config)
set(GLFW_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/nanogui/ext/glfw/include)
set(GLEW_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/nanogui/ext/glew/include)
set(NANOVG_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/nanogui/ext/nanovg/src)
set(NANOGUI_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/nanogui/include)
set(EIGEN_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/nanogui/ext/eigen)
set(DOCOPT_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/docopt)
set(TINYDIR_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/tinydir)
set(STB_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/stb)
set(SPDLOG_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/spdlog/include)

#============================================================================
# Compile remainder of the codebase with compiler warnings turned on
#============================================================================
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

#============================================================================
# Export these variables to the parent scope
#============================================================================
set(CompilerFlags
    CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
    CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS
    CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_MINSIZEREL
    CMAKE_C_FLAGS_RELWITHDEBINFO COMPILE_DEFINITIONS U_CMAKE_BUILD_TYPE
    CMAKE_MACOSX_RPATH
    OPENEXR_INCLUDE_DIRS GLFW_INCLUDE_DIR GLEW_INCLUDE_DIR
    NANOVG_INCLUDE_DIR NANOGUI_EXTRA_INCS NANOGUI_EXTRA_DEFS
    NANOGUI_EXTRA_LIBS NANOGUI_INCLUDE_DIR EIGEN_INCLUDE_DIR
    DOCOPT_INCLUDE_DIR TINYDIR_INCLUDE_DIR STB_INCLUDE_DIR SPDLOG_INCLUDE_DIR
    CMAKE_CXX_STANDARD CMAKE_CXX_STANDARD_REQUIRED CMAKE_CXX_EXTENSIONS
	Boost_INCLUDE_DIRS Boost_LIBRARIES Boost_REGEX_LIBRARY)
foreach(CompilerFlag ${CompilerFlags})
    set(${CompilerFlag} "${${CompilerFlag}}" PARENT_SCOPE)
endforeach()
