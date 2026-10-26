# Run at install time, after dylibbundler has copied the libraries HDRView links into HDRView.app/Contents/libs.
#
# dylibbundler rewrites every rpath a library carries to the same @executable_path/../libs/, so a library built
# with two rpaths (Homebrew's libde265 has @loader_path/../lib and @loader_path/../../lib) ends up with that one
# twice, and dyld refuses to load a library with a duplicate LC_RPATH. This deletes the extra copies and
# re-signs what it changed, the library and then the bundle sealing it, since an edit invalidates the
# signature and the kernel kills a process that maps code whose signature is invalid.

set(app "${CMAKE_INSTALL_PREFIX}/HDRView.app")
file(GLOB binaries "${app}/Contents/MacOS/HDRView" "${app}/Contents/libs/*.dylib")

set(changed FALSE)
foreach(binary IN LISTS binaries)
  execute_process(COMMAND otool -l "${binary}" OUTPUT_VARIABLE load_commands RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    continue()
  endif()

  # each LC_RPATH load command is followed, two lines later, by "path <rpath> (offset N)"
  string(REGEX MATCHALL "cmd LC_RPATH\n[^\n]*\n *path ([^\n]*) \\(offset" entries "${load_commands}")
  set(seen "")
  set(edited FALSE)
  foreach(entry IN LISTS entries)
    string(REGEX REPLACE ".*path (.*) \\(offset" "\\1" rpath "${entry}")
    if(rpath IN_LIST seen)
      # deletes one occurrence per call, so the first stays
      execute_process(COMMAND install_name_tool -delete_rpath "${rpath}" "${binary}" ERROR_QUIET)
      message(STATUS "Removed a duplicate rpath ${rpath} from ${binary}")
      set(edited TRUE)
    else()
      list(APPEND seen "${rpath}")
    endif()
  endforeach()

  if(edited)
    execute_process(COMMAND codesign --force --sign - "${binary}" OUTPUT_QUIET ERROR_QUIET)
    set(changed TRUE)
  endif()
endforeach()

if(changed)
  execute_process(COMMAND codesign --force --sign - "${app}" OUTPUT_QUIET ERROR_QUIET)
endif()
