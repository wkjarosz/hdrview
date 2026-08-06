# Fetches the prebuilt sokol-shdc binary (via sokol-tools-bin, no fips required) and provides
# sokol_shdc_generate() to cross-compile an annotated-GLSL (.sglsl) source into plain per-backend
# shader text at both configure time (so files exist before any configure-time consumers, e.g.
# hello_imgui's own asset packaging) and build time (so editing a .sglsl file and re-running
# `cmake --build` alone picks up the change).

if(CMAKE_HOST_APPLE)
  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
    set(_sokol_shdc_platform_dir "osx_arm64")
  else()
    set(_sokol_shdc_platform_dir "osx")
  endif()
  set(_sokol_shdc_exe_name "sokol-shdc")
elseif(CMAKE_HOST_WIN32)
  set(_sokol_shdc_platform_dir "win32")
  set(_sokol_shdc_exe_name "sokol-shdc.exe")
elseif(CMAKE_HOST_UNIX)
  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_sokol_shdc_platform_dir "linux_arm64")
  else()
    set(_sokol_shdc_platform_dir "linux")
  endif()
  set(_sokol_shdc_exe_name "sokol-shdc")
else()
  message(FATAL_ERROR "SokolShaderCompiler: unsupported host platform for prebuilt sokol-shdc")
endif()

CPMAddPackage(
  NAME sokol_tools_bin
  GITHUB_REPOSITORY floooh/sokol-tools-bin
  GIT_TAG 9adef5465d8b9e7f412b0ffd48017e2741628c27
  DOWNLOAD_ONLY YES
)

set(SOKOL_SHDC_EXECUTABLE "${sokol_tools_bin_SOURCE_DIR}/bin/${_sokol_shdc_platform_dir}/${_sokol_shdc_exe_name}")

if(NOT EXISTS "${SOKOL_SHDC_EXECUTABLE}")
  message(FATAL_ERROR "SokolShaderCompiler: expected sokol-shdc binary not found at ${SOKOL_SHDC_EXECUTABLE}")
endif()

if(NOT CMAKE_HOST_WIN32)
  # The executable bit doesn't reliably survive CPM's fetch (tarball vs. git clone), so force it.
  file(
    CHMOD "${SOKOL_SHDC_EXECUTABLE}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
  )
endif()

message(STATUS "SokolShaderCompiler: using sokol-shdc at ${SOKOL_SHDC_EXECUTABLE}")

# Running list of custom targets created by sokol_shdc_generate(), so callers can make the main app
# target depend on all of them once it exists (hello_imgui_add_app() is called much later in the
# top-level CMakeLists.txt than the shader sources are declared).
set(HDRVIEW_SOKOL_SHDC_TARGETS
    ""
    CACHE INTERNAL "Custom targets that (re-)generate sokol-shdc output; see SokolShaderCompiler.cmake"
)

