//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once
#include <CLI/CLI.hpp>

//
// include bundled or external copy of fmtlib's color support
//
#include <spdlog/tweakme.h>

#if !defined(SPDLOG_USE_STD_FORMAT)
#if !defined(SPDLOG_FMT_EXTERNAL)
#ifdef SPDLOG_HEADER_ONLY
#ifndef FMT_HEADER_ONLY
#define FMT_HEADER_ONLY
#endif
#endif
#include <spdlog/fmt/bundled/color.h>
#else
#include <fmt/color.h>
#endif
#endif

class ColorFormatter : public CLI::Formatter
{
public:
    std::string make_option_name(const CLI::Option *opt, bool is_positional) const override
    {
        return fmt::format(fmt::emphasis::bold | fg(fmt::color::cornflower_blue), "{}",
                           CLI::Formatter::make_option_name(opt, is_positional));
    }
    std::string make_option_opts(const CLI::Option *opt) const override
    {
        return fmt::format(fg(fmt::color::light_sea_green), "{}", CLI::Formatter::make_option_opts(opt));
    }
    std::string make_option_desc(const CLI::Option *opt) const override
    {
        return fmt::format(fg(fmt::color::dim_gray), "{}", CLI::Formatter::make_option_desc(opt));
    }
};