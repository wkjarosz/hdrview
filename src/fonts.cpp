#include "fonts.h"
#include "app.h"
#include "hello_imgui/hello_imgui_assets.h"
#include "hello_imgui/hello_imgui_font.h"
#include "timer.h"

using namespace std;
using namespace HelloImGui;

ImFont *HDRViewApp::font(const string &name) const
{
    if (name == "sans regular")
        return m_sans_regular;
    else if (name == "sans bold")
        return m_sans_bold;
    else if (name == "mono regular")
        return m_mono_regular;
    else if (name == "mono bold")
        return m_mono_bold;
    else
        throw runtime_error(fmt::format("Font with name '{}' was not loaded.", name));
}

void HDRViewApp::load_fonts()
{
    Timer timer;
    spdlog::info("Loading fonts...");
    auto load_font = [](const string &font_path, float size = 14.f)
    {
        if (!AssetExists(font_path))
            spdlog::critical("Cannot find the font asset '{}'!");

        LoadFont(font_path, size);

        // merge in icon font
        if (!AssetExists(FONT_ICON_FILE_NAME_MY))
            spdlog::critical("Cannot find the icon font '{}'", FONT_ICON_FILE_NAME_MY);

        FontLoadingParams iconFontParams;
        iconFontParams.mergeToLastFont       = true;
        iconFontParams.fontConfig.PixelSnapH = true;

#if defined(HDRVIEW_ICONSET_FA6)
        auto icon_font_size                        = 0.85f * size;
        iconFontParams.fontConfig.GlyphMinAdvanceX = iconFontParams.fontConfig.GlyphMaxAdvanceX =
            icon_font_size * DpiFontLoadingFactor() * 1.25f;
        iconFontParams.fontConfig.GlyphOffset.x = icon_font_size * DpiFontLoadingFactor() * 0.05f;
#elif defined(HDRVIEW_ICONSET_LC)
        auto icon_font_size                     = size;
        iconFontParams.fontConfig.GlyphOffset.x = icon_font_size * DpiFontLoadingFactor() * 0.03f;
        iconFontParams.fontConfig.GlyphOffset.y = icon_font_size * DpiFontLoadingFactor() * 0.20f;
#elif defined(HDRVIEW_ICONSET_MS)
        auto icon_font_size                     = 1.28571429f * size;
        iconFontParams.fontConfig.GlyphOffset.x = icon_font_size * DpiFontLoadingFactor() * 0.01f;
        iconFontParams.fontConfig.GlyphOffset.y = icon_font_size * DpiFontLoadingFactor() * 0.2f;
#elif defined(HDRVIEW_ICONSET_MD)
        auto icon_font_size                     = size;
        iconFontParams.fontConfig.GlyphOffset.x = icon_font_size * DpiFontLoadingFactor() * 0.01f;
        iconFontParams.fontConfig.GlyphOffset.y = icon_font_size * DpiFontLoadingFactor() * 0.2f;
#elif defined(HDRVIEW_ICONSET_MDI)
        auto icon_font_size                   = size;
        iconFontParams.fontConfig.GlyphOffset = icon_font_size * DpiFontLoadingFactor() * float2{0.02f, 0.1f};

#endif
        return LoadFont(FONT_ICON_FILE_NAME_MY, icon_font_size, iconFontParams);
    };

    m_sans_regular = load_font("fonts/Roboto/Roboto-Regular.ttf");
    m_sans_bold    = load_font("fonts/Roboto/Roboto-Bold.ttf");
    m_mono_regular = load_font("fonts/Roboto/RobotoMono-Regular.ttf");
    m_mono_bold    = load_font("fonts/Roboto/RobotoMono-Bold.ttf");
    spdlog::info("done loading fonts.");
}