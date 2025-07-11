/** \file app.cpp
    \author Wojciech Jarosz
*/

#include "app.h"

#include "hello_imgui/dpi_aware.h"
#include "hello_imgui/hello_imgui.h"
#include "imcmd_command_palette.h"
#include "imgui_internal.h"
#include "implot.h"

#include "fonts.h"

#include "colormap.h"

#include "texture.h"

#include "opengl_check.h"

#include "colorspace.h"

#include "json.h"
#include "version.h"

#include <ImfHeader.h>
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

#include <filesystem>
namespace fs = std::filesystem;

#include "emscripten_utils.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten_browser_file.h>
#else
#include "portable-file-dialogs.h"
#endif

#ifdef HELLOIMGUI_USE_SDL2
#include <SDL.h>
#endif

#ifdef HELLOIMGUI_USE_GLFW3
#include <GLFW/glfw3.h>
#ifdef __APPLE__
// on macOS, we need to include this to get the NS api for opening files
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#endif
#endif

using std::to_string;
using std::unique_ptr;

#if defined(__EMSCRIPTEN__)
static float g_scroll_multiplier = 10.0f;
#else
static float g_scroll_multiplier = 1.0f;
#endif

struct WatchedPixel
{
    int2 pixel;
    int3 color_mode{0, 0, 0}; //!< Color mode for current, reference, and composite pixels
};

static vector<WatchedPixel> g_watched_pixels;

static std::mt19937     g_rand(53);
static constexpr float  MIN_ZOOM                              = 0.01f;
static constexpr float  MAX_ZOOM                              = 512.f;
static constexpr size_t g_max_recent                          = 15;
static bool             g_show_help                           = false;
static bool             g_show_command_palette                = false;
static bool             g_show_tweak_window                   = false;
static bool             g_show_demo_window                    = false;
static bool             g_show_debug_window                   = false;
static bool             g_show_bg_color_picker                = false;
static char             g_filter_buffer[256]                  = {0};
static int              g_file_list_mode                      = 1; // 0: images only; 1: list; 2: tree;
static bool             g_request_sort                        = false;
static bool             g_short_names                         = false;
static MouseMode_       g_mouse_mode                          = MouseMode_PanZoom;
static bool             g_mouse_mode_enabled[MouseMode_COUNT] = {true, false, false};

#define g_blank_icon ""

static HDRViewApp *g_hdrview = nullptr;

void init_hdrview(std::optional<float> exposure, std::optional<float> gamma, std::optional<bool> dither,
                  std::optional<bool> force_sdr, std::optional<bool> apple_keys, const vector<string> &in_files)
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
    spdlog::info("Overriding Apple-keyboard behavior: {}", apple_keys.has_value());

    g_hdrview = new HDRViewApp(exposure, gamma, dither, force_sdr, apple_keys, in_files);
}

HDRViewApp *hdrview() { return g_hdrview; }

HDRViewApp::HDRViewApp(std::optional<float> force_exposure, std::optional<float> force_gamma,
                       std::optional<bool> force_dither, std::optional<bool> force_sdr,
                       std::optional<bool> force_apple_keys, vector<string> in_files)
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
    // if there is a screen with a non-retina resolution connected to an otherwise retina mac, the fonts may
    // look blurry. Here we force that macs always use the 2X retina scale factor for fonts. Produces crisp
    // fonts on the retina screen, at the cost of more jagged fonts on screen set to a non-retina resolution.
    m_params.dpiAwareParams.dpiWindowSizeFactor = 1.f;
    // m_params.dpiAwareParams.fontRenderingScale  = 0.5f;
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

    // Setting this to true allows multiple viewports where you can drag windows outside out the main window in
    // order to put their content into new native windows
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
    // HelloImGui will display an empty menu bar, which we can fill with our own menu items via the callback
    // `ShowMenus`
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

    HelloImGui::DockableWindow histogram_window;
    histogram_window.label             = "Histogram";
    histogram_window.dockSpaceName     = "HistogramSpace";
    histogram_window.isVisible         = true;
    histogram_window.rememberIsVisible = true;
    histogram_window.GuiFunction       = [this]
    {
        if (auto img = current_image())
            img->draw_histogram();
    };

    HelloImGui::DockableWindow channel_stats_window;
    channel_stats_window.label             = "Channel statistics";
    channel_stats_window.dockSpaceName     = "HistogramSpace";
    channel_stats_window.isVisible         = true;
    channel_stats_window.rememberIsVisible = true;
    channel_stats_window.GuiFunction       = [this]
    {
        if (auto img = current_image())
            return img->draw_channel_stats();
    };

    HelloImGui::DockableWindow file_window;
    file_window.label             = "Images";
    file_window.dockSpaceName     = "ImagesSpace";
    file_window.isVisible         = true;
    file_window.rememberIsVisible = true;
    file_window.GuiFunction       = [this] { draw_file_window(); };

    HelloImGui::DockableWindow info_window;
    info_window.label             = "Info";
    info_window.dockSpaceName     = "RightSpace";
    info_window.isVisible         = true;
    info_window.rememberIsVisible = true;
    info_window.imGuiWindowFlags  = ImGuiWindowFlags_HorizontalScrollbar;
    info_window.GuiFunction       = [this]
    {
        if (auto img = current_image())
            return img->draw_info();
    };

    HelloImGui::DockableWindow pixel_inspector_window;
    pixel_inspector_window.label             = "Pixel inspector";
    pixel_inspector_window.dockSpaceName     = "RightBottomSpace";
    pixel_inspector_window.isVisible         = true;
    pixel_inspector_window.rememberIsVisible = true;
    pixel_inspector_window.GuiFunction       = [this] { draw_pixel_inspector_window(); };

    HelloImGui::DockableWindow channel_window;
    channel_window.label             = "Channels";
    channel_window.dockSpaceName     = "ImagesSpace";
    channel_window.isVisible         = true;
    channel_window.rememberIsVisible = true;
    channel_window.GuiFunction       = [this]
    {
        if (auto img = current_image())
            img->draw_channels_list(m_reference == m_current, true);
    };

    // the window showing the spdlog messages
    HelloImGui::DockableWindow log_window;
    log_window.label             = "Log";
    log_window.dockSpaceName     = "LogSpace";
    log_window.isVisible         = false;
    log_window.rememberIsVisible = true;
    log_window.GuiFunction       = [this] { ImGui::GlobalSpdLogWindow().draw(font("mono regular"), 14); };

    HelloImGui::DockableWindow advanced_settings_window;
    advanced_settings_window.label             = "Advanced settings";
    advanced_settings_window.dockSpaceName     = "RightSpace";
    advanced_settings_window.isVisible         = false;
    advanced_settings_window.rememberIsVisible = true;
    advanced_settings_window.GuiFunction       = [this]
    {
        // if (ImGui::TreeNode("Clip warnings"))
        {
            ImGui::Checkbox("##Draw clip warning", &m_draw_clip_warnings);
            ImGui::SameLine();
            ImGui::PushItemWidth(-5 * HelloImGui::EmSize());
            ImGui::BeginDisabled(!m_draw_clip_warnings);
            ImGui::DragFloatRange2("Clip warning", &m_clip_range.x, &m_clip_range.y, 0.01f, 0.f, 0.f, "min: %.1f",
                                   "max: %.1f");
            ImGui::EndDisabled();
            ImGui::PopItemWidth();
            // ImGui::TreePop();
        }
    };

#ifdef _WIN32
    ImGuiKey modKey = ImGuiMod_Alt;
#else
    ImGuiKey modKey = ImGuiMod_Super;
#endif

    // docking layouts
    m_params.dockingParams.layoutName      = "Standard";
    m_params.dockingParams.dockableWindows = {histogram_window, channel_stats_window,    file_window,
                                              info_window,      pixel_inspector_window,  channel_window,
                                              log_window,       advanced_settings_window};
    vector<ImGuiKeyChord> window_keychords = {
        ImGuiKey_F5, ImGuiKey_F6, ImGuiKey_F7, ImGuiKey_F8, ImGuiKey_F9, ImGuiKey_F10, modKey | ImGuiKey_GraveAccent,
        ImGuiKey_F11};
    m_params.dockingParams.dockingSplits = {
        HelloImGui::DockingSplit{"MainDockSpace", "HistogramSpace", ImGuiDir_Left, 0.2f},
        HelloImGui::DockingSplit{"HistogramSpace", "ImagesSpace", ImGuiDir_Down, 0.75f},
        HelloImGui::DockingSplit{"MainDockSpace", "LogSpace", ImGuiDir_Down, 0.25f},
        HelloImGui::DockingSplit{"MainDockSpace", "RightSpace", ImGuiDir_Right, 0.25f},
        HelloImGui::DockingSplit{"RightSpace", "RightBottomSpace", ImGuiDir_Down, 0.5f}};

