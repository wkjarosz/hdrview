# Standalone script (run via `cmake -DFILE=<path> -P StripLeadingVersionLine.cmake`) that removes a leading
# `#version ...` line from a generated GLSL file, if present. shader_gl.cpp prepends its own
# `#version 300 es`/`#version 330 core` line at runtime based on the detected GL context (the old hand-written
# .glsl sources never had one of their own), but sokol-shdc's generated output always starts with one -- left
# in place, the file would end up with two back-to-back #version lines, which GLSL rejects ("#version directive
# must occur before anything else").
#
# Reads/writes the file as one whole string (not via file(STRINGS), which splits into a CMake list and would
# mangle the semicolons that appear on essentially every GLSL line).

if(NOT FILE)
  message(FATAL_ERROR "StripLeadingVersionLine.cmake: -DFILE=<path> is required")
endif()

file(READ "${FILE}" _content)
string(REGEX REPLACE "^#version[^\n]*\n" "" _content "${_content}")
file(WRITE "${FILE}" "${_content}")
