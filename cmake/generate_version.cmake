# Generates version.cpp with git information at during build (and not just configure)
#
# Based on:
#
# https://mind.be/compile-time-git-version-info-using-cmake/
#
# and
#
# https://www.mattkeeter.com/blog/2018-01-06-versioning/
#

# Find Git or bail out
find_package(Git)
if(NOT GIT_FOUND)
  message(FATAL_ERROR "[VersionFromGit] Git not found")
endif(NOT GIT_FOUND)

# Git describe
execute_process(
  COMMAND "${GIT_EXECUTABLE}" describe --tags --dirty
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  RESULT_VARIABLE git_result
  OUTPUT_VARIABLE git_describe
  ERROR_VARIABLE git_error
  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE
)
if(NOT git_result EQUAL 0)
  message(WARNING "[VersionFromGit] Failed to execute Git: ${git_error}")
  set(git_describe "")
endif()

# Get Git tag
execute_process(
  COMMAND "${GIT_EXECUTABLE}" describe --tags --abbrev=0
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  RESULT_VARIABLE git_result
  OUTPUT_VARIABLE git_tag
  ERROR_VARIABLE git_error
  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE
)
if(NOT git_result EQUAL 0)
  message(WARNING "[VersionFromGit] Failed to execute Git: ${git_error}")
  set(git_tag "")
endif()

if(git_tag MATCHES "^v(0|[1-9][0-9]*)[.](0|[1-9][0-9]*)[.](0|[1-9][0-9]*)(-[.0-9A-Za-z-]+)?([+][.0-9A-Za-z-]+)?$")
  set(version_major "${CMAKE_MATCH_1}")
  set(version_minor "${CMAKE_MATCH_2}")
  set(version_patch "${CMAKE_MATCH_3}")
  set(identifiers "${CMAKE_MATCH_4}")
  set(metadata "${CMAKE_MATCH_5}")
else()
  message(WARNING "[VersionFromGit] Git tag isn't valid semantic version: [${git_tag}]")
  set(version_major "0")
  set(version_minor "0")
  set(version_patch "0")
  set(identifiers "")
  set(metadata "")
endif()

if("${git_tag}" STREQUAL "${git_describe}")
  set(git_at_a_tag ON)
endif()

if(NOT git_at_a_tag)
  # Extract the Git hash (if one exists)
  string(REGEX MATCH "g[0-9a-f]+$" git_hash "${git_describe}")
endif()

# Construct the version variables
set(version ${version_major}.${version_minor}.${version_patch})
set(semver ${version})

# Identifiers
if(identifiers MATCHES ".+")
  string(SUBSTRING "${identifiers}" 1 -1 identifiers)
  set(semver "${semver}-${identifiers}")
endif()

# Metadata TODO Split and join (add Git hash inbetween)
if(metadata MATCHES ".+")
  string(SUBSTRING "${metadata}" 1 -1 metadata)
  # Split
  string(REPLACE "." ";" metadata "${metadata}")
endif()

if(NOT git_at_a_tag)

  list(APPEND metadata "${git_hash}")

  # Timestamp
  if(DEFINED ARG_TIMESTAMP)
    string(
      TIMESTAMP
      timestamp "${ARG_TIMESTAMP}"
      UTC
    )
    list(APPEND metadata "${timestamp}")
  endif(DEFINED ARG_TIMESTAMP)

endif()

# Join
string(REPLACE ";" "." metadata "${metadata}")

if(metadata MATCHES ".+")
  set(semver "${semver}+${metadata}")
endif()

# Log the results
message(
  STATUS
    "Version: ${version}
     Git tag:     [${git_tag}]
     Git hash:    [${git_hash}]
     Decorated:   [${git_describe}]
     Identifiers: [${identifiers}]
     Metadata:    [${metadata}]
     SemVer:      [${semver}]"
)

# Set variables
set(GIT_TAG ${git_tag})
set(GIT_HASH ${git_hash})
set(GIT_DESCRIBE ${git_describe})
set(SEMVER ${semver})
set(VERSION ${version})
set(VERSION_MAJOR ${version_major})
set(VERSION_MINOR ${version_minor})
set(VERSION_PATCH ${version_patch})

string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M")
message(STATUS "Saving build timestamp: ${BUILD_TIME}")

# Multiply CMAKE_SIZEOF_VOID_P by 8 to get the bitness
math(EXPR BITNESS "${CMAKE_SIZEOF_VOID_P} * 8")
set(VERSION_LONG "${GIT_DESCRIBE} (${BITNESS} bit)")

configure_file("${SRC_DIR}/version.cpp.in" "${BIN_DIR}/version.cpp" @ONLY)
