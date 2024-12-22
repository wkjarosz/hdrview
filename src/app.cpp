/** \file app.cpp
    \author Wojciech Jarosz
*/

#include "app.h"

#include "hello_imgui/dpi_aware.h"
#include "hello_imgui/hello_imgui.h"
#include "imcmd_command_palette.h"
#include "imgui_ext.h"
#include "imgui_internal.h"
#include "implot.h"
#include "implot_internal.h"

#include "fonts.h"

#include "opengl_check.h"

#include "colorspace.h"

#include "json.h"
#include "sviewstream.h"
#include "texture.h"
#include "timer.h"
#include "version.h"

#include <ImfThreading.h>

#include <spdlog/spdlog.h>

#include <cmath>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <utility>

#ifdef __EMSCRIPTEN__
#include "emscripten_browser_file.h"
#include <string_view>
using std::string_view;
#else
#include "portable-file-dialogs.h"
#endif

#ifdef HELLOIMGUI_USE_SDL
#include <SDL.h>
#endif

#ifdef HELLOIMGUI_USE_GLFW
#include <GLFW/glfw3.h>

#ifdef __APPLE__
// on macOS, we need to include this to get the NS api for opening files
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#endif
#endif

using namespace linalg::ostream_overloads;

using std::to_string;
using std::unique_ptr;
using json = nlohmann::json;

#ifdef __EMSCRIPTEN__
EM_JS(int, screen_width, (), { return screen.width; });
EM_JS(int, screen_height, (), { return screen.height; });
EM_JS(int, window_width, (), { return window.innerWidth; });
EM_JS(int, window_height, (), { return window.innerHeight; });
EM_JS(bool, isAppleDevice, (), {
    const userAgent = navigator.userAgent;
    return (userAgent.includes("Macintosh") || userAgent.includes("iPad") || userAgent.includes("iPhone") ||
            userAgent.includes("iPod"));
});
#endif

bool hostIsApple()
{
#if defined(__EMSCRIPTEN__)
    return isAppleDevice();
#elif defined(__APPLE__)
    return true;
#else
    return false;
#endif
}

static std::mt19937     g_rand(53);
static constexpr float  MIN_ZOOM               = 0.01f;
static constexpr float  MAX_ZOOM               = 512.f;
static constexpr size_t g_max_recent           = 15;
static bool             g_show_help            = false;
static bool             g_show_command_palette = false;
static bool             g_show_tweak_window    = false;
static bool             g_show_bg_color_picker = false;
#define g_blank_icon ""

void MenuItem(const Action &a)
{
    if (a.needs_menu)
    {
        if (ImGui::BeginMenuEx(a.name.c_str(), a.icon.c_str(), a.enabled()))
        {
            a.callback();
            ImGui::EndMenu();
        }
    }
    else
    {
        if (ImGui::MenuItemEx(a.name, a.icon, ImGui::GetKeyChordNameTranslated(a.chord), a.p_selected, a.enabled()))
            a.callback();
        if (!a.tooltip.empty())
            ImGui::WrappedTooltip(a.tooltip.c_str());
    }
}

void IconButton(const Action &a)
{
    if (ImGui::IconButton(fmt::format("{}##{}", a.icon, a.name).c_str()))
        a.callback();
    if (a.chord)
        ImGui::WrappedTooltip(fmt::format("{} ({})", a.name, ImGui::GetKeyChordNameTranslated(a.chord)).c_str());
    else
        ImGui::WrappedTooltip(fmt::format("{}", a.name, ImGui::GetKeyChordNameTranslated(a.chord)).c_str());
}

void Checkbox(const Action &a)
{
    ImGui::Checkbox(a.name.c_str(), a.p_selected);
    if (!a.tooltip.empty() || a.chord)
    {
        string parenthesized_chord = a.chord ? fmt::format("({})", ImGui::GetKeyChordNameTranslated(a.chord)) : "";
        string tooltip             = fmt::format("{}{}", a.tooltip, parenthesized_chord);
        ImGui::WrappedTooltip(tooltip.c_str());
    }
}

static HDRViewApp *g_hdrview = nullptr;

void init_hdrview(std::optional<float> exposure, std::optional<float> gamma, std::optional<bool> dither,
                  std::optional<bool> force_sdr, const vector<string> &in_files)
{
    if (g_hdrview)
    {
        spdlog::critical("HDRView already created!");
        exit(EXIT_FAILURE);
    }

    if (in_files.empty())
        g_show_help = true;

    spdlog::info("Overriding exposure: {}", exposure.has_value());
    spdlog::info("Overriding gamma: {}", gamma.has_value());
    spdlog::info("Overriding dither: {}", dither.has_value());
    spdlog::info("Forcing SDR: {}", force_sdr.has_value());

    g_hdrview = new HDRViewApp(exposure, gamma, dither, force_sdr, in_files);
}

HDRViewApp *hdrview() { return g_hdrview; }

