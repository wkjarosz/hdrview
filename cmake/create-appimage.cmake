# AppImage creation script for HDRView
# This script is invoked by CPack to create an AppImage package

include(CMakePrintHelpers)
cmake_print_variables(CPACK_TEMPORARY_DIRECTORY)
cmake_print_variables(CPACK_TOPLEVEL_DIRECTORY)
cmake_print_variables(CPACK_PACKAGE_DIRECTORY)
cmake_print_variables(CPACK_PACKAGE_FILE_NAME)
cmake_print_variables(CMAKE_SYSTEM_PROCESSOR)

# Determine project root (script lives in cmake/ under project root)
set(CREATE_APPIMAGE_SCRIPT_DIR "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(PROJECT_ROOT "${CREATE_APPIMAGE_SCRIPT_DIR}/.." ABSOLUTE)

# Fetched afresh on every run, never taken from the PATH: linuxdeploy publishes only a rolling "continuous"
# build, and an old one's patchelf breaks the RELR relocations current distributions build their libraries
# with, so the AppImage crashes on launch. The previous download is kept only to package offline.
set(LINUXDEPLOY_EXECUTABLE ${CPACK_PACKAGE_DIRECTORY}/dependencies/linuxdeploy-${CMAKE_SYSTEM_PROCESSOR}.AppImage)
file(DOWNLOAD
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${CMAKE_SYSTEM_PROCESSOR}.AppImage
    ${LINUXDEPLOY_EXECUTABLE}.part
    INACTIVITY_TIMEOUT 10
    STATUS LINUXDEPLOY_DOWNLOAD)
list(GET LINUXDEPLOY_DOWNLOAD 0 LINUXDEPLOY_DOWNLOAD_CODE)
if(LINUXDEPLOY_DOWNLOAD_CODE EQUAL 0)
    file(RENAME ${LINUXDEPLOY_EXECUTABLE}.part ${LINUXDEPLOY_EXECUTABLE})
    execute_process(COMMAND chmod +x ${LINUXDEPLOY_EXECUTABLE})
else()
    file(REMOVE ${LINUXDEPLOY_EXECUTABLE}.part)
    if(NOT EXISTS ${LINUXDEPLOY_EXECUTABLE})
        message(FATAL_ERROR "Could not download linuxdeploy: ${LINUXDEPLOY_DOWNLOAD}")
    endif()
    message(WARNING "Could not download linuxdeploy (${LINUXDEPLOY_DOWNLOAD}); using the copy downloaded earlier")
endif()

# Ensure desktop file is present in the AppDir (some CPack configs may not stage it)
set(APPDIR "${CPACK_TEMPORARY_DIRECTORY}")

# Copy desktop file from project assets into AppDir
set(DESKTOP_SRC "${PROJECT_ROOT}/assets/app_settings/linux/io.github.wkjarosz.hdrview.desktop")
if(EXISTS "${DESKTOP_SRC}")
  file(MAKE_DIRECTORY "${APPDIR}/usr/share/applications")
  file(COPY "${DESKTOP_SRC}" DESTINATION "${APPDIR}/usr/share/applications")
  message(STATUS "Copied desktop file to ${APPDIR}/usr/share/applications")
endif()

# Copy generated icons (if present) or source icons into AppDir hicolor dirs
set(LINUX_ICON_DIR "${PROJECT_ROOT}/build/linux-local/linux_icons")
set(SRC_ICON_DIR "${PROJECT_ROOT}/assets/app_settings")
# Generate linux icons on-demand for the AppDir if they are not present
# (moved from main CMakeLists so packaging has the artifact it needs).
if(NOT EXISTS "${LINUX_ICON_DIR}/icon-16.png")
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        set(MASTER_ICON "${PROJECT_ROOT}/assets/app_settings/icon.png")
        set(ICON_GENERATOR_SCRIPT "${PROJECT_ROOT}/cmake/linux_png_icon_to_multiple_sizes.py")
        message(STATUS "Generating Linux icons from ${MASTER_ICON}")
        execute_process(
            COMMAND ${Python3_EXECUTABLE} ${ICON_GENERATOR_SCRIPT} ${MASTER_ICON} ${LINUX_ICON_DIR}
            RESULT_VARIABLE ICON_GENERATION_RESULT
            OUTPUT_VARIABLE ICON_GENERATION_OUTPUT
            ERROR_VARIABLE ICON_GENERATION_ERROR
        )
        if(NOT ICON_GENERATION_RESULT EQUAL 0)
            message(WARNING "Failed to generate Linux icons: ${ICON_GENERATION_ERROR}")
            message(WARNING "Install the Pillow package: pip install Pillow")
        else()
            message(STATUS "${ICON_GENERATION_OUTPUT}")
        endif()
    else()
        message(WARNING "Python3 not found. Cannot generate Linux icons automatically.")
    endif()
endif()
# Prefer the source icons in assets (these are present in the repo); fall back to generated icons
if(EXISTS "${SRC_ICON_DIR}/icon-16.png")
    file(GLOB SRC_ICON_FILES "${SRC_ICON_DIR}/icon-*.png")
    foreach(I IN LISTS SRC_ICON_FILES)
        get_filename_component(NAME_WE "${I}" NAME_WE)
        string(REPLACE "icon-" "" SIZE "${NAME_WE}")
        set(TARGET_DIR "${APPDIR}/usr/share/icons/hicolor/${SIZE}x${SIZE}/apps")
        file(MAKE_DIRECTORY "${TARGET_DIR}")
        file(INSTALL DESTINATION "${TARGET_DIR}" TYPE FILE RENAME "HDRView.png" FILES "${I}")
    endforeach()
elseif(EXISTS "${LINUX_ICON_DIR}")
    file(GLOB ICON_FILES "${LINUX_ICON_DIR}/icon-*.png")
    foreach(I IN LISTS ICON_FILES)
        get_filename_component(NAME_WE "${I}" NAME_WE)
        string(REPLACE "icon-" "" SIZE "${NAME_WE}")
        set(TARGET_DIR "${APPDIR}/usr/share/icons/hicolor/${SIZE}x${SIZE}/apps")
        file(MAKE_DIRECTORY "${TARGET_DIR}")
        file(INSTALL DESTINATION "${TARGET_DIR}" TYPE FILE RENAME "HDRView.png" FILES "${I}")
    endforeach()
endif()

# Also copy master icon.png to scalable/apps if present in assets
if(EXISTS "${SRC_ICON_DIR}/icon.png")
    file(MAKE_DIRECTORY "${APPDIR}/usr/share/icons/hicolor/scalable/apps")
    file(INSTALL DESTINATION "${APPDIR}/usr/share/icons/hicolor/scalable/apps" TYPE FILE RENAME "HDRView.png" FILES "${SRC_ICON_DIR}/icon.png")
endif()

## No need to manually copy the executable; it is staged by CPack via install(TARGETS ...)

# Lets AppImageUpdate fetch the newest release's AppImage for this architecture, by way of the .zsync file
# linuxdeploy writes next to it; "latest" skips pre-releases. The architecture is the file name's last field.
string(REGEX MATCH "[^-]+$" APPIMAGE_ARCH "${CPACK_PACKAGE_FILE_NAME}")
set(UPDATE_INFORMATION "gh-releases-zsync|wkjarosz|hdrview|latest|HDRView-*-${APPIMAGE_ARCH}.appimage.zsync")

execute_process(
    COMMAND
        ${CMAKE_COMMAND} -E env
        OUTPUT=${CPACK_PACKAGE_FILE_NAME}.appimage
        "LDAI_UPDATE_INFORMATION=${UPDATE_INFORMATION}"
        VERSION=${CPACK_PACKAGE_VERSION}
        NO_STRIP=1
        ${LINUXDEPLOY_EXECUTABLE}
        --appdir=${CPACK_TEMPORARY_DIRECTORY}
        --output=appimage
    WORKING_DIRECTORY ${CPACK_PACKAGE_DIRECTORY}
    COMMAND_ECHO STDOUT
    RESULT_VARIABLE LINUXDEPLOY_RESULT
)
# CPack reports success whatever this script's commands return, so a failed packaging must stop it here.
if(NOT LINUXDEPLOY_RESULT EQUAL 0)
    message(FATAL_ERROR "linuxdeploy failed (${LINUXDEPLOY_RESULT}); no AppImage was produced")
endif()
