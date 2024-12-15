#include "fonts.h"
#include "app.h"

using namespace std;

void HDRViewApp::load_fonts()
{
    auto load_font = [](const string &font_path, int size)
    {
        if (!HelloImGui::AssetExists(font_path))
            spdlog::critical("Cannot find the font asset '{}'!");

        return HelloImGui::LoadFont(font_path, (float)size);
    };

    auto append_icon_font = [](const string &path, float size, ImWchar icon_min, ImWchar icon_max)
    {
        if (HelloImGui::AssetExists(path))
        {
            HelloImGui::FontLoadingParams iconFontParams;
            iconFontParams.mergeToLastFont   = true;
            iconFontParams.useFullGlyphRange = false;
            iconFontParams.glyphRanges.push_back({icon_min, icon_max});
            iconFontParams.fontConfig.PixelSnapH = true;

#if defined(FONT_AWESOME_6_ICONS)
            auto icon_font_size                        = 0.85f * size;
            iconFontParams.fontConfig.GlyphMinAdvanceX = iconFontParams.fontConfig.GlyphMaxAdvanceX =
                icon_font_size * HelloImGui::DpiFontLoadingFactor() * 1.25f;
            iconFontParams.fontConfig.GlyphOffset.x = icon_font_size * HelloImGui::DpiFontLoadingFactor() * 0.05f;
#elif defined(LUCIDE_ICONS)
            auto icon_font_size                     = size;
            iconFontParams.fontConfig.GlyphOffset.x = icon_font_size * HelloImGui::DpiFontLoadingFactor() * 0.03f;
            iconFontParams.fontConfig.GlyphOffset.y = icon_font_size * HelloImGui::DpiFontLoadingFactor() * 0.20f;
#elif defined(MATERIAL_SYMBOLS_ICONS)
            auto icon_font_size                     = 1.28571429f * size;
            iconFontParams.fontConfig.GlyphOffset.x = icon_font_size * HelloImGui::DpiFontLoadingFactor() * 0.01f;
            iconFontParams.fontConfig.GlyphOffset.y = icon_font_size * HelloImGui::DpiFontLoadingFactor() * 0.2f;
#elif defined(MATERIAL_DESIGN_ICONS)
            auto icon_font_size                     = size;
            iconFontParams.fontConfig.GlyphOffset.x = icon_font_size * HelloImGui::DpiFontLoadingFactor() * 0.01f;
            iconFontParams.fontConfig.GlyphOffset.y = icon_font_size * HelloImGui::DpiFontLoadingFactor() * 0.2f;
#elif defined(MATERIAL_DESIGN_ICONS_ICONS)
            auto icon_font_size = size;
            iconFontParams.fontConfig.GlyphOffset =
                icon_font_size * HelloImGui::DpiFontLoadingFactor() * float2{0.02f, 0.1f};

#endif
            HelloImGui::LoadFont(path, icon_font_size, iconFontParams);
        }
        else
            spdlog::critical("Cannot find the icon font '{}'", path);
    };

    // default font size gets icons
    for (auto font_size : {14, 10, 18})
    {
        m_fonts[{"sans regular", font_size}] = load_font("fonts/Roboto/Roboto-Regular.ttf", font_size);
        append_icon_font(FONT_ICON_FILE_NAME_MY, font_size, ICON_MIN_MY, ICON_MAX_MY);

        m_fonts[{"sans bold", font_size}] = load_font("fonts/Roboto/Roboto-Bold.ttf", font_size);
        append_icon_font(FONT_ICON_FILE_NAME_MY, font_size, ICON_MIN_MY, ICON_MAX_MY);

        m_fonts[{"mono regular", font_size}] = load_font("fonts/Roboto/RobotoMono-Regular.ttf", font_size);
        append_icon_font(FONT_ICON_FILE_NAME_MY, font_size, ICON_MIN_MY, ICON_MAX_MY);

        m_fonts[{"mono bold", font_size}] = load_font("fonts/Roboto/RobotoMono-Bold.ttf", font_size);
        append_icon_font(FONT_ICON_FILE_NAME_MY, font_size, ICON_MIN_MY, ICON_MAX_MY);
    }

    for (auto font_size : {16, 30})
    {
        m_fonts[{"sans regular", font_size}] = load_font("fonts/Roboto/Roboto-Regular.ttf", font_size);

        m_fonts[{"sans bold", font_size}] = load_font("fonts/Roboto/Roboto-Bold.ttf", font_size);

        m_fonts[{"mono regular", font_size}] = load_font("fonts/Roboto/RobotoMono-Regular.ttf", font_size);

        m_fonts[{"mono bold", font_size}] = load_font("fonts/Roboto/RobotoMono-Bold.ttf", font_size);
    }
}