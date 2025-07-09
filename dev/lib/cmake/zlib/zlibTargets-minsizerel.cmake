#----------------------------------------------------------------
# Generated CMake target import file for configuration "MinSizeRel".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "ZLIB::ZLIB" for configuration "MinSizeRel"
set_property(TARGET ZLIB::ZLIB APPEND PROPERTY IMPORTED_CONFIGURATIONS MINSIZEREL)
set_target_properties(ZLIB::ZLIB PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_MINSIZEREL "C"
  IMPORTED_LOCATION_MINSIZEREL "${_IMPORT_PREFIX}/lib/libz.a"
  )

list(APPEND _cmake_import_check_targets ZLIB::ZLIB )
list(APPEND _cmake_import_check_files_for_ZLIB::ZLIB "${_IMPORT_PREFIX}/lib/libz.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