HDRViewApp::HDRViewApp(std::optional<float> force_exposure, std::optional<float> force_gamma,
                       std::optional<bool> force_dither, std::optional<bool> force_sdr, vector<string> in_files)
{
#if defined(__EMSCRIPTEN__) && !defined(HELLOIMGUI_EMSCRIPTEN_PTHREAD)
    // if threading is disabled, create no threads
    unsigned threads = 0;
#elif defined(HELLOIMGUI_EMSCRIPTEN_PTHREAD)
    // if threading is enabled in emscripten, then use just 1 thread
    unsigned threads = 1;
#else
    unsigned threads = std::thread::hardware_concurrency();
#endif

    spdlog::debug("Setting global OpenEXR thread count to {}", threads);
    Imf::setGlobalThreadCount(threads);
    spdlog::debug("OpenEXR reports global thread count as {}", Imf::globalThreadCount());

#if defined(__APPLE__)
    // if there is a screen with a non-retina resolution connected to an otherwise retina mac, the fonts may look
    // blurry. Here we force that macs always use the 2X retina scale factor for fonts. Produces crisp fonts on the
    // retina screen, at the cost of more jagged fonts on screen set to a non-retina resolution.
    m_params.dpiAwareParams.dpiWindowSizeFactor = 1.f;
    m_params.dpiAwareParams.fontRenderingScale  = 0.5f;
#endif

    bool use_edr = HelloImGui::hasEdrSupport() && !force_sdr;

    if (force_sdr)
        spdlog::info("Forcing SDR display mode (display {} support EDR mode)",
                     HelloImGui::hasEdrSupport() ? "would otherwise" : "would not anyway");

    m_params.rendererBackendOptions.requestFloatBuffer = use_edr;
    spdlog::info("Launching GUI with {} display support.", use_edr ? "EDR" : "SDR");
    spdlog::info("Creating a {} framebuffer.", m_params.rendererBackendOptions.requestFloatBuffer
                                                   ? "floating-point precision"
                                                   : "standard precision");

    // set up HelloImGui parameters
    m_params.appWindowParams.windowGeometry.size     = {1200, 800};
    m_params.appWindowParams.windowTitle             = "HDRView";
    m_params.appWindowParams.restorePreviousGeometry = false;

    // Setting this to true allows multiple viewports where you can drag windows outside out the main window in order to
    // put their content into new native windows
    m_params.imGuiWindowParams.enableViewports        = false;
    m_params.imGuiWindowParams.defaultImGuiWindowType = HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;
    // m_params.imGuiWindowParams.backgroundColor        = float4{0.15f, 0.15f, 0.15f, 1.f};

    // Load additional font
    m_params.callbacks.LoadAdditionalFonts = [this]() { load_fonts(); };

    //
    // Menu bar
    //
    // Here, we fully customize the menu bar:
    // by setting `showMenuBar` to true, and `showMenu_App` and `showMenu_View` to false,
    // HelloImGui will display an empty menu bar, which we can fill with our own menu items via the callback `ShowMenus`
    m_params.imGuiWindowParams.showMenuBar   = true;
    m_params.imGuiWindowParams.showMenu_App  = false;
    m_params.imGuiWindowParams.showMenu_View = false;
    // Inside `ShowMenus`, we can call `HelloImGui::ShowViewMenu` and `HelloImGui::ShowAppMenu` if desired
    m_params.callbacks.ShowMenus = [this]() { draw_menus(); };

    //
    // Toolbars
    //
    // ImGui::GetFrameHeight() * 1.4f
    m_top_toolbar_options.sizeEm = 2.2f;
    // HelloImGui::PixelSizeToEm(ImGui::GetFrameHeight());
    m_top_toolbar_options.WindowPaddingEm = ImVec2(0.7f, 0.35f);
    m_params.callbacks.AddEdgeToolbar(
        HelloImGui::EdgeToolbarType::Top, [this]() { draw_top_toolbar(); }, m_top_toolbar_options);

    //
    // Status bar
    //
    // We use the default status bar of Hello ImGui
    m_params.imGuiWindowParams.showStatusBar  = true;
    m_params.imGuiWindowParams.showStatus_Fps = false;
    m_params.callbacks.ShowStatus             = [this]() { draw_status_bar(); };

    //
    // Dockable windows
    //
    // the histogram window
    HelloImGui::DockableWindow histogram_window;
    histogram_window.label             = "Histogram";
    histogram_window.dockSpaceName     = "HistogramSpace";
    histogram_window.isVisible         = true;
    histogram_window.rememberIsVisible = true;
    histogram_window.GuiFunction       = [this] { draw_histogram_window(); };

    // the file window
    HelloImGui::DockableWindow file_window;
    file_window.label             = "Images";
    file_window.dockSpaceName     = "ImagesSpace";
    file_window.isVisible         = true;
    file_window.rememberIsVisible = true;
    file_window.GuiFunction       = [this] { draw_file_window(); };

    // the info window
    HelloImGui::DockableWindow info_window;
    info_window.label             = "Info";
    info_window.dockSpaceName     = "ImagesSpace";
    info_window.isVisible         = true;
    info_window.rememberIsVisible = true;
    info_window.GuiFunction       = [this] { draw_info_window(); };

    // the channels window
    HelloImGui::DockableWindow channel_window;
    channel_window.label             = "Channels";
    channel_window.dockSpaceName     = "ChannelsSpace";
    channel_window.isVisible         = true;
    channel_window.rememberIsVisible = true;
    channel_window.GuiFunction       = [this] { draw_channel_window(); };

    // the window showing the spdlog messages
    HelloImGui::DockableWindow log_window;
    log_window.label             = "Log";
    log_window.dockSpaceName     = "LogSpace";
    log_window.isVisible         = false;
    log_window.rememberIsVisible = true;
    log_window.GuiFunction       = [this] { ImGui::GlobalSpdLogWindow().draw(font("mono regular", 14)); };

    // docking layouts
    m_params.dockingParams.layoutName      = "Standard";
    m_params.dockingParams.dockableWindows = {histogram_window, file_window, info_window, channel_window, log_window};
    m_params.dockingParams.dockingSplits   = {
        HelloImGui::DockingSplit{"MainDockSpace", "HistogramSpace", ImGuiDir_Left, 0.2f},
        HelloImGui::DockingSplit{"HistogramSpace", "ImagesSpace", ImGuiDir_Down, 0.75f},
        HelloImGui::DockingSplit{"ImagesSpace", "ChannelsSpace", ImGuiDir_Down, 0.25f},
        HelloImGui::DockingSplit{"MainDockSpace", "LogSpace", ImGuiDir_Down, 0.25f}};

#if defined(HELLOIMGUI_USE_GLFW)
    m_params.callbacks.PostInit_AddPlatformBackendCallbacks = [this]
    {
        spdlog::trace("Registering glfw drop callback");
        // spdlog::trace("m_params.backendPointers.glfwWindow: {}", m_params.backendPointers.glfwWindow);
        glfwSetDropCallback((GLFWwindow *)m_params.backendPointers.glfwWindow,
                            [](GLFWwindow *w, int count, const char **filenames)
                            {
                                vector<string> arg(count);
                                for (int i = 0; i < count; ++i) arg[i] = filenames[i];
                                hdrview()->load_images(arg);
                            });
#ifdef __APPLE__
        // On macOS, the mechanism for opening an application passes filenames
        // through the NS api rather than CLI arguments, which means we need
        // special handling of these through GLFW.
        // There are two components to this special handling:
        // (both of which need to happen here instead of HDRViewApp() because GLFW needs to have been initialized first)

        // 1) Check if any filenames were passed via the NS api when the first instance of HDRView is launched.
        const char *const *opened_files = glfwGetOpenedFilenames();
        if (opened_files)
        {
            spdlog::trace("Opening files passed in through the NS api...");
            vector<string> args;
            for (auto p = opened_files; *p; ++p) { args.emplace_back(string(*p)); }
            load_images(args);
        }

        // 2) Register a callback on the running instance of HDRView for when the user:
        //    a) drags a file onto the HDRView app icon in the dock, and/or
        //    b) launches HDRView with files (either from the command line or Finder) when another instance is already
        //       running
        glfwSetOpenedFilenamesCallback(
            [](const char *image_file)
            {
                spdlog::trace("Receiving an app drag-drop event through the NS api for file '{}'", image_file);
                hdrview()->load_images({string(image_file)});
            });
#endif
    };
#endif

    //
    // Load user settings at `PostInit` and save them at `BeforeExit`
    //
    m_params.iniFolderType      = HelloImGui::IniFolderType::AppUserConfigFolder;
    m_params.iniFilename        = "HDRView/settings.ini";
    m_params.callbacks.PostInit = [this, force_exposure, force_gamma, force_dither]
    {
        load_settings();
        if (force_exposure.has_value())
            m_exposure_live = m_exposure = *force_exposure;
        if (force_gamma.has_value())
            m_gamma_live = m_gamma = *force_gamma;
        else
            m_sRGB = true;
        if (force_dither.has_value())
            m_dither = *force_dither;

        setup_rendering();
    };
    m_params.callbacks.BeforeExit = [this] { save_settings(); };

    // Change style
    m_params.callbacks.SetupImGuiStyle = []()
    {
        // Apply default style
        HelloImGui::ImGuiDefaultSettings::SetupDefaultImGuiStyle();
        // Create a tweaked theme
        ImGuiTheme::ImGuiTweakedTheme tweakedTheme;
        tweakedTheme.Theme           = ImGuiTheme::ImGuiTheme_DarculaDarker;
        tweakedTheme.Tweaks.Rounding = 4.0f;

        tweakedTheme.Tweaks.ValueMultiplierBg      = 0.5;
        tweakedTheme.Tweaks.ValueMultiplierFrameBg = 0.5;

        // Apply the tweaked theme
        ImGuiTheme::ApplyTweakedTheme(tweakedTheme);

        // make things like radio buttons look nice and round
        ImGui::GetStyle().CircleTessellationMaxError = 0.1f;

        // Then apply further modifications to ImGui style
        ImGui::GetStyle().DisabledAlpha            = 0.5;
        ImGui::GetStyle().WindowRounding           = 0;
        ImGui::GetStyle().WindowBorderSize         = 1.0;
        ImGui::GetStyle().WindowMenuButtonPosition = ImGuiDir_Right;
        ImGui::GetStyle().WindowPadding            = ImVec2(8, 8);
        ImGui::GetStyle().FrameRounding            = 3;
        ImGui::GetStyle().PopupRounding            = 4;
        ImGui::GetStyle().GrabRounding             = 2;
        ImGui::GetStyle().ScrollbarRounding        = 4;
        ImGui::GetStyle().TabRounding              = 4;
        ImGui::GetStyle().WindowRounding           = 6;
        ImGui::GetStyle().DockingSeparatorSize     = 2;
        ImGui::GetStyle().SeparatorTextBorderSize  = 1;
        ImGui::GetStyle().TabBarBorderSize         = 2;
        ImGui::GetStyle().FramePadding             = ImVec2(4, 4);

        ImVec4 *colors                             = ImGui::GetStyle().Colors;
        colors[ImGuiCol_Text]                      = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_TextDisabled]              = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_WindowBg]                  = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_ChildBg]                   = ImVec4(0.04f, 0.04f, 0.04f, 0.20f);
        colors[ImGuiCol_PopupBg]                   = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        colors[ImGuiCol_Border]                    = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
        colors[ImGuiCol_BorderShadow]              = ImVec4(1.00f, 1.00f, 1.00f, 0.16f);
        colors[ImGuiCol_FrameBg]                   = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]            = ImVec4(1.00f, 1.00f, 1.00f, 0.13f);
        colors[ImGuiCol_FrameBgActive]             = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
        colors[ImGuiCol_TitleBg]                   = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_TitleBgActive]             = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]          = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
        colors[ImGuiCol_MenuBarBg]                 = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]               = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]             = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]      = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]       = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
        colors[ImGuiCol_CheckMark]                 = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrab]                = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]          = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
        colors[ImGuiCol_Button]                    = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
        colors[ImGuiCol_ButtonHovered]             = ImVec4(1.00f, 1.00f, 1.00f, 0.13f);
        colors[ImGuiCol_ButtonActive]              = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
        colors[ImGuiCol_Header]                    = ImVec4(0.18f, 0.34f, 0.59f, 1.00f);
        colors[ImGuiCol_HeaderHovered]             = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
        colors[ImGuiCol_HeaderActive]              = ImVec4(0.29f, 0.58f, 1.00f, 1.00f);
        colors[ImGuiCol_Separator]                 = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
        colors[ImGuiCol_SeparatorHovered]          = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
        colors[ImGuiCol_SeparatorActive]           = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
        colors[ImGuiCol_ResizeGrip]                = ImVec4(1.00f, 1.00f, 1.00f, 0.25f);
        colors[ImGuiCol_ResizeGripHovered]         = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
        colors[ImGuiCol_ResizeGripActive]          = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
        colors[ImGuiCol_TabHovered]                = ImVec4(0.30f, 0.58f, 1.00f, 1.00f);
        colors[ImGuiCol_Tab]                       = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
        colors[ImGuiCol_TabSelected]               = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
        colors[ImGuiCol_TabSelectedOverline]       = ImVec4(0.30f, 0.58f, 1.00f, 0.00f);
        colors[ImGuiCol_TabDimmed]                 = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
        colors[ImGuiCol_TabDimmedSelected]         = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
        colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.30f, 0.58f, 1.00f, 0.00f);
        colors[ImGuiCol_DockingPreview]            = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
        colors[ImGuiCol_DockingEmptyBg]            = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_PlotLines]                 = ImVec4(0.47f, 0.47f, 0.47f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered]          = ImVec4(1.00f, 0.39f, 0.00f, 1.00f);
        colors[ImGuiCol_PlotHistogram]             = ImVec4(0.59f, 0.59f, 0.59f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered]      = ImVec4(1.00f, 0.39f, 0.00f, 1.00f);
        colors[ImGuiCol_TableHeaderBg]             = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]         = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
        colors[ImGuiCol_TableBorderLight]          = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
        colors[ImGuiCol_TableRowBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt]             = ImVec4(1.00f, 1.00f, 1.00f, 0.08f);
        colors[ImGuiCol_TextLink]                  = ImVec4(0.26f, 0.50f, 0.96f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]            = ImVec4(1.00f, 1.00f, 1.00f, 0.16f);
        colors[ImGuiCol_DragDropTarget]            = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
        colors[ImGuiCol_NavCursor]                 = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight]     = ImVec4(0.24f, 0.47f, 0.81f, 1.00f);
        colors[ImGuiCol_NavWindowingDimBg]         = ImVec4(0.00f, 0.00f, 0.00f, 0.59f);
        colors[ImGuiCol_ModalWindowDimBg]          = ImVec4(0.00f, 0.00f, 0.00f, 0.59f);
    };

    m_params.callbacks.ShowGui = [this]()
    {
        add_pending_images();

        draw_about_dialog();

        draw_command_palette();

        // popup version of the below; commented out because it doesn't allow right-clicking to change the color picker
        // type
        //
        // if (g_show_bg_color_picker)
        //     ImGui::OpenPopup("Background color");
        // g_show_bg_color_picker = false;
        // ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, 5.f * HelloImGui::EmSize()),
        //                         ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
        // ImGui::SetNextWindowFocus();
        // if (ImGui::BeginPopup("Background color", ImGuiWindowFlags_NoSavedSettings |
        // ImGuiWindowFlags_AlwaysAutoResize))
        // {
        //     ImGui::TextUnformatted("Choose custom background color");
        //     ImGui::ColorPicker3("##Custom background color", (float *)&m_bg_color,
        //                         ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
        //     ImGui::EndPopup();
        // }

        // window version of the above
        if (g_show_bg_color_picker)
        {
            // Center window horizontally, align near top vertically
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, 5.f * HelloImGui::EmSize()),
                                    ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
            if (ImGui::Begin("Choose custom background color", &g_show_bg_color_picker,
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoDocking))
            {
                static float4 previous_bg_color = m_bg_color;
                if (ImGui::IsWindowAppearing())
                    previous_bg_color = m_bg_color;
                ImGui::ColorPicker4("##Custom background color", (float *)&m_bg_color,
                                    ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoAlpha,
                                    (float *)&previous_bg_color);

                ImGui::Dummy(HelloImGui::EmToVec2(1.f, 0.5f));
                if (ImGui::Button("OK", HelloImGui::EmToVec2(5.f, 0.f)) || ImGui::Shortcut(ImGuiKey_Enter))
                    g_show_bg_color_picker = false;
                ImGui::SameLine();
                if (ImGui::Button("Cancel", HelloImGui::EmToVec2(5.f, 0.f)) || ImGui::Shortcut(ImGuiKey_Escape))
                {
                    m_bg_color             = previous_bg_color;
                    g_show_bg_color_picker = false;
                }
            }
            ImGui::End();
        }

        if (g_show_tweak_window)
        {
            auto &tweakedTheme = HelloImGui::GetRunnerParams()->imGuiWindowParams.tweakedTheme;
            ImGui::SetNextWindowSize(HelloImGui::EmToVec2(20.f, 46.f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Theme Tweaks", &g_show_tweak_window))
            {
                if (ImGuiTheme::ShowThemeTweakGui(&tweakedTheme))
                    ApplyTweakedTheme(tweakedTheme);
            }
            ImGui::End();
        }
    };
    m_params.callbacks.CustomBackground        = [this]() { draw_background(); };
    m_params.callbacks.AnyBackendEventCallback = [this](void *event) { return process_event(event); };

    //
    // Actions and command palette
    //
    {
        const auto always_enabled = []() { return true; };
        auto       add_action     = [this](const Action &a)
        {
            m_action_map[a.name] = m_actions.size();
            m_actions.push_back(a);
        };
        add_action({"Open image...", ICON_MY_OPEN_IMAGE, ImGuiMod_Ctrl | ImGuiKey_O, 0, [this]() { open_image(); }});

        add_action({"About HDRView", ICON_MY_ABOUT, ImGuiKey_H, 0, []() {}, always_enabled, false, &g_show_help});
        add_action({"Quit", ICON_MY_QUIT, ImGuiMod_Ctrl | ImGuiKey_Q, 0, [this]() { m_params.appShallExit = true; }});

        add_action({"Command palette...", ICON_MY_COMMAND_PALETTE, ImGuiKey_ModCtrl | ImGuiKey_ModShift | ImGuiKey_P, 0,
                    []() {}, always_enabled, false, &g_show_command_palette});

        add_action(
            {"Theme tweak window", ICON_MY_TWEAK_THEME, 0, 0, []() {}, always_enabled, false, &g_show_tweak_window});

        static bool toolbar_on = m_params.callbacks.edgesToolbars.find(HelloImGui::EdgeToolbarType::Top) !=
                                 m_params.callbacks.edgesToolbars.end();
        add_action({"Show top toolbar", ICON_MY_TOOLBAR, 0, 0,
                    [this]()
                    {
                        if (!toolbar_on)
                            m_params.callbacks.edgesToolbars.erase(HelloImGui::EdgeToolbarType::Top);
                        else
                            m_params.callbacks.AddEdgeToolbar(
                                HelloImGui::EdgeToolbarType::Top, [this]() { draw_top_toolbar(); },
                                m_top_toolbar_options);
                    },
                    always_enabled, false, &toolbar_on});
        add_action({"Show menu bar", ICON_MY_HIDE_ALL_WINDOWS, 0, 0, []() {}, always_enabled, false,
                    &m_params.imGuiWindowParams.showMenuBar});
        add_action({"Show status bar", ICON_MY_STATUSBAR, 0, 0, []() {}, always_enabled, false,
                    &m_params.imGuiWindowParams.showStatusBar});
        add_action({"Show FPS in status bar", ICON_MS_SPEED, 0, 0, []() {}, always_enabled, false,
                    &m_params.imGuiWindowParams.showStatus_Fps});
        add_action(
            {"Enable idling", g_blank_icon, 0, 0, []() {}, always_enabled, false, &m_params.fpsIdling.enableIdling});

        auto any_window_hidden = [this]()
        {
            for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                if (dockableWindow.canBeClosed && dockableWindow.includeInViewMenu && !dockableWindow.isVisible)
                    return true;
            return false;
        };

        add_action({"Show all windows", ICON_MY_SHOW_ALL_WINDOWS, ImGuiKey_Tab, 0,
                    [this]()
                    {
                        for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                            if (dockableWindow.canBeClosed && dockableWindow.includeInViewMenu)
                                dockableWindow.isVisible = true;
                    },
                    any_window_hidden});

        add_action({"Hide all windows", ICON_MY_HIDE_ALL_WINDOWS, ImGuiKey_Tab, 0,
                    [this]()
                    {
                        for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                            if (dockableWindow.canBeClosed && dockableWindow.includeInViewMenu)
                                dockableWindow.isVisible = false;
                    },
                    [any_window_hidden]() { return !any_window_hidden(); }});

        add_action({"Show entire GUI", ICON_MY_SHOW_ALL_WINDOWS, ImGuiMod_Shift | ImGuiKey_Tab, 0,
                    [this]()
                    {
                        for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                            if (dockableWindow.canBeClosed && dockableWindow.includeInViewMenu)
                                dockableWindow.isVisible = true;
                        m_params.imGuiWindowParams.showMenuBar   = true;
                        m_params.imGuiWindowParams.showStatusBar = true;
                        m_params.callbacks.AddEdgeToolbar(
                            HelloImGui::EdgeToolbarType::Top, [this]() { draw_top_toolbar(); }, m_top_toolbar_options);
                    },
                    [this, any_window_hidden]()
                    {
                        return any_window_hidden() || !m_params.imGuiWindowParams.showMenuBar ||
                               !m_params.imGuiWindowParams.showStatusBar || !toolbar_on;
                    }});

        add_action({"Hide entire GUI", ICON_MS_CHECK_BOX_OUTLINE_BLANK, ImGuiMod_Shift | ImGuiKey_Tab, 0,
                    [this]()
                    {
                        for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                            if (dockableWindow.canBeClosed && dockableWindow.includeInViewMenu)
                                dockableWindow.isVisible = false;
                        m_params.imGuiWindowParams.showMenuBar   = false;
                        m_params.imGuiWindowParams.showStatusBar = false;
                        m_params.callbacks.edgesToolbars.erase(HelloImGui::EdgeToolbarType::Top);
                    },
                    [this, any_window_hidden]()
                    {
                        return !any_window_hidden() || m_params.imGuiWindowParams.showMenuBar ||
                               m_params.imGuiWindowParams.showStatusBar || toolbar_on;
                    }});

        add_action({"Restore default layout", ICON_MY_RESTORE_LAYOUT, 0, 0,
                    [this]() { m_params.dockingParams.layoutReset = true; },
                    [this]() { return !m_params.dockingParams.dockableWindows.empty(); }});

        for (size_t i = 0; i < m_params.dockingParams.dockableWindows.size(); ++i)
        {
            HelloImGui::DockableWindow &w = m_params.dockingParams.dockableWindows[i];
            add_action({fmt::format("Show {} window", w.label).c_str(), g_blank_icon, 0, 0, []() {},
                        [&w]() { return w.canBeClosed; }, false, &w.isVisible});
        }

        add_action({"Decrease exposure", ICON_MY_REDUCE_EXPOSURE, ImGuiKey_E, ImGuiInputFlags_Repeat,
                    [this]() { m_exposure_live = m_exposure -= 0.25f; }});
        add_action({"Increase exposure", ICON_MY_INCREASE_EXPOSURE, ImGuiMod_Shift | ImGuiKey_E, ImGuiInputFlags_Repeat,
                    [this]() { m_exposure_live = m_exposure += 0.25f; }});
        add_action({"Reset tonemapping", ICON_MY_RESET_TONEMAPPING, 0, 0, [this]()
                    {
                        m_exposure_live = m_exposure = 0.0f;
                        m_gamma_live = m_gamma = 2.2f;
                        m_sRGB                 = true;
                    }});
        add_action({"Normalize exposure", ICON_MY_NORMALIZE_EXPOSURE, ImGuiKey_N, 0, [this]()
                    {
                        if (auto img = current_image())
                        {
                            float m     = 0.f;
                            auto &group = img->groups[img->selected_group];
                            for (int c = 0; c < group.num_channels && c < 3; ++c)
                                m = std::max(m, img->channels[group.channels[c]].get_stats()->summary.maximum);

                            m_exposure_live = m_exposure = log2(1.f / m);
                        }
                    }});
        if (m_params.rendererBackendOptions.requestFloatBuffer)
            add_action({"Clamp to LDR", ICON_MY_CLAMP_TO_LDR, ImGuiMod_Ctrl | ImGuiKey_L, 0, []() {}, always_enabled,
                        false, &m_clamp_to_LDR});
        else
            add_action({"Dither", ICON_MY_DITHER, 0, 0, []() {}, always_enabled, false, &m_dither});

        add_action({"Draw pixel grid", ICON_MY_SHOW_GRID, ImGuiMod_Ctrl | ImGuiKey_G, 0, []() {}, always_enabled, false,
                    &m_draw_grid});
        add_action({"Draw pixel values", g_blank_icon, ImGuiMod_Ctrl | ImGuiKey_P, 0, []() {}, always_enabled, false,
                    &m_draw_pixel_info});

        add_action({"Draw data window", ICON_MY_DATA_WINDOW, ImGuiKey_None, 0, []() {}, always_enabled, false,
                    &m_draw_data_window});
        add_action({"Draw display window", ICON_MY_DISPLAY_WINDOW, ImGuiKey_None, 0, []() {}, always_enabled, false,
                    &m_draw_display_window});

        add_action({"sRGB", g_blank_icon, 0, 0, []() {}, always_enabled, false, &m_sRGB,
                    "Use the sRGB non-linear response curve (instead of gamma correction)"});
        add_action({"Decrease gamma", g_blank_icon, ImGuiKey_G, ImGuiInputFlags_Repeat, [this]()
                    { m_gamma_live = m_gamma = std::max(0.02f, m_gamma - 0.02f); }, [this]() { return !m_sRGB; }});
        add_action({"Increase gamma", g_blank_icon, ImGuiMod_Shift | ImGuiKey_G, ImGuiInputFlags_Repeat, [this]()
                    { m_gamma_live = m_gamma = std::max(0.02f, m_gamma + 0.02f); }, [this]() { return !m_sRGB; }});

        auto if_img = [this]() { return current_image() != nullptr; };

        // below actions are only available if there is an image

#if !defined(__EMSCRIPTEN__)
        add_action({"Save as...", ICON_MY_SAVE_AS, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, 0,
                    [this]()
                    {
                        string filename =
                            pfd::save_file("Save as", g_blank_icon,
                                           {
                                               "Supported image files",
                                               fmt::format("*.{}", fmt::join(Image::savable_formats(), "*.")),
                                           })
                                .result();

                        if (!filename.empty())
                            save_as(filename);
                    },
                    if_img});

#else
        add_action({"Save as...", ICON_MY_SAVE_AS, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, 0,
                    [this]()
                    {
                        if (current_image())
                        {
                            string filename;
                            string filter = fmt::format("*.{}", fmt::join(Image::savable_formats(), " *."));
                            ImGui::TextUnformatted(
                                "Please enter a filename. Format is deduced from the accepted extensions:");
                            ImGui::TextFmt("\t{}", filter);
                            ImGui::Separator();
                            if (ImGui::InputTextWithHint("##Filename", "Enter a filename and press <return>", &filename,
                                                         ImGuiInputTextFlags_EnterReturnsTrue))
                            {
                                ImGui::CloseCurrentPopup();

                                if (!filename.empty())
                                    save_as(filename);
                            }
                        }
                    },
                    if_img, true});
#endif

        // switch the current image using the image number (one-based indexing)
        for (int n = 1; n <= 10; ++n)
            add_action({fmt::format("Go to image {}", n), ICON_MY_IMAGE, ImGuiKey_0 + mod(n, 10), 0,
                        [this, n]() { set_current_image_index(nth_visible_image_index(mod(n - 1, 10))); },
                        [this, n]()
                        {
                            auto i = nth_visible_image_index(mod(n - 1, 10));
                            return is_valid(i) && i != m_current;
                        }});

        // select the reference image using Cmd + image number (one-based indexing)
        for (int n = 1; n <= 10; ++n)
            add_action({fmt::format("Set image {} as reference", n), ICON_MY_REFERENCE_IMAGE,
                        ImGuiMod_Ctrl | (ImGuiKey_0 + mod(n, 10)), 0,
                        [this, n]()
                        {
                            auto nth_visible = nth_visible_image_index(mod(n - 1, 10));
                            if (m_reference == nth_visible)
                                m_reference = -1;
                            else
                                set_reference_image_index(nth_visible);
                        },
                        [this, n]()
                        {
                            auto i = nth_visible_image_index(mod(n - 1, 10));
                            return is_valid(i);
                        }});

#ifdef _WIN32
        ImGuiKey modKey = ImGuiMod_Alt;
#else
        ImGuiKey modKey = ImGuiMod_Super;
#endif
        // switch the selected channel group using Ctrl + number key (one-based indexing)
        for (size_t n = 1u; n <= 10u; ++n)
            add_action({fmt::format("Go to channel group {}", n), ICON_MY_CHANNEL_GROUP,
                        modKey | ImGuiKey(ImGuiKey_0 + mod(int(n), 10)), 0,
                        [this, n]() { current_image()->selected_group = mod(int(n - 1), 10); },
                        [this, n]() { return current_image() && current_image()->groups.size() > n - 1; }});

        add_action({"Close", ICON_MY_CLOSE, ImGuiMod_Ctrl | ImGuiKey_W, ImGuiInputFlags_Repeat,
                    [this]() { close_image(); }, if_img});
        add_action({"Close all", ICON_MY_CLOSE_ALL, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_W, 0,
                    [this]() { close_all_images(); }, if_img});

        add_action({"Go to next image", g_blank_icon, ImGuiKey_DownArrow, ImGuiInputFlags_Repeat,
                    [this]() { set_current_image_index(next_visible_image_index(m_current, Forward)); },
                    [this]()
                    {
                        auto i = next_visible_image_index(m_current, Forward);
                        return is_valid(i) && i != m_current;
                    }});
        add_action({"Go to previous image", g_blank_icon, ImGuiKey_UpArrow, ImGuiInputFlags_Repeat,
                    [this]() { set_current_image_index(next_visible_image_index(m_current, Backward)); },
                    [this]()
                    {
                        auto i = next_visible_image_index(m_current, Backward);
                        return is_valid(i) && i != m_current;
                    }});
        add_action({"Go to next channel group", g_blank_icon, ImGuiKey_RightArrow, ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        auto img            = current_image();
                        img->selected_group = mod(img->selected_group + 1, (int)img->groups.size());
                    },
                    [this]() { return current_image() && current_image()->groups.size() > 1; }});
        add_action({"Go to previous channel group", g_blank_icon, ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        auto img            = current_image();
                        img->selected_group = mod(img->selected_group - 1, (int)img->groups.size());
                    },
                    [this]() { return current_image() && current_image()->groups.size() > 1; }});

        add_action({"Zoom out", ICON_MY_ZOOM_OUT, ImGuiKey_Minus, ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        zoom_out();
                        m_auto_fit_display = m_auto_fit_data = false;
                    },
                    if_img});
        add_action({"Zoom in", ICON_MY_ZOOM_IN, ImGuiKey_Equal, ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        zoom_in();
                        m_auto_fit_display = m_auto_fit_data = false;
                    },
                    if_img});
        add_action({"100\%", ICON_MY_ZOOM_100, 0, 0,
                    [this]()
                    {
                        set_zoom_level(0.f);
                        m_auto_fit_display = m_auto_fit_data = false;
                    },
                    if_img});
        add_action({"Center", ICON_MY_CENTER, ImGuiKey_C, 0,
                    [this]()
                    {
                        center();
                        m_auto_fit_display = m_auto_fit_data = false;
                    },
                    if_img});
        add_action({"Fit display window", ICON_MY_FIT_TO_WINDOW, ImGuiKey_F, 0,
                    [this]()
                    {
                        fit_display_window();
                        m_auto_fit_display = m_auto_fit_data = false;
                    },
                    if_img});
        add_action({"Auto fit display window", ICON_MY_FIT_TO_WINDOW, ImGuiMod_Shift | ImGuiKey_F, 0,
                    [this]() { m_auto_fit_data = false; }, if_img, false, &m_auto_fit_display});
        add_action({"Fit data window", ICON_MY_FIT_TO_WINDOW, ImGuiMod_Alt | ImGuiKey_F, 0,
                    [this]()
                    {
                        fit_data_window();
                        m_auto_fit_display = m_auto_fit_data = false;
                    },
                    if_img});
        add_action({"Auto fit data window", ICON_MY_FIT_TO_WINDOW, ImGuiMod_Shift | ImGuiMod_Alt | ImGuiKey_F, 0,
                    [this]() { m_auto_fit_display = false; }, if_img, false, &m_auto_fit_data});
    }

    // load any passed-in images
    load_images(in_files);
}

