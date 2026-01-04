#----------------------------------------------------------------
# Generated CMake target import file for configuration "MinSizeRel".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "WebP::sharpyuv" for configuration "MinSizeRel"
set_property(TARGET WebP::sharpyuv APPEND PROPERTY IMPORTED_CONFIGURATIONS MINSIZEREL)
set_target_properties(WebP::sharpyuv PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_MINSIZEREL "C"
  IMPORTED_LOCATION_MINSIZEREL "${_IMPORT_PREFIX}/lib/libsharpyuv.a"
  )

list(APPEND _cmake_import_check_targets WebP::sharpyuv )
list(APPEND _cmake_import_check_files_for_WebP::sharpyuv "${_IMPORT_PREFIX}/lib/libsharpyuv.a" )

# Import target "WebP::webpdecoder" for configuration "MinSizeRel"
set_property(TARGET WebP::webpdecoder APPEND PROPERTY IMPORTED_CONFIGURATIONS MINSIZEREL)
set_target_properties(WebP::webpdecoder PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_MINSIZEREL "C"
  IMPORTED_LOCATION_MINSIZEREL "${_IMPORT_PREFIX}/lib/libwebpdecoder.a"
  )

list(APPEND _cmake_import_check_targets WebP::webpdecoder )
list(APPEND _cmake_import_check_files_for_WebP::webpdecoder "${_IMPORT_PREFIX}/lib/libwebpdecoder.a" )

# Import target "WebP::webp" for configuration "MinSizeRel"
set_property(TARGET WebP::webp APPEND PROPERTY IMPORTED_CONFIGURATIONS MINSIZEREL)
set_target_properties(WebP::webp PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_MINSIZEREL "C"
  IMPORTED_LOCATION_MINSIZEREL "${_IMPORT_PREFIX}/lib/libwebp.a"
  )

list(APPEND _cmake_import_check_targets WebP::webp )
list(APPEND _cmake_import_check_files_for_WebP::webp "${_IMPORT_PREFIX}/lib/libwebp.a" )

# Import target "WebP::webpdemux" for configuration "MinSizeRel"
set_property(TARGET WebP::webpdemux APPEND PROPERTY IMPORTED_CONFIGURATIONS MINSIZEREL)
set_target_properties(WebP::webpdemux PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_MINSIZEREL "C"
  IMPORTED_LOCATION_MINSIZEREL "${_IMPORT_PREFIX}/lib/libwebpdemux.a"
  )

list(APPEND _cmake_import_check_targets WebP::webpdemux )
list(APPEND _cmake_import_check_files_for_WebP::webpdemux "${_IMPORT_PREFIX}/lib/libwebpdemux.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
