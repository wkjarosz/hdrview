cmake_minimum_required (VERSION 3.1)
project(hdrview)

add_subdirectory(ext ext_build)

include_directories(
    # GLFW library for OpenGL context creation
    ${GLFW_INCLUDE_DIR}
    # GLEW library for accessing OpenGL functions
    ${GLEW_INCLUDE_DIR}
    # NanoVG drawing library
    ${NANOVG_INCLUDE_DIR}
    # NanoGUI user interface library
    ${NANOGUI_INCLUDE_DIR}
    ${NANOGUI_EXTRA_INCS}
    # OpenEXR high dynamic range bitmap library
    ${OPENEXR_INCLUDE_DIRS}
    # tinydir
    ${TINYDIR_INCLUDE_DIR}
    # stb
    ${STB_INCLUDE_DIR}
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

add_executable(HDRView MACOSX_BUNDLE
    src/color.h
    src/common.cpp
    src/common.h
    src/dither-matrix256.h
    src/editimagepanel.cpp
    src/editimagepanel.h
    src/envmap.cpp
    src/envmap.h
    src/fwd.h
    src/gldithertexture.h
    src/glimage.cpp
    src/glimage.h
    src/hdrimage.cpp
    src/hdrimage.h
    src/hdrimagemanager.cpp
    src/hdrimagemanager.h
    src/hdrimageviewer.cpp
    src/hdrimageviewer.h
    src/hdrview.cpp
    src/hdrviewer.cpp
    src/hdrviewer.h
    src/helpwindow.cpp
    src/helpwindow.h
    src/histogrampanel.cpp
    src/histogrampanel.h
    src/imagebutton.cpp
    src/imagebutton.h
    src/imagelistpanel.cpp
    src/imagelistpanel.h
    src/multigraph.cpp
    src/multigraph.h
    src/pfm.h
    src/pfm.cpp
    src/ppm.h
    src/ppm.cpp
    src/range.h
    ${EXTRA_SOURCE})

add_definitions(${NANOGUI_EXTRA_DEFS})

set_target_properties(HDRView PROPERTIES OUTPUT_NAME "HDRView")

if (APPLE)
    # Build an application bundle on OSX
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "HDRView")
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_BUNDLE_GUI_IDENTIFIER "com.im.HDRView")
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_ICON_FILE icon.icns)
    set_target_properties(HDRView PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/MacOSXBundleInfo.plist.in)
    set_source_files_properties(resources/icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
elseif (NOT WIN32)
    # Insulate from a few types of ABI changes by statically linking against libgcc and libstdc++
    set_target_properties(HDRView PROPERTIES LINK_FLAGS "-static-libgcc")
endif()

if (UNIX AND NOT ${U_CMAKE_BUILD_TYPE} MATCHES DEBUG)
    add_custom_command(TARGET HDRView POST_BUILD COMMAND strip $<TARGET_FILE:HDRView>)
endif()


add_executable(hdrbatch
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
    src/hdrbatch.cpp
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