void HDRViewApp::setup_rendering()
{
    try
    {
        m_render_pass = new RenderPass(false, true);
        m_render_pass->set_cull_mode(RenderPass::CullMode::Disabled);
        m_render_pass->set_depth_test(RenderPass::DepthTest::Always, false);
        m_render_pass->set_clear_color(float4(0.15f, 0.15f, 0.15f, 1.f));

        m_shader = new Shader(m_render_pass,
                              /* An identifying name */
                              "ImageView", Shader::from_asset("shaders/image-shader_vert"),
                              Shader::prepend_includes(Shader::from_asset("shaders/image-shader_frag"),
                                                       {"shaders/colorspaces", "shaders/colormaps"}),
                              Shader::BlendMode::AlphaBlend);

        const float positions[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, -1.f, 1.f, 1.f, -1.f, 1.f};

        m_shader->set_buffer("position", VariableType::Float32, {6, 2}, positions);
        m_render_pass->set_cull_mode(RenderPass::CullMode::Disabled);

        Image::make_default_textures();

        m_shader->set_texture("dither_texture", Image::dither_texture());
        set_image_textures();

        ImGui::GetIO().ConfigMacOSXBehaviors = hostIsApple();

        spdlog::info("Successfully initialized graphics API!");
    }
    catch (const std::exception &e)
    {
        spdlog::error("Shader initialization failed!:\n\t{}.", e.what());
    }
}