# sokol_shdc_generate(
#   INPUT <path/to/source.sglsl>
#   OUTPUT_DIR <dir>       # generated files are written as <OUTPUT_DIR>/<NAME>_<program>_<slang>_<stage>.<ext>
#   NAME <basename>
#   PROGRAM <program-name> # the name after @program in the .sglsl source
#   SLANG <slang1> [<slang2> ...]   # e.g. glsl300es metal_macos
#   RENAME <from1> <to1> [<from2> <to2> ...]   # optional: rename sokol-shdc's own <program>_<slang>_<stage>.<ext>
#                                               # output (relative to OUTPUT_DIR) to the filenames Shader::from_asset()
#                                               # expects, e.g. image_shader_glsl300es_vertex.glsl -> image-shader_vert.glsl
# )
function(sokol_shdc_generate)
  set(oneValueArgs INPUT OUTPUT_DIR NAME PROGRAM)
  set(multiValueArgs SLANG RENAME EXTRA_DEPENDS)
  cmake_parse_arguments(ARG "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT ARG_INPUT OR NOT ARG_OUTPUT_DIR OR NOT ARG_NAME OR NOT ARG_SLANG)
    message(FATAL_ERROR "sokol_shdc_generate: INPUT, OUTPUT_DIR, NAME, and SLANG are all required")
  endif()

  string(REPLACE ";" ":" _slang_joined "${ARG_SLANG}")
  file(MAKE_DIRECTORY "${ARG_OUTPUT_DIR}")
  set(_out_base "${ARG_OUTPUT_DIR}/${ARG_NAME}")

  set(_sokol_shdc_command
      "${SOKOL_SHDC_EXECUTABLE}" --input "${ARG_INPUT}" --output "${_out_base}" --slang "${_slang_joined}"
      --format=bare --no-log-cmdline
  )

  # sokol-shdc always names bare-format output "<output>_<program>_<slang>_<stage>.<ext>"; rename to whatever
  # Shader::from_asset() expects. list(LENGTH ...) guards odd-length RENAME lists (a from without a matching to).
  set(_rename_commands "")
  list(LENGTH ARG_RENAME _rename_len)
  math(EXPR _rename_pairs "${_rename_len} / 2")
  math(EXPR _rename_last_idx "${_rename_pairs} - 1")
  if(_rename_pairs GREATER 0)
    foreach(_i RANGE 0 ${_rename_last_idx})
      math(EXPR _from_idx "${_i} * 2")
      math(EXPR _to_idx "${_from_idx} + 1")
      list(GET ARG_RENAME ${_from_idx} _from)
      list(GET ARG_RENAME ${_to_idx} _to)
      list(APPEND _rename_commands COMMAND ${CMAKE_COMMAND} -E rename "${ARG_OUTPUT_DIR}/${_from}"
           "${ARG_OUTPUT_DIR}/${_to}"
      )
    endforeach()
  endif()

  # Run once now (configure time) so outputs exist before any configure-time consumer looks for them.
  execute_process(
    COMMAND ${_sokol_shdc_command}
    RESULT_VARIABLE _sokol_shdc_result
    OUTPUT_VARIABLE _sokol_shdc_log
    ERROR_VARIABLE _sokol_shdc_log
  )
  if(NOT _sokol_shdc_result EQUAL 0)
    message(FATAL_ERROR "sokol-shdc failed on ${ARG_INPUT}:\n${_sokol_shdc_log}")
  endif()
  # Renames run as separate execute_process() calls (not one call with multiple COMMANDs, which would pipe
  # each command's stdout into the next rather than running them as an independent sequence).
  if(_rename_pairs GREATER 0)
    foreach(_i RANGE 0 ${_rename_last_idx})
      math(EXPR _from_idx "${_i} * 2")
      math(EXPR _to_idx "${_from_idx} + 1")
      list(GET ARG_RENAME ${_from_idx} _from)
      list(GET ARG_RENAME ${_to_idx} _to)
      execute_process(COMMAND ${CMAKE_COMMAND} -E rename "${ARG_OUTPUT_DIR}/${_from}" "${ARG_OUTPUT_DIR}/${_to}")
    endforeach()
  endif()

  # Also re-run at build time, so `cmake --build` alone (no reconfigure) picks up .sglsl edits.
  add_custom_command(
    OUTPUT "${_out_base}.stamp"
    COMMAND ${_sokol_shdc_command} ${_rename_commands}
    COMMAND ${CMAKE_COMMAND} -E touch "${_out_base}.stamp"
    DEPENDS "${ARG_INPUT}" ${ARG_EXTRA_DEPENDS}
    COMMENT "sokol-shdc: compiling ${ARG_NAME}"
    VERBATIM
  )
  add_custom_target(sokol_shdc_${ARG_NAME} DEPENDS "${_out_base}.stamp")

  set(HDRVIEW_SOKOL_SHDC_TARGETS
      "${HDRVIEW_SOKOL_SHDC_TARGETS};sokol_shdc_${ARG_NAME}"
      CACHE INTERNAL "Custom targets that (re-)generate sokol-shdc output; see SokolShaderCompiler.cmake"
  )
endfunction()
