# FindLibRaw.cmake
# Module to find system LibRaw library
#
# This module will define the following variables:
#
# - LibRaw_FOUND        True if LibRaw was found
# - LIBRAW_INCLUDE_DIRS Where to find libraw headers
# - LIBRAW_LIBRARIES    List of libraries to link against
# - LIBRAW_VERSION      Version of LibRaw (e.g., 0.21.4)
#
# If found, it will also define an imported target:
# - libraw::libraw_r

include(FindPackageHandleStandardArgs)

# Try using pkg-config first
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_LIBRAW QUIET libraw)
  set(LIBRAW_VERSION ${PC_LIBRAW_VERSION})
endif()

find_path(
  LIBRAW_INCLUDE_DIR libraw/libraw.h
  HINTS ${PC_LIBRAW_INCLUDE_DIRS}
  PATH_SUFFIXES include
  DOC "The directory where libraw headers reside"
)

find_library(
  LIBRAW_LIBRARY raw_r
  HINTS ${PC_LIBRAW_LIBRARY_DIRS}
  PATH_SUFFIXES lib lib64
  DOC "The libraw library"
)

find_package_handle_standard_args(
  LibRaw
  REQUIRED_VARS LIBRAW_INCLUDE_DIR LIBRAW_LIBRARY
  VERSION_VAR LIBRAW_VERSION
)

if(LibRaw_FOUND)
  # Explicitly set LibRaw_FOUND in cache to ensure it's visible to CPM
  set(LibRaw_FOUND TRUE CACHE BOOL "LibRaw was found" FORCE)
  # Export variables to cache so they persist to parent scope
  set(LIBRAW_INCLUDE_DIRS "${LIBRAW_INCLUDE_DIR}" CACHE INTERNAL "")
  set(LIBRAW_LIBRARIES "${LIBRAW_LIBRARY}" CACHE INTERNAL "")
  set(LIBRAW_VERSION "${LIBRAW_VERSION}" CACHE INTERNAL "")

  if(NOT TARGET libraw::libraw_r)
    add_library(libraw::libraw_r UNKNOWN IMPORTED)
    set_target_properties(
      libraw::libraw_r PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${LIBRAW_INCLUDE_DIRS}"
      IMPORTED_LOCATION "${LIBRAW_LIBRARIES}"
    )
  endif()
endif()

mark_as_advanced(LIBRAW_INCLUDE_DIR LIBRAW_LIBRARY LIBRAW_VERSION)