void HDRViewApp::load_settings()
{
    spdlog::info("Loading user settings from '{}'", HelloImGui::IniSettingsLocation(m_params));

    auto s = HelloImGui::LoadUserPref("UserSettings");
    if (s.empty())
        return;

    try
    {
        json j = json::parse(s);
        m_recent_files.clear();
        spdlog::debug("Restoring recent file list...");
        m_recent_files = j.value<std::vector<std::string>>("recent files", m_recent_files);
        m_bg_mode =
            (EBGMode)std::clamp(j.value<int>("background mode", (int)m_bg_mode), (int)BG_BLACK, (int)NUM_BG_MODES - 1);
        m_bg_color.xyz() = j.value<float3>("background color", m_bg_color.xyz());

        m_draw_data_window    = j.value<bool>("draw data window", m_draw_data_window);
        m_draw_display_window = j.value<bool>("draw display window", m_draw_display_window);
        m_auto_fit_data       = j.value<bool>("auto fit data window", m_auto_fit_data);
        m_auto_fit_display    = j.value<bool>("auto fit display window", m_auto_fit_display);
        m_draw_pixel_info     = j.value<bool>("draw pixel info", m_draw_pixel_info);
        m_draw_grid           = j.value<bool>("draw pixel grid", m_draw_grid);
        m_exposure_live = m_exposure = j.value<float>("exposure", m_exposure);
        m_gamma_live = m_gamma = j.value<float>("gamma", m_gamma);
        m_sRGB                 = j.value<bool>("sRGB", m_sRGB);
        m_clamp_to_LDR         = j.value<bool>("clamp to LDR", m_clamp_to_LDR);
        m_dither               = j.value<bool>("dither", m_dither);
    }
    catch (json::exception &e)
    {
        spdlog::error("Error while parsing user settings: {}", e.what());
    }
}

void HDRViewApp::save_settings()
{
    spdlog::info("Saving user settings to '{}'", HelloImGui::IniSettingsLocation(m_params));

    json j;
    j["recent files"]            = m_recent_files;
    j["background mode"]         = (int)m_bg_mode;
    j["background color"]        = m_bg_color.xyz();
    j["draw data window"]        = m_draw_data_window;
    j["draw display window"]     = m_draw_display_window;
    j["auto fit data window"]    = m_auto_fit_data;
    j["auto fit display window"] = m_auto_fit_display;
    j["draw pixel info"]         = m_draw_pixel_info;
    j["draw pixel grid"]         = m_draw_grid;
    j["exposure"]                = m_exposure;
    j["gamma"]                   = m_gamma;
    j["sRGB"]                    = m_sRGB;
    j["clamp to LDR"]            = m_clamp_to_LDR;
    j["dither"]                  = m_dither;
    j["verbosity"]               = spdlog::get_level();
    HelloImGui::SaveUserPref("UserSettings", j.dump(4));
}

void HDRViewApp::draw_status_bar()
{
    const float y = ImGui::GetCursorPosY() - HelloImGui::EmSize(0.15f);
    if (m_pending_images.size())
    {
        ImGui::SetCursorPos({ImGui::GetStyle().ItemSpacing.x, y});
        ImGui::BusyBar(
            -1.f, {HelloImGui::EmSize(15.f), 0.f},
            fmt::format("Loading {} image{}", m_pending_images.size(), m_pending_images.size() > 1 ? "s" : "").c_str());
        ImGui::SameLine();
    }

    float x = ImGui::GetCursorPosX() + ImGui::GetStyle().ItemSpacing.x;

    auto sized_text = [&](float em_size, const string &text, float align = 1.f)
    {
        float item_width = HelloImGui::EmSize(em_size);
        float text_width = ImGui::CalcTextSize(text.c_str()).x;
        float spacing    = ImGui::GetStyle().ItemInnerSpacing.x;

        ImGui::SameLine(x + align * (item_width - text_width));
        ImGui::SetCursorPosY(y);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        x += item_width + spacing;
    };

    if (auto img = current_image())
    {
        auto &io = ImGui::GetIO();
        if (vp_pos_in_viewport(vp_pos_at_app_pos(io.MousePos)))
        {
            auto hovered_pixel = int2{pixel_at_app_pos(io.MousePos)};

            float4 color32 = image_pixel(hovered_pixel);
            auto  &group   = img->groups[img->selected_group];

            sized_text(3, fmt::format("({:>6d},", hovered_pixel.x), 0.f);
            sized_text(3, fmt::format("{:>6d})", hovered_pixel.y), 0.f);

            if (img->contains(hovered_pixel))
            {
                sized_text(0.5f, "=", 0.5f);
                for (int c = 0; c < group.num_channels; ++c)
                    sized_text(3.5f, fmt::format("{}{: 6.3f}{}", c == 0 ? "(" : "", color32[c],
                                                 c == group.num_channels - 1 ? ")" : ","));
            }
        }

        float real_zoom = m_zoom * pixel_ratio();
        int   numer     = (real_zoom < 1.0f) ? 1 : (int)round(real_zoom);
        int   denom     = (real_zoom < 1.0f) ? (int)round(1.0f / real_zoom) : 1;
        x               = ImGui::GetIO().DisplaySize.x - HelloImGui::EmSize(11.f) -
            (m_params.imGuiWindowParams.showStatus_Fps ? HelloImGui::EmSize(14.f) : HelloImGui::EmSize(0.f));
        sized_text(10.f, fmt::format("{:7.2f}% ({:d}:{:d})", real_zoom * 100, numer, denom));
    }
}

