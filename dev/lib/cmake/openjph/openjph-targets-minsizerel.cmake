#----------------------------------------------------------------
# Generated CMake target import file for configuration "MinSizeRel".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "openjph" for configuration "MinSizeRel"
set_property(TARGET openjph APPEND PROPERTY IMPORTED_CONFIGURATIONS MINSIZEREL)
set_target_properties(openjph PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_MINSIZEREL "CXX"
  IMPORTED_LOCATION_MINSIZEREL "${_IMPORT_PREFIX}/lib/libopenjph.a"
  )

list(APPEND _cmake_import_check_targets openjph )
list(APPEND _cmake_import_check_files_for_openjph "${_IMPORT_PREFIX}/lib/libopenjph.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
