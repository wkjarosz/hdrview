# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# - Find libjpeg-turbo
# Find the libjpeg-turbo compression library and includes
#
# TurboJpeg_INCLUDE_DIRS - where to find turbojpeg.h, etc.
# TurboJpeg_LIBRARIES - List of libraries when using TurboJpeg.
# TurboJpeg_FOUND - True if TurboJpeg found.

find_path(TurboJpeg_INCLUDE_DIRS NAMES turbojpeg.h)
find_library(TurboJpeg_LIBRARIES NAMES libturbojpeg turbojpeg)
mark_as_advanced(TurboJpeg_LIBRARIES TurboJpeg_INCLUDE_DIRS)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(TurboJpeg DEFAULT_MSG TurboJpeg_LIBRARIES TurboJpeg_INCLUDE_DIRS)

if (TurboJpeg_FOUND AND NOT (TARGET TurboJpeg::TurboJpeg))
  add_library(TurboJpeg::TurboJpeg UNKNOWN IMPORTED)
  set_target_properties(TurboJpeg::TurboJpeg
    PROPERTIES
      IMPORTED_LOCATION ${TurboJpeg_LIBRARIES}
      INTERFACE_INCLUDE_DIRECTORIES ${TurboJpeg_INCLUDE_DIRS}
  )
endif()