# Defines:
# HDRVIEW_TARGET_ARCHS (list, e.g. "arm64;x86_64")
# HDRVIEW_TARGET_ARCH (string: "arm64", "x86_64" or "Universal")
# HDRVIEW_BITNESS (8/64)
# HDRVIEW_IS_UNIVERSAL (ON/OFF)
# HDRVIEW_HOST_ARCH (the host processor)
# 
function(detect_architecture)
  # Bitness (32/64)
  if(DEFINED CMAKE_SIZEOF_VOID_P)
    math(EXPR _BITNESS "${CMAKE_SIZEOF_VOID_P} * 8")
  else()
    # Fallback: assume 64-bit
    set(_BITNESS 64)
  endif()
  set(HDRVIEW_BITNESS "${_BITNESS}" PARENT_SCOPE)
  
  # Host processor string for informational use
  if(DEFINED CMAKE_HOST_SYSTEM_PROCESSOR)
    set(HDRVIEW_HOST_ARCH "${CMAKE_HOST_SYSTEM_PROCESSOR}" PARENT_SCOPE)
  else()
    set(HDRVIEW_HOST_ARCH "${CMAKE_SYSTEM_PROCESSOR}" PARENT_SCOPE)
  endif()

  # Special-case Emscripten/WebAssembly early: canonicalize to wasm32
  if(EMSCRIPTEN)
    set(HDRVIEW_TARGET_ARCHS "wasm32" PARENT_SCOPE)
    set(HDRVIEW_TARGET_ARCH "wasm32" PARENT_SCOPE)
    set(HDRVIEW_IS_UNIVERSAL OFF PARENT_SCOPE)
    return()
  endif()

  # Determine the target architecture list. Prefer CMAKE_OSX_ARCHITECTURES
  # (may be a semicolon-separated list) when present; otherwise use
  # CMAKE_SYSTEM_PROCESSOR as a single-entry list.
  if(DEFINED CMAKE_OSX_ARCHITECTURES AND CMAKE_OSX_ARCHITECTURES)
    set(_archs ${CMAKE_OSX_ARCHITECTURES})
  else()
    set(_archs ${CMAKE_SYSTEM_PROCESSOR})
  endif()

  # Expose the canonical list form
  set(HDRVIEW_TARGET_ARCHS "${_archs}" PARENT_SCOPE)

  # Determine a single descriptive string
  set(_lower_archs "${_archs}")
  string(TOLOWER "${_lower_archs}" _lower_archs)

  list(FIND _archs "arm64" _has_arm)
  list(FIND _archs "x86_64" _has_x86)
  if(_has_arm GREATER -1 AND _has_x86 GREATER -1)
    set(HDRVIEW_TARGET_ARCH "Universal" PARENT_SCOPE)
    set(HDRVIEW_IS_UNIVERSAL ON PARENT_SCOPE)
  elseif(_has_arm GREATER -1)
    set(HDRVIEW_TARGET_ARCH "arm64" PARENT_SCOPE)
    set(HDRVIEW_IS_UNIVERSAL OFF PARENT_SCOPE)
  elseif(_has_x86 GREATER -1)
    set(HDRVIEW_TARGET_ARCH "x86_64" PARENT_SCOPE)
    set(HDRVIEW_IS_UNIVERSAL OFF PARENT_SCOPE)
  elseif(_lower_archs MATCHES "(^|;)(i386|i686|x86)(;|$)")
    # Handle 32-bit x86 canonical names (i386/i686/x86)
    set(HDRVIEW_TARGET_ARCH "x86" PARENT_SCOPE)
    set(HDRVIEW_IS_UNIVERSAL OFF PARENT_SCOPE)
  else()
    list(GET _archs 0 _first)
    set(HDRVIEW_TARGET_ARCH "${_first}" PARENT_SCOPE)
    set(HDRVIEW_IS_UNIVERSAL OFF PARENT_SCOPE)
  endif()
endfunction()