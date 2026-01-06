# AppImage creation script for HDRView
# This script is invoked by CPack to create an AppImage package

include(CMakePrintHelpers)
cmake_print_variables(CPACK_TEMPORARY_DIRECTORY)
cmake_print_variables(CPACK_TOPLEVEL_DIRECTORY)
cmake_print_variables(CPACK_PACKAGE_DIRECTORY)
cmake_print_variables(CPACK_PACKAGE_FILE_NAME)
cmake_print_variables(CMAKE_SYSTEM_PROCESSOR)

find_program(LINUXDEPLOY_EXECUTABLE
    NAMES linuxdeploy linuxdeploy-${CMAKE_SYSTEM_PROCESSOR}.AppImage
    PATHS ${CPACK_PACKAGE_DIRECTORY}/dependencies/)

if (NOT LINUXDEPLOY_EXECUTABLE)
    message(WARNING "Couldn't find linuxdeploy. Downloading pre-built binary instead.")
    set(LINUXDEPLOY_EXECUTABLE ${CPACK_PACKAGE_DIRECTORY}/dependencies/linuxdeploy-${CMAKE_SYSTEM_PROCESSOR}.AppImage)
    file(DOWNLOAD 
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${CMAKE_SYSTEM_PROCESSOR}.AppImage
        ${LINUXDEPLOY_EXECUTABLE}
        INACTIVITY_TIMEOUT 10
        LOG ${CPACK_PACKAGE_DIRECTORY}/linuxdeploy/download.log
        STATUS LINUXDEPLOY_DOWNLOAD)
    execute_process(COMMAND chmod +x ${LINUXDEPLOY_EXECUTABLE} COMMAND_ECHO STDOUT)
endif()

execute_process(
    COMMAND
        ${CMAKE_COMMAND} -E env
        OUTPUT=${CPACK_PACKAGE_FILE_NAME}.appimage
        VERSION=${CPACK_PACKAGE_VERSION}
        NO_STRIP=1
        ${LINUXDEPLOY_EXECUTABLE}
        --appdir=${CPACK_TEMPORARY_DIRECTORY}
        --output=appimage
    WORKING_DIRECTORY ${CPACK_PACKAGE_DIRECTORY}
    COMMAND_ECHO STDOUT
)
