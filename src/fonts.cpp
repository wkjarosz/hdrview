#include "fonts.h"
#include "app.h"
#include "timer.h"

using namespace std;

void HDRViewApp::load_fonts()
{
    Timer timer;
    spdlog::info("Loading fonts...");
    auto load_font = [](const string &font_path, int size)
    {
        if (!HelloImGui::AssetExists(font_path))
            spdlog::critical("Cannot find the font asset '{}'!");

        return HelloImGui::LoadFont(font_path, (float)size);
    };

    auto append_icon_font = [](const string &path, float size, const vector<HelloImGui::ImWcharPair> &glyphRanges)
    {
        if (HelloImGui::AssetExists(path))
        {
            HelloImGui::FontLoadingParams iconFontParams;
            iconFontParams.mergeToLastFont       = true;
            iconFontParams.fontConfig.PixelSnapH = true;
            iconFontParams.useFullGlyphRange     = false;
            iconFontParams.glyphRanges           = glyphRanges;

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

    vector<HelloImGui::ImWcharPair> glyphRanges = {};
    {
        // load all icon glyphs
        // glyphRanges.push_back({ICON_MIN_MY, ICON_MAX_MY});

        // only load the icon glyphs we use
        ImVector<ImWchar>        ranges;
        ImFontGlyphRangesBuilder builder;

        builder.AddText(ICON_MY_OPEN_IMAGE);
        builder.AddText(ICON_MY_ABOUT);
        builder.AddText(ICON_MY_FIT_AXES);
        builder.AddText(ICON_MY_MANUAL_AXES);
        builder.AddText(ICON_MY_LIST_OL);
        builder.AddText(ICON_MY_VISIBILITY);
        builder.AddText(ICON_MY_KEY_CONTROL);
        builder.AddText(ICON_MY_KEY_COMMAND);
        builder.AddText(ICON_MY_KEY_OPTION);
        builder.AddText(ICON_MY_KEY_SHIFT);
        builder.AddText(ICON_MY_CHANNEL_GROUP);
        builder.AddText(ICON_MY_QUIT);
        builder.AddText(ICON_MY_COMMAND_PALETTE);
        builder.AddText(ICON_MY_TWEAK_THEME);
        builder.AddText(ICON_MY_SHOW_ALL_WINDOWS);
        builder.AddText(ICON_MY_HIDE_ALL_WINDOWS);
        builder.AddText(ICON_MY_RESTORE_LAYOUT);
        builder.AddText(ICON_MY_EXPOSURE);
        builder.AddText(ICON_MY_REDUCE_EXPOSURE);
        builder.AddText(ICON_MY_INCREASE_EXPOSURE);
        builder.AddText(ICON_MY_RESET_TONEMAPPING);
        builder.AddText(ICON_MY_NORMALIZE_EXPOSURE);
        builder.AddText(ICON_MY_DITHER);
        builder.AddText(ICON_MY_CLAMP_TO_LDR);
        builder.AddText(ICON_MY_SHOW_GRID);
        builder.AddText(ICON_MY_SAVE_AS);
        builder.AddText(ICON_MY_CLOSE);
        builder.AddText(ICON_MY_CLOSE_ALL);
        builder.AddText(ICON_MY_ZOOM_OUT);
        builder.AddText(ICON_MY_ZOOM_IN);
        builder.AddText(ICON_MY_ZOOM_100);
        builder.AddText(ICON_MY_FIT_TO_WINDOW);
        builder.AddText(ICON_MY_CENTER);
        builder.AddText(ICON_MY_FILTER);
        builder.AddText(ICON_MY_DELETE);
        builder.AddText(ICON_MY_IMAGE);
        builder.AddText(ICON_MY_IMAGES);
        builder.AddText(ICON_MY_REFERENCE_IMAGE);
        builder.AddText(ICON_MY_THEME);
        builder.AddText(ICON_MY_ARROW_UP);
        builder.AddText(ICON_MY_ARROW_DOWN);
        builder.AddText(ICON_MY_KEY_RETURN);
        builder.AddText(ICON_MY_KEY_ESC);
        builder.AddText(ICON_MY_LOG_LEVEL);
        builder.AddText(ICON_MY_LOG_LEVEL_TRACE);
        builder.AddText(ICON_MY_LOG_LEVEL_DEBUG);
        builder.AddText(ICON_MY_LOG_LEVEL_INFO);
        builder.AddText(ICON_MY_LOG_LEVEL_WARN);
        builder.AddText(ICON_MY_LOG_LEVEL_ERROR);
        builder.AddText(ICON_MY_LOG_LEVEL_CRITICAL);
        builder.AddText(ICON_MY_LOG_LEVEL_OFF);
        builder.AddText(ICON_MY_GREATER_EQUAL);
        builder.AddText(ICON_MY_TRASH_CAN);
        builder.AddText(ICON_MY_LOCK);
        builder.AddText(ICON_MY_LOCK_OPEN);
        builder.AddText(ICON_MY_TEXT_WRAP_ON);
        builder.AddText(ICON_MY_TEXT_WRAP_OFF);
        builder.AddText(ICON_MY_WIDEST);
        builder.AddText(ICON_MY_LINK);
        builder.AddText(ICON_MY_TOOLBAR);
        builder.AddText(ICON_MY_STATUSBAR);
        builder.AddText(ICON_MY_HIDE_GUI);
        builder.AddText(ICON_MY_FPS);
        builder.AddText(ICON_MY_DISPLAY_WINDOW);
        builder.AddText(ICON_MY_DATA_WINDOW);

        // Build the final result (ordered ranges with all the unique characters submitted)
        builder.BuildRanges(&ranges);
        // spdlog::info("Icon font ranges: {}", ranges.size());
        for (int i = 0; i < ranges.size() - 1; i += 2) glyphRanges.push_back({ranges[i], ranges[i + 1]});
    }

    // default font size gets icons
    for (auto font_size : {14, 10, 16, 18, 30})
    {
        m_fonts[{"sans regular", font_size}] = load_font("fonts/Roboto/Roboto-Regular.ttf", font_size);
        append_icon_font(FONT_ICON_FILE_NAME_MY, font_size, glyphRanges);

        m_fonts[{"sans bold", font_size}] = load_font("fonts/Roboto/Roboto-Bold.ttf", font_size);
        append_icon_font(FONT_ICON_FILE_NAME_MY, font_size, glyphRanges);

        m_fonts[{"mono regular", font_size}] = load_font("fonts/Roboto/RobotoMono-Regular.ttf", font_size);
        append_icon_font(FONT_ICON_FILE_NAME_MY, font_size, glyphRanges);

        m_fonts[{"mono bold", font_size}] = load_font("fonts/Roboto/RobotoMono-Bold.ttf", font_size);
        append_icon_font(FONT_ICON_FILE_NAME_MY, font_size, glyphRanges);
    }
    // spdlog::info("\ttook {} seconds.", (timer.elapsed() / 1000.f));
}