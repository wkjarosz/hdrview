# Findlibwebp.cmake
# Module to find system libwebp library
#
# This module will define the following variables:
#
# - libwebp_FOUND       True if libwebp was found
# - WEBP_INCLUDE_DIRS   Where to find webp headers
# - WEBP_LIBRARIES      List of libraries to link against
# - WEBP_VERSION        Version of libwebp (e.g., 1.6.0)
#
# If found, it will also define imported targets:
# - webp
# - webpdemux

include(FindPackageHandleStandardArgs)

# Try using pkg-config first
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_WEBP QUIET libwebp)
  pkg_check_modules(PC_WEBPDEMUX QUIET libwebpdemux)
  set(WEBP_VERSION ${PC_WEBP_VERSION})
endif()

find_path(
  WEBP_INCLUDE_DIR webp/decode.h
  HINTS ${PC_WEBP_INCLUDE_DIRS}
  PATH_SUFFIXES include
  DOC "The directory where webp headers reside"
)

find_library(
  WEBP_LIBRARY webp
  HINTS ${PC_WEBP_LIBRARY_DIRS}
  PATH_SUFFIXES lib lib64
  DOC "The webp library"
)

find_library(
  WEBPDEMUX_LIBRARY webpdemux
  HINTS ${PC_WEBPDEMUX_LIBRARY_DIRS}
  PATH_SUFFIXES lib lib64
  DOC "The webpdemux library"
)

find_package_handle_standard_args(
  libwebp
  REQUIRED_VARS WEBP_INCLUDE_DIR WEBP_LIBRARY WEBPDEMUX_LIBRARY
  VERSION_VAR WEBP_VERSION
)

if(libwebp_FOUND)
  # Explicitly set libwebp_FOUND in cache to ensure it's visible to CPM
  set(libwebp_FOUND TRUE CACHE BOOL "libwebp was found" FORCE)
  # Export variables to cache so they persist to parent scope
  set(WEBP_INCLUDE_DIRS "${WEBP_INCLUDE_DIR}" CACHE INTERNAL "")
  set(WEBP_LIBRARIES "${WEBP_LIBRARY}" CACHE INTERNAL "")
  set(WEBP_VERSION "${WEBP_VERSION}" CACHE INTERNAL "")

  if(NOT TARGET webp)
    add_library(webp UNKNOWN IMPORTED)
    set_target_properties(
      webp PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${WEBP_INCLUDE_DIRS}"
      IMPORTED_LOCATION "${WEBP_LIBRARY}"
    )
  endif()

  if(NOT TARGET webpdemux)
    add_library(webpdemux UNKNOWN IMPORTED)
    set_target_properties(
      webpdemux PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${WEBP_INCLUDE_DIRS}"
      IMPORTED_LOCATION "${WEBPDEMUX_LIBRARY}"
    )
  endif()
endif()

mark_as_advanced(WEBP_INCLUDE_DIR WEBP_LIBRARY WEBPDEMUX_LIBRARY WEBP_VERSION)