#if defined(HELLOIMGUI_USE_GLFW3)
    m_params.callbacks.PostInit_AddPlatformBackendCallbacks = [this]
    {
        spdlog::trace("Registering glfw drop callback");
        // spdlog::trace("m_params.backendPointers.glfwWindow: {}", m_params.backendPointers.glfwWindow);
        glfwSetDropCallback((GLFWwindow *)m_params.backendPointers.glfwWindow,
                            [](GLFWwindow *, int count, const char **filenames)
                            {
                                spdlog::debug("Received glfw drop event");
                                vector<string> arg(count);
                                for (int i = 0; i < count; ++i) arg[i] = filenames[i];
                                hdrview()->load_images(arg);
                            });
#ifdef __APPLE__
        // On macOS, the mechanism for opening an application passes filenames
        // through the NS api rather than CLI arguments, which means we need
        // special handling of these through GLFW.
        // There are two components to this special handling:
        // (both of which need to happen here instead of HDRViewApp() because GLFW needs to have been
        // initialized first)

        // 1) Check if any filenames were passed via the NS api when the first instance of HDRView is launched.
        const char *const *opened_files = glfwGetCocoaOpenedFilenames();
        if (opened_files)
        {
            spdlog::debug("Passing files in through the NS api...");
            vector<string> args;
            for (auto p = opened_files; *p; ++p) { args.emplace_back(string(*p)); }
            load_images(args);
        }

        // 2) Register a callback on the running instance of HDRView for when the user:
        //    a) drags a file onto the HDRView app icon in the dock, and/or
        //    b) launches HDRView with files (either from the command line or Finder) when another instance is
        //    already
        //       running
        glfwSetCocoaOpenedFilenamesCallback(
            [](const char *image_file)
            {
                spdlog::debug("Receiving an app drag-drop event through the NS api for file '{}'", image_file);
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
    m_params.callbacks.PostInit = [this, force_exposure, force_gamma, force_dither, force_apple_keys]
    {
        setup_imgui_clipboard();
        load_settings();
        setup_rendering();

        if (force_exposure.has_value())
            m_exposure_live = m_exposure = *force_exposure;
        if (force_gamma.has_value())
            m_gamma_live = m_gamma = *force_gamma;
        if (force_dither.has_value())
            m_dither = *force_dither;

        auto is_safari = host_is_safari();
        auto is_apple  = host_is_apple();
        spdlog::info("Host is Apple: {}", is_apple);
        spdlog::info("Running in Safari: {}", is_safari);

        ImGui::GetIO().ConfigMacOSXBehaviors = is_apple;
        if (force_apple_keys.has_value())
            ImGui::GetIO().ConfigMacOSXBehaviors = *force_apple_keys;

        spdlog::info("Using {}-style keyboard behavior",
                     ImGui::GetIO().ConfigMacOSXBehaviors ? "Apple" : "Windows/Linux");
    };
    m_params.callbacks.BeforeExit = [this]
    {
        Image::cleanup_default_textures();
        Colormap::cleanup();
        save_settings();
    };

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
        colors[ImGuiCol_TextLink]                  = ImVec4(0.30f, 0.58f, 1.00f, 1.00f);
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

        // popup version of the below; commented out because it doesn't allow right-clicking to change the color
        // picker type
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

        if (g_show_demo_window)
            ImGui::ShowDemoWindow(&g_show_demo_window);

        if (g_show_debug_window)
        {
            ImGui::SetNextWindowSize(HelloImGui::EmToVec2(20.f, 46.f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Debug", &g_show_debug_window))
            {
                // ImGui::Text("Hello ImGui version: %s", HelloImGui::VersionString().c_str());
                ImGui::Text("ImGui version: %s", ImGui::GetVersion());
                // ImGui::Text("Renderer backend: %s", m_params.rendererBackendOptions.backendName.c_str());
                // ImGui::Text("DPI aware: %s", m_params.dpiAwareParams.isDpiAware ? "yes" : "no");
                ImGui::Text("EDR support: %s", HelloImGui::hasEdrSupport() ? "yes" : "no");
                // ImGui::Text("HDRView version: %s", HDRVIEW_VERSION_STRING);

                if (ImPlot::BeginPlot("Line Plots"))
                {
                    ImPlot::SetupAxes("x", "sRGB(x)");

                    auto f = [](float x) { return EOTF_BT2100_HLG(x); };
                    auto g = [](float y) { return OETF_BT2100_HLG(y); };

                    const int    N = 101;
                    static float xs1[N], ys1[N];
                    for (int i = 0; i < N; ++i)
                    {
                        xs1[i] = i / float(N - 1);
                        ys1[i] = f(xs1[i]);
                    }
                    static float xs2[N], ys2[N];
                    for (int i = 0; i < N; ++i)
                    {
                        ys2[i] = lerp(0.0f, ys1[N - 1], i / float(N - 1));
                        xs2[i] = g(ys2[i]);
                    }

                    ImPlot::PlotLine("x, f(x)", xs1, ys1, N);
                    ImPlot::PlotLine("f^{-1}(y), y", xs2, ys2, N);
                    ImPlot::EndPlot();
                }
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
        auto       add_action     = [this](const ImGui::Action &a)
        {
            m_action_map[a.name] = m_actions.size();
            m_actions.push_back(a);
        };
        add_action({"Open image...", ICON_MY_OPEN_IMAGE, ImGuiMod_Ctrl | ImGuiKey_O, 0, [this]() { open_image(); }});

#if !defined(__EMSCRIPTEN__)
        add_action({"Open folder...", ICON_MY_OPEN_IMAGE, ImGuiKey_None, 0, [this]() { open_folder(); }});
#endif

#if defined(__EMSCRIPTEN__)
        add_action({"Open URL...", ICON_MY_OPEN_IMAGE, ImGuiKey_None, 0,
                    [this]()
                    {
                        string url;
                        if (ImGui::InputTextWithHint("##URL", "Enter an image URL and press <return>", &url,
                                                     ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            ImGui::CloseCurrentPopup();
                            load_url(url);
                        }
                    },
                    always_enabled, true});
#endif

        add_action(
            {"Help", ICON_MY_ABOUT, ImGuiMod_Shift | ImGuiKey_Slash, 0, []() {}, always_enabled, false, &g_show_help});
        add_action({"Quit", ICON_MY_QUIT, ImGuiMod_Ctrl | ImGuiKey_Q, 0, [this]() { m_params.appShallExit = true; }});

        add_action({"Command palette...", ICON_MY_COMMAND_PALETTE, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, 0,
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
        add_action({"Show FPS in status bar", ICON_MY_FPS, 0, 0, []() {}, always_enabled, false,
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

        add_action({"Hide entire GUI", ICON_MY_HIDE_GUI, ImGuiMod_Shift | ImGuiKey_Tab, 0,
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
            add_action({fmt::format("Show {} window", w.label).c_str(), g_blank_icon, window_keychords[i], 0, []() {},
                        [&w]() { return w.canBeClosed; }, false, &w.isVisible});
        }

        add_action({"Decrease exposure", ICON_MY_REDUCE_EXPOSURE, ImGuiKey_E, ImGuiInputFlags_Repeat,
                    [this]() { m_exposure_live = m_exposure -= 0.25f; }});
        add_action({"Increase exposure", ICON_MY_INCREASE_EXPOSURE, ImGuiMod_Shift | ImGuiKey_E, ImGuiInputFlags_Repeat,
                    [this]() { m_exposure_live = m_exposure += 0.25f; }});
        add_action({"Reset tonemapping", ICON_MY_RESET_TONEMAPPING, 0, 0, [this]()
                    {
                        m_exposure_live = m_exposure = 0.0f;
                        m_gamma_live = m_gamma = 1.0f;
                        m_tonemap              = Tonemap_Gamma;
                    }});
        add_action({"Normalize exposure", ICON_MY_NORMALIZE_EXPOSURE, ImGuiKey_N, 0, [this]()
                    {
                        if (auto img = current_image())
                        {
                            float m     = 0.f;
                            auto &group = img->groups[img->selected_group];

                            bool3 should_include[NUM_CHANNELS] = {
                                {true, true, true},   // RGB
                                {true, false, false}, // RED
                                {false, true, false}, // GREEN
                                {false, false, true}, // BLUE
                                {true, true, true},   // ALPHA
                                {true, true, true}    // Y
                            };
                            for (int c = 0; c < std::min(group.num_channels, 3); ++c)
                            {
                                if (group.num_channels >= 3 && !should_include[m_channel][c])
                                    continue;
                                m = std::max(m, img->channels[group.channels[c]].get_stats()->summary.maximum);
                            }

                            m_exposure_live = m_exposure = log2(1.f / m);
                        }
                    }});
        if (m_params.rendererBackendOptions.requestFloatBuffer)
            add_action({"Clamp to LDR", ICON_MY_CLAMP_TO_LDR, ImGuiMod_Ctrl | ImGuiKey_L, 0, []() {}, always_enabled,
                        false, &m_clamp_to_LDR});
        add_action({"Dither", ICON_MY_DITHER, 0, 0, []() {}, always_enabled, false, &m_dither});

        add_action({"Draw pixel grid", ICON_MY_SHOW_GRID, ImGuiMod_Ctrl | ImGuiKey_G, 0, []() {}, always_enabled, false,
                    &m_draw_grid});
        add_action({"Draw pixel values", ICON_MY_SHOW_PIXEL_VALUES, ImGuiMod_Ctrl | ImGuiKey_P, 0, []() {},
                    always_enabled, false, &m_draw_pixel_info});

        add_action({"Draw data window", ICON_MY_DATA_WINDOW, ImGuiKey_None, 0, []() {}, always_enabled, false,
                    &m_draw_data_window});
        add_action({"Draw display window", ICON_MY_DISPLAY_WINDOW, ImGuiKey_None, 0, []() {}, always_enabled, false,
                    &m_draw_display_window});

        add_action({"Decrease gamma/Previous colormap", g_blank_icon, ImGuiKey_G, ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        switch (m_tonemap)
                        {
                        default: [[fallthrough]];
                        case Tonemap_Gamma: m_gamma_live = m_gamma = std::max(0.02f, m_gamma - 0.02f); break;
                        case Tonemap_FalseColor: [[fallthrough]];
                        case Tonemap_PositiveNegative:
                            m_colormap_index = mod(m_colormap_index - 1, (int)m_colormaps.size());
                            break;
                        }
                    },
                    always_enabled});
        add_action({"Increase gamma/Next colormap", g_blank_icon, ImGuiMod_Shift | ImGuiKey_G, ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        switch (m_tonemap)
                        {
                        default: [[fallthrough]];
                        case Tonemap_Gamma: m_gamma_live = m_gamma = std::max(0.02f, m_gamma + 0.02f); break;
                        case Tonemap_FalseColor: [[fallthrough]];
                        case Tonemap_PositiveNegative:
                            m_colormap_index = mod(m_colormap_index + 1, (int)m_colormaps.size());
                            break;
                        }
                    },
                    always_enabled});

        add_action({"Pan and zoom", ICON_MY_PAN_ZOOM_TOOL, ImGuiKey_Space, 0,
                    []()
                    {
                        for (int i = 0; i < MouseMode_COUNT; ++i) g_mouse_mode_enabled[i] = false;
                        g_mouse_mode                            = MouseMode_PanZoom;
                        g_mouse_mode_enabled[MouseMode_PanZoom] = true;
                    },
                    always_enabled, false, &g_mouse_mode_enabled[MouseMode_PanZoom]});
        add_action({"Rectangular select", ICON_MY_SELECT, ImGuiKey_M, 0,
                    []()
                    {
                        for (int i = 0; i < MouseMode_COUNT; ++i) g_mouse_mode_enabled[i] = false;
                        g_mouse_mode                                         = MouseMode_RectangularSelection;
                        g_mouse_mode_enabled[MouseMode_RectangularSelection] = true;
                    },
                    always_enabled, false, &g_mouse_mode_enabled[MouseMode_RectangularSelection]});
        add_action({"Pixel/color inspector", ICON_MY_WATCHED_PIXEL, ImGuiKey_I, 0,
                    []()
                    {
                        for (int i = 0; i < MouseMode_COUNT; ++i) g_mouse_mode_enabled[i] = false;
                        g_mouse_mode                                   = MouseMode_ColorInspector;
                        g_mouse_mode_enabled[MouseMode_ColorInspector] = true;
                    },
                    always_enabled, false, &g_mouse_mode_enabled[MouseMode_ColorInspector]});

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
        add_action({"Export image as...", ICON_MY_SAVE_AS, ImGuiKey_None, 0,
                    [this]()
                    {
                        string filename =
                            pfd::save_file("Export image as", g_blank_icon,
                                           {
                                               "Supported image files",
                                               fmt::format("*.{}", fmt::join(Image::savable_formats(), "*.")),
                                           })
                                .result();

                        if (!filename.empty())
                            export_as(filename);
                    },
                    if_img});

#else
        add_action({"Save as...", ICON_MY_SAVE_AS, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, 0,
                    [this]()
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
                    },
                    if_img, true});
        add_action({"Export image as...", ICON_MY_SAVE_AS, ImGuiKey_None, 0,
                    [this]()
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
                                export_as(filename);
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

        // switch the selected channel group using Ctrl + number key (one-based indexing)
        for (int n = 1; n <= 10; ++n)
            add_action({fmt::format("Go to channel group {}", n), ICON_MY_CHANNEL_GROUP,
                        modKey | ImGuiKey(ImGuiKey_0 + mod(n, 10)), 0,
                        [this, n]()
                        {
                            auto img            = current_image();
                            img->selected_group = img->nth_visible_group_index(mod(n - 1, 10));
                        },
                        [this, n]()
                        {
                            if (auto img = current_image())
                            {
                                auto i = img->nth_visible_group_index(mod(n - 1, 10));
                                return img->is_valid_group(i) && i != img->selected_group;
                            }
                            return false;
                        }});
        // switch the reference channel group using Shift + Ctrl + number key (one-based indexing)
        for (int n = 1; n <= 10; ++n)
            add_action({fmt::format("Set channel group {} as reference", n), ICON_MY_REFERENCE_IMAGE,
                        ImGuiMod_Shift | modKey | ImGuiKey(ImGuiKey_0 + mod(n, 10)), 0,
                        [this, n]()
                        {
                            auto img         = current_image();
                            auto nth_visible = img->nth_visible_group_index(mod(n - 1, 10));
                            if (img->reference_group == nth_visible)
                            {
                                img->reference_group = -1;
                                m_reference          = -1;
                            }
                            else
                            {
                                img->reference_group = nth_visible;
                                m_reference          = m_current;
                            }
                        },
                        [this, n]()
                        {
                            if (auto img = current_image())
                            {
                                auto i = img->nth_visible_group_index(mod(n - 1, 10));
                                return img->is_valid_group(i);
                            }
                            return false;
                        }});

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
        add_action({"Make next image the reference", g_blank_icon, ImGuiMod_Shift | ImGuiKey_DownArrow,
                    ImGuiInputFlags_Repeat,
                    [this]() { set_reference_image_index(next_visible_image_index(m_reference, Forward)); },
                    [this]()
                    {
                        auto i = next_visible_image_index(m_reference, Forward);
                        return is_valid(i) && i != m_reference;
                    }});
        add_action({"Make previous image the reference", g_blank_icon, ImGuiMod_Shift | ImGuiKey_UpArrow,
                    ImGuiInputFlags_Repeat,
                    [this]() { set_reference_image_index(next_visible_image_index(m_reference, Backward)); },
                    [this]()
                    {
                        auto i = next_visible_image_index(m_reference, Backward);
                        return is_valid(i) && i != m_reference;
                    }});
        add_action({"Go to next channel group", g_blank_icon, ImGuiKey_RightArrow, ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        auto img            = current_image();
                        img->selected_group = img->next_visible_group_index(img->selected_group, Forward);
                    },
                    [this]() { return current_image() != nullptr; }});
        add_action({"Go to previous channel group", g_blank_icon, ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        auto img            = current_image();
                        img->selected_group = img->next_visible_group_index(img->selected_group, Backward);
                    },
                    [this]() { return current_image() != nullptr; }});
        add_action({"Go to next channel group in reference", g_blank_icon, ImGuiMod_Shift | ImGuiKey_RightArrow,
                    ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        // if no reference image is selected, use the current image
                        if (!reference_image())
                            m_reference = m_current;
                        auto img             = reference_image();
                        img->reference_group = img->next_visible_group_index(img->reference_group, Forward);
                    },
                    [this]() { return reference_image() || current_image(); }});
        add_action({"Go to previous channel group in reference", g_blank_icon, ImGuiMod_Shift | ImGuiKey_LeftArrow,
                    ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        // if no reference image is selected, use the current image
                        if (!reference_image())
                            m_reference = m_current;
                        auto img             = reference_image();
                        img->reference_group = img->next_visible_group_index(img->reference_group, Backward);
                    },
                    [this]() { return reference_image() || current_image(); }});

        add_action({"Zoom out", ICON_MY_ZOOM_OUT, ImGuiKey_Minus, ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        zoom_out();
                        cancel_autofit();
                    },
                    if_img});
        add_action({"Zoom in", ICON_MY_ZOOM_IN, ImGuiKey_Equal, ImGuiInputFlags_Repeat,
                    [this]()
                    {
                        zoom_in();
                        cancel_autofit();
                    },
                    if_img});
        add_action({"100%", ICON_MY_ZOOM_100, 0, 0,
                    [this]()
                    {
                        set_zoom_level(0.f);
                        cancel_autofit();
                    },
                    if_img});
        add_action({"Center", ICON_MY_CENTER, ImGuiKey_C, 0,
                    [this]()
                    {
                        center();
                        cancel_autofit();
                    },
                    if_img});
        add_action({"Fit display window", ICON_MY_FIT_TO_WINDOW, ImGuiKey_F, 0,
                    [this]()
                    {
                        fit_display_window();
                        cancel_autofit();
                    },
                    if_img});
        add_action({"Auto fit display window", ICON_MY_FIT_TO_WINDOW, ImGuiMod_Shift | ImGuiKey_F, 0,
                    [this]() { m_auto_fit_selection = m_auto_fit_data = false; }, if_img, false, &m_auto_fit_display});
        add_action({"Fit data window", ICON_MY_FIT_TO_WINDOW, ImGuiMod_Alt | ImGuiKey_F, 0,
                    [this]()
                    {
                        fit_data_window();
                        cancel_autofit();
                    },
                    if_img});
        add_action({"Auto fit data window", ICON_MY_FIT_TO_WINDOW, ImGuiMod_Shift | ImGuiMod_Alt | ImGuiKey_F, 0,
                    [this]() { m_auto_fit_selection = m_auto_fit_display = false; }, if_img, false, &m_auto_fit_data});
        add_action({"Fit selection", ICON_MY_FIT_TO_WINDOW, ImGuiKey_None, 0,
                    [this]()
                    {
                        fit_selection();
                        cancel_autofit();
                    },
                    [if_img, this]() { return if_img() && m_roi.has_volume(); }});
        add_action({"Auto fit selection", ICON_MY_FIT_TO_WINDOW, ImGuiKey_None, 0,
                    [this]() { m_auto_fit_display = m_auto_fit_data = false; },
                    [if_img, this]() { return if_img() && m_roi.has_volume(); }, false, &m_auto_fit_selection});
        add_action({"Flip horizontally", ICON_MY_FLIP_HORIZ, ImGuiKey_H, 0, []() {}, if_img, false, &m_flip.x});
        add_action({"Flip vertically", ICON_MY_FLIP_VERT, ImGuiKey_V, 0, []() {}, if_img, false, &m_flip.y});
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

        m_shader = new Shader(
            m_render_pass,
            /* An identifying name */
            "ImageView", Shader::from_asset("shaders/image-shader_vert"),
            Shader::prepend_includes(Shader::from_asset("shaders/image-shader_frag"), {"shaders/colorspaces"}),
            Shader::BlendMode::AlphaBlend);

        const float positions[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, -1.f, 1.f, 1.f, -1.f, 1.f};

        m_shader->set_buffer("position", VariableType::Float32, {6, 2}, positions);
        m_render_pass->set_cull_mode(RenderPass::CullMode::Disabled);

        Image::make_default_textures();
        Colormap::initialize();

        m_shader->set_texture("dither_texture", Image::dither_texture());
        set_image_textures();
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
        m_auto_fit_selection  = j.value<bool>("auto fit selection", m_auto_fit_selection);
        m_draw_pixel_info     = j.value<bool>("draw pixel info", m_draw_pixel_info);
        m_draw_grid           = j.value<bool>("draw pixel grid", m_draw_grid);
        m_exposure_live = m_exposure = j.value<float>("exposure", m_exposure);
        m_gamma_live = m_gamma = j.value<float>("gamma", m_gamma);
        m_tonemap              = j.value<Tonemap>("tonemap", m_tonemap);
        m_clamp_to_LDR         = j.value<bool>("clamp to LDR", m_clamp_to_LDR);
        m_dither               = j.value<bool>("dither", m_dither);
        g_file_list_mode       = j.value<int>("file list mode", g_file_list_mode);
        g_short_names          = j.value<bool>("short names", g_short_names);
        m_draw_clip_warnings   = j.value<bool>("draw clip warnings", m_draw_clip_warnings);
        m_clip_range           = j.value<float2>("clip range", m_clip_range);
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
    j["auto fit selection"]      = m_auto_fit_selection;
    j["draw pixel info"]         = m_draw_pixel_info;
    j["draw pixel grid"]         = m_draw_grid;
    j["exposure"]                = m_exposure;
    j["gamma"]                   = m_gamma;
    j["tonemap"]                 = m_tonemap;
    j["clamp to LDR"]            = m_clamp_to_LDR;
    j["dither"]                  = m_dither;
    j["verbosity"]               = spdlog::get_level();
    j["file list mode"]          = g_file_list_mode;
    j["short names"]             = g_short_names;
    j["draw clip warnings"]      = m_draw_clip_warnings;
    j["clip range"]              = m_clip_range;
    HelloImGui::SaveUserPref("UserSettings", j.dump(4));
}

void HDRViewApp::draw_status_bar()
{
    const float y = ImGui::GetCursorPosY() - HelloImGui::EmSize(0.15f);
    if (m_pending_images.size())
    {
        ImGui::SetCursorPos({ImGui::GetStyle().ItemSpacing.x, y});
        ImGui::ProgressBar(
            -1.0f * (float)ImGui::GetTime(), HelloImGui::EmToVec2(15.f, 0.f),
            fmt::format("Loading {} image{}", m_pending_images.size(), m_pending_images.size() > 1 ? "s" : "").c_str());
        ImGui::SameLine();
    }
    else if (m_remaining_download > 0)
    {
        ImGui::SetCursorPos({ImGui::GetStyle().ItemSpacing.x, y});
        ImGui::ProgressBar((100 - m_remaining_download) / 100.f, HelloImGui::EmToVec2(15.f, 0.f), "Downloading image");
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
        auto &io  = ImGui::GetIO();
        auto  ref = reference_image();
        if (vp_pos_in_viewport(vp_pos_at_app_pos(io.MousePos)))
        {
            auto hovered_pixel = int2{pixel_at_app_pos(io.MousePos)};

            float4 top   = img->raw_pixel(hovered_pixel);
            float4 pixel = top;
            if (ref && ref->data_window.contains(hovered_pixel))
            {
                float4 bottom = ref->raw_pixel(hovered_pixel);
                // blend with reference image if available
                pixel = float4{blend(top.x, bottom.x, m_blend_mode), blend(top.y, bottom.y, m_blend_mode),
                               blend(top.z, bottom.z, m_blend_mode), blend(top.w, bottom.w, m_blend_mode)};
            }

            auto &group = img->groups[img->selected_group];

            sized_text(3, fmt::format("({:>6d},", hovered_pixel.x), 0.f);
            sized_text(3, fmt::format("{:>6d})", hovered_pixel.y), 0.f);

            if (img->contains(hovered_pixel))
            {
                sized_text(0.5f, "=", 0.5f);
                for (int c = 0; c < group.num_channels; ++c)
                    sized_text(3.5f, fmt::format("{}{: 6.3f}{}", c == 0 ? "(" : "", pixel[c],
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
        MenuItem(action("Open folder..."));
#endif

#if defined(__EMSCRIPTEN__)
        MenuItem(action("Open URL..."));
#else

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
        MenuItem(action("Export image as..."));

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
        MenuItem(action("Fit selection"));
        MenuItem(action("Auto fit selection"));
        MenuItem(action("Flip horizontally"));
        MenuItem(action("Flip vertically"));

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

        MenuItem(action("Increase gamma/Next colormap"));
        MenuItem(action("Decrease gamma/Previous colormap"));

        ImGui::Separator();

        MenuItem(action("Reset tonemapping"));
        if (m_params.rendererBackendOptions.requestFloatBuffer)
            MenuItem(action("Clamp to LDR"));
        MenuItem(action("Dither"));

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools"))
    {
        MenuItem(action("Pan and zoom"));
        MenuItem(action("Rectangular select"));
        MenuItem(action("Pixel/color inspector"));

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

    if (ImGui::BeginMenu("Developer"))
    {
        ImGui::MenuItem("Dear ImGui demo window", NULL, &g_show_demo_window);
        ImGui::MenuItem("Debug window", NULL, &g_show_debug_window);

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
        current_image()->save(os, filename, powf(2.0f, m_exposure_live), true, m_dither);
#else
        std::ostringstream os;
        current_image()->save(os, filename, powf(2.0f, m_exposure_live), true, m_dither);
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

void HDRViewApp::export_as(const string &filename) const
{
    try
    {
        Image img(current_image()->size(), 4);
        img.finalize();
        auto bounds     = current_image()->data_window;
        int  block_size = std::max(1, 1024 * 1024 / img.size().x);
        parallel_for(blocked_range<int>(0, img.size().y, block_size),
                     [this, &img, bounds](int begin_y, int end_y, int, int)
                     {
                         for (int y = begin_y; y < end_y; ++y)
                             for (int x = 0; x < img.size().x; ++x)
                             {
                                 float4 v = pixel_value(int2{x, y} + bounds.min, false, 2);

                                 img.channels[0](x, y) = v[0];
                                 img.channels[1](x, y) = v[1];
                                 img.channels[2](x, y) = v[2];
                                 img.channels[3](x, y) = v[3];
                             }
                     });

#if !defined(__EMSCRIPTEN__)
        std::ofstream os{filename, std::ios_base::binary};
        img.save(os, filename, 1.f, true, m_dither);
#else
        std::ostringstream os;
        img.save(os, filename, 1.f, true, m_dither);
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
        spdlog::error("An error occurred while exporting to '{}':\n\t{}.", filename, e.what());
    }
    catch (...)
    {
        spdlog::error("An unknown error occurred while exporting to '{}'.", filename);
    }
}

void HDRViewApp::load_images(const vector<string> &filenames)
{
    for (auto f : filenames) load_image(f);
}

void HDRViewApp::open_image()
{
#if defined(__EMSCRIPTEN__)

    // due to this bug, we just allow all file types on safari:
    // https://stackoverflow.com/questions/72013027/safari-cannot-upload-file-w-unknown-mime-type-shows-tempimage,
    string extensions =
        host_is_safari() ? "*" : fmt::format(".{}", fmt::join(Image::loadable_formats(), ",.")) + ",image/*";

    // open the browser's file selector, and pass the file to the upload handler
    spdlog::debug("Requesting file from user...");
    emscripten_browser_file::upload(
        extensions,
        [](const string &filename, const string &mime_type, string_view buffer, void *my_data = nullptr)
        {
            if (buffer.empty())
                spdlog::debug("User canceled upload.");
            else
            {
                auto [size, unit] = human_readable_size(buffer.size());
                spdlog::debug("User uploaded a {:.0f} {} file with filename '{}' of mime-type '{}'", size, unit,
                              filename, mime_type);
                hdrview()->load_image(filename, buffer);
            }
        });
#else
    string extensions = fmt::format("*.{}", fmt::join(Image::loadable_formats(), " *."));

    load_images(pfd::open_file("Open image(s)", "", {"Image files", extensions}, pfd::opt::multiselect).result());
#endif
}

void HDRViewApp::open_folder()
{
#if !defined(__EMSCRIPTEN__)
    load_images({pfd::select_folder("Open images in folder", "").result()});
#endif
}

// Note: the filename is passed by value in case its an element of m_recent_files, which we modify
void HDRViewApp::load_image(const string filename, const string_view buffer)
{
    auto load_one = [this](const string &filename, const string_view buffer, bool add_to_recent)
    {
        try
        {
            spdlog::info("Loading file '{}'...", filename);
            // convert the buffer (if any) to a string so the async thread has its own copy,
            // then load from the string or filename depending on whether the buffer is empty
            m_pending_images.emplace_back(std::make_shared<PendingImages>(
                filename,
                [buffer_str = string(buffer), filename]()
                {
                    if (buffer_str.empty())
                    {
                        auto u8p = std::filesystem::u8path(filename.c_str());
                        if (std::ifstream is{u8p, std::ios_base::binary})
                            return Image::load(is, filename);
                        else
                        {
                            spdlog::error("File '{}' doesn't exist.", u8p.string());
                            return vector<ImagePtr>{};
                        }
                    }
                    else
                    {
                        std::istringstream is{buffer_str};
                        return Image::load(is, filename);
                    }
                },
                add_to_recent));
        }
        catch (const std::exception &e)
        {
            spdlog::error("Could not load image \"{}\": {}.", filename, e.what());
            return;
        }
    };

    auto path = fs::path(filename);
    if (fs::is_directory(path))
    {
        spdlog::info("Loading images from folder '{}'", filename);

        std::error_code ec;
        auto            canon_p = fs::canonical(path);
        for (auto const &entry : fs::directory_iterator{canon_p, ec})
        {
            if (!entry.is_directory())
                load_one(entry.path().u8string(), buffer, false);
        }
        m_recent_files.erase(std::remove(m_recent_files.begin(), m_recent_files.end(), filename), m_recent_files.end());
        add_recent_file(filename);
    }
    else if (!buffer.empty())
    {
        // if we have a buffer, we assume it is a file that has been downloaded
        // and we load it directly from the buffer
        spdlog::info("Loading image from buffer with size {} bytes", buffer.size());
        load_one(filename, buffer, true);
    }
    else if (fs::exists(path) && fs::is_regular_file(path))
    {
        // remove any instances of filename from the recent files list until we know it has loaded successfully
        m_recent_files.erase(std::remove(m_recent_files.begin(), m_recent_files.end(), filename), m_recent_files.end());
        load_one(filename, buffer, true);
    }
    else if (!fs::exists(path))
        spdlog::error("File '{}' does not exist.", filename);
}

void HDRViewApp::load_url(const string_view url)
{
    if (url.empty())
        return;

#if !defined(__EMSCRIPTEN__)
    spdlog::error("load_url only supported via emscripten");
#else
    spdlog::info("Entered URL: {}", url);

    struct Payload
    {
        string      url;
        HDRViewApp *hdrview;
    };
    auto data = new Payload{string(url), this};

    m_remaining_download = 100;
    emscripten_async_wget2_data(
        data->url.c_str(), "GET", nullptr, data, true,
        (em_async_wget2_data_onload_func)[](unsigned, void *data, void *buffer, unsigned buffer_size) {
            auto   payload = reinterpret_cast<Payload *>(data);
            string url     = payload->url; // copy the url
            delete payload;

            auto filename    = get_filename(url);
            auto char_buffer = reinterpret_cast<const char *>(buffer);
            spdlog::info("Downloaded file '{}' with size {} from url '{}'", filename, buffer_size, url);
            hdrview()->load_image(url, {char_buffer, (size_t)buffer_size});
        },
        (em_async_wget2_data_onerror_func)[](unsigned, void *data, int err, const char *desc) {
            auto   payload                         = reinterpret_cast<Payload *>(data);
            string url                             = payload->url; // copy the url
            payload->hdrview->m_remaining_download = 0;
            delete payload;

            spdlog::error("Downloading the file '{}' failed; {}: '{}'.", url, err, desc);
        },
        (em_async_wget2_data_onprogress_func)[](unsigned, void *data, int bytes_loaded, int total_bytes) {
            auto payload = reinterpret_cast<Payload *>(data);

            payload->hdrview->m_remaining_download = (total_bytes - bytes_loaded) / total_bytes;
        });

    // emscripten_async_wget_data(
    //     data->url.c_str(), data,
    //     (em_async_wget_onload_func)[](void *data, void *buffer, int buffer_size) {
    //         auto   payload = reinterpret_cast<Payload *>(data);
    //         string url     = payload->url; // copy the url
    //         delete payload;

    //         auto filename    = get_filename(url);
    //         auto char_buffer = reinterpret_cast<const char *>(buffer);
    //         spdlog::info("Downloaded file '{}' with size {} from url '{}'", filename, buffer_size, url);
    //         hdrview()->load_image(url, {char_buffer, (size_t)buffer_size});
    //     },
    //     (em_arg_callback_func)[](void *data) {
    //         auto   payload = reinterpret_cast<Payload *>(data);
    //         string url     = payload->url; // copy the url
    //         delete payload;

    //         spdlog::error("Downloading the file '{}' failed.", url);
    //     });
#endif
}

void HDRViewApp::add_recent_file(const string &f)
{
    m_recent_files.push_back(f);
    if (m_recent_files.size() > g_max_recent)
        m_recent_files.erase(m_recent_files.begin(), m_recent_files.end() - g_max_recent);
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
            if (p->add_to_recent)
                add_recent_file(p->filename);

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

        update_visibility(); // this also calls set_image_textures();
        g_request_sort = true;
    }
}

void HDRViewApp::set_image_textures()
{
    try
    {
        // bind the primary and secondary images, or a placehold black texture when we have no current or
        // reference image
        if (auto img = current_image())
            img->set_as_texture(Target_Primary);
        else
            Image::set_null_texture(Target_Primary);

        if (auto ref = reference_image())
            ref->set_as_texture(Target_Secondary);
        else
            Image::set_null_texture(Target_Secondary);
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

    update_visibility(); // this also calls set_image_textures();
}

void HDRViewApp::close_all_images()
{
    m_images.clear();
    m_current   = -1;
    m_reference = -1;
    update_visibility(); // this also calls set_image_textures();
}

void HDRViewApp::run()
{
    ImPlot::CreateContext();
    HelloImGui::Run(m_params);
    ImPlot::DestroyContext();
}

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
        throw std::runtime_error(fmt::format("Font with name '{}' was not loaded.", name));
}

HDRViewApp::~HDRViewApp() {}

float4 HDRViewApp::pixel_value(int2 p, bool raw, int which_image) const
{
    auto img1 = current_image();
    auto img2 = reference_image();

    float4 value;

    if (which_image == 0)
        value = img1 ? (raw ? img1->raw_pixel(p, Target_Primary) : img1->rgba_pixel(p, Target_Primary)) : float4{0.f};
    else if (which_image == 1)
        value =
            img2 ? (raw ? img2->raw_pixel(p, Target_Secondary) : img2->rgba_pixel(p, Target_Secondary)) : float4{0.f};
    else if (which_image == 2)
    {
        auto rgba1 = img1 ? img1->rgba_pixel(p, Target_Primary) : float4{0.f};
        auto rgba2 = img2 ? img2->rgba_pixel(p, Target_Secondary) : float4{0.f};
        value      = blend(rgba1, rgba2, m_blend_mode);
    }

    return raw ? value
               : ::tonemap(float4{powf(2.f, m_exposure_live) * value.xyz(), value.w}, m_gamma_live, m_tonemap,
                           m_colormaps[m_colormap_index]);
}

static void pixel_color_widget(const int2 &pixel, int &color_mode, int which_image, bool allow_copy = false)
{
    float4   color32         = hdrview()->pixel_value(pixel, true, which_image);
    float4   displayed_color = LinearToSRGB(hdrview()->pixel_value(pixel, false, which_image));
    uint32_t hex             = color_f128_to_u32(color_u32_to_f128(color_f128_to_u32(displayed_color)));
    int4     ldr_color       = int4{float4{color_u32_to_f128(hex)} * 255.f};
    bool3    inside          = {false, false, false};

    int    components       = 4;
    string channel_names[4] = {"R", "G", "B", "A"};
    if (which_image != 2)
    {
        ConstImagePtr img;
        ChannelGroup  group;
        if (which_image == 0)
        {
            if (!hdrview()->current_image())
                return;
            img        = hdrview()->current_image();
            components = color_mode == 0 ? img->groups[img->selected_group].num_channels : 4;
            group      = img->groups[img->selected_group];
            inside[0]  = img->contains(pixel);
        }
        else if (which_image == 1)
        {
            if (!hdrview()->reference_image())
                return;
            img        = hdrview()->reference_image();
            components = color_mode == 0 ? img->groups[img->reference_group].num_channels : 4;
            group      = img->groups[img->reference_group];
            inside[1]  = img->contains(pixel);
        }

        if (color_mode == 0)
            for (int c = 0; c < components; ++c)
                channel_names[c] = Channel::tail(img->channels[group.channels[c]].name);
    }
    inside[2] = inside[0] || inside[1];

    ImGuiColorEditFlags color_flags = ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreviewHalf;
    if (ImGui::ColorButton("colorbutton", displayed_color, color_flags))
        ImGui::OpenPopup("dropdown");
    ImGui::SetItemTooltip("Click to change value format%s", allow_copy ? " or copy to clipboard." : ".");

    // ImGui::PushFont(sans_font);
    if (ImGui::BeginPopup("dropdown"))
    {
        if (allow_copy && ImGui::Selectable("Copy to clipboard"))
        {
            string buf;
            if (color_mode == 0)
            {
                if (components == 4)
                    buf = fmt::format("({:g}, {:g}, {:g}, {:g})", color32.x, color32.y, color32.z, color32.w);
                else if (components == 3)
                    buf = fmt::format("({:g}, {:g}, {:g})", color32.x, color32.y, color32.z);
                else if (components == 2)
                    buf = fmt::format("({:g}, {:g})", color32.x, color32.y);
                else
                    buf = fmt::format("{:g}", color32.x);
            }
            else if (color_mode == 1)
                buf = fmt::format("({:g}, {:g}, {:g}, {:g})", displayed_color.x, displayed_color.y, displayed_color.z,
                                  displayed_color.w);
            else if (color_mode == 2)
                buf = fmt::format("({:d}, {:d}, {:d}, {:d})", ldr_color.x, ldr_color.y, ldr_color.z, ldr_color.w);
            else if (color_mode == 3)
                buf = fmt::format("#{:02X}{:02X}{:02X}{:02X}", ldr_color.x, ldr_color.y, ldr_color.z, ldr_color.w);
            ImGui::SetClipboardText(buf.c_str());
        }
        ImGui::SeparatorText("Display as:");
        if (ImGui::Selectable("Raw values", color_mode == 0))
            color_mode = 0;
        if (ImGui::Selectable("Displayed color (32-bit)", color_mode == 1))
            color_mode = 1;
        if (ImGui::Selectable("Displayed color (8-bit)", color_mode == 2))
            color_mode = 2;
        if (ImGui::Selectable("Displayed color (hex)", color_mode == 3))
            color_mode = 3;

        ImGui::EndPopup();
    }

    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

    float w_full = ImGui::GetContentRegionAvail().x;
    // width available to all items (without spacing)
    float w_items    = w_full - ImGui::GetStyle().ItemInnerSpacing.x * (components - 1);
    float prev_split = w_items;
    // distributes the available width without jitter during resize
    auto set_item_width = [&prev_split, w_items, components](int c)
    {
        float next_split = ImMax(IM_TRUNC(w_items * (components - 1 - c) / components), 1.f);
        ImGui::SetNextItemWidth(ImMax(prev_split - next_split, 1.0f));
        prev_split = next_split;
    };

    ImGui::BeginDisabled(!inside[which_image]);
    ImGui::BeginGroup();
    if (color_mode == 0)
    {
        for (int c = 0; c < components; ++c)
        {
            if (c > 0)
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

            set_item_width(c);
            ImGui::InputFloat(fmt::format("##component {}", c).c_str(), &color32[c], 0.f, 0.f,
                              fmt::format("{}: %g", channel_names[c]).c_str(), ImGuiInputTextFlags_ReadOnly);
        }
    }
    else if (color_mode == 1)
    {
        for (int c = 0; c < components; ++c)
        {
            if (c > 0)
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

            set_item_width(c);
            ImGui::InputFloat(fmt::format("##component {}", c).c_str(), &displayed_color[c], 0.f, 0.f,
                              fmt::format("{}: %g", channel_names[c]).c_str(), ImGuiInputTextFlags_ReadOnly);
        }
    }
    else if (color_mode == 2)
    {
        for (int c = 0; c < components; ++c)
        {
            if (c > 0)
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);

            set_item_width(c);
            ImGui::InputScalarN(fmt::format("##component {}", c).c_str(), ImGuiDataType_S32, &ldr_color[c], 1, NULL,
                                NULL, fmt::format("{}: %d", channel_names[c]).c_str(), ImGuiInputTextFlags_ReadOnly);
        }
    }
    else if (color_mode == 3)
    {
        ImGui::SetNextItemWidth(IM_TRUNC(w_full));
        ImGui::InputScalar("##hex color", ImGuiDataType_S32, &hex, NULL, NULL, "#%08X", ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::EndGroup();
    ImGui::EndDisabled();
}

void HDRViewApp::draw_pixel_inspector_window()
{
    if (!current_image())
        return;

    auto PixelHeader = [](const string &title, int2 &pixel, bool *p_visible = nullptr)
    {
        bool open = ImGui::CollapsingHeader(title.c_str(), p_visible, ImGuiTreeNodeFlags_DefaultOpen);

        ImGuiInputTextFlags_ flags = p_visible ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_ReadOnly;
        ImGui::BeginDisabled(p_visible == nullptr);
        // slightly convoluted process to show the coordinates as drag elements within the header
        ImGui::SameLine();
        auto  fpy = ImGui::GetStyle().FramePadding.y;
        float drag_size =
            0.5f * (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemInnerSpacing.x - ImGui::GetFrameHeight());
        ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + fpy);
        ImGui::SetNextItemWidth(drag_size);
        ImGui::DragInt("##pixel x coordinates", &pixel.x, 1.f, 0, 0, "X: %d", flags);
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + fpy);
        ImGui::SetNextItemWidth(drag_size);
        ImGui::DragInt("##pixel y coordinates", &pixel.y, 1.f, 0, 0, "Y: %d", flags);
        ImGui::PopStyleVar();
        ImGui::EndDisabled();

        return open;
    };

    auto &io = ImGui::GetIO();

    ImGui::SeparatorText("Selection:");
    // float sz = ImGui::GetContentRegionAvail().x * 0.65f + ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize(" Min,Max ").x);
    ImGui::DragInt4("Min,Max", &m_roi_live.min.x);
    if (ImGui::IsItemDeactivatedAfterEdit())
        m_roi = m_roi_live;
    ImGui::SetItemTooltip("W x H: (%d x %d)", m_roi_live.size().x, m_roi_live.size().y);

    ImGui::SeparatorText("Watched pixels:");

    auto hovered_pixel = int2{pixel_at_app_pos(io.MousePos)};
    if (PixelHeader(ICON_MY_CURSOR_ARROW "##hovered pixel", hovered_pixel))
    {
        static int3 color_mode = {0, 0, 0};
        ImGui::PushID("Current");
        pixel_color_widget(hovered_pixel, color_mode.x, 0);
        ImGui::SetItemTooltip("Hovered pixel values in current channel.");
        ImGui::PopID();

        ImGui::PushID("Reference");
        pixel_color_widget(hovered_pixel, color_mode.y, 1);
        ImGui::SetItemTooltip("Hovered pixel values in reference channel.");
        ImGui::PopID();

        ImGui::PushID("Composite");
        pixel_color_widget(hovered_pixel, color_mode.z, 2);
        ImGui::SetItemTooltip("Hovered pixel values in composite.");
        ImGui::PopID();

        ImGui::Spacing();
    }

    ImGui::Checkbox("Show " ICON_MY_WATCHED_PIXEL "s in viewport", &m_draw_watched_pixels);

    int delete_idx = -1;
    for (int i = 0; i < (int)g_watched_pixels.size(); ++i)
    {
        auto &wp = g_watched_pixels[i];

        ImGui::PushID(i);
        bool visible = true;
        if (PixelHeader(fmt::format("{}{}", ICON_MY_WATCHED_PIXEL, i + 1), wp.pixel, &visible))
        {
            ImGui::PushID("Current");
            pixel_color_widget(wp.pixel, wp.color_mode.x, 0, true);
            ImGui::SetItemTooltip("Pixel %s%d values in current channel.", ICON_MY_WATCHED_PIXEL, i + 1);
            ImGui::PopID();

            ImGui::PushID("Reference");
            pixel_color_widget(wp.pixel, wp.color_mode.y, 1, true);
            ImGui::SetItemTooltip("Pixel %s%d values in reference channel.", ICON_MY_WATCHED_PIXEL, i + 1);
            ImGui::PopID();

            ImGui::PushID("Composite");
            pixel_color_widget(wp.pixel, wp.color_mode.z, 2, true);
            ImGui::SetItemTooltip("Pixel %s%d values in composite.", ICON_MY_WATCHED_PIXEL, i + 1);
            ImGui::PopID();

            ImGui::Spacing();
        }
        ImGui::PopID();

        if (!visible)
            delete_idx = i;
    }
    if (delete_idx >= 0)
        g_watched_pixels.erase(g_watched_pixels.begin() + delete_idx);
}

static void calculate_tree_visibility(LayerTreeNode &node, Image &image)
{
    node.visible_groups = 0;
    node.hidden_groups  = 0;
    if (node.leaf_layer >= 0)
    {
        auto &layer = image.layers[node.leaf_layer];
        for (size_t g = 0; g < layer.groups.size(); ++g)
            if (image.groups[layer.groups[g]].visible)
                ++node.visible_groups;
            else
                ++node.hidden_groups;
    }

    for (auto &[child_name, child_node] : node.children)
    {
        calculate_tree_visibility(child_node, image);
        node.visible_groups += child_node.visible_groups;
        node.hidden_groups += child_node.hidden_groups;
    }
}

void HDRViewApp::update_visibility()
{
    // compute image:channel visibility and update selection indices
    vector<string> visible_image_names;
    for (auto img : m_images)
    {
        const string prefix = img->partname + (img->partname.empty() ? "" : ".");

        // compute visibility of all groups
        img->any_groups_visible = false;
        for (auto &g : img->groups)
        {
            // check if any of the contained channels in the group pass the channel filter
            g.visible = false;
            for (int c = 0; c < g.num_channels && !g.visible; ++c)
                g.visible |= m_channel_filter.PassFilter((prefix + img->channels[g.channels[c]].name).c_str());
            img->any_groups_visible |= g.visible;
        }

        // an image is visible if its filename passes the file filter and it has at least one visible group
        img->visible = m_file_filter.PassFilter(img->filename.c_str()) && img->any_groups_visible;

        if (img->visible)
            visible_image_names.emplace_back(img->file_and_partname());

        calculate_tree_visibility(img->root, *img);

        // if the selected group is hidden, select the next visible group
        if (img->is_valid_group(img->selected_group) && !img->groups[img->selected_group].visible)
        {
            auto old = img->selected_group;
            if ((img->selected_group = img->next_visible_group_index(img->selected_group, Forward)) == old)
                img->selected_group = -1; // no visible groups left
        }

        // if the reference group is hidden, clear it
        // TODO: keep it, but don't display it
        if (img->is_valid_group(img->reference_group) && !img->groups[img->reference_group].visible)
            img->reference_group = -1;
    }

    // go to the next visible image if the current one is hidden
    if (!is_valid(m_current) || !m_images[m_current]->visible)
    {
        auto old = m_current;
        if ((m_current = next_visible_image_index(m_current, Forward)) == old)
            m_current = -1; // no visible images left
    }

    // if the reference is hidden, clear it
    // TODO: keep it, but don't display it
    if (is_valid(m_reference) && !m_images[m_reference]->visible)
        m_reference = -1;

    //
    // compute short (i.e. unique) names for visible images

    // determine common vs. unique parts of visible filenames
    auto [begin_short_offset, end_short_offset] = find_common_prefix_suffix(visible_image_names);
    // we'll add ellipses, so don't shorten if we don't save much space
    if (begin_short_offset <= 4)
        begin_short_offset = 0;
    if (end_short_offset <= 4)
        end_short_offset = 0;
    for (auto img : m_images)
    {
        if (!img->visible)
            continue;

        auto   long_name   = img->file_and_partname();
        size_t short_begin = begin_short_offset;
        size_t short_end   = std::max(long_name.size() - (size_t)end_short_offset, short_begin);

        // Extend beginning and ending of short region to entire word/number
        if (isalnum(long_name[short_begin]))
            while (short_begin > 0 && isalnum(long_name[short_begin - 1])) --short_begin;
        if (isalnum(long_name[short_end - 1]))
            while (short_end < long_name.size() && isalnum(long_name[short_end])) ++short_end;

        // just use the filename if all file paths are identical
        if (short_begin == short_end)
            img->short_name = get_filename(img->file_and_partname());
        else
            img->short_name = long_name.substr(short_begin, short_end - short_begin);

        // add ellipses to indicate where we shortened
        if (short_begin != 0)
            img->short_name = "..." + img->short_name;
        if (short_end != long_name.size())
            img->short_name = img->short_name + "...";
    }

    set_image_textures();
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
            const bool is_selected = (m_channel == n);
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

    bool show_button = m_file_filter.IsActive() || m_channel_filter.IsActive(); // save here to avoid flicker
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 2.f * (button_size.x + ImGui::GetStyle().ItemSpacing.x));
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InputTextWithHint("##file filter", ICON_MY_FILTER " Filter 'file pattern:channel pattern'",
                                 g_filter_buffer, IM_ARRAYSIZE(g_filter_buffer)))
    {
        // copy everything before first ':' into m_file_filter.InputBuf, and everything after into
        // m_channel_filter.InputBuf
        if (auto colon = strchr(g_filter_buffer, ':'))
        {
            int file_filter_length    = int(colon - g_filter_buffer + 1);
            int channel_filter_length = IM_ARRAYSIZE(g_filter_buffer) - file_filter_length;
            ImStrncpy(m_file_filter.InputBuf, g_filter_buffer, file_filter_length);
            ImStrncpy(m_channel_filter.InputBuf, colon + 1, channel_filter_length);
        }
        else
        {
            ImStrncpy(m_file_filter.InputBuf, g_filter_buffer, IM_ARRAYSIZE(m_file_filter.InputBuf));
            m_channel_filter.InputBuf[0] = 0; // Clear channel filter if no colon is found
        }

        m_file_filter.Build();
        m_channel_filter.Build();

        update_visibility();
    }
    ImGui::WrappedTooltip(
        "Filter visible images and channel groups.\n\nOnly images with filenames matching the file pattern and "
        "channels matching the channel pattern will be shown. A pattern is a comma-separated list of strings "
        "that must be included or excluded (if prefixed with a '-').");
    if (show_button)
    {
        ImGui::SameLine(0.f, 0.f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - button_size.x);
        if (ImGui::IconButton(ICON_MY_DELETE))
        {
            m_file_filter.Clear();
            m_channel_filter.Clear();
            g_filter_buffer[0] = 0;
            update_visibility();
        }
    }

    ImGui::SameLine();
    if (ImGui::IconButton(g_short_names ? ICON_MY_SHORT_NAMES "##short names button"
                                        : ICON_MY_FULL_NAMES "##short names button"))
        g_short_names = !g_short_names;
    ImGui::WrappedTooltip(g_short_names ? "Click to show full filenames."
                                        : "Click to show only the unique portion of each file name.");

    static const std::string s_view_mode_icons[] = {ICON_MY_NO_CHANNEL_GROUP, ICON_MY_LIST_VIEW, ICON_MY_TREE_VIEW};

    ImGui::SameLine();
    if (ImGui::BeginComboButton("##channel list mode", s_view_mode_icons[g_file_list_mode].data()))
    {
        if (ImGui::Selectable((s_view_mode_icons[0] + " Only images (do not list channel groups)").c_str(),
                              g_file_list_mode == 0))
            g_file_list_mode = 0;
        if (ImGui::Selectable((s_view_mode_icons[1] + " Flat list of layers and channels").c_str(),
                              g_file_list_mode == 1))
            g_file_list_mode = 1;
        if (ImGui::Selectable((s_view_mode_icons[2] + " Tree view of layers and channels").c_str(),
                              g_file_list_mode == 2))
            g_file_list_mode = 2;

        ImGui::EndComboButton();
    }
    ImGui::WrappedTooltip("Choose how the images and layers are listed below");

    static constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate |
                                                   ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit |
                                                   ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_BordersH |
                                                   ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("ImageList", 2, table_flags))
    {
        const float icon_width = ImGui::IconSize().x;

        ImGui::TableSetupColumn(ICON_MY_LIST_OL,
                                ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_IndentDisable,
                                1.25f * icon_width);
        // ImGui::TableSetupColumn(ICON_MY_VISIBILITY,
        //                         ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable,
        //                         icon_width);
        ImGui::TableSetupColumn(g_file_list_mode ? "File:part or channel group" : "File:part.layer.channel group",
                                ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_IndentEnable);
        ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
        ImGui::TableHeadersRow();

        ImGuiSortDirection direction = ImGuiSortDirection_None;
        if (ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs())
            if (sort_specs->SpecsCount)
            {
                direction = sort_specs->Specs[0].SortDirection;
                if (sort_specs->SpecsDirty || g_request_sort)
                {
                    spdlog::info("Sorting {}", (int)direction);
                    auto old_current   = current_image();
                    auto old_reference = reference_image();
                    std::sort(m_images.begin(), m_images.end(),
                              [direction](const ImagePtr &a, const ImagePtr &b)
                              {
                                  return (direction == ImGuiSortDirection_Ascending)
                                             ? a->file_and_partname() < b->file_and_partname()
                                             : a->file_and_partname() > b->file_and_partname();
                              });

                    // restore selection
                    if (old_current)
                        m_current = int(std::find(m_images.begin(), m_images.end(), old_current) - m_images.begin());
                    if (old_reference)
                        m_reference =
                            int(std::find(m_images.begin(), m_images.end(), old_reference) - m_images.begin());
                }

                sort_specs->SpecsDirty = g_request_sort = false;
            }

        static ImGuiTreeNodeFlags base_node_flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen |
                                                    ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                                    ImGuiTreeNodeFlags_OpenOnArrow;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 0.5f * icon_width);
        int id                 = 0;
        int visible_img_number = 0;
        int hidden_images      = 0;
        int hidden_groups      = 0;

        for (int i = 0; i < num_images(); ++i)
        {
            auto &img          = m_images[i];
            bool  is_current   = m_current == i;
            bool  is_reference = m_reference == i;

            if (!img->visible)
            {
                ++hidden_images;
                continue;
            }

            ++visible_img_number;

            ImGuiTreeNodeFlags node_flags = base_node_flags;

            ImGui::PushFont(g_file_list_mode == 0 ? m_sans_regular : m_sans_bold, 14.f);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushRowColors(is_current, is_reference, ImGui::GetIO().KeyShift);
            ImGui::TextAligned(fmt::format("{}", visible_img_number), 1.0f);

            // ImGui::TableNextColumn();
            // auto tmp_pos = ImGui::GetCursorScreenPos();
            // ImGui::TextUnformatted(is_current ? ICON_MY_VISIBILITY : "");
            // ImGui::SetCursorScreenPos(tmp_pos);
            // ImGui::TextUnformatted(is_reference ? ICON_MY_REFERENCE_IMAGE : "");

            ImGui::TableNextColumn();

            if (is_current || is_reference)
                node_flags |= ImGuiTreeNodeFlags_Selected;
            if (g_file_list_mode == 0)
            {
                node_flags |= ImGuiTreeNodeFlags_Leaf;
                ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
            }

            // right-align the truncated file name
            string filename;
            string icon     = img->groups.size() > 1 ? ICON_MY_IMAGES : ICON_MY_IMAGE;
            string ellipsis = " ";
            {
                auto &selected_group =
                    img->groups[(is_reference && !is_current) ? img->reference_group : img->selected_group];
                string group_name =
                    selected_group.num_channels == 1 ? selected_group.name : "(" + selected_group.name + ")";
                auto  &channel    = img->channels[selected_group.channels[0]];
                string layer_path = Channel::head(channel.name);
                filename          = (g_short_names ? img->short_name : img->file_and_partname()) +
                           (g_file_list_mode ? "" : img->delimiter() + layer_path + group_name);

                const float avail_width = ImGui::GetContentRegionAvail().x - ImGui::GetTreeNodeToLabelSpacing();
                while (ImGui::CalcTextSize((icon + ellipsis + filename).c_str()).x > avail_width &&
                       filename.length() > 1)
                {
                    filename = filename.substr(1);
                    ellipsis = " ...";
                }
            }

            // auto item = m_images[i];
            bool open = ImGui::TreeNodeEx((void *)(intptr_t)i, node_flags, "%s", (icon + ellipsis + filename).c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                if (ImGui::GetIO().KeyShift)
                    m_reference = is_reference ? -1 : i;
                else
                    m_current = i;
                set_image_textures();
                spdlog::trace("Setting image {} to the {} image", i, is_reference ? "reference" : "current");
            }

            ImGui::PopStyleColor(3);

            if (direction == ImGuiSortDirection_None)
            {
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                {
                    // Set payload to carry the index of our item
                    ImGui::SetDragDropPayload("DND_IMAGE", &i, sizeof(int));

                    // Display preview
                    ImGui::TextUnformatted("Move here");
                    if (ImGui::BeginTable("ImageList", 2, table_flags))
                    {
                        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 1.25f * icon_width);
                        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextAligned(fmt::format("{}", visible_img_number), 1.0f);
                        ImGui::TableNextColumn();
                        ImGui::Text(icon + ellipsis + filename);
                        ImGui::EndTable();
                    }
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_IMAGE"))
                    {
                        IM_ASSERT(payload->DataSize == sizeof(int));
                        int payload_i = *(const int *)payload->Data;

                        // move image at payload_i to i, and shift all images in between
                        if (payload_i < i)
                            for (int j = payload_i; j < i; ++j) std::swap(m_images[j], m_images[j + 1]);
                        else
                            for (int j = payload_i; j > i; --j) std::swap(m_images[j], m_images[j - 1]);

                        // maintain the current and reference images
                        if (m_current == payload_i)
                            m_current = i;
                        if (m_reference == payload_i)
                            m_reference = i;
                    }
                    ImGui::EndDragDropTarget();
                }
            }

            if (open)
            {
                ImGui::PushFont(m_sans_regular, 0.f);
                int visible_groups = 1;
                if (g_file_list_mode == 0)
                    ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
                else if (g_file_list_mode == 1)
                {
                    visible_groups = img->draw_channel_rows(i, id, is_current, is_reference);
                    MY_ASSERT(visible_groups == img->root.visible_groups,
                              "Unexpected number of visible groups; {} != {}", visible_groups,
                              img->root.visible_groups);
                }
                else
                {
                    visible_groups = img->draw_channel_tree(i, id, is_current, is_reference);
                    MY_ASSERT(visible_groups == img->root.visible_groups,
                              "Unexpected number of visible groups; {} != {}", visible_groups,
                              img->root.visible_groups);
                }

                hidden_groups += (int)img->groups.size() - visible_groups;

                ImGui::PopFont();

                if (open)
                    ImGui::TreePop();
            }

            ImGui::PopFont();
        }

        if (hidden_images || hidden_groups)
        {
            ImGui::BeginDisabled();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // ImGui::TextUnformatted(ICON_MY_VISIBILITY_OFF);
            ImGui::TableNextColumn();
            auto images_str = hidden_images > 1 ? "s" : "";
            auto groups_str = hidden_groups > 1 ? "s" : "";
            if (hidden_groups)
            {
                if (hidden_images)
                    ImGui::TextFmt("{} {} image{} and {} channel group{} hidden", ICON_MY_VISIBILITY_OFF, hidden_images,
                                   images_str, hidden_groups, groups_str);
                else
                    ImGui::TextFmt("{} {} channel group{} hidden", ICON_MY_VISIBILITY_OFF, hidden_groups, groups_str);
            }
            else
                ImGui::TextFmt("{} {} image{} hidden", ICON_MY_VISIBILITY_OFF, hidden_images, images_str);
            ImGui::EndDisabled();
        }
        ImGui::PopStyleVar(2);

        ImGui::EndTable();
    }
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
        auto center_pixel = Box2f(img->data_window).center();
        reposition_pixel_to_vp_pos(center_pos, center_pixel);
    }
}

void HDRViewApp::fit_selection()
{
    if (current_image() && m_roi.has_volume())
    {
        m_zoom = minelem(viewport_size() / m_roi.size());

        auto center_pos   = float2(viewport_size() / 2.f);
        auto center_pixel = Box2f(m_roi).center();
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
    if (auto img = current_image())
        pixel = select(m_flip, img->display_window.max - pixel - 1, pixel);

    // Calculate where the new offset must be in order to satisfy the image position equation.
    m_offset = position - (pixel * m_zoom) - center_offset();
}

Box2f HDRViewApp::scaled_display_window(ConstImagePtr img) const
{
    Box2f dw = img ? Box2f{img->display_window} : Box2f{{0, 0}, {0, 0}};
    dw.min *= m_zoom;
    dw.max *= m_zoom;
    return dw;
}

Box2f HDRViewApp::scaled_data_window(ConstImagePtr img) const
{
    Box2f dw = img ? Box2f{img->data_window} : Box2f{{0, 0}, {0, 0}};
    dw.min *= m_zoom;
    dw.max *= m_zoom;
    return dw;
}

float HDRViewApp::pixel_ratio() const { return ImGui::GetIO().DisplayFramebufferScale.x; }

float2 HDRViewApp::center_offset() const
{
    auto   dw     = scaled_display_window(current_image());
    float2 offset = (viewport_size() - dw.size()) / 2.f - dw.min;

    // Adjust for flipping: if flipped, offset from the opposite side
    // if (current_image())
    {
        if (m_flip.x)
            offset.x += dw.min.x;
        if (m_flip.y)
            offset.y += dw.min.y;
    }
    return offset;
}

float2 HDRViewApp::image_position(ConstImagePtr img) const
{
    auto   dw  = scaled_data_window(img);
    auto   dsw = scaled_display_window(img);
    float2 pos = m_offset + center_offset() + select(m_flip, dsw.max - dw.min, dw.min);

    // Adjust for flipping: move the image to the opposite side if flipped
    // if (img)
    // {
    //     if (m_flip.x)
    //         pos.x += m_offset.x + center_offset().x + (dsw.max.x - dw.min.x);
    //     if (m_flip.y)
    //         pos.y += m_offset.y + center_offset().y + (dsw.max.y - dw.min.y);
    // }
    return pos / viewport_size();
}

float2 HDRViewApp::image_scale(ConstImagePtr img) const
{
    auto   dw    = scaled_data_window(img);
    float2 scale = dw.size() / viewport_size();

    // Negate scale for flipped axes
    // if (img)
    {
        if (m_flip.x)
            scale.x = -scale.x;
        if (m_flip.y)
            scale.y = -scale.y;
    }
    return scale;
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

    if (alpha <= 0.0f)
        return;

    ImDrawList *draw_list = ImGui::GetBackgroundDrawList();

    ImColor col_fg(1.0f, 1.0f, 1.0f, alpha);
    ImColor col_bg(0.2f, 0.2f, 0.2f, alpha);

    auto bounds =
        Box2i{int2(pixel_at_vp_pos({0.f, 0.f})), int2(pixel_at_vp_pos(viewport_size()))}.make_valid().expand(1);

    // draw vertical lines
    for (int x = bounds.min.x; x <= bounds.max.x; ++x)
        draw_list->AddLine(app_pos_at_pixel(float2((float)x, (float)bounds.min.y)),
                           app_pos_at_pixel(float2((float)x, (float)bounds.max.y)), col_bg, 4.f);

    // draw horizontal lines
    for (int y = bounds.min.y; y <= bounds.max.y; ++y)
        draw_list->AddLine(app_pos_at_pixel(float2((float)bounds.min.x, (float)y)),
                           app_pos_at_pixel(float2((float)bounds.max.x, (float)y)), col_bg, 4.f);

    // and now again with the foreground color
    for (int x = bounds.min.x; x <= bounds.max.x; ++x)
        draw_list->AddLine(app_pos_at_pixel(float2((float)x, (float)bounds.min.y)),
                           app_pos_at_pixel(float2((float)x, (float)bounds.max.y)), col_fg, 2.f);
    for (int y = bounds.min.y; y <= bounds.max.y; ++y)
        draw_list->AddLine(app_pos_at_pixel(float2((float)bounds.min.x, (float)y)),
                           app_pos_at_pixel(float2((float)bounds.max.x, (float)y)), col_fg, 2.f);
}

void HDRViewApp::draw_pixel_info() const
{
    auto img = current_image();
    if (!img || !m_draw_pixel_info)
        return;

    auto ref = reference_image();

    static constexpr float2 align     = {0.5f, 0.5f};
    auto                    mono_font = m_mono_bold;

    auto  &group  = img->groups[img->selected_group];
    auto   colors = group.colors();
    string names[4];
    string longest_name;
    for (int c = 0; c < group.num_channels; ++c)
    {
        auto &channel = img->channels[group.channels[c]];
        names[c]      = Channel::tail(channel.name);
        if (names[c].length() > longest_name.length())
            longest_name = names[c];
    }

    ImGui::PushFont(m_mono_bold, 30);
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

    if (alpha <= 0.0f)
        return;

    // fade value for the (x,y) coordinates shown at further zoom
    float factor2 = std::clamp((m_zoom - coord_threshold) / (1.25f * coord_threshold), 0.f, 1.f);
    float alpha2  = smoothstep(0.0f, 1.0f, factor2);

    ImDrawList *draw_list = ImGui::GetBackgroundDrawList();

    ImGui::PushFont(mono_font, 30);

    auto bounds =
        Box2i{int2(pixel_at_vp_pos({0.f, 0.f})), int2(pixel_at_vp_pos(viewport_size()))}.make_valid().expand(1);

    for (int y = bounds.min.y; y < bounds.max.y; ++y)
    {
        for (int x = bounds.min.x; x < bounds.max.x; ++x)
        {
            auto   pos   = app_pos_at_pixel(float2(x + 0.5f, y + 0.5f));
            float4 top   = img->raw_pixel({x, y});
            float4 pixel = top;
            if (ref && ref->data_window.contains({x, y}))
            {
                float4 bottom = ref->raw_pixel({x, y});
                // blend with reference image if available
                pixel = float4{blend(top.x, bottom.x, m_blend_mode), blend(top.y, bottom.y, m_blend_mode),
                               blend(top.z, bottom.z, m_blend_mode), blend(top.w, bottom.w, m_blend_mode)};
            }
            if (alpha2 > 0.f)
            {
                float2 c_pos = pos + float2{0.f, (-1 - 0.5f * (group.num_channels - 1)) * line_height};
                auto   text  = fmt::format("({},{})", x, y);
                ImGui::AddTextAligned(draw_list, c_pos + int2{1, 2}, ImGui::GetColorU32(IM_COL32_BLACK, alpha2), text,
                                      align);
                ImGui::AddTextAligned(draw_list, c_pos, ImGui::GetColorU32(IM_COL32_WHITE, alpha2), text, align);
            }

            for (int c = 0; c < group.num_channels; ++c)
            {
                float2 c_pos = pos + float2{0.f, (c - 0.5f * (group.num_channels - 1)) * line_height};
                auto   text  = fmt::format("{:>2s}:{: > 7.3f}", names[c], pixel[c]);
                ImGui::AddTextAligned(draw_list, c_pos + int2{1, 2}, ImGui::GetColorU32(IM_COL32_BLACK, alpha), text,
                                      align);
                ImGui::AddTextAligned(draw_list, c_pos, ImColor{float4{colors[c].xyz(), alpha}}, text, align);
            }
        }
    }
    ImGui::PopFont();
}

void HDRViewApp::draw_image_border() const
{
    auto draw_list = ImGui::GetBackgroundDrawList();

    auto cimg = current_image();
    auto rimg = reference_image();

    if (!cimg && !rimg)
        return;

    if (cimg && cimg->data_window.has_volume())
    {
        auto data_window =
            Box2f{app_pos_at_pixel(float2{cimg->data_window.min}), app_pos_at_pixel(float2{cimg->data_window.max})}
                .make_valid();
        auto display_window = Box2f{app_pos_at_pixel(float2{cimg->display_window.min}),
                                    app_pos_at_pixel(float2{cimg->display_window.max})}
                                  .make_valid();
        bool non_trivial = cimg->data_window != cimg->display_window || cimg->data_window.min != int2{0, 0};
        ImGui::PushRowColors(true, false);
        if (m_draw_data_window)
            ImGui::DrawLabeledRect(draw_list, data_window, ImGui::GetColorU32(ImGuiCol_HeaderActive), "Data window",
                                   {0.f, 0.f}, non_trivial);
        if (m_draw_display_window && non_trivial)
            ImGui::DrawLabeledRect(draw_list, display_window, ImGui::GetColorU32(ImGuiCol_Header), "Display window",
                                   {1.f, 1.f}, true);
        ImGui::PopStyleColor(3);
    }

    if (rimg && rimg->data_window.has_volume())
    {
        auto data_window =
            Box2f{app_pos_at_pixel(float2{rimg->data_window.min}), app_pos_at_pixel(float2{rimg->data_window.max})}
                .make_valid();
        auto display_window = Box2f{app_pos_at_pixel(float2{rimg->display_window.min}),
                                    app_pos_at_pixel(float2{rimg->display_window.max})}
                                  .make_valid();
        ImGui::PushRowColors(false, true, true);
        if (m_draw_data_window)
            ImGui::DrawLabeledRect(draw_list, data_window, ImGui::GetColorU32(ImGuiCol_HeaderActive),
                                   "Reference data window", {1.f, 0.f}, true);
        if (m_draw_display_window)
            ImGui::DrawLabeledRect(draw_list, display_window, ImGui::GetColorU32(ImGuiCol_Header),
                                   "Reference display window", {0.f, 1.f}, true);
        ImGui::PopStyleColor(3);
    }

    if (m_roi_live.has_volume())
    {
        Box2f crop_window{app_pos_at_pixel(float2{m_roi_live.min}), app_pos_at_pixel(float2{m_roi_live.max})};
        ImGui::DrawLabeledRect(draw_list, crop_window, ImGui::ColorConvertFloat4ToU32(float4{float3{0.5f}, 1.f}),
                               "Selection", {0.5f, 1.f}, true);
    }
}

void HDRViewApp::draw_watched_pixels() const
{
    if (!current_image() || !m_draw_watched_pixels)
        return;

    auto draw_list = ImGui::GetBackgroundDrawList();

    ImGui::PushFont(m_sans_bold, 14);
    for (int i = 0; i < (int)g_watched_pixels.size(); ++i)
        ImGui::DrawCrosshairs(draw_list, app_pos_at_pixel(g_watched_pixels[i].pixel + 0.5f), fmt::format(" {}", i + 1));
    ImGui::PopFont();
}

void HDRViewApp::draw_image() const
{
    if (current_image() && !current_image()->data_window.is_empty())
    {
        float2 randomness(std::generate_canonical<float, 10>(g_rand) * 255,
                          std::generate_canonical<float, 10>(g_rand) * 255);

        m_shader->set_uniform("time", (float)ImGui::GetTime());
        m_shader->set_uniform("draw_clip_warnings", m_draw_clip_warnings);
        m_shader->set_uniform("clip_range", m_clip_range);
        m_shader->set_uniform("randomness", randomness);
        m_shader->set_uniform("gain", powf(2.0f, m_exposure_live));
        m_shader->set_uniform("gamma", m_gamma_live);
        m_shader->set_uniform("tonemap_mode", (int)m_tonemap);
        m_shader->set_uniform("clamp_to_LDR", m_clamp_to_LDR);
        m_shader->set_uniform("do_dither", m_dither);

        m_shader->set_uniform("primary_pos", image_position(current_image()));
        m_shader->set_uniform("primary_scale", image_scale(current_image()));

        m_shader->set_uniform("blend_mode", (int)m_blend_mode);
        m_shader->set_uniform("channel", (int)m_channel);
        m_shader->set_uniform("bg_mode", (int)m_bg_mode);
        m_shader->set_uniform("bg_color", m_bg_color);

        m_shader->set_texture("colormap", Colormap::texture(m_colormaps[m_colormap_index]));

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

    // ImGui::Begin("Texture window");
    // ImGui::Image((ImTextureID)(intptr_t)Colormap::texture(m_colormap)->texture_handle(),
    //              ImGui::GetContentRegionAvail());
    // ImGui::End();
}

void HDRViewApp::draw_top_toolbar()
{
    auto img = current_image();

    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(m_sans_bold, 16);
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

    ImGui::SetNextItemWidth(HelloImGui::EmSize(7.5));
    ImGui::Combo("##Tonemapping", (int *)&m_tonemap, "Gamma\0Colormap (+)\0Colormap (±)\0");
    ImGui::SetItemTooltip("Set the tonemapping mode.");

    switch (m_tonemap)
    {
    default: [[fallthrough]];
    case Tonemap_Gamma:
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(HelloImGui::EmSize(8));
        ImGui::SliderFloat("##GammaSlider", &m_gamma_live, 0.02f, 9.f, "%5.3f");
        if (ImGui::IsItemDeactivatedAfterEdit())
            m_gamma = m_gamma_live;
        ImGui::SetItemTooltip("Set the exponent for gamma correction.");
    }
    break;
    case Tonemap_FalseColor: [[fallthrough]];
    case Tonemap_PositiveNegative:
    {
        ImGui::SameLine();
        auto p = ImGui::GetCursorScreenPos();

        if (ImPlot::ColormapButton(Colormap::name(m_colormaps[m_colormap_index]),
                                   ImVec2(HelloImGui::EmSize(8), ImGui::GetFrameHeight()),
                                   m_colormaps[m_colormap_index]))
            ImGui::OpenPopup("colormap_dropdown");
        ImGui::SetItemTooltip("Click to choose a colormap.");

        ImGui::SetNextWindowPos(ImVec2{p.x, p.y + ImGui::GetFrameHeight()});
        ImGui::PushStyleVarX(ImGuiStyleVar_WindowPadding, ImGui::GetStyle().FramePadding.x);
        auto tmp = ImGui::BeginPopup("colormap_dropdown");
        ImGui::PopStyleVar();
        if (tmp)
        {
            for (int n = 0; n < (int)m_colormaps.size(); n++)
            {
                const bool is_selected = (m_colormap_index == n);
                if (ImGui::Selectable((string("##") + Colormap::name(m_colormaps[n])).c_str(), is_selected, 0,
                                      ImVec2(0, ImGui::GetFrameHeight())))
                    m_colormap_index = n;
                ImGui::SameLine(0.f, 0.f);

                ImPlot::ColormapButton(
                    Colormap::name(m_colormaps[n]),
                    ImVec2(HelloImGui::EmSize(8) - ImGui::GetStyle().WindowPadding.x, ImGui::GetFrameHeight()),
                    m_colormaps[n]);

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndPopup();
        }
    }
    break;
    }
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
        // fbsize is the size of the window in physical pixels while accounting for dpi factor on retina
        // screens. For retina displays, io.DisplaySize is the size of the window in logical pixels so we it by
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

        if (!io.WantCaptureMouse && current_image())
        {
            auto vp_mouse_pos   = vp_pos_at_app_pos(io.MousePos);
            bool cancel_autofit = false;
            auto scroll         = float2{io.MouseWheelH, io.MouseWheel} * g_scroll_multiplier;

            if (length2(scroll) > 0.f)
            {
                cancel_autofit = true;
                if (ImGui::IsKeyDown(ImGuiMod_Shift))
                    // panning
                    reposition_pixel_to_vp_pos(vp_mouse_pos + scroll * 4.f, pixel_at_vp_pos(vp_mouse_pos));
                else
                    zoom_at_vp_pos(scroll.y / 4.f, vp_mouse_pos);
            }

            if (g_mouse_mode == MouseMode_RectangularSelection)
            {
                // set m_roi based on dragged region
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    m_roi_live = Box2i{int2{0}};
                else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    m_roi_live.make_empty();
                    m_roi_live.enclose(int2{pixel_at_app_pos(io.MouseClickedPos[0])});
                    m_roi_live.enclose(int2{pixel_at_app_pos(io.MousePos)});
                }
                else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    m_roi = m_roi_live;
            }
            else if (g_mouse_mode == MouseMode_ColorInspector)
            {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    // add watched pixel
                    g_watched_pixels.emplace_back(WatchedPixel{int2{pixel_at_app_pos(io.MousePos)}});
                else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    if (g_watched_pixels.size())
                        g_watched_pixels.back().pixel = int2{pixel_at_app_pos(io.MousePos)};
                }
            }
            else
            {
                float2 drag_delta{ImGui::GetMouseDragDelta(ImGuiMouseButton_Left)};
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    cancel_autofit = true;
                    reposition_pixel_to_vp_pos(vp_mouse_pos + drag_delta, pixel_at_vp_pos(vp_mouse_pos));
                    ImGui::ResetMouseDragDelta();
                }
            }

            if (cancel_autofit)
                this->cancel_autofit();
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
        if (m_auto_fit_selection)
            fit_selection();

        m_render_pass->begin();
        draw_image();
        m_render_pass->end();

        draw_pixel_info();
        draw_pixel_grid();
        draw_image_border();
        draw_watched_pixels();

        if (current_image())
        {
            ImGui::PushFont(m_sans_bold, 18);
            auto   draw_list = ImGui::GetBackgroundDrawList();
            float2 pos       = ImGui::GetIO().MousePos;
            if (g_mouse_mode == MouseMode_RectangularSelection)
            {
                // draw selection indicator
                ImGui::AddTextAligned(draw_list, pos + int2{18} + int2{1, 1}, IM_COL32_BLACK, ICON_MY_SELECT,
                                      {0.5f, 0.5f});
                ImGui::AddTextAligned(draw_list, pos + int2{18}, IM_COL32_WHITE, ICON_MY_SELECT, {0.5f, 0.5f});
            }
            else if (g_mouse_mode == MouseMode_ColorInspector)
            {
                // draw pixel watcher indicator
                ImGui::DrawCrosshairs(draw_list, pos + int2{18}, " +");
            }
            // else if (g_mouse_mode == MouseMode_PanZoom)
            // {
            //     // draw pixel watcher indicator
            //     ImGui::AddTextAligned(draw_list, pos + int2{18} + int2{1, 1}, IM_COL32_BLACK, ICON_MY_PAN_ZOOM_TOOL,
            //                           {0.5f, 0.5f});
            //     ImGui::AddTextAligned(draw_list, pos + int2{18}, IM_COL32_WHITE, ICON_MY_PAN_ZOOM_TOOL, {0.5f,
            //     0.5f});
            // }
            ImGui::PopFont();
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("Drawing failed:\n\t{}.", e.what());
    }
}

int HDRViewApp::next_visible_image_index(int index, EDirection direction) const
{
    return next_matching_index(m_images, index, [](size_t, const ImagePtr &img) { return img->visible; }, direction);
}

int HDRViewApp::nth_visible_image_index(int n) const
{
    return (int)nth_matching_index(m_images, (size_t)n, [](size_t, const ImagePtr &img) { return img->visible; });
}

bool HDRViewApp::process_event(void *e)
{
#ifdef HELLOIMGUI_USE_SDL2
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
    (void)e; // prevent unreferenced formal parameter warning
    return false;
}

void HDRViewApp::draw_command_palette()
{
    if (g_show_command_palette)
        ImGui::OpenPopup("Command palette...");

    g_show_command_palette = false;

    auto &io = ImGui::GetIO();

    // Center window horizontally, align near top vertically
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, 5.f * HelloImGui::EmSize()), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));

    ImGui::SetNextWindowSize(ImVec2{400, 0}, ImGuiCond_Always);

    float remaining_height            = ImGui::GetMainViewport()->Size.y - ImGui::GetCursorScreenPos().y;
    float search_result_window_height = remaining_height - 2.f * HelloImGui::EmSize();

    // Set constraints to allow horizontal resizing based on content
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0, 0), ImVec2(io.DisplaySize.x - 2.f * HelloImGui::EmSize(), search_result_window_height));

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
            ImCmd::SetStyleFont(ImCmdTextType_Regular, m_sans_regular);
            ImCmd::SetStyleFont(ImCmdTextType_Highlight, m_sans_bold);
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
                               {
                                   ImGui::GlobalSpdLogWindow().sink()->set_level(
                                       spdlog::level::level_enum(selected_option));
                                   spdlog::info("Setting verbosity threshold to level {:d}.", selected_option);
                               },
                               nullptr, ICON_MY_LOG_LEVEL});

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
            ImGui::ScopedFont sf{m_sans_bold, 10};

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

    auto &io = ImGui::GetIO();

    // Center window horizontally, align near top vertically
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, 5.f * HelloImGui::EmSize()), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));

    ImGui::SetNextWindowFocus();
    constexpr float icon_size = 128.f;
    float2          col_width = {icon_size + HelloImGui::EmSize(), 32 * HelloImGui::EmSize()};
    col_width[1]              = std::clamp(col_width[1], 5 * HelloImGui::EmSize(),
                                           io.DisplaySize.x - ImGui::GetStyle().WindowPadding.x -
                                               2 * ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().ScrollbarSize - col_width[0]);

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
                ImGui::ScopedFont sf{m_sans_bold, 30};
                ImGui::HyperlinkText("HDRView", "https://github.com/wkjarosz/hdrview");
            }

            ImGui::PushFont(m_sans_bold, 18);
            ImGui::TextUnformatted(version());
            ImGui::PopFont();
            ImGui::PushFont(m_sans_regular, 10);
