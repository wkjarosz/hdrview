/** \file app.h
    \author Wojciech Jarosz
*/

#pragma once

#include "json.h"

/// A wrapper over HelloImGui's themes, with two additional custom themes (dark and light)
struct Theme
{
    static constexpr int DARK_THEME   = -1; // Dark theme
    static constexpr int LIGHT_THEME  = -2; // Light theme
    static constexpr int CUSTOM_THEME = -3; // Custom theme

    static const char *name(int t);
    const char        *name() const { return Theme::name(theme); }

    void set(int t);
    void load(json j);
    void save(json &j) const;

    bool operator==(int t) const { return theme == t; }

private:
    int theme = DARK_THEME;
};

// theme.h
inline bool operator==(int t, const Theme &obj) { return obj == t; }