void HDRViewApp::draw_menus()
{
    if (ImGui::BeginMenu("File"))
    {
        MenuItem(action("Open image..."));

#if !defined(__EMSCRIPTEN__)
        ImGui::BeginDisabled(m_recent_files.empty());
        if (ImGui::BeginMenuEx("Open recent", ICON_MY_OPEN_IMAGE))
        {
            size_t i = m_recent_files.size() - 1;
            for (auto f = m_recent_files.rbegin(); f != m_recent_files.rend(); ++f, --i)
            {
                string short_name = (f->length() < 100) ? *f : f->substr(0, 47) + "..." + f->substr(f->length() - 50);
                if (ImGui::MenuItem(fmt::format("{}##File{}", short_name, i).c_str()))
                {
                    load_image(*f);
                    break;
                }
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Clear recently opened"))
                m_recent_files.clear();
            ImGui::EndMenu();
        }
        ImGui::EndDisabled();
#endif

        ImGui::Separator();

        MenuItem(action("Save as..."));

        ImGui::Separator();

        MenuItem(action("Close"));
        MenuItem(action("Close all"));

        ImGui::Separator();

        MenuItem(action("Quit"));

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        MenuItem(action("Zoom in"));
        MenuItem(action("Zoom out"));
        MenuItem(action("Center"));
        MenuItem(action("100%"));
        MenuItem(action("Fit display window"));
        MenuItem(action("Auto fit display window"));
        MenuItem(action("Fit data window"));
        MenuItem(action("Auto fit data window"));

        ImGui::Separator();

        MenuItem(action("Draw pixel grid"));
        MenuItem(action("Draw pixel values"));
        MenuItem(action("Draw data window"));
        MenuItem(action("Draw display window"));

        ImGui::Separator();

        MenuItem(action("Increase exposure"));
        MenuItem(action("Decrease exposure"));
        MenuItem(action("Normalize exposure"));

        ImGui::Separator();

        MenuItem(action("sRGB"));
        MenuItem(action("Increase gamma"));
        MenuItem(action("Decrease gamma"));

        ImGui::Separator();

        MenuItem(action("Reset tonemapping"));
        if (m_params.rendererBackendOptions.requestFloatBuffer)
            MenuItem(action("Clamp to LDR"));
        else
            MenuItem(action("Dither"));

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Windows"))
    {
        MenuItem(action("Command palette..."));

        ImGui::Separator();

        MenuItem(action("Restore default layout"));

        ImGui::Separator();

        MenuItem(action("Show entire GUI"));
        MenuItem(action("Hide entire GUI"));

        MenuItem(action("Show all windows"));
        MenuItem(action("Hide all windows"));

        ImGui::Separator();

        for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
        {
            if (!dockableWindow.includeInViewMenu)
                continue;

            MenuItem(action(fmt::format("Show {} window", dockableWindow.label)));
        }

        ImGui::Separator();

        MenuItem(action("Show top toolbar"));
        MenuItem(action("Show status bar"));
        MenuItem(action("Show FPS in status bar"));

        ImGui::EndMenu();
    }

    static const char *info_icon = ICON_MY_ABOUT;
    auto posX = (ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(info_icon).x -
                 ImGui::GetStyle().ItemSpacing.x);
    if (posX > ImGui::GetCursorPosX())
        ImGui::SetCursorPosX(posX);
    if (ImGui::MenuItem(info_icon))
        g_show_help = true;
}

void HDRViewApp::save_as(const string &filename) const
{
    try
    {
#if !defined(__EMSCRIPTEN__)
        std::ofstream os{filename, std::ios_base::binary};
        current_image()->save(os, filename);
#else
        std::ostringstream os;
        current_image()->save(os, filename);
        string buffer = os.str();
        emscripten_browser_file::download(
            filename,                                    // the default filename for the browser to save.
            "application/octet-stream",                  // the MIME type of the data, treated as if it were a webserver
                                                         // serving a file
            string_view(buffer.c_str(), buffer.length()) // a buffer describing the data to download
        );
#endif
    }
    catch (const std::exception &e)
    {
        spdlog::error("An error occurred while saving to '{}':\n\t{}.", filename, e.what());
    }
    catch (...)
    {
        spdlog::error("An unknown error occurred while saving to '{}'.", filename);
    }
}

void HDRViewApp::load_images(const vector<string> &filenames)
{
    for (auto f : filenames)
    {
        auto formats = Image::loadable_formats();
        if (formats.find(get_extension(f)) != formats.end())
            load_image(f);
        else
            spdlog::debug("Skipping unsupported file '{}'", f);
    }
}

void HDRViewApp::open_image()
{
#if defined(__EMSCRIPTEN__)
    auto handle_upload_file =
        [](const string &filename, const string &mime_type, string_view buffer, void *my_data = nullptr)
    {
        spdlog::trace("Loading uploaded file with filename '{}'", filename);
        hdrview()->load_image(filename, buffer);
    };

    string extensions = fmt::format(".{}", fmt::join(Image::loadable_formats(), ",.")) + ",image/*";

    // open the browser's file selector, and pass the file to the upload handler
    spdlog::debug("Requesting file from user...");
    emscripten_browser_file::upload(extensions, handle_upload_file, this);
#else
    string extensions = fmt::format("*.{}", fmt::join(Image::loadable_formats(), " *."));

    load_images(pfd::open_file("Open image(s)", "", {"Image files", extensions}, pfd::opt::multiselect).result());
#endif
}

void HDRViewApp::load_image(const string filename, string_view buffer)
{
    // Note: the filename is passed by value in case its an element of m_recent_files, which we modify
    spdlog::debug("Loading file '{}'...", filename);
    try
    {
        // convert the buffer (if any) to a string so the async thread has its own copy
        // then load from the string or filename depending on whether the buffer is empty
        m_pending_images.emplace_back(std::make_shared<PendingImages>(
            filename,
            [buffer_str = string(buffer), filename]()
            {
                if (buffer_str.empty())
                {
                    std::ifstream is{std::filesystem::u8path(filename.c_str()), std::ios_base::binary};
                    return Image::load(is, filename);
                }
                else
                {
                    std::istringstream is{buffer_str};
                    return Image::load(is, filename);
                }
            }));

        // remove any instances of filename from the recent files list until we know it has loaded successfully
        m_recent_files.erase(std::remove(m_recent_files.begin(), m_recent_files.end(), filename), m_recent_files.end());
    }
    catch (const std::exception &e)
    {
        spdlog::error("Could not load image \"{}\": {}.", filename, e.what());
        return;
    }
}

void HDRViewApp::add_pending_images()
{
    // Criterion to check if a image is ready, and copy it into our m_images vector if so
    auto removable = [this](shared_ptr<PendingImages> p)
    {
        if (p->images.ready())
        {
            // get the result, add any loaded images, and report that we can remove this task
            auto new_images = p->images.get();
            if (new_images.empty())
                return true;

            for (auto &i : new_images) m_images.push_back(i);

            // if loading was successful, add the filename to the recent list and limit to g_max_recent files
            m_recent_files.push_back(p->filename);
            if (m_recent_files.size() > g_max_recent)
                m_recent_files.erase(m_recent_files.begin(), m_recent_files.end() - g_max_recent);

            return true;
        }
        else
            return false;
    };

    auto num_previous = m_images.size();

    // move elements matching the criterion to the end of the vector, and then erase all matching elements
    m_pending_images.erase(std::remove_if(m_pending_images.begin(), m_pending_images.end(), removable),
                           m_pending_images.end());

    auto num_added = m_images.size() - num_previous;
    if (num_added > 0)
    {
        spdlog::info("Added {} new image{}.", num_added, (num_added > 1) ? "s" : "");
        // only select the newly loaded image if there is no valid selection
        // if (is_valid(m_current))
        //     return;

        m_current = int(m_images.size() - 1);

        // now upload the textures
        set_image_textures();
    }
}

void HDRViewApp::set_image_textures()
{
    try
    {
        // bind the primary and secondary images, or a placehold black texture when we have no current or reference
        // image
        if (auto img = current_image())
            img->set_as_texture(img->selected_group, *m_shader, "primary");
        else
            Image::set_null_texture(*m_shader, "primary");

        if (auto ref = reference_image())
            ref->set_as_texture(ref->selected_group, *m_shader, "secondary");
        else
            Image::set_null_texture(*m_shader, "secondary");
    }
    catch (const std::exception &e)
    {
        spdlog::error("Could not upload texture to graphics backend: {}.", e.what());
    }
}

void HDRViewApp::close_image()
{
    if (!current_image())
        return;

    // select the next image down the list
    int next = next_visible_image_index(m_current, Forward);
    if (next < m_current) // there is no visible image after this one, go to previous visible
        next = next_visible_image_index(m_current, Backward);

    m_images.erase(m_images.begin() + m_current);

    // adjust the indices after erasing the current image
    set_current_image_index(next < m_current ? next : next - 1);
    set_reference_image_index(m_reference < m_current ? m_reference : m_reference - 1);

    set_image_textures();
}

void HDRViewApp::close_all_images()
{
    m_images.clear();
    m_current   = -1;
    m_reference = -1;
    set_image_textures();
}

void HDRViewApp::run()
{
    ImPlot::CreateContext();
    HelloImGui::Run(m_params);
    ImPlot::DestroyContext();
}

ImFont *HDRViewApp::font(const string &name, int size) const
{
    if (size < 0)
    {
        // Determine the non-scaled size that corresponds to the current, dpi-scaled font size
        for (auto font_size : {14, 10, 16, 18, 30})
        {
            if (int(HelloImGui::DpiFontLoadingFactor() * font_size) == int(ImGui::GetFontSize()))
            {
                size = font_size;
                break;
            }
        }
    }

    try
    {
        return m_fonts.at({name, size});
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error(
            fmt::format("Font with name '{}' and size {} (={}) was not loaded.", name, size, ImGui::GetFontSize()));
    }
}

HDRViewApp::~HDRViewApp() {}

void HDRViewApp::draw_info_window()
{
    if (auto img = current_image())
        return img->draw_info();
}

void HDRViewApp::draw_histogram_window()
{
    if (auto img = current_image())
        img->draw_histogram();
}

void HDRViewApp::draw_file_window()
{
    ImGui::BeginDisabled(!current_image());
    if (ImGui::BeginCombo("Mode", blend_mode_names()[m_blend_mode].c_str(), ImGuiComboFlags_HeightLargest))
    {
        for (int n = 0; n < NUM_BLEND_MODES; ++n)
        {
            const bool is_selected = (m_blend_mode == n);
            if (ImGui::Selectable(blend_mode_names()[n].c_str(), is_selected))
            {
                m_blend_mode = (EBlendMode)n;
                spdlog::debug("Switching to blend mode {}.", n);
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Channel", channel_names()[m_channel].c_str(), ImGuiComboFlags_HeightLargest))
    {
        for (int n = 0; n < NUM_CHANNELS; ++n)
        {
            const bool is_selected = (m_blend_mode == n);
            if (ImGui::Selectable(channel_names()[n].c_str(), is_selected))
            {
                m_channel = (EChannel)n;
                spdlog::debug("Switching to channel {}.", n);
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    if (!num_images())
        return;

    const ImVec2 button_size = ImGui::IconButtonSize();

    bool show_button = m_filter.IsActive(); // save here to avoid flicker
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - (button_size.x + ImGui::GetStyle().ItemSpacing.x));
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InputTextWithHint("##file filter",
                                 ICON_MY_FILTER " Filter list of images (format: [include|-exclude][,...]; e.g. "
                                                "\"include_this,-but_not_this,also_include_this\")",
                                 m_filter.InputBuf, IM_ARRAYSIZE(m_filter.InputBuf)))
        m_filter.Build();
    ImGui::WrappedTooltip(
        "Filter open image list so that only images with a filename containing the search string will be visible.");
    if (show_button)
    {
        ImGui::SameLine(0.f, 0.f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - button_size.x);
        if (ImGui::IconButton(ICON_MY_DELETE))
            m_filter.Clear();
    }

    ImGui::SameLine();
    static bool show_channels = true;
    if (ImGui::IconButton(show_channels ? ICON_MY_CHANNEL_GROUP : ICON_MY_IMAGES))
        show_channels = !show_channels;
    ImGui::WrappedTooltip(show_channels ? "Click to show only images." : "Click to show images and channel groups.");

    ImGuiSelectableFlags   selectable_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
    static ImGuiTableFlags table_flags =
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_BordersH;

    if (ImGui::BeginTable("ImageList", 3, table_flags))
    {
        const float icon_width = ImGui::IconSize().x;
        ImGui::TableSetupColumn(ICON_MY_LIST_OL, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable,
                                1.75f * icon_width);
        ImGui::TableSetupColumn(ICON_MY_VISIBILITY,
                                ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable, icon_width);
        ImGui::TableSetupColumn(show_channels ? "File/part or channel group name"
                                              : "File/part:layer.channel group name",
                                ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_IndentEnable);
        ImGui::TableHeadersRow();

        static ImGuiOnceUponAFrame once;
        bool                       oaf                = false; // once;
        int                        id                 = 0;
        int                        visible_img_number = 0;
        for (int i = 0; i < num_images(); ++i)
        {
            auto &img          = m_images[i];
            bool  is_current   = m_current == i;
            bool  is_reference = m_reference == i;

            if (!is_visible(img))
                continue;

            ++visible_img_number;

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::PushRowColors(is_current, (!is_current && ImGui::GetIO().KeyCtrl) || is_reference);
            if (ImGui::Selectable(fmt::format("##image_{}_selectable", i + 1).c_str(), is_current || is_reference,
                                  selectable_flags))
            {
                if (ImGui::GetIO().KeyCtrl)
                    m_reference = is_reference ? -1 : i;
                else
                    m_current = i;
                set_image_textures();
                spdlog::trace("Setting image {} to the {} image", i, is_reference ? "reference" : "current");
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();

            auto image_num_str = fmt::format("{}", visible_img_number);
            ImGui::TextAligned(image_num_str, 1.0f);

            ImGui::TableNextColumn();
            auto tmp_pos = ImGui::GetCursorScreenPos();
            ImGui::TextUnformatted(is_current ? ICON_MY_VISIBILITY : "");
            ImGui::SetCursorScreenPos(tmp_pos);
            ImGui::TextUnformatted(is_reference ? ICON_MY_REFERENCE_IMAGE : "");

            // right-align the truncated file name
            auto  &selected_group = img->groups[img->selected_group];
            string group_name     = selected_group.name;
            auto  &channel        = img->channels[selected_group.channels[0]];
            string layer_path     = Channel::head(channel.name);
            string filename       = img->file_and_partname() + (show_channels ? "" : ":" + layer_path + group_name);

            ImGui::TableNextColumn();
            string ellipsis    = "";
            float  avail_width = ImGui::GetContentRegionAvail().x;
            while (ImGui::CalcTextSize((ellipsis + filename).c_str()).x > avail_width && filename.length() > 1)
            {
                filename = filename.substr(1);
                ellipsis = "...";
            }
            ImGui::TextAligned(ellipsis + filename, 1.f);

            if (show_channels && img->groups.size() > 1)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                for (size_t l = 0; l < img->layers.size(); ++l)
                {
                    auto &layer = img->layers[l];

                    for (size_t g = 0; g < layer.groups.size(); ++g)
                    {
                        auto  &group = img->groups[layer.groups[g]];
                        string name  = string(ICON_MY_CHANNEL_GROUP) + " " + layer.name + group.name;

                        bool is_selected_channel  = is_current && img->selected_group == layer.groups[g];
                        bool is_reference_channel = is_reference && img->selected_group == layer.groups[g];

                        if (oaf)
                            spdlog::info("Image {}; Group {} is selected: {}, reference: {}", filename, group.name,
                                         is_selected_channel, is_reference_channel);
                        ImGui::PushRowColors(is_selected_channel, is_reference_channel);
                        {
                            ImGui::TableNextRow();

                            ImGui::TableNextColumn();
                            string shortcut = is_current && layer.groups[g] < 10
                                                  ? fmt::format(ICON_MY_KEY_CONTROL "{}", mod(layer.groups[g] + 1, 10))
                                                  : "";
                            ImGui::TextAligned(shortcut, 1.0f);

                            ImGui::TableNextColumn();
                            if (ImGui::Selectable(
                                    fmt::format("{}##{}", is_selected_channel ? ICON_MY_VISIBILITY : "", id++).c_str(),
                                    is_selected_channel || is_reference_channel, selectable_flags))
                            {
                                if (ImGui::GetIO().KeyCtrl)
                                {
                                    // check if we are already the reference channel group
                                    if (is_reference && img->selected_group == layer.groups[g])
                                    {
                                        m_reference         = -1;
                                        img->selected_group = 0;
                                    }
                                    else
                                    {
                                        m_reference         = i;
                                        img->selected_group = layer.groups[g];
                                    }
                                }
                                else
                                {
                                    m_current           = i;
                                    img->selected_group = layer.groups[g];
                                }
                                set_image_textures();
                            }

                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(name);
                        }
                        ImGui::PopStyleColor(3);
                    }
                }
                ImGui::PopStyleColor();
            }
        }

        ImGui::EndTable();
    }
}

void HDRViewApp::draw_channel_window()
{
    if (auto img = current_image())
        img->draw_channels_list();
}

void HDRViewApp::center() { m_offset = float2(0.f, 0.f); }

void HDRViewApp::fit_display_window()
{
    if (auto img = current_image())
    {
        m_zoom = minelem(viewport_size() / img->display_window.size());
        center();
    }
}

void HDRViewApp::fit_data_window()
{
    if (auto img = current_image())
    {
        m_zoom = minelem(viewport_size() / img->data_window.size());

        auto center_pos   = float2(viewport_size() / 2.f);
        auto center_pixel = Box2f(current_image()->data_window).center();
        reposition_pixel_to_vp_pos(center_pos, center_pixel);
    }
}

float HDRViewApp::zoom_level() const { return log(m_zoom * pixel_ratio()) / log(m_zoom_sensitivity); }

void HDRViewApp::set_zoom_level(float level)
{
    m_zoom = std::clamp(std::pow(m_zoom_sensitivity, level) / pixel_ratio(), MIN_ZOOM, MAX_ZOOM);
}

void HDRViewApp::zoom_at_vp_pos(float amount, float2 focus_vp_pos)
{
    if (amount == 0.f)
        return;

    auto  focused_pixel = pixel_at_vp_pos(focus_vp_pos); // save focused pixel coord before modifying zoom
    float scale_factor  = std::pow(m_zoom_sensitivity, amount);
    m_zoom              = std::clamp(scale_factor * m_zoom, MIN_ZOOM, MAX_ZOOM);
    // reposition so focused_pixel is still under focus_app_pos
    reposition_pixel_to_vp_pos(focus_vp_pos, focused_pixel);
}

void HDRViewApp::zoom_in()
{
    // keep position at center of window fixed while zooming
    auto center_pos   = float2(viewport_size() / 2.f);
    auto center_pixel = pixel_at_vp_pos(center_pos);

    // determine next higher power of 2 zoom level
    float level_for_sensitivity = std::ceil(log(m_zoom) / log(2.f) + 0.5f);
    float new_scale             = std::pow(2.f, level_for_sensitivity);
    m_zoom                      = std::clamp(new_scale, MIN_ZOOM, MAX_ZOOM);
    reposition_pixel_to_vp_pos(center_pos, center_pixel);
}

void HDRViewApp::zoom_out()
{
    // keep position at center of window fixed while zooming
    auto center_pos   = float2(viewport_size() / 2.f);
    auto center_pixel = pixel_at_vp_pos(center_pos);

    // determine next lower power of 2 zoom level
    float level_for_sensitivity = std::floor(log(m_zoom) / log(2.f) - 0.5f);
    float new_scale             = std::pow(2.f, level_for_sensitivity);
    m_zoom                      = std::clamp(new_scale, MIN_ZOOM, MAX_ZOOM);
    reposition_pixel_to_vp_pos(center_pos, center_pixel);
}

void HDRViewApp::reposition_pixel_to_vp_pos(float2 position, float2 pixel)
{
    // Calculate where the new offset must be in order to satisfy the image position equation.
    m_offset = position - (pixel * m_zoom) - center_offset();
}

Box2f HDRViewApp::scaled_display_window(ConstImagePtr img) const
{
    if (!img)
        img = current_image();
    Box2f dw = img ? Box2f{img->display_window} : Box2f{{0, 0}, {0, 0}};
    dw.min *= m_zoom;
    dw.max *= m_zoom;
    return dw;
}

Box2f HDRViewApp::scaled_data_window(ConstImagePtr img) const
{
    if (!img)
        img = current_image();
    Box2f dw = img ? Box2f{img->data_window} : Box2f{{0, 0}, {0, 0}};
    dw.min *= m_zoom;
    dw.max *= m_zoom;
    return dw;
}

float HDRViewApp::pixel_ratio() const { return ImGui::GetIO().DisplayFramebufferScale.x; }

float2 HDRViewApp::center_offset(ConstImagePtr img) const
{
    auto dw = scaled_display_window(img);
    return (viewport_size() - dw.size()) / 2.f - dw.min;
}

float2 HDRViewApp::image_position(ConstImagePtr img) const
{
    auto dw = scaled_data_window(img);
    return (m_offset + center_offset(img) + dw.min) / viewport_size();
}

float2 HDRViewApp::image_scale(ConstImagePtr img) const
{
    auto dw = scaled_data_window(img);
    return dw.size() / viewport_size();
}

void HDRViewApp::draw_pixel_grid() const
{
    if (!current_image())
        return;

    static const int s_grid_threshold = 10;

    if (!m_draw_grid || (s_grid_threshold == -1) || (m_zoom <= s_grid_threshold))
        return;

    float factor = std::clamp((m_zoom - s_grid_threshold) / (2 * s_grid_threshold), 0.f, 1.f);
    float alpha  = lerp(0.0f, 1.0f, smoothstep(0.0f, 1.0f, factor));

    ImDrawList *draw_list = ImGui::GetBackgroundDrawList();

    // only draw within the image viewport
    draw_list->PushClipRect(app_pos_at_vp_pos({0.f, 0.f}), app_pos_at_vp_pos(viewport_size()), true);
    if (alpha > 0.0f)
    {
        ImColor col_fg(1.0f, 1.0f, 1.0f, alpha);
        ImColor col_bg(0.2f, 0.2f, 0.2f, alpha);

        auto screen_bounds = Box2i{int2(pixel_at_vp_pos({0.f, 0.f})) - 1, int2(pixel_at_vp_pos(viewport_size())) + 1};
        auto bounds        = screen_bounds.intersect(current_image()->data_window);

        // draw vertical lines
        for (int x = bounds.min.x; x <= bounds.max.x; ++x)
            draw_list->AddLine(app_pos_at_pixel(float2(x, bounds.min.y)), app_pos_at_pixel(float2(x, bounds.max.y)),
                               col_bg, 4.f);

        // draw horizontal lines
        for (int y = bounds.min.y; y <= bounds.max.y; ++y)
            draw_list->AddLine(app_pos_at_pixel(float2(bounds.min.x, y)), app_pos_at_pixel(float2(bounds.max.x, y)),
                               col_bg, 4.f);

        // and now again with the foreground color
        for (int x = bounds.min.x; x <= bounds.max.x; ++x)
            draw_list->AddLine(app_pos_at_pixel(float2(x, bounds.min.y)), app_pos_at_pixel(float2(x, bounds.max.y)),
                               col_fg, 2.f);
        for (int y = bounds.min.y; y <= bounds.max.y; ++y)
            draw_list->AddLine(app_pos_at_pixel(float2(bounds.min.x, y)), app_pos_at_pixel(float2(bounds.max.x, y)),
                               col_fg, 2.f);
    }
    draw_list->PopClipRect();
}

void HDRViewApp::draw_pixel_info() const
{
    if (!current_image() || !m_draw_pixel_info)
        return;

    static constexpr float3 white     = float3{1.f};
    static constexpr float3 black     = float3{0.f};
    static constexpr float2 align     = {0.5f, 0.5f};
    auto                    mono_font = font("mono bold", 30);

    auto  &group  = current_image()->groups[current_image()->selected_group];
    auto   colors = group.colors();
    string names[4];
    string longest_name;
    for (int c = 0; c < group.num_channels; ++c)
    {
        auto &channel = current_image()->channels[group.channels[c]];
        names[c]      = Channel::tail(channel.name);
        if (names[c].length() > longest_name.length())
            longest_name = names[c];
    }

    ImGui::PushFont(mono_font);
    static float line_height = ImGui::CalcTextSize("").y;
    const float2 channel_threshold2 =
        float2{ImGui::CalcTextSize((longest_name + ": 31.000").c_str()).x, group.num_channels * line_height};
    const float2 coord_threshold2  = channel_threshold2 + float2{0.f, 2.f * line_height};
    const float  channel_threshold = maxelem(channel_threshold2);
    const float  coord_threshold   = maxelem(coord_threshold2);
    ImGui::PopFont();

    if (m_zoom <= channel_threshold)
        return;

    // fade value for the channel values shown at sufficient zoom
    float factor = std::clamp((m_zoom - channel_threshold) / (1.25f * channel_threshold), 0.f, 1.f);
    float alpha  = smoothstep(0.0f, 1.0f, factor);

    // fade value for the (x,y) coordinates shown at further zoom
    float factor2 = std::clamp((m_zoom - coord_threshold) / (1.25f * coord_threshold), 0.f, 1.f);
    float alpha2  = smoothstep(0.0f, 1.0f, factor2);

    ImDrawList *draw_list = ImGui::GetBackgroundDrawList();

    // only draw within the image viewport
    draw_list->PushClipRect(app_pos_at_vp_pos({0.f, 0.f}), app_pos_at_vp_pos(viewport_size()), true);
    if (alpha > 0.0f)
    {
        ImGui::ScopedFont sf{mono_font};

        auto screen_bounds = Box2i{int2(pixel_at_vp_pos({0.f, 0.f})) - 1, int2(pixel_at_vp_pos(viewport_size())) + 1};
        auto bounds        = screen_bounds.intersect(current_image()->data_window);

        for (int y = bounds.min.y; y < bounds.max.y; ++y)
        {
            for (int x = bounds.min.x; x < bounds.max.x; ++x)
            {
                auto   pos   = app_pos_at_pixel(float2(x + 0.5f, y + 0.5f));
                float4 pixel = image_pixel({x, y});
                if (alpha2 > 0.f)
                {
                    float2 c_pos = pos + float2{0.f, (-1 - 0.5f * (group.num_channels - 1)) * line_height};
                    auto   text  = fmt::format("({},{})", x, y);
                    ImGui::AddTextAligned(draw_list, c_pos + int2{1, 2}, ImColor{float4{black, alpha2}}, text, align);
                    ImGui::AddTextAligned(draw_list, c_pos, ImColor{float4{white, alpha2}}, text, align);
                }

                for (int c = 0; c < group.num_channels; ++c)
                {
                    float2 c_pos = pos + float2{0.f, (c - 0.5f * (group.num_channels - 1)) * line_height};
                    auto   text  = fmt::format("{:>2s}:{: > 7.3f}", names[c], pixel[c]);
                    ImGui::AddTextAligned(draw_list, c_pos + int2{1, 2}, ImColor{float4{black, alpha2}}, text, align);
                    ImGui::AddTextAligned(draw_list, c_pos, ImColor{float4{colors[c].xyz(), alpha}}, text, align);
                }
            }
        }
    }
    draw_list->PopClipRect();
}

void HDRViewApp::draw_image_border() const
{
    if (!current_image() || minelem(current_image()->size()) == 0)
        return;

    constexpr float  thickness = 3.f;
    constexpr float2 fudge     = float2{thickness * 0.5f - 0.5f, -(thickness * 0.5f - 0.5f)};
    float2           pad       = HelloImGui::EmToVec2({0.25, 0.125});
    auto             draw_list = ImGui::GetBackgroundDrawList();

    auto draw_image_window =
        [&](const Box2f &image_window, ImGuiCol col_idx, const string &text, const float2 &align, bool draw_label)
    {
        Box2f window{app_pos_at_pixel(image_window.min), app_pos_at_pixel(image_window.max)};
        draw_list->AddRect(window.min, window.max, ImGui::GetColorU32(col_idx), 0.f, ImDrawFlags_None, thickness);

        if (!draw_label)
            return;

        float2 shifted_align = (2.f * align - float2{1.f});
        float2 text_size     = ImGui::CalcTextSize(text.c_str());
        float2 tab_size      = text_size + pad * 2.f;
        float  fade          = 1.f - smoothstep(0.5f * window.size().x, 1.0f * window.size().x, tab_size.x);
        if (fade == 0.0f)
            return;

        Box2f tab_box = {float2{0.f}, tab_size};
        tab_box.move_min_to(
            // move to the correct corner while accounting for the tab size
            window.min + align * (window.size() - tab_size) +
            // shift the tab outside the window
            shifted_align * (fudge + float2{0, tab_size.y}));
        draw_list->AddRectFilled(tab_box.min, tab_box.max, ImGui::GetColorU32(col_idx, fade),
                                 std::clamp(ImGui::GetStyle().TabRounding, 0.0f, tab_size.x * 0.5f - 1.0f),
                                 shifted_align.y < 0.f ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersBottom);
        ImGui::AddTextAligned(draw_list, tab_box.min + align * tab_box.size() - shifted_align * pad,
                              ImGui::GetColorU32(ImGuiCol_Text, fade), text, align);
    };

    // only draw within the image viewport
    draw_list->PushClipRect(app_pos_at_vp_pos({0.f, 0.f}), app_pos_at_vp_pos(viewport_size()), true);
    {
        bool non_trivial = current_image()->data_window != current_image()->display_window ||
                           current_image()->data_window.min != int2{0, 0};

        if (m_draw_data_window)
            draw_image_window(Box2f{current_image()->data_window}, ImGuiCol_TabActive, "Data window", {0.f, 0.f},
                              non_trivial);
        if (m_draw_display_window && non_trivial)
            draw_image_window(Box2f{current_image()->display_window}, ImGuiCol_TabUnfocused, "Display window",
                              {1.f, 1.f}, true);
    }
    draw_list->PopClipRect();
}

void HDRViewApp::draw_image() const
{
    if (current_image() && !current_image()->data_window.is_empty())
    {
        float2 randomness(std::generate_canonical<float, 10>(g_rand) * 255,
                          std::generate_canonical<float, 10>(g_rand) * 255);

        m_shader->set_uniform("randomness", randomness);
        m_shader->set_uniform("gain", powf(2.0f, m_exposure_live));
        m_shader->set_uniform("gamma", m_gamma);
        m_shader->set_uniform("sRGB", m_sRGB);
        m_shader->set_uniform("clamp_to_LDR", m_clamp_to_LDR);
        m_shader->set_uniform("do_dither", m_dither);

        m_shader->set_uniform("primary_pos", image_position(current_image()));
        m_shader->set_uniform("primary_scale", image_scale(current_image()));

        m_shader->set_uniform("blend_mode", (int)m_blend_mode);
        m_shader->set_uniform("channel", (int)m_channel);
        m_shader->set_uniform("bg_mode", (int)m_bg_mode);
        m_shader->set_uniform("bg_color", m_bg_color);

        if (reference_image())
        {
            m_shader->set_uniform("has_reference", true);
            m_shader->set_uniform("secondary_pos", image_position(reference_image()));
            m_shader->set_uniform("secondary_scale", image_scale(reference_image()));
        }
        else
        {
            m_shader->set_uniform("has_reference", false);
            m_shader->set_uniform("secondary_pos", float2{0.f});
            m_shader->set_uniform("secondary_scale", float2{1.f});
        }

        m_shader->begin();
        m_shader->draw_array(Shader::PrimitiveType::Triangle, 0, 6, false);
        m_shader->end();
    }
}

void HDRViewApp::draw_top_toolbar()
{
    auto img = current_image();

    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(font("sans bold", 16));
    ImGui::TextUnformatted(ICON_MY_EXPOSURE);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(HelloImGui::EmSize(8));
    ImGui::SliderFloat("##ExposureSlider", &m_exposure_live, -9.f, 9.f, "%5.2f");
    if (ImGui::IsItemDeactivatedAfterEdit())
        m_exposure = m_exposure_live;

    ImGui::SameLine();

    ImGui::BeginDisabled(!img);
    IconButton(action("Normalize exposure"));
    ImGui::EndDisabled();

    ImGui::SameLine();

    IconButton(action("Reset tonemapping"));
    ImGui::SameLine();

    Checkbox(action("sRGB"));
    ImGui::SameLine();

    ImGui::BeginDisabled(m_sRGB);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Gamma:");

    ImGui::SameLine();

    ImGui::SetNextItemWidth(HelloImGui::EmSize(8));
    ImGui::SliderFloat("##GammaSlider", &m_gamma_live, 0.02f, 9.f, "%5.3f");
    if (ImGui::IsItemDeactivatedAfterEdit())
        m_gamma = m_gamma_live;

    ImGui::EndDisabled();
    ImGui::SameLine();

    if (m_params.rendererBackendOptions.requestFloatBuffer)
    {
        Checkbox(action("Clamp to LDR"));
        ImGui::SameLine();
    }

    Checkbox(action("Draw pixel grid"));
    ImGui::SameLine();

    Checkbox(action("Draw pixel values"));
    ImGui::SameLine();
}

void HDRViewApp::draw_background()
{
    process_shortcuts();

    try
    {
        auto &io = ImGui::GetIO();

        //
        // calculate the viewport sizes
        // fbsize is the size of the window in physical pixels while accounting for dpi factor on retina screens.
        // For retina displays, io.DisplaySize is the size of the window in logical pixels so we it by
        // io.DisplayFramebufferScale to get the physical pixel size for the framebuffer.
        float2 fbscale = io.DisplayFramebufferScale;
        int2   fbsize  = int2{float2{io.DisplaySize} * fbscale};
        spdlog::trace("DisplayFramebufferScale: {}, DpiWindowSizeFactor: {}, DpiFontLoadingFactor: {}",
                      float2{io.DisplayFramebufferScale}, HelloImGui::DpiWindowSizeFactor(),
                      HelloImGui::DpiFontLoadingFactor());
        m_viewport_min  = {0.f, 0.f};
        m_viewport_size = io.DisplaySize;
        if (auto id = m_params.dockingParams.dockSpaceIdFromName("MainDockSpace"))
            if (auto central_node = ImGui::DockBuilderGetCentralNode(*id))
            {
                m_viewport_size = central_node->Size;
                m_viewport_min  = central_node->Pos;
            }

        if (!io.WantCaptureMouse)
        {
            auto vp_mouse_pos = vp_pos_at_app_pos(io.MousePos);
            auto scroll       = float2{io.MouseWheelH, io.MouseWheel};
#if defined(__EMSCRIPTEN__)
            scroll *= 10.0f;
#endif
            float2 drag_delta{ImGui::GetMouseDragDelta(ImGuiMouseButton_Left)};
            bool   cancel_autofit = true;
            if (length2(drag_delta) > 0.f && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                reposition_pixel_to_vp_pos(vp_mouse_pos + drag_delta, pixel_at_vp_pos(vp_mouse_pos));
                ImGui::ResetMouseDragDelta();
            }
            else if (length2(scroll) > 0.f)
            {
                if (ImGui::IsKeyDown(ImGuiMod_Shift))
                    // panning
                    reposition_pixel_to_vp_pos(vp_mouse_pos + scroll * 4.f, pixel_at_vp_pos(vp_mouse_pos));
                else
                    zoom_at_vp_pos(scroll.y / 4.f, vp_mouse_pos);
            }
            else
                cancel_autofit = false;

            if (cancel_autofit)
                m_auto_fit_display = m_auto_fit_data = false;
        }

        //
        // clear the framebuffer and set up the viewport
        //

        // RenderPass expects things in framebuffer coordinates
        m_render_pass->resize(fbsize);
        m_render_pass->set_viewport(int2(m_viewport_min * fbscale), int2(m_viewport_size * fbscale));

        if (m_auto_fit_display)
            fit_display_window();
        if (m_auto_fit_data)
            fit_data_window();

        m_render_pass->begin();

        draw_image();
        draw_pixel_info();
        draw_pixel_grid();
        draw_image_border();

        m_render_pass->end();
    }
    catch (const std::exception &e)
    {
        spdlog::error("Drawing failed:\n\t{}.", e.what());
    }
}

bool HDRViewApp::is_visible(const ConstImagePtr &img) const
{
    return m_filter.PassFilter(img->file_and_partname().c_str());
}

bool HDRViewApp::is_visible(int index) const
{
    if (!is_valid(index))
        return true;

    return is_visible(image(index));
}

int HDRViewApp::next_visible_image_index(int index, EDirection direction) const
{
    return next_matching_index(
        m_images, index, [this](size_t, const ImagePtr &img) { return is_visible(img); }, direction);
}

int HDRViewApp::nth_visible_image_index(int n) const
{
    return (int)nth_matching_index(m_images, (size_t)n,
                                   [this](size_t, const ImagePtr &img) { return is_visible(img); });
}

bool HDRViewApp::process_event(void *e)
{
#ifdef HELLOIMGUI_USE_SDL
    auto &io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return false;

    SDL_Event *event = static_cast<SDL_Event *>(e);
    switch (event->type)
    {
    case SDL_QUIT: spdlog::trace("Got an SDL_QUIT event"); break;
    case SDL_WINDOWEVENT: spdlog::trace("Got an SDL_WINDOWEVENT event"); break;
    case SDL_MOUSEWHEEL: spdlog::trace("Got an SDL_MOUSEWHEEL event"); break;
    case SDL_MOUSEMOTION: spdlog::trace("Got an SDL_MOUSEMOTION event"); break;
    case SDL_MOUSEBUTTONDOWN: spdlog::trace("Got an SDL_MOUSEBUTTONDOWN event"); break;
    case SDL_MOUSEBUTTONUP: spdlog::trace("Got an SDL_MOUSEBUTTONUP event"); break;
    case SDL_FINGERMOTION: spdlog::trace("Got an SDL_FINGERMOTION event"); break;
    case SDL_FINGERDOWN: spdlog::trace("Got an SDL_FINGERDOWN event"); break;
    case SDL_MULTIGESTURE:
    {
        spdlog::trace("Got an SDL_MULTIGESTURE event; numFingers: {}; dDist: {}; x: {}, y: {}; io.MousePos: {}, {}; "
                      "io.MousePosFrac: {}, {}",
                      event->mgesture.numFingers, event->mgesture.dDist, event->mgesture.x, event->mgesture.y,
                      io.MousePos.x, io.MousePos.y, io.MousePos.x / io.DisplaySize.x, io.MousePos.y / io.DisplaySize.y);
        constexpr float cPinchZoomThreshold(0.0001f);
        constexpr float cPinchScale(80.0f);
        if (event->mgesture.numFingers == 2 && fabs(event->mgesture.dDist) >= cPinchZoomThreshold)
        {
            // Zoom in/out by positive/negative mPinch distance
            zoom_at_vp_pos(event->mgesture.dDist * cPinchScale, vp_pos_at_app_pos(io.MousePos));
            return true;
        }
    }
    break;
    case SDL_FINGERUP: spdlog::trace("Got an SDL_FINGERUP event"); break;
    }
#endif
    return false;
}

void HDRViewApp::draw_command_palette()
{
    if (g_show_command_palette)
        ImGui::OpenPopup("Command palette...");

    g_show_command_palette = false;

    float2 display_size = ImGui::GetIO().DisplaySize;
#ifdef __EMSCRIPTEN__
    display_size = float2{(float)window_width(), (float)window_height()};
#endif
    // Center window horizontally, align near top vertically
    ImGui::SetNextWindowPos(ImVec2(display_size.x / 2, 5.f * HelloImGui::EmSize()), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));

    ImGui::SetNextWindowSize(ImVec2{400, 0}, ImGuiCond_Always);

    float remaining_height            = ImGui::GetMainViewport()->Size.y - ImGui::GetCursorScreenPos().y;
    float search_result_window_height = remaining_height - 2.f * HelloImGui::EmSize();

    // Set constraints to allow horizontal resizing based on content
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0, 0), ImVec2(display_size.x - 2.f * HelloImGui::EmSize(), search_result_window_height));

    if (ImGui::BeginPopup("Command palette...", ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (ImGui::IsWindowAppearing())
        {
            spdlog::trace("Creating ImCmd context");
            if (ImCmd::GetCurrentContext())
            {
                ImCmd::RemoveAllCaches();
                ImCmd::DestroyContext();
            }
            ImCmd::CreateContext();
            ImCmd::SetStyleFont(ImCmdTextType_Regular, font("sans regular", 14));
            ImCmd::SetStyleFont(ImCmdTextType_Highlight, font("sans bold", 14));
            ImCmd::SetStyleFlag(ImCmdTextType_Highlight, ImCmdTextFlag_Underline, true);
            // ImVec4 highlight_font_color(1.0f, 1.0f, 1.0f, 1.0f);
            auto highlight_font_color = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
            ImCmd::SetStyleColor(ImCmdTextType_Highlight, ImGui::ColorConvertFloat4ToU32(highlight_font_color));

            for (auto &a : m_actions)
            {
                if (a.enabled())
                    ImCmd::AddCommand({a.name, a.p_selected ? [&a](){
                *a.p_selected = !*a.p_selected;a.callback();} : a.callback, nullptr, nullptr, a.icon, ImGui::GetKeyChordNameTranslated(a.chord), a.p_selected});
            }

#if !defined(__EMSCRIPTEN__)
            // add a two-step command to list and open recent files
            if (!m_recent_files.empty())
                ImCmd::AddCommand({"Open recent",
                                   [this]()
                                   {
                                       vector<string> short_names;
                                       size_t         i = m_recent_files.size() - 1;
                                       for (auto f = m_recent_files.rbegin(); f != m_recent_files.rend(); ++f, --i)
                                           short_names.push_back((f->length() < 60) ? *f
                                                                                    : f->substr(0, 32) + "..." +
                                                                                          f->substr(f->length() - 25));
                                       ImCmd::Prompt(short_names);
                                       ImCmd::SetNextCommandPaletteSearchBoxFocused();
                                   },
                                   [this](int selected_option)
                                   {
                                       int idx = int(m_recent_files.size() - 1) - selected_option;
                                       if (idx >= 0 && idx < int(m_recent_files.size()))
                                           load_image(m_recent_files[idx]);
                                   },
                                   nullptr, ICON_MY_OPEN_IMAGE});

#endif

            // set logging verbosity. This is a two-step command
            ImCmd::AddCommand({"Set logging verbosity",
                               []()
                               {
                                   ImCmd::Prompt(std::vector<std::string>{"0: trace", "1: debug", "2: info", "3: warn",
                                                                          "4: err", "5: critical", "6: off"});
                                   ImCmd::SetNextCommandPaletteSearchBoxFocused();
                               },
                               [](int selected_option)
                               { spdlog::set_level(spdlog::level::level_enum(selected_option)); }, nullptr,
                               ICON_MY_LOG_LEVEL});

            // set background color. This is a two-step command
            ImCmd::AddCommand({"Set background color",
                               []()
                               {
                                   ImCmd::Prompt(std::vector<std::string>{"0: black", "1: white", "2: dark checker",
                                                                          "3: light checker", "4: custom..."});
                                   ImCmd::SetNextCommandPaletteSearchBoxFocused();
                               },
                               [this](int selected_option)
                               {
                                   m_bg_mode =
                                       (EBGMode)std::clamp(selected_option, (int)BG_BLACK, (int)NUM_BG_MODES - 1);
                                   if (m_bg_mode == BG_CUSTOM_COLOR)
                                       g_show_bg_color_picker = true;
                               },
                               nullptr, g_blank_icon});

            // add two-step theme selection command
            ImCmd::AddCommand({"Set theme",
                               []()
                               {
                                   vector<string> theme_names;
                                   for (int i = 0; i < ImGuiTheme::ImGuiTheme_Count; ++i)
                                       theme_names.push_back(ImGuiTheme::ImGuiTheme_Name((ImGuiTheme::ImGuiTheme_)(i)));

                                   ImCmd::Prompt(theme_names);
                                   ImCmd::SetNextCommandPaletteSearchBoxFocused();
                               },
                               [](int selected_option)
                               {
                                   ImGuiTheme::ImGuiTheme_ theme = (ImGuiTheme::ImGuiTheme_)(selected_option);
                                   HelloImGui::GetRunnerParams()->imGuiWindowParams.tweakedTheme.Theme = theme;
                                   ImGuiTheme::ApplyTheme(theme);
                               },
                               nullptr, ICON_MY_THEME});

            ImCmd::SetNextCommandPaletteSearchBoxFocused();
            ImCmd::SetNextCommandPaletteSearch("");
        }

        // ImCmd::SetNextCommandPaletteSearchBoxFocused(); // always focus the search box
        ImCmd::CommandPalette("Command palette", "Filter commands...");

        if (ImCmd::IsAnyItemSelected() || ImGui::GlobalShortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteOverActive) ||
            ImGui::GlobalShortcut(ImGuiMod_Ctrl | ImGuiKey_Period, ImGuiInputFlags_RouteOverActive))
        {
            // Close window when user selects an item, hits escape, or unfocuses the command palette window
            // (clicking elsewhere)
            ImGui::CloseCurrentPopup();
            g_show_command_palette = false;
        }

        if (ImGui::BeginTable("PaletteHelp", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ContextMenuInBody))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::ScopedFont sf{font("sans bold", 10)};

            string txt;
            txt = "Navigate (" ICON_MY_ARROW_UP ICON_MY_ARROW_DOWN ")";
            ImGui::TableNextColumn();
            ImGui::TextAligned(txt, 0.f);

            ImGui::TableNextColumn();
            txt = "Use (" ICON_MY_KEY_RETURN ")";
            ImGui::TextAligned(txt, 0.5f);

            ImGui::TableNextColumn();
            txt = "Dismiss (" ICON_MY_KEY_ESC ")";
            ImGui::TextAligned(txt, 1.f);

            ImGui::PopStyleColor();

            ImGui::EndTable();
        }

        ImGui::EndPopup();
    }
}

void HDRViewApp::process_shortcuts()
{
    if (ImGui::GetIO().WantCaptureKeyboard)
    {
        spdlog::trace("Not processing shortcuts because ImGui wants to capture the keyboard (frame: {})",
                      ImGui::GetFrameCount());
        return;
    }

    spdlog::trace("Processing shortcuts (frame: {})", ImGui::GetFrameCount());

    for (auto &a : m_actions)
        if (a.chord)
            if (a.enabled() && ImGui::GlobalShortcut(a.chord, a.flags))
            {
                spdlog::trace("Processing shortcut for action '{}' (frame: {})", a.name, ImGui::GetFrameCount());
                if (a.p_selected)
                    *a.p_selected = !*a.p_selected;
                a.callback();
#ifdef __EMSCRIPTEN__
                ImGui::GetIO().ClearInputKeys(); // FIXME: somehow needed in emscripten, otherwise the key (without
                                                 // modifiers) needs to be pressed before this chord is detected again
#endif
                break;
            }

    set_image_textures();
}

void HDRViewApp::draw_about_dialog()
{
    if (g_show_help)
        ImGui::OpenPopup("About");

    // work around HelloImGui rendering a couple frames to figure out sizes
    if (ImGui::GetFrameCount() > 1)
        g_show_help = false;

    float2 display_size = ImGui::GetIO().DisplaySize;
#ifdef __EMSCRIPTEN__
    display_size = float2{(float)window_width(), (float)window_height()};
#endif
    // Center window horizontally, align near top vertically
    ImGui::SetNextWindowPos(ImVec2(display_size.x / 2, 5.f * HelloImGui::EmSize()), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));

    ImGui::SetNextWindowFocus();
    constexpr float icon_size = 128.f;
    float2          col_width = {icon_size + HelloImGui::EmSize(), 32 * HelloImGui::EmSize()};
    col_width[1]              = std::clamp(col_width[1], 5 * HelloImGui::EmSize(),
                                           display_size.x - ImGui::GetStyle().WindowPadding.x - 2 * ImGui::GetStyle().ItemSpacing.x -
                                               ImGui::GetStyle().ScrollbarSize - col_width[0]);

    ImGui::SetNextWindowContentSize(float2{col_width[0] + col_width[1] + ImGui::GetStyle().ItemSpacing.x, 0});

    if (ImGui::BeginPopup("About", ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Spacing();

        if (ImGui::BeginTable("about_table1", 2))
        {
            ImGui::TableSetupColumn("icon", ImGuiTableColumnFlags_WidthFixed, col_width[0]);
            ImGui::TableSetupColumn("description", ImGuiTableColumnFlags_WidthFixed, col_width[1]);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // right align the image
            ImGui::AlignCursor(icon_size + 0.5f * HelloImGui::EmSize(), 1.f);
            HelloImGui::ImageFromAsset("app_settings/icon.png", {icon_size, icon_size}); // show the app icon

            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[1]);

            {
                ImGui::ScopedFont sf{font("sans bold", 30)};
                ImGui::HyperlinkText("https://github.com/wkjarosz/hdrview", "HDRView");
            }

            ImGui::PushFont(font("sans bold", 18));
            ImGui::TextUnformatted(version());
            ImGui::PopFont();
            ImGui::PushFont(font("sans regular", 10));
#if defined(__EMSCRIPTEN__)
            ImGui::TextFmt("Built with emscripten using the {} backend on {}.", backend(), build_timestamp());
#else
            ImGui::TextFmt("Built using the {} backend on {}.", backend(), build_timestamp());
#endif
            ImGui::PopFont();

            ImGui::Spacing();

            ImGui::PushFont(font("sans bold", 16));
            ImGui::TextUnformatted(
                "HDRView is a simple research-oriented tool for examining, comparing, manipulating, and "
                "converting high-dynamic range images.");
            ImGui::PopFont();

            ImGui::Spacing();

            ImGui::TextUnformatted(
                "It is developed by Wojciech Jarosz, and is available under a 3-clause BSD license.");

            ImGui::PopTextWrapPos();
            ImGui::EndTable();
        }

        auto item_and_description = [this, col_width](const char *name, const char *desc, const char *url = nullptr)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImGui::AlignCursor(name, 1.f);
            ImGui::PushFont(font("sans bold", 14));
            ImGui::HyperlinkText(url, name);
            ImGui::PopFont();
            ImGui::TableNextColumn();

            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[1] - HelloImGui::EmSize());
            ImGui::PushFont(font("sans regular", 14));
            ImGui::TextUnformatted(desc);
            ImGui::PopFont();
        };

        if (ImGui::BeginTabBar("AboutTabBar"))
        {
            if (ImGui::BeginTabItem("Keybindings", nullptr))
            {
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[0] + col_width[1]);

                ImGui::PushFont(font("sans bold", 14));
                ImGui::TextAligned("The main keyboard shortcut to remember is:", 0.5f);
                ImGui::PopFont();

                ImGui::PushFont(font("mono regular", 30));
                ImGui::TextAligned(ImGui::GetKeyChordNameTranslated(action("Command palette...").chord), 0.5f);
                ImGui::PopFont();

                ImGui::TextUnformatted("This opens the command palette, which lists every available HDRView command "
                                       "along with its keyboard shortcuts (if any).");
                ImGui::Spacing();

                ImGui::TextUnformatted("Many commands and their keyboard shortcuts are also listed in the menu bar.");

                ImGui::TextUnformatted(
                    "Additonally, left-click+drag will pan the image view, and scrolling the mouse/pinching "
                    "will zoom in and out.");

                ImGui::Spacing();
                ImGui::PopTextWrapPos();

                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Credits"))
            {
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[0] + col_width[1]);
                ImGui::TextUnformatted(
                    "HDRView additionally makes use of the following external libraries and techniques (in "
                    "alphabetical order):\n\n");
                ImGui::PopTextWrapPos();

                if (ImGui::BeginTable("about_table2", 2))
                {
                    ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_WidthFixed, col_width[0]);
                    ImGui::TableSetupColumn("two", ImGuiTableColumnFlags_WidthFixed, col_width[1]);

                    item_and_description("colormaps", "Matt Zucker's degree 6 polynomial colormaps.",
                                         "https://www.shadertoy.com/view/3lBXR3");
                    item_and_description("Dear ImGui", "Omar Cornut's immediate-mode graphical user interface for C++.",
                                         "https://github.com/ocornut/imgui");
#ifdef __EMSCRIPTEN__
                    item_and_description("emscripten", "An MIT-licensed LLVM-to-WebAssembly compiler.",
                                         "https://github.com/emscripten-core/emscripten");
                    item_and_description("emscripten-browser-file",
                                         "Armchair Software's MIT-licensed header-only C++ library "
                                         "to open and save files in the browser.",
                                         "https://github.com/Armchair-Software/emscripten-browser-file");
#endif
                    item_and_description("{fmt}", "A modern formatting library.", "https://github.com/fmtlib/fmt");
                    item_and_description("Hello ImGui", "Pascal Thomet's cross-platform starter-kit for Dear ImGui.",
                                         "https://github.com/pthom/hello_imgui");
                    item_and_description(
                        "linalg", "Sterling Orsten's public domain, single header short vector math library for C++.",
                        "https://github.com/sgorsten/linalg");
                    item_and_description("NanoGUI", "Bits of code from Wenzel Jakob's BSD-licensed NanoGUI library.",
                                         "https://github.com/mitsuba-renderer/nanogui");
                    item_and_description("OpenEXR", "High Dynamic-Range (HDR) image file format.",
                                         "https://github.com/AcademySoftwareFoundation/openexr");
#ifndef __EMSCRIPTEN__
                    item_and_description("portable-file-dialogs",
                                         "Sam Hocevar's WTFPL portable GUI dialogs library, C++11, single-header.",
                                         "https://github.com/samhocevar/portable-file-dialogs");
#endif
                    item_and_description("stb_image/write/resize",
                                         "Single-Header libraries for loading/writing/resizing images.",
                                         "https://github.com/nothings/stb");
                    item_and_description("tev", "Some code is adapted from Thomas Müller's tev.",
                                         "https://github.com/Tom94/tev");
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (ImGui::Button("Dismiss", HelloImGui::EmToVec2(8.f, 0.f)) || ImGui::Shortcut(ImGuiKey_Escape) ||
            ImGui::Shortcut(ImGuiKey_Enter) || ImGui::Shortcut(ImGuiKey_Space) || ImGui::Shortcut(ImGuiKey_H))
        {
            ImGui::CloseCurrentPopup();
            g_show_help = false;
        }

        ImGui::ScrollWhenDraggingOnVoid(ImVec2(0.0f, -ImGui::GetIO().MouseDelta.y), ImGuiMouseButton_Left);
        ImGui::EndPopup();
    }
}
