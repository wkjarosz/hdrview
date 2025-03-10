# cmake-format: off
# Module to find libuhdr
#
# This module defines the following variables:
#
# - libuhdr_FOUND         True if libuhdr was found.
# - LIBUHDR_INCLUDES      Directory to include for libuhdr headers
# - LIBUHDR_LIBRARY       Library to link to
# 
# If found, it will also define an imported targets for the library:
# - uhdr
# 
# cmake-format: on

include(FindPackageHandleStandardArgs)

find_path(
  LIBUHDR_INCLUDES
  NAMES ultrahdr_api.h
  PATH_SUFFIXES include
)
mark_as_advanced(LIBUHDR_INCLUDES)

find_library(LIBUHDR_LIBRARY uhdr PATH_SUFFIXES lib)

find_package_handle_standard_args(libuhdr REQUIRED_VARS LIBUHDR_INCLUDES LIBUHDR_LIBRARY)

if(libuhdr_FOUND)
  if(NOT TARGET core)
    add_library(core UNKNOWN IMPORTED)
    set_target_properties(
      core
      PROPERTIES CXX_STANDARD 17
                 INTERFACE_INCLUDE_DIRECTORIES "${LIBUHDR_INCLUDES}"
                 INTERFACE_LINK_LIBRARIES "${LIBUHDR_LIBRARY}"
                 IMPORTED_LOCATION "${LIBUHDR_LIBRARY}"
    )
  endif()
endif(libuhdr_FOUND)
