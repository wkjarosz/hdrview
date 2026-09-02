//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <optional>
#include <string>
#include <string_view>

int         version_major();
int         version_minor();
int         version_patch();
int         version_combined();
std::string version();
std::string git_hash();
std::string git_describe();
std::string build_timestamp();
std::string architecture();
std::string backend();

struct SemVer
{
    int major = 0, minor = 0, patch = 0;

    // same combining formula as version_combined(), which is defined in terms of this
    int combined() const { return patch + 100 * (minor + 100 * major); }
};

/// Parses a leading "MAJOR.MINOR.PATCH" out of `s` (an optional leading 'v' is skipped; anything after the
/// patch number, e.g. "-6-gbd77763-dirty", is ignored). Returns std::nullopt if `s` doesn't start with
/// three dot-separated non-negative integers.
std::optional<SemVer> parse_version(std::string_view s);