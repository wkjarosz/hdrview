# cmake-format: off
#
# Module to find libjxl
#
# This modules defines the following variables:
# - JXL_FOUND       True if libjxl was found.
# - JXL_INCLUDES    Directory to include for libjxl headers
# - JXL_LIBRARIES   Libraries to link to
# 
# If found, it will also define imported targets for the libraries:
# - jxl
# - jxl_threads
# - jxl_cms
# 
# cmake-format: on

include(FindPackageHandleStandardArgs)

find_path(JXL_INCLUDE_DIR NAMES jxl/decode.h jxl/encode.h)
mark_as_advanced(JXL_INCLUDE_DIR)

if(JXL_INCLUDE_DIR)
  file(STRINGS "${JXL_INCLUDE_DIR}/jxl/version.h" TMP REGEX "^#define JPEGXL_MAJOR_VERSION .*$")
  string(REGEX MATCHALL "[0-9]+" JPEGXL_MAJOR_VERSION ${TMP})
  file(STRINGS "${JXL_INCLUDE_DIR}/jxl/version.h" TMP REGEX "^#define JPEGXL_MINOR_VERSION .*$")
  string(REGEX MATCHALL "[0-9]+" JPEGXL_MINOR_VERSION ${TMP})
  file(STRINGS "${JXL_INCLUDE_DIR}/jxl/version.h" TMP REGEX "^#define JPEGXL_PATCH_VERSION .*$")
  string(REGEX MATCHALL "[0-9]+" JPEGXL_PATCH_VERSION ${TMP})
  set(JXL_VERSION "${JPEGXL_MAJOR_VERSION}.${JPEGXL_MINOR_VERSION}.${JPEGXL_PATCH_VERSION}")
endif()

find_library(JXL_LIBRARY NAMES jxl)
mark_as_advanced(JXL_LIBRARY JXL_VERSION)

find_library(JXL_THREADS_LIBRARY NAMES jxl_threads)
mark_as_advanced(JXL_THREADS_LIBRARY)

find_library(JXL_CMS_LIBRARY NAMES jxl_cms)
mark_as_advanced(JXL_CMS_LIBRARY)

find_package_handle_standard_args(
  JXL
  REQUIRED_VARS JXL_LIBRARY JXL_THREADS_LIBRARY JXL_INCLUDE_DIR
  VERSION_VAR JXL_VERSION
)

if(JXL_FOUND)
  set(JXL_LIBRARIES ${JXL_LIBRARY} ${JXL_THREADS_LIBRARY} ${JXL_CMS_LIBRARY})
  set(JXL_INCLUDES ${JXL_INCLUDE_DIR})

  if(JXL_LIBRARY AND NOT TARGET jxl)
    add_library(jxl SHARED IMPORTED)
    set_target_properties(
      jxl
      PROPERTIES INTERFACE_COMPILE_FEATURES "cxx_std_11"
                 INTERFACE_INCLUDE_DIRECTORIES "${JXL_INCLUDES}"
                 INTERFACE_LINK_LIBRARIES "${JXL_LIBRARY}"
                 IMPORTED_LOCATION "${JXL_LIBRARY}"
    )
  endif()

  if(JXL_THREADS_LIBRARY AND NOT TARGET jxl_threads)
    add_library(jxl_threads SHARED IMPORTED)
    set_target_properties(
      jxl_threads
      PROPERTIES INTERFACE_COMPILE_FEATURES "cxx_std_11"
                 INTERFACE_INCLUDE_DIRECTORIES "${JXL_INCLUDES}"
                 INTERFACE_LINK_LIBRARIES "${JXL_THREADS_LIBRARY}"
                 IMPORTED_LOCATION "${JXL_THREADS_LIBRARY}"
    )
  endif()

  if(JXL_CMS_LIBRARY AND NOT TARGET jxl_cms)
    add_library(jxl_cms SHARED IMPORTED)
    set_target_properties(
      jxl_cms
      PROPERTIES INTERFACE_COMPILE_FEATURES "cxx_std_11"
                 INTERFACE_INCLUDE_DIRECTORIES "${JXL_INCLUDES}"
                 INTERFACE_LINK_LIBRARIES "${JXL_CMS_LIBRARY}"
                 IMPORTED_LOCATION "${JXL_CMS_LIBRARY}"
    )
  endif()
endif(JXL_FOUND)
