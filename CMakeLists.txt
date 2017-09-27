cmake_minimum_required (VERSION 3.1)
project(hdrview)

set(HDRVIEW_VERSION "0.1.dev")

# Set ourselves as the startup project in visual studio.
# Not available until cmake 3.6, but doesn't break older versions.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT hdrview)

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

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ext)

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

include_directories(
    # GLFW library for OpenGL context creation
    ${GLFW_INCLUDE_DIR}
    # GLEW library for accessing OpenGL functions
    ${GLEW_INCLUDE_DIR}
    # stb
    ${STB_INCLUDE_DIR}
    # tinydngloader
    ${TINYDNG_INCLUDE_DIR}
    # NanoGUI user interface library
    ${NANOGUI_INCLUDE_DIR}
    ${NANOGUI_EXTRA_INCS}
    # OpenEXR high dynamic range bitmap library
    ${OPENEXR_INCLUDE_DIRS}
    # tinydir
    ${TINYDIR_INCLUDE_DIR}
    # docopt
    ${DOCOPT_INCLUDE_DIR}
    # spdlog
    ${SPDLOG_INCLUDE_DIR}
	# boost REGEX
	${Boost_INCLUDE_DIRS}
)

# Resource file (icons etc.)
set(EXTRA_SOURCE "")
if (APPLE)
    set(EXTRA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.icns")
elseif(WIN32)
    set(EXTRA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.rc")
endif()

add_executable(HDRView
               src/color.cpp
               src/color.h
               src/colorspace.cpp
               src/colorspace.h
               src/common.cpp
               src/common.h
               src/dither-matrix256.h
               src/editimagepanel.cpp
               src/editimagepanel.h
               src/envmap.cpp
               src/envmap.h
               src/fwd.h
               src/glimage.cpp
               src/glimage.h
               src/hdrimage.cpp
               src/hdrimage.h
               src/hdrimageio.cpp
               src/hdrimagemanager.cpp
               src/hdrimagemanager.h
               src/hdrimageviewer.cpp
               src/hdrimageviewer.h
               src/hdrview.cpp
               src/hdrviewer.cpp
               src/hdrviewer.h
               src/helpwindow.cpp
               src/helpwindow.h
               src/imagebutton.cpp
               src/imagebutton.h
               src/imagelistpanel.cpp
               src/imagelistpanel.h
               src/imageshader.cpp
               src/imageshader.h
               src/multigraph.cpp
               src/multigraph.h
               src/parallelfor.cpp
               src/parallelfor.h
               src/pfm.h
               src/pfm.cpp
               src/ppm.h
               src/ppm.cpp
               src/range.h
               src/timer.h
               src/well.cpp
               src/well.h
               ${EXTRA_SOURCE})

add_definitions(${NANOGUI_EXTRA_DEFS})

set_target_properties(HDRView PROPERTIES OUTPUT_NAME "HDRView")

if (APPLE)
    # Build an application bundle on OSX
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE TRUE)
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "HDRView")
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_INFO_STRING "High dynamic range image viewer and comparison tool, version ${HDRVIEW_VERSION}.")
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_COPYRIGHT "Copyright 2017, Wojciech Jarosz. Freely available under the BSD license.")
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_BUNDLE_GUI_IDENTIFIER "com.im.HDRView")
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_ICON_FILE icon.icns)
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_BUNDLE_VERSION ${HDRVIEW_VERSION})
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_SHORT_VERSION_STRING ${HDRVIEW_VERSION})
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_LONG_VERSION_STRING ${HDRVIEW_VERSION})
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/MacOSXBundleInfo.plist.in)
    set_target_properties(HDRView PROPERTIES RESOURCE resources/icon.icns)
    set_source_files_properties(resources/icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
elseif (NOT WIN32)
    # Insulate from a few types of ABI changes by statically linking against libgcc and libstdc++
    set_target_properties(HDRView PROPERTIES LINK_FLAGS "-static-libgcc")
endif()

if (UNIX AND NOT ${U_CMAKE_BUILD_TYPE} MATCHES DEBUG)
    add_custom_command(TARGET HDRView POST_BUILD COMMAND strip $<TARGET_FILE:HDRView>)
endif()

add_executable(hdrbatch
               src/color.cpp
               src/colorspace.h
               src/colorspace.cpp
               src/common.cpp
               src/common.h
               src/envmap.cpp
               src/envmap.h
               src/pfm.h
               src/pfm.cpp
               src/ppm.h
               src/ppm.cpp
               src/dither-matrix256.h
               src/color.h
               src/hdrimage.h
               src/hdrimage.cpp
               src/hdrimageio.cpp
               src/hdrbatch.cpp
               src/parallelfor.cpp
               src/parallelfor.h
               src/range.h)


add_executable(force-random-dither
    src/forced-random-dither.cpp)

target_link_libraries(HDRView IlmImf nanogui docopt_s ${NANOGUI_EXTRA_LIBS} ${Boost_REGEX_LIBRARY})
target_link_libraries(hdrbatch IlmImf docopt_s ${Boost_REGEX_LIBRARY})
target_link_libraries(force-random-dither nanogui ${NANOGUI_EXTRA_LIBS})

if (NOT ${CMAKE_VERSION} VERSION_LESS 3.3 AND IWYU)
    find_program(iwyu_path NAMES include-what-you-use iwyu)
    if (iwyu_path)
        set_property(TARGET HDRView hdrbatch force-random-dither PROPERTY CXX_INCLUDE_WHAT_YOU_USE ${iwyu_path})
    endif()
endif()

if (APPLE)
    install(TARGETS HDRView BUNDLE DESTINATION "/Applications")
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