#if defined(__EMSCRIPTEN__)
            ImGui::TextFmt("Built with emscripten using the {} backend on {}.", backend(), build_timestamp());
#else
            ImGui::TextFmt("Built using the {} backend on {}.", backend(), build_timestamp());
#endif
            ImGui::PopFont();

            ImGui::Spacing();

            ImGui::PushFont(m_sans_bold, 16);
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
            ImGui::PushFont(m_sans_bold, 14);
            ImGui::HyperlinkText(name, url);
            ImGui::PopFont();
            ImGui::TableNextColumn();

            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[1] - HelloImGui::EmSize());
            ImGui::PushFont(m_sans_regular, 14);
            ImGui::TextUnformatted(desc);
            ImGui::PopFont();
        };

        if (ImGui::BeginTabBar("AboutTabBar"))
        {
            if (ImGui::BeginTabItem("Keybindings", nullptr))
            {
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[0] + col_width[1]);

                ImGui::PushFont(m_sans_bold, 14);
                ImGui::TextAligned("The main keyboard shortcut to remember is:", 0.5f);
                ImGui::PopFont();

                ImGui::PushFont(font("mono regular"), 30);
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

                    item_and_description("Dear ImGui", "Omar Cornut's immediate-mode graphical user interface for C++.",
                                         "https://github.com/ocornut/imgui");
#ifdef HDRVIEW_ENABLE_UHDR
                    item_and_description("libuhdr", "For loading Ultra HDR JPEG files.",
                                         "https://github.com/google/libultrahdr");
#endif
#ifdef HDRVIEW_ENABLE_JPEGXL
                    item_and_description("libjxl", "For loading JPEG-XL files.", "https://github.com/libjxl/libjxl");
#endif
#ifdef HDRVIEW_ENABLE_HEIF
                    item_and_description("libheif", "For loading HEIF, HEIC, AVIF, and AVIFS files.",
                                         "https://github.com/strukturag/libheif");
#endif
#ifdef HDRVIEW_ENABLE_LIBPNG
                    item_and_description("libpng", "For loading PNG files.", "https://github.com/pnggroup/libpng");
#endif
#ifdef HDRVIEW_ENABLE_LCMS2
                    item_and_description("lcms2", "LittleCMS color management engine.",
                                         "https://github.com/mm2/Little-CMS");
#endif
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
                    item_and_description("stb_image/write", "Single-Header libraries for loading/writing images.",
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
            ImGui::Shortcut(ImGuiKey_Enter) || ImGui::Shortcut(ImGuiKey_Space) ||
            ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_Slash))
        {
            ImGui::CloseCurrentPopup();
            g_show_help = false;
        }

        ImGui::ScrollWhenDraggingOnVoid(ImVec2(0.0f, -ImGui::GetIO().MouseDelta.y), ImGuiMouseButton_Left);
        ImGui::EndPopup();
    }
}
