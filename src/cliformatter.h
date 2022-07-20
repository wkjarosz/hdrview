//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once
#include <CLI/CLI.hpp>
#include <spdlog/fmt/bundled/ranges.h>
#include <spdlog/fmt/bundled/color.h>

class ColorFormatter : public CLI::Formatter {
public:
    std::string make_option_name(const CLI::Option * opt, bool b) const override
    {
        return fmt::format(fmt::emphasis::bold | fg(fmt::color::cornflower_blue), "{}", CLI::Formatter::make_option_name(opt, b));
    }
    std::string make_option_opts(const CLI::Option * opt) const override
    {
        return fmt::format(fg(fmt::color::light_sea_green), "{}", CLI::Formatter::make_option_opts(opt));
    }
    std::string make_option_desc(const CLI::Option * opt) const override
    {
        return fmt::format(fg(fmt::color::dim_gray), "{}", CLI::Formatter::make_option_desc(opt));
    }
};