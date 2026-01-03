# Findlibexif.cmake
# Module to find system libexif library
#
# This module will define the following variables:
#
# - libexif_FOUND        True if libexif was found
# - LIBEXIF_INCLUDE_DIRS Where to find libexif headers
# - LIBEXIF_LIBRARIES    List of libraries to link against
# - LIBEXIF_VERSION      Version of libexif (e.g., 0.6.24)
#
# If found, it will also define an imported target:
# - exif

include(FindPackageHandleStandardArgs)

# Try using pkg-config first
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_LIBEXIF QUIET libexif)
  set(LIBEXIF_VERSION ${PC_LIBEXIF_VERSION})
endif()

find_path(
  LIBEXIF_INCLUDE_DIR libexif/exif-data.h
  HINTS ${PC_LIBEXIF_INCLUDE_DIRS}
  PATH_SUFFIXES include
  DOC "The directory where libexif headers reside"
)

find_library(
  LIBEXIF_LIBRARY exif
  HINTS ${PC_LIBEXIF_LIBRARY_DIRS}
  PATH_SUFFIXES lib lib64
  DOC "The libexif library"
)

find_package_handle_standard_args(
  libexif
  REQUIRED_VARS LIBEXIF_INCLUDE_DIR LIBEXIF_LIBRARY
  VERSION_VAR LIBEXIF_VERSION
)

if(libexif_FOUND)
  # Explicitly set libexif_FOUND in cache to ensure it's visible to CPM
  set(libexif_FOUND TRUE CACHE BOOL "libexif was found" FORCE)
  # Export variables to cache so they persist to parent scope
  set(LIBEXIF_INCLUDE_DIRS "${LIBEXIF_INCLUDE_DIR}" CACHE INTERNAL "")
  set(LIBEXIF_LIBRARIES "${LIBEXIF_LIBRARY}" CACHE INTERNAL "")
  set(LIBEXIF_VERSION "${LIBEXIF_VERSION}" CACHE INTERNAL "")

  if(NOT TARGET exif)
    add_library(exif UNKNOWN IMPORTED)
    set_target_properties(
      exif PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${LIBEXIF_INCLUDE_DIRS}"
      IMPORTED_LOCATION "${LIBEXIF_LIBRARIES}"
    )
  endif()
endif()

mark_as_advanced(LIBEXIF_INCLUDE_DIR LIBEXIF_LIBRARY LIBEXIF_VERSION)
