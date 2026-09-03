#include "app.h"

#include <algorithm>

#include "dithermatrix256.h"
#include "fonts.h"
#include "image.h"
#include "imgui.h"
#include "imgui_ext.h"
#include "platform_utils.h"
#include <hello_imgui/dpi_aware.h>
#include <hello_imgui/hello_imgui.h>

#include <ImfThreading.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#include "portable-file-dialogs.h"
#endif

#ifdef HELLOIMGUI_USE_GLFW3
#include <GLFW/glfw3.h>
#ifdef __APPLE__
// on macOS, we need to include this to get the NS api for opening files
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#endif
#endif

using namespace std;
using namespace HelloImGui;

static HDRViewApp *g_hdrview = nullptr;

void init_hdrview(optional<float> exposure, optional<float> gamma, optional<bool> dither, optional<bool> force_sdr,
                  optional<bool> apple_keys, const vector<string> &in_files)
{
    if (g_hdrview)
    {
        spdlog::critical("HDRView already created!");
        exit(EXIT_FAILURE);
    }

    spdlog::info("Overriding exposure: {}", exposure.has_value());
    spdlog::info("Overriding gamma: {}", gamma.has_value());
    spdlog::info("Overriding dither: {}", dither.has_value());
    spdlog::info("Forcing SDR: {}", force_sdr.has_value());
    spdlog::info("Overriding Apple-keyboard behavior: {}", apple_keys.has_value());

    g_hdrview = new HDRViewApp(exposure, gamma, dither, force_sdr, apple_keys, in_files);

    // load after g_hdrview is set: session loading calls hdrview()
    g_hdrview->load_images(in_files);
}

HDRViewApp *hdrview() { return g_hdrview; }

const char *mouse_mode_action_name(MouseMode m)
{
    switch (m)
    {
    case MouseMode_PanZoom: return "Pan and zoom";
    case MouseMode_RectangularSelection: return "Rectangular select";
    case MouseMode_ColorInspector: return "Pixel/color inspector";
    default: return "Pan and zoom";
    }
}

void HDRViewApp::set_mouse_mode(MouseMode m)
{
    m_mouse_mode = m;
    for (int i = 0; i < MouseMode_COUNT; ++i) m_mouse_mode_enabled[i] = (i == m);
}

HDRViewApp::HDRViewApp(optional<float> force_exposure, optional<float> force_gamma, optional<bool> force_dither,
                       optional<bool> force_sdr, optional<bool> force_apple_keys, vector<string> in_files)
{
    setup_window_and_backend(force_sdr);
    setup_hello_imgui_params();
    m_edit_commands = all_edit_commands();

    auto window_setup = setup_dockable_windows();
    setup_platform_backend_callbacks(in_files);
    setup_persistence_callbacks(force_exposure, force_gamma, force_dither, force_apple_keys);
    setup_imgui_style_callbacks();
    setup_frame_callbacks();
    setup_dialogs(in_files);
    setup_actions(window_setup.mod_key, window_setup.window_info);
}

void HDRViewApp::setup_window_and_backend(optional<bool> force_sdr)
{
#if defined(__EMSCRIPTEN__) && !defined(HELLOIMGUI_EMSCRIPTEN_PTHREAD)
    // if threading is disabled, create no threads
    unsigned threads = 0;
#elif defined(HELLOIMGUI_EMSCRIPTEN_PTHREAD)
    // if threading is enabled in emscripten, then use just 1 thread
    unsigned threads = 1;
#else
    unsigned threads = thread::hardware_concurrency();
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

    // Whether it is worth asking for a floating-point framebuffer at all. Only macOS can answer up front:
    // hasEdrSupport() inspects the attached NSScreens' EDR headroom. Elsewhere the display's capabilities are
    // window-scoped and there is no window yet, so we ask and find out afterwards (see m_float_buffer, set in
    // PostInit_AddPlatformBackendCallbacks below).
    m_force_sdr = force_sdr.value_or(false);
    if (m_force_sdr)
        spdlog::info("Forcing SDR display mode.");

#if defined(__APPLE__)
    const bool want_float_buffer = hasEdrSupport() && !m_force_sdr;
#else
    const bool want_float_buffer = !m_force_sdr;
#endif

    m_params.rendererBackendOptions.requestFloatBuffer = want_float_buffer;

#if defined(GLFW_WAYLAND_COLOR_MANAGEMENT)
    // Wayland only negotiates a color space for our surface if color management is enabled before
    // glfwInit(), and it defaults off. Init hints are written to a file-static that glfwInit() copies in, so
    // setting it here -- before HelloImGui::Run() -- is the documented way to do this from an application
    // and needs no cooperation from Hello ImGui.
    //
    // Gated on wanting a float buffer: once color management is on, the compositor tags every surface's
    // color space, not just float-buffer ones, so an ordinary SDR window could get negotiated into a
    // non-default color space for no benefit.
    if (want_float_buffer)
        glfwInitHint(GLFW_WAYLAND_COLOR_MANAGEMENT, GLFW_TRUE);
#endif

#if defined(HELLOIMGUI_HAS_OPENGL) && !defined(__EMSCRIPTEN__)
    // Our generated desktop shaders are GLSL 4.10 (see sokol_shdc_generate() in CMakeLists.txt), so
    // request a matching context instead of Hello ImGui's default 3.3 core. GlslVersion is left
    // alone: it configures ImGui's own shaders, whose default is still valid under 4.1 core.
    m_params.rendererBackendOptions.openGlOptions.MajorVersion = 4;
    m_params.rendererBackendOptions.openGlOptions.MinorVersion = 1;
#endif

    spdlog::info("Requesting a {} framebuffer.", want_float_buffer ? "floating-point precision" : "standard precision");
}

void HDRViewApp::setup_hello_imgui_params()
{
    // set up HelloImGui parameters
    m_params.appWindowParams.windowGeometry.size     = {1200, 800};
    m_params.appWindowParams.windowTitle             = "HDRView";
    m_params.appWindowParams.restorePreviousGeometry = true;

    // Setting this to true allows multiple viewports where you can drag windows outside out the main window in
    // order to put their content into new native windows
    m_params.imGuiWindowParams.enableViewports        = false;
    m_params.imGuiWindowParams.defaultImGuiWindowType = DefaultImGuiWindowType::ProvideFullScreenDockSpace;
    // m_params.imGuiWindowParams.backgroundColor        = float4{0.15f, 0.15f, 0.15f, 1.f};

    m_params.fpsIdling.rememberEnableIdling = true;

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
    m_top_toolbar_options.sizeEm          = 2.34285714f; // (14+8+1)/14 + 2*0.35
    m_top_toolbar_options.WindowPaddingEm = ImVec2(0.7f, 0.35f);
    m_params.callbacks.AddEdgeToolbar(EdgeToolbarType::Top, [this]() { draw_top_toolbar(); }, m_top_toolbar_options);

    //
    // Status bar
    //
    // We use the default status bar of Hello ImGui
    m_params.imGuiWindowParams.showStatusBar  = false;
    m_params.imGuiWindowParams.showStatus_Fps = false;
    m_params.callbacks.ShowStatus             = [this]() { draw_status_bar(); };
}

HDRViewApp::WindowSetupInfo HDRViewApp::setup_dockable_windows()
{
    //
    // Dockable windows
    //

    DockableWindow statistics_window{"Pixel statistics", "RightSpace", [this] { draw_statistics_window(); }};
    DockableWindow file_window{"Images", "ImagesSpace", [this] { draw_file_window(); }};
    file_window.focusWindowAtNextFrame = true;

    DockableWindow info_window{"Info", "RightSpace", [this]
                               {
                                   if (auto img = current_image())
                                       return img->draw_info();
                               }};
    DockableWindow colorspace_window{"Colorspace", "RightSpace", [this]
                                     {
                                         if (auto img = current_image())
                                             return img->draw_colorspace();
                                     }};
    // The vertical scrollbar is unconditional: the chromaticity diagram's height is locked to the available
    // width, so a scrollbar toggling in and out would resize the diagram, which can toggle it again every frame.
    colorspace_window.imGuiWindowFlags =
        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar;

    // Hidden by default, like the log: an image with no history has nothing to show, and the Edit menu
    // already names what undo and redo would do.
    DockableWindow history_window{"History", "RightSpace", [this] { draw_history_window(); }, false};

    DockableWindow log_window{
        "Log", "LogSpace",
        [this] { ImGui::GlobalSpdLogWindow().draw(font("mono regular"), ImGui::GetStyle().FontSizeBase); }, false};

#if !defined(__EMSCRIPTEN__)
    // Both halves of this window are images that arrive without being opened: a folder being watched, and a
    // renderer pushing pixels in. The window keeps its name so existing saved layouts still place it.
    DockableWindow watched_folders_window{"Watched Folders", "WatchedFoldersSpace", [this]
                                          {
#if HDRVIEW_ENABLE_IPC
                                              draw_ipc_gui();
#endif
                                              m_image_loader.draw_gui();
                                          }};
#endif

#ifdef _WIN32
    ImGuiKey modKey = ImGuiMod_Alt;
#else
    ImGuiKey modKey = ImGuiMod_Super;
#endif

    // docking layouts
    m_params.dockingParams.layoutName      = "Pixel peeper";
    m_params.dockingParams.dockableWindows = {statistics_window,
                                              file_window,
                                              info_window,
                                              colorspace_window,
                                              history_window,
                                              log_window
#if !defined(__EMSCRIPTEN__)
                                              ,
                                              watched_folders_window
#endif
    };
    auto log_window_it =
        std::find_if(m_params.dockingParams.dockableWindows.begin(), m_params.dockingParams.dockableWindows.end(),
                     [](const DockableWindow &w) { return w.label == "Log"; });
    m_log_window = log_window_it != m_params.dockingParams.dockableWindows.end() ? &*log_window_it : nullptr;

    // Order here must match the order of dockableWindows above.
    vector<DockableWindowExtraInfo> window_info = {{ImGuiKey_F6, ICON_MY_STATISTICS_WINDOW},
                                                   {ImGuiKey_F7, ICON_MY_FILES_WINDOW},
                                                   {ImGuiMod_Ctrl | ImGuiKey_I, ICON_MY_INFO_WINDOW},
                                                   {ImGuiKey_F8, ICON_MY_COLORSPACE_WINDOW},
                                                   {ImGuiKey_F9, ICON_MY_HISTORY},
                                                   {modKey | ImGuiKey_GraveAccent, ICON_MY_LOG_WINDOW}
#if !defined(__EMSCRIPTEN__)
                                                   ,
                                                   {ImGuiKey_None, ICON_MY_ADD_WATCHED_FOLDER}
#endif
    };

    // Left column: "Images" occupies the top 80%, "Watched Folders" the bottom 20%. Each dock space holds a
    // single window, so auto-hide their tab bars.
    std::vector<DockingSplit> docking_splits = {
        DockingSplit{"MainDockSpace", "ImagesSpace", ImGuiDir_Left, 0.2f, ImGuiDockNodeFlags_AutoHideTabBar},
        DockingSplit{"ImagesSpace", "WatchedFoldersSpace", ImGuiDir_Down, 0.2f, ImGuiDockNodeFlags_AutoHideTabBar},
        DockingSplit{"MainDockSpace", "LogSpace", ImGuiDir_Down, 0.25f},
        DockingSplit{"MainDockSpace", "RightSpace", ImGuiDir_Right, 0.25f}};
    m_params.dockingParams.dockingSplits = docking_splits;

    // Builds an alternate layout from the same dockable windows as the default one, letting `customize`
    // override each copy's dockSpaceName/isVisible/etc.
    auto make_layout = [&](string name, vector<DockingSplit> splits, auto &&customize)
    {
        DockingParams p;
        p.layoutName      = std::move(name);
        p.dockingSplits   = std::move(splits);
        p.dockableWindows = m_params.dockingParams.dockableWindows;
        for (auto &w : p.dockableWindows) customize(w);
        return p;
    };

    // "Image browser": the panel geometry of "Pixel peeper", but only Images/Watched Folders start open.
    DockingParams image_browser_layout =
        make_layout("Image browser", docking_splits,
                    [](DockableWindow &w) { w.isVisible = (w.label == "Images" || w.label == "Watched Folders"); });

    // "Metadata": Images and Watched Folders tabbed together atop the left column, Info below them, Log along
    // the bottom; only those three start open.
    DockingParams metadata_layout =
        make_layout("Metadata",
                    {DockingSplit{"MainDockSpace", "ImagesSpace", ImGuiDir_Left, 0.2f},
                     DockingSplit{"ImagesSpace", "InfoSpace", ImGuiDir_Down, 0.54f, ImGuiDockNodeFlags_AutoHideTabBar},
                     DockingSplit{"MainDockSpace", "LogSpace", ImGuiDir_Down, 0.25f},
                     DockingSplit{"MainDockSpace", "RightSpace", ImGuiDir_Right, 0.25f}},
                    [](DockableWindow &w)
                    {
                        if (w.label == "Watched Folders")
                            w.dockSpaceName = "ImagesSpace";
                        else if (w.label == "Info")
                            w.dockSpaceName = "InfoSpace";
                        w.isVisible = (w.label == "Images" || w.label == "Info" || w.label == "Watched Folders");
                    });

    m_params.alternativeDockingLayouts = {image_browser_layout, metadata_layout};

    return {modKey, window_info};
}

void HDRViewApp::setup_platform_backend_callbacks(vector<string> in_files)
{
#if defined(HELLOIMGUI_USE_GLFW3)
    m_params.callbacks.PostInit_AddPlatformBackendCallbacks = [this, in_files]
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

        // Hello ImGui clears requestFloatBuffer when the request could not be satisfied, so by now this is
        // the achieved framebuffer, not the one we asked for.
        m_float_buffer = m_params.rendererBackendOptions.requestFloatBuffer;
        spdlog::info("Got a {} framebuffer.", m_float_buffer ? "floating-point precision" : "standard precision");

        // Seed the display color space before the first frame; update_colorpass() re-queries it every frame.
        m_display_cs = query_display_colorspace(m_params.backendPointers.glfwWindow);
        spdlog::info("Display color space is {} ({} HDR).", m_display_cs.name(),
                     supports_hdr() ? "supports" : "does not support");
#ifdef __APPLE__
        // On macOS, the mechanism for opening an application passes filenames
        // through the NS api rather than CLI arguments, which means we need
        // special handling of these through GLFW.
        // There are two components to this special handling:
        // (both of which need to happen here instead of HDRViewApp() because GLFW needs to have been
        // initialized first)

        // 1) Check if any filenames were passed via the NS api when the first instance of HDRView is launched.
        // However, this also seemingly returns (just the last) command-line argument, so if in_files is not empty, we
        // ignore it and rely on that mechanism to load the files
        if (in_files.empty())
        {
            const char *const *opened_files = glfwGetOpenedFilenames();
            if (opened_files)
            {
                spdlog::debug("Passing files in through the NS api...");
                vector<string> args;
                for (auto p = opened_files; *p; ++p) { args.emplace_back(string(*p)); }
                load_images(args);
            }
        }

        // 2) Register a callback on the running instance of HDRView for when the user:
        //    a) drags a file onto the HDRView app icon in the dock, and/or
        //    b) launches HDRView with files (either from the command line or Finder) when another instance is
        //    already
        //       running
        glfwSetOpenedFilenamesCallback(
            [](const char *image_file)
            {
                spdlog::debug("Receiving an app drag-drop event through the NS api for file '{}'", image_file);
                hdrview()->load_images({string(image_file)});
            });
#endif
    };
#endif
}

void HDRViewApp::setup_persistence_callbacks(optional<float> force_exposure, optional<float> force_gamma,
                                             optional<bool> force_dither, optional<bool> force_apple_keys)
{
    //
    // Load user settings at `PostInit` and save them at `BeforeExit`
    //

    m_params.iniFolderType      = IniFolderType::AppUserConfigFolder;
    m_params.iniFilename        = "HDRView/settings.ini";
    m_params.callbacks.PostInit = [this, force_exposure, force_gamma, force_dither, force_apple_keys]
    {
#if defined(__EMSCRIPTEN__)
        // see platform_utils
        install_touch_handlers();
        install_navigation_guard();
#endif

        spdlog::info("Loading user settings from '{}'", IniSettingsLocation(m_params).value_or("(disabled)"));

        auto s = LoadUserPref("UserSettings");
        if (!s.empty())
        {
            try
            {
                json j = json::parse(s);
                spdlog::debug("Restoring recent file list...");
                m_image_loader.set_recent_files(j.value<vector<string>>("recent files", {}));
                m_bg_mode = (BackgroundMode_)clamp(j.value<int>("background mode", (int)m_bg_mode), (int)BGMode_Black,
                                                   (int)BGMode_COUNT - 1);
                m_bg_color.xyz() = j.value<float3>("background color", m_bg_color.xyz());

                m_draw_data_window    = j.value<bool>("draw data window", m_draw_data_window);
                m_draw_display_window = j.value<bool>("draw display window", m_draw_display_window);
                m_auto_fit_data       = j.value<bool>("auto fit data window", m_auto_fit_data);
                m_auto_fit_display    = j.value<bool>("auto fit display window", m_auto_fit_display);
                m_auto_fit_selection  = j.value<bool>("auto fit selection", m_auto_fit_selection);
                m_draw_pixel_info     = j.value<bool>("draw pixel info", m_draw_pixel_info);
                m_draw_grid           = j.value<bool>("draw pixel grid", m_draw_grid);
                m_exposure_live = m_exposure = j.value<float>("exposure", m_exposure);
                m_gamma_live = m_gamma = std::max(MIN_GAMMA, j.value<float>("gamma", m_gamma));
                m_tonemap        = (Tonemap_)clamp<int>(j.value<Tonemap_>("tonemap", m_tonemap), 0, Tonemap_COUNT - 1);
                m_clamp_to_LDR   = j.value<bool>("clamp to LDR", m_clamp_to_LDR);
                m_dither         = j.value<bool>("dither", m_dither);
                m_file_list_mode = clamp<int>(j.value<int>("file list mode", m_file_list_mode), 0, 2);
                m_short_names    = j.value<bool>("short names", m_short_names);
                // "draw clip warnings" is the older key, a single toggle covering both ends; fall back to it
                // so settings written by an earlier version still take effect
                bool both          = j.value<bool>("draw clip warnings", false);
                m_clip_warnings    = j.value<bool2>("clip warnings", bool2{both, both});
                m_show_FPS         = j.value<bool>("show FPS", m_show_FPS);
                m_clip_range       = j.value<float2>("clip range", m_clip_range);
                m_histogram_height = j.value<float>("histogram height", m_histogram_height);
                // Clamped: it indexes pixel_color_widget()'s current/reference/composite choices, and the
                // settings file is ordinary user-editable text.
                m_status_pixel_target = clamp<int>(j.value<int>("status pixel target", m_status_pixel_target), 0, 2);
                m_x_scale        = clamp<int>(j.value<int>("histogram x scale", m_x_scale), 0, AxisScale_COUNT - 1);
                m_y_scale        = clamp<int>(j.value<int>("histogram y scale", m_y_scale), 0, AxisScale_COUNT - 1);
                m_playback_speed = j.value<float>("playback speed", m_playback_speed);
                m_colormap_index = clamp<int>(j.value<int>("colormap index", 0), 0, (int)std::size(m_colormaps) - 1);
                m_show_developer_menu    = j.value<bool>("show developer menu", m_show_developer_menu);
                m_show_tool_palette      = j.value<bool>("show tool palette", m_show_tool_palette);
                m_tool_palette_collapsed = j.value<bool>("tool palette collapsed", m_tool_palette_collapsed);
                m_tool_palette_vertical  = j.value<bool>("tool palette vertical", m_tool_palette_vertical);
                m_tool_palette_corner    = clamp<int>(j.value<int>("tool palette corner", m_tool_palette_corner), 0, 3);
            }
            catch (json::exception &e)
            {
                spdlog::error("Error while parsing user settings: {}", e.what());
            }
        }
        else
        {
            spdlog::warn("No user settings found, using defaults.");
        }

        setup_rendering();

        if (force_exposure.has_value())
            m_exposure_live = m_exposure = *force_exposure;
        // Only the floor: gamma is inverted before use, so zero divides by zero and a negative value sends
        // a black pixel to infinity. Large values are just steep curves and are left alone.
        if (force_gamma.has_value())
            m_gamma_live = m_gamma = std::max(MIN_GAMMA, *force_gamma);
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
        cleanup_colorpass();
        m_shader.reset();
        m_render_pass.reset();

        spdlog::info("Saving user settings to '{}'", IniSettingsLocation(m_params).value_or("(disabled)"));

        json j;
        j["recent files"]            = m_image_loader.recent_files();
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
        j["file list mode"]          = m_file_list_mode;
        j["short names"]             = m_short_names;
        j["clip warnings"]           = m_clip_warnings;
        j["show FPS"]                = m_show_FPS;
        j["clip range"]              = m_clip_range;
        j["histogram height"]        = m_histogram_height;
        j["status pixel target"]     = m_status_pixel_target;
        j["histogram x scale"]       = m_x_scale;
        j["histogram y scale"]       = m_y_scale;
        j["show developer menu"]     = m_show_developer_menu;
        j["show tool palette"]       = m_show_tool_palette;
        j["tool palette collapsed"]  = m_tool_palette_collapsed;
        j["tool palette vertical"]   = m_tool_palette_vertical;
        j["tool palette corner"]     = m_tool_palette_corner;
        j["playback speed"]          = m_playback_speed;
        j["colormap index"]          = m_colormap_index;

        m_theme.save(j, m_params.dpiAwareParams.dpiWindowSizeFactor);

        SaveUserPref("UserSettings", j.dump(4));

        // Stop the thread pool here rather than in its static destructor: that destructor's ordering against
        // other statics (e.g. spdlog's logger registry, which worker threads touch) is unspecified, so a
        // shutting-down worker could race the destruction of globals it depends on. try_singleton() is a
        // no-op if the pool was never used.
        if (auto *pool = stp::ThreadPool::try_singleton())
            pool->stop();
    };
}

void HDRViewApp::setup_imgui_style_callbacks()
{
    // Change style
    m_params.callbacks.SetupImGuiStyle = [this]()
    {
        json j;
        try
        {
            auto s = LoadUserPref("UserSettings");
            if (!s.empty())
                j = json::parse(s);
        }
        catch (const std::exception &e)
        {
            spdlog::error("Error while parsing user settings: {}", e.what());
        }

        m_theme.load(j);
    };

    m_params.callbacks.SetupImGuiConfig = []()
    {
        ImGuiIO &io = ImGui::GetIO();
        // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    };
}

void HDRViewApp::wake_event_loop()
{
    // glfwPostEmptyEvent() is the one GLFW entry point documented as callable from any thread, which is what
    // makes this usable from the IPC receive thread. The web build has no GLFW here, and its frame loop is
    // driven by the browser rather than by events.
#if defined(HELLOIMGUI_USE_GLFW3)
    // Nothing to wake before the window exists, and calling into GLFW that early is an error.
    if (m_params.backendPointers.glfwWindow)
        glfwPostEmptyEvent();
#endif
}

void HDRViewApp::post_to_main_thread(std::function<void()> f)
{
    if (!f)
        return;

    std::lock_guard lock{m_main_thread_mutex};
    m_main_thread_queue.push_back(std::move(f));
}

void HDRViewApp::drain_main_thread_queue()
{
    // Swap the queue out under the lock and run the callables without it: they reach back into the app, and
    // work they post belongs to the next frame.
    std::vector<std::function<void()>> todo;
    {
        std::lock_guard lock{m_main_thread_mutex};
        todo.swap(m_main_thread_queue);
    }

    for (auto &f : todo)
    {
        try
        {
            f();
        }
        catch (const std::exception &e)
        {
            // one bad task must not take the frame down with it
            spdlog::error("Exception while running main-thread task: {}", e.what());
        }
    }
}

void HDRViewApp::setup_frame_callbacks()
{
    m_params.callbacks.ShowGui = [this]()
    {
        drain_main_thread_queue();
        drain_running_filter();

#if HDRVIEW_ENABLE_IPC
        // See m_ipc_listen_requested: the toggle's Action needs a bool, but the socket is the truth.
        m_ipc_listen_requested = m_ipc_server.is_listening();
#endif

        process_shortcuts();

        for (auto &d : m_dialogs) d->draw(d->open);

        // recompute toolbar height in case the font size was changed
        // this is require because HelloImGui decided to specify toolbar sizes in Ems, but we want the padding and size
        // to be consistent with other ImGui elements (1 line high + standard Frame padding)
        if (auto it = m_params.callbacks.edgesToolbars.find(EdgeToolbarType::Top);
            it != m_params.callbacks.edgesToolbars.end())
        {
            m_top_toolbar_options.WindowPaddingEm =
                PixelsToEm(ImVec2(ImGui::GetStyle().WindowPadding.x, ImGui::GetStyle().FramePadding.y));
            m_top_toolbar_options.sizeEm =
                PixelSizeToEm(ImGui::GetFrameHeight() + 1) + 2.f * m_top_toolbar_options.WindowPaddingEm.y;
            it->second.options = m_top_toolbar_options;
        }

        m_image_loader.get_loaded_images(
            [this](ImagePtr new_image, ImagePtr to_replace, bool should_select)
            {
                // A replacement whose target has gone was a reload of an image closed while it was still
                // loading; appending it would put back the image that was just closed.
                const int idx = to_replace ? image_index(to_replace) : -1;
                if (to_replace && !is_valid(idx))
                {
                    spdlog::debug("Discarding reload of '{}': it was closed while loading.", new_image->filename);
                    return;
                }

#if !defined(__EMSCRIPTEN__)
                std::error_code ec;
                auto            path = fs::weakly_canonical(new_image->filename, ec);
                if (ec)
                    return;

                m_active_directories.insert(path.parent_path());
#endif

                if (is_valid(idx))
                    m_images[idx] = new_image;
                else
                    m_images.push_back(new_image);

                if (should_select)
                    m_current = is_valid(idx) ? idx : int(m_images.size() - 1);

                resolve_loading_session_image(new_image);

                update_visibility(); // this also calls set_image_textures();
                m_request_sort = true;
            });

        if (m_loading_session && m_image_loader.num_pending_images() == 0)
            finish_loading_session();

        draw_tool_palette();
        draw_tweak_window();
        draw_developer_windows();
    };
    m_params.callbacks.CustomBackground = [this]() { draw_background(); };
    m_params.callbacks.BeforeSwap       = [this]() { end_colorpass_frame(); };
}

auto HDRViewApp::dialog(const string &title) -> PopupDialog &
{
    for (auto &d : m_dialogs)
        if (d->title == title)
            return *d;
    spdlog::critical("Unknown dialog: '{}'", title);
    exit(EXIT_FAILURE);
}

void HDRViewApp::setup_dialogs(const vector<string> &in_files)
{
    m_dialogs.push_back(
        make_unique<PopupDialog>("About", [this](bool &open) { draw_about_dialog(open); }, in_files.empty()));
    m_dialogs.push_back(
        make_unique<PopupDialog>("Command palette...", [this](bool &open) { draw_command_palette(open); }));
    m_dialogs.push_back(make_unique<PopupDialog>("Save as...", [this](bool &open) { draw_save_as_dialog(open); }));
    m_dialogs.push_back(
        make_unique<PopupDialog>("Image loading options...", [](bool &open) { draw_load_image_options_dialog(open); }));
    m_dialogs.push_back(
        make_unique<PopupDialog>("Replace session?", [this](bool &open) { draw_confirm_load_session_dialog(open); }));
    m_dialogs.push_back(make_unique<PopupDialog>("Discard unsaved changes?",
                                                 [this](bool &open) { draw_confirm_discard_dialog(open); }));
    // Every command that has one, so a dialog cannot be forgotten when a command is added, and all of
    // them wear the same shell, subject selector and footer; see draw_edit_command_dialog().
    for (auto &cmd : m_edit_commands)
        if (cmd->info().has_dialog)
        {
            EditCommand *c = cmd.get();
            m_dialogs.push_back(make_unique<PopupDialog>(c->info().names.front(), [this, c](bool &open)
                                                         { draw_edit_command_dialog(*c, open); }));
        }

    m_dialogs.push_back(
        make_unique<PopupDialog>("Applying filter...", [this](bool &open) { draw_filter_progress_dialog(open); }));
    m_dialogs.push_back(
        make_unique<PopupDialog>("Loading session...", [this](bool &open) { draw_loading_session_dialog(open); }));
    m_dialogs.push_back(make_unique<PopupDialog>(
        "Create dither image...",
        [this](bool &open)
        {
            static int2  size = {256, 256};
            static bool  tent = false;
            static Box1f range{0.0f, 1.0f};
            ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
            if (ImGui::BeginModalDialog("Create dither image...", open, ImGui::DialogPosition::Center))
            {
                ImGui::InputInt2("Size", &size.x);
                ImGui::Checkbox("Tent dither", &tent);
                ImGui::DragFloatRange2("Value range", &range.min.x, &range.max.x, 0.01f, -FLT_MAX, FLT_MAX, "min: %.3f",
                                       "max: %.3f");

                auto result = ImGui::DialogButtons("Create");
                if (result == ImGui::DialogResult::Cancel)
                    ImGui::CloseCurrentPopup();
                else if (result == ImGui::DialogResult::Confirm)
                {
                    auto img = std::make_shared<Image>(size, 1);

                    float dst_range = range.size().x;

                    int block_size = std::max(1, 1024 * 1024 / size.x);

                    parallel_for(blocked_range<int>(0, size.y, block_size),
                                 [img, w = size.x, dst_range](int begin_y, int end_y, int, int)
                                 {
                                     for (int y = begin_y; y < end_y; ++y)
                                         for (int x = 0; x < w; ++x)
                                         {
                                             float dither_val       = tent ? tent_dither(x, y) : box_dither(x, y);
                                             img->channels[0](x, y) = (dither_val + 0.5f) * dst_range + range.min.x;
                                         }
                                 });

                    img->filename = fmt::format("dither_{}x{}_{}_{:.3f}-{:.3f}", size.x, size.y, tent ? "tent" : "box",
                                                range.min.x, range.max.x);
                    img->path     = fs::u8path(img->filename);
                    img->finalize();

                    m_images.push_back(img);
                    m_current = int(m_images.size()) - 1;
                    update_visibility(); // this also calls set_image_textures();
                    m_request_sort = true;

                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }));
    m_dialogs.push_back(make_unique<PopupDialog>(
        "Create gradient image...",
        [this](bool &open)
        {
            static int2   res    = {256, 256};
            static float  dither = 1.f;
            static int    levels = 1;
            static float4 c00{0.f, 0.f, 1.f, 1.f}, c10{1.f, 0.f, 0.f, 1.f}, c11{1.f, 1.f, 0.f, 1.f},
                c01{0.f, 1.f, 0.f, 1.f};
            ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
            if (ImGui::BeginModalDialog("Create gradient image...", open, ImGui::DialogPosition::Center))
            {
                ImGui::InputInt2("Resolution", &res.x);
                static int channel_mode = 1; // Default to RGB
                static int num_channels = 3;
                if (ImGui::Combo("Channels", &channel_mode, "Gray\0RGB\0RGBA\0"))
                    num_channels = channel_mode == 0 ? 1 : channel_mode == 1 ? 3 : 4;
                ImGui::SliderInt("Quantization levels", &levels, 2, 256);
                ImGui::Tooltip("If >= 2, quantize the result to this many discrete levels.");
                ImGui::BeginDisabled(levels <= 1);
                ImGui::SliderFloat("Dither amount", &dither, 0.0f, 1.0f);
                ImGui::EndDisabled();
                auto flags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float;
                // ImGui::TextUnformatted("Top-left color");
                // ImGui::SameLine();
                ImGui::ColorEdit4("##Top-left color", &c00.x, flags);
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::ColorEdit4("Corner colors##Top-right color", &c10.x, flags);
                // ImGui::SameLine();
                // ImGui::TextUnformatted("Top-right color");
                // ImGui::TextUnformatted("Bottom-left color");
                // ImGui::SameLine();
                ImGui::ColorEdit4("##Bottom-left color", &c01.x, flags);
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::ColorEdit4("##Bottom-right color", &c11.x, flags);
                // ImGui::SameLine();
                // ImGui::TextUnformatted("Bottom-right color");

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                auto result = ImGui::DialogButtons("Create");
                if (result == ImGui::DialogResult::Cancel)
                    ImGui::CloseCurrentPopup();
                else if (result == ImGui::DialogResult::Confirm)
                {
                    auto img = std::make_shared<Image>(res, num_channels);

                    int block_size = std::max(1, 1024 * 1024 / res.x);

                    // from https://computergraphics.stackexchange.com/a/8777
                    // Dithers and quantizes color value c in [0, 1] to the given number of levels.
                    auto dither_quantize = [](float c, int levels, int x, int y, float amount)
                    {
                        float cmax = float(levels) - 1.f;
                        float ci   = (c * cmax);

                        // Symmetric triangular distribution on [-1, 1] for general case; uniform distribution on [-0.5,
                        // 0.5] when near boundary
                        float d = (ci - amount + 0.5f < 0.0 || ci + amount + 0.5f >= cmax + 1.0f)
                                      ? box_dither(x, y)
                                      : 2.f * tent_dither(x, y);
                        return int(std::clamp(ci + amount * d + 0.5f, 0.0f, cmax));
                    };

                    parallel_for(blocked_range<int>(0, res.y, block_size),
                                 [img, w = res.x, h = res.y, dither_quantize](int begin_y, int end_y, int, int)
                                 {
                                     for (int y = begin_y; y < end_y; ++y)
                                         for (int x = 0; x < w; ++x)
                                         {
                                             float  u      = (x + 0.5f) / w;
                                             float  v      = (y + 0.5f) / h;
                                             float4 bilerp = c00 * (1 - u) * (1 - v) + c10 * u * (1 - v) +
                                                             c01 * (1 - u) * v + c11 * u * v;

                                             for (int c = 0; c < (int)img->channels.size(); ++c)
                                                 img->channels[c](x, y) =
                                                     levels > 1 ? dither_quantize(bilerp[c], levels, x, y, dither) /
                                                                      (levels - 1.f)
                                                                : bilerp[c];
                                         }
                                 });

                    img->filename = fmt::format("gradient_{}x{}", res.x, res.y);
                    img->path     = fs::u8path(img->filename);
                    img->finalize();

                    m_images.push_back(img);
                    m_current = int(m_images.size()) - 1;
                    update_visibility(); // this also calls set_image_textures();
                    m_request_sort = true;

                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }));
}

void HDRViewApp::setup_actions(ImGuiKey modKey, const vector<DockableWindowExtraInfo> &window_info)
{
    //
    // Actions and command palette
    //
    {
        const auto always_enabled = []() { return true; };
        const auto if_img         = [this]() { return current_image() != nullptr; };

        // The emscripten GLFW port calls preventDefault() on every key it receives except cut/copy/paste, so
        // on the web HDRView's chords reach it -- but only the ones the browser delivers at all. Ctrl/Cmd+Q,
        // +W and +Shift+W never reach the page, so leaving them bound would advertise a shortcut that quits
        // the browser or closes the tab.
#if defined(__EMSCRIPTEN__)
        constexpr bool k_browser_reserved = true;
#else
        constexpr bool k_browser_reserved = false;
#endif
        using ImGui::Action;
        auto add = [this](const Action &a)
        {
            // Actions are keyed by their primary name, so registering one twice silently replaces the first.
            if (m_actions.count(a.names[0]))
                spdlog::error("Action '{}' is registered more than once; the earlier one is unreachable.", a.names[0]);
            m_actions[a.names[0]] = a;
        };
        add(Action{{"Open image..."}, ICON_MY_OPEN_IMAGE, ImGuiMod_Ctrl | ImGuiKey_O, 0, [this]() { open_image(); }});

        add(Action{{"Create gradient image..."}, ICON_MY_DITHER, ImGuiKey_None, 0, [this]() {
                       dialog("Create gradient image...").open = true;
                   }});
        add(Action{{"Create dither image..."}, ICON_MY_DITHER, ImGuiKey_None, 0, [this]() {
                       dialog("Create dither image...").open = true;
                   }});

        add(Action{{"Image loading options..."}, ICON_MY_SETTINGS_WINDOW, ImGuiKey_None, 0, [this]() {
                       dialog("Image loading options...").open = true;
                   }});

#if !defined(__EMSCRIPTEN__)
        add(Action{{"Open folder..."}, ICON_MY_OPEN_FOLDER, ImGuiKey_None, 0, [this]() { open_folder(); }});

        add(Action{{reveal_in_file_manager_text()},
                   ICON_MY_OPEN_FOLDER,
                   ImGuiKey_None,
                   0,
                   [this]()
                   {
                       if (auto img = current_image())
                       {
                           string filename, entry_fn;
                           split_zip_entry(img->filename, filename, entry_fn);
                           show_in_file_manager(filename.c_str());
                       }
                   },
                   if_img});

#endif

#if defined(__EMSCRIPTEN__)
        add(Action{{"Open URL..."},
                   ICON_MY_OPEN_IMAGE,
                   ImGuiKey_None,
                   0,
                   [this]()
                   {
                       char url[256];
                       if (ImGui::InputTextWithHint("##URL", "Enter an image URL and press <return>", url,
                                                    IM_ARRAYSIZE(url), ImGuiInputTextFlags_EnterReturnsTrue))
                       {
                           ImGui::CloseCurrentPopup();
                           load_url(url);
                       }
                   },
                   always_enabled,
                   true});
        add(Action{
            {"Load session bundle..."}, ICON_MY_OPEN_IMAGE, ImGuiKey_None, 0, [this]() { open_session_bundle(); }});
#endif

        add(Action{{"Show help"},
                   ICON_MY_ABOUT,
                   ImGuiMod_Shift | ImGuiKey_Slash,
                   0,
                   []() {},
                   always_enabled,
                   false,
                   &dialog("About").open});
        add(Action{{"Quit"},
                   ICON_MY_QUIT,
                   k_browser_reserved ? ImGuiKey_None : (ImGuiMod_Ctrl | ImGuiKey_Q),
                   0,
                   [this]()
                   {
                       if (!any_image_modified())
                       {
                           m_params.appShallExit = true;
                           return;
                       }

                       m_pending_discard                       = PendingDiscard::Quit;
                       dialog("Discard unsaved changes?").open = true;
                   }});

        add(Action{{"Command palette..."},
                   ICON_MY_COMMAND_PALETTE,
                   ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P,
                   0,
                   []() {},
                   always_enabled,
                   false,
                   &dialog("Command palette...").open});

        static bool toolbar_on =
            m_params.callbacks.edgesToolbars.find(EdgeToolbarType::Top) != m_params.callbacks.edgesToolbars.end();
        add(Action{{"Show top toolbar"},
                   ICON_MY_TOOLBAR,
                   0,
                   0,
                   [this]()
                   {
                       if (!toolbar_on)
                           m_params.callbacks.edgesToolbars.erase(EdgeToolbarType::Top);
                       else
                           m_params.callbacks.AddEdgeToolbar(
                               EdgeToolbarType::Top, [this]() { draw_top_toolbar(); }, m_top_toolbar_options);
                   },
                   always_enabled,
                   false,
                   &toolbar_on});
        add(Action{{"Show tool palette"}, ICON_MY_TOOLBAR, 0, 0, []() {}, always_enabled, false, &m_show_tool_palette});
        add(Action{{"Show menu bar"},
                   ICON_MY_HIDE_ALL_WINDOWS,
                   0,
                   0,
                   []() {},
                   always_enabled,
                   false,
                   &m_params.imGuiWindowParams.showMenuBar});
        add(Action{{"Show status bar"},
                   ICON_MY_STATUSBAR,
                   0,
                   0,
                   []() {},
                   always_enabled,
                   false,
                   &m_params.imGuiWindowParams.showStatusBar});
        add(Action{{"Show FPS in status bar"}, ICON_MY_FPS, 0, 0, []() {}, always_enabled, false, &m_show_FPS});
        add(Action{
            {"Enable idling"}, ICON_MY_BLANK, 0, 0, []() {}, always_enabled, false, &m_params.fpsIdling.enableIdling});

        auto any_window_hidden = [this]()
        {
            for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                if (dockableWindow.canBeClosed && !dockableWindow.isVisible)
                    return true;
            return false;
        };

        add(Action{{"Show all windows"},
                   ICON_MY_SHOW_ALL_WINDOWS,
                   ImGuiKey_Tab,
                   0,
                   [this]()
                   {
                       for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                           if (dockableWindow.canBeClosed)
                               dockableWindow.isVisible = true;
                   },
                   any_window_hidden});

        add(Action{{"Hide all windows"},
                   ICON_MY_HIDE_ALL_WINDOWS,
                   ImGuiKey_Tab,
                   0,
                   [this]()
                   {
                       for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                           if (dockableWindow.canBeClosed)
                               dockableWindow.isVisible = false;
                   },
                   [any_window_hidden]() { return !any_window_hidden(); }});

        add(Action{{"Show entire GUI"},
                   ICON_MY_SHOW_ALL_WINDOWS,
                   ImGuiMod_Shift | ImGuiKey_Tab,
                   0,
                   [this]()
                   {
                       for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                           if (dockableWindow.canBeClosed)
                               dockableWindow.isVisible = true;
                       m_params.imGuiWindowParams.showMenuBar   = true;
                       m_params.imGuiWindowParams.showStatusBar = true;
                       m_params.callbacks.AddEdgeToolbar(
                           EdgeToolbarType::Top, [this]() { draw_top_toolbar(); }, m_top_toolbar_options);
                       toolbar_on          = true;
                       m_show_tool_palette = true;
                   },
                   [this, any_window_hidden]()
                   {
                       return any_window_hidden() || !m_params.imGuiWindowParams.showMenuBar ||
                              !m_params.imGuiWindowParams.showStatusBar || !toolbar_on || !m_show_tool_palette;
                   }});

        add(Action{{"Hide entire GUI"},
                   ICON_MY_HIDE_GUI,
                   ImGuiMod_Shift | ImGuiKey_Tab,
                   0,
                   [this]()
                   {
                       for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                           if (dockableWindow.canBeClosed)
                               dockableWindow.isVisible = false;
                       m_params.imGuiWindowParams.showMenuBar   = false;
                       m_params.imGuiWindowParams.showStatusBar = false;
                       m_params.callbacks.edgesToolbars.erase(EdgeToolbarType::Top);
                       toolbar_on          = false;
                       m_show_tool_palette = false;
                   },
                   [this, any_window_hidden]()
                   {
                       return !any_window_hidden() || m_params.imGuiWindowParams.showMenuBar ||
                              m_params.imGuiWindowParams.showStatusBar || toolbar_on || m_show_tool_palette;
                   }});

        add(Action{{"Restore default layout"},
                   ICON_MY_RESTORE_LAYOUT,
                   0,
                   0,
                   [this]() { m_params.dockingParams.layoutReset = true; },
                   [this]() { return !m_params.dockingParams.dockableWindows.empty(); }});

        add(Action{{"Show developer menu"},
                   ICON_MY_DEVELOPER_WINDOW,
                   0,
                   0,
                   []() {},
                   always_enabled,
                   false,
                   &m_show_developer_menu});
        add(Action{{"Show Dear ImGui demo window"},
                   ICON_MY_DEMO_WINDOW,
                   0,
                   0,
                   []() {},
                   always_enabled,
                   false,
                   &m_show_demo_window});
        add(Action{{"Show debug window"},
                   ICON_MY_LOG_LEVEL_DEBUG,
                   0,
                   0,
                   []() {},
                   always_enabled,
                   false,
                   &m_show_debug_window});
        add(Action{
            {"Theme tweak window"}, ICON_MY_TWEAK_THEME, 0, 0, []() {}, always_enabled, false, &m_show_tweak_window});
        add(Action{{"Locate settings file"},
                   ICON_MY_DEVELOPER_WINDOW,
                   0,
                   0,
                   [this]()
                   {
                       if (auto loc = IniSettingsLocation(m_params))
                           show_in_file_manager(loc->c_str());
                   },
                   always_enabled,
                   false});

        for (size_t i = 0; i < m_params.dockingParams.dockableWindows.size(); ++i)
        {
            DockableWindow &w = m_params.dockingParams.dockableWindows[i];
            add(Action{{fmt::format("Show {} window", w.label).c_str()},
                       window_info[i].icon,
                       window_info[i].chord,
                       0,
                       []() {},
                       [&w]() { return w.canBeClosed; },
                       false,
                       &w.isVisible});
        }

        add(Action{{"Decrease exposure"}, ICON_MY_DECREASE_EXPOSURE, ImGuiKey_E, ImGuiInputFlags_Repeat, [this]() {
                       m_exposure_live = m_exposure -= 0.25f;
                   }});
        add(Action{{"Increase exposure"},
                   ICON_MY_INCREASE_EXPOSURE,
                   ImGuiMod_Shift | ImGuiKey_E,
                   ImGuiInputFlags_Repeat,
                   [this]() { m_exposure_live = m_exposure += 0.25f; }});
        add(Action{{"Reset tonemapping"},
                   ICON_MY_RESET_TONEMAPPING,
                   0,
                   0,
                   [this]()
                   {
                       m_exposure_live = m_exposure = 0.f;
                       m_offset_live = m_offset = 0.f;
                       m_gamma_live = m_gamma = 1.f;
                       m_tonemap              = Tonemap_Gamma;
                   },
                   always_enabled,
                   false,
                   nullptr,
                   "Reset the exposure and blackpoint offset to 0."});
        add(Action{
            {"Reverse colormap"}, ICON_MY_INVERT_COLORMAP, 0, 0, []() {}, always_enabled, false, &m_reverse_colormap});
        // Registered unconditionally: there is no window yet to ask about HDR, and the answer changes as the
        // window moves between monitors. The enabled predicate consults supports_hdr() live instead.
        add(Action{{"Clamp to LDR"},
                   ICON_MY_CLAMP_TO_LDR,
                   ImGuiMod_Ctrl | ImGuiKey_L,
                   0,
                   []() {},
                   [this]() { return supports_hdr(); },
                   false,
                   &m_clamp_to_LDR});
        add(Action{{"Dither"}, ICON_MY_DITHER, 0, 0, []() {}, always_enabled, false, &m_dither});
        add(Action{{"Shadow clipping", "Zebra stripes: shadows"},
                   ICON_MY_ZEBRA_STRIPES,
                   0,
                   0,
                   []() {},
                   always_enabled,
                   false,
                   &m_clip_warnings.x});
        add(Action{{"Highlight clipping", "Zebra stripes: highlights"},
                   ICON_MY_ZEBRA_STRIPES,
                   0,
                   0,
                   []() {},
                   always_enabled,
                   false,
                   &m_clip_warnings.y});

        add(Action{{"Draw pixel grid"},
                   ICON_MY_SHOW_GRID,
                   ImGuiMod_Ctrl | ImGuiKey_G,
                   0,
                   []() {},
                   always_enabled,
                   false,
                   &m_draw_grid});
        add(Action{{"Draw pixel values"},
                   ICON_MY_SHOW_PIXEL_VALUES,
                   ImGuiMod_Ctrl | ImGuiKey_P,
                   0,
                   []() {},
                   always_enabled,
                   false,
                   &m_draw_pixel_info});

        add(Action{{"Draw data window"},
                   ICON_MY_DATA_WINDOW,
                   ImGuiKey_None,
                   0,
                   []() {},
                   always_enabled,
                   false,
                   &m_draw_data_window});
        add(Action{{"Draw display window"},
                   ICON_MY_DISPLAY_WINDOW,
                   ImGuiKey_None,
                   0,
                   []() {},
                   always_enabled,
                   false,
                   &m_draw_display_window});

        add(Action{{"Decrease gamma/Previous colormap"},
                   ICON_MY_DECREASE_GAMMA,
                   ImGuiKey_G,
                   ImGuiInputFlags_Repeat,
                   [this]()
                   {
                       switch (m_tonemap)
                       {
                       default: [[fallthrough]];
                       case Tonemap_Gamma: m_gamma_live = m_gamma = std::max(0.02f, m_gamma - 0.02f); break;
                       case Tonemap_FalseColor: [[fallthrough]];
                       case Tonemap_PositiveNegative:
                           m_colormap_index = mod(m_colormap_index - 1, (int)std::size(m_colormaps));
                           break;
                       }
                   },
                   always_enabled});
        add(Action{{"Increase gamma/Next colormap"},
                   ICON_MY_INCREASE_GAMMA,
                   ImGuiMod_Shift | ImGuiKey_G,
                   ImGuiInputFlags_Repeat,
                   [this]()
                   {
                       switch (m_tonemap)
                       {
                       default: [[fallthrough]];
                       case Tonemap_Gamma: m_gamma_live = m_gamma = std::max(0.02f, m_gamma + 0.02f); break;
                       case Tonemap_FalseColor: [[fallthrough]];
                       case Tonemap_PositiveNegative:
                           m_colormap_index = mod(m_colormap_index + 1, (int)std::size(m_colormaps));
                           break;
                       }
                   },
                   always_enabled});

        // The tools are a radio group: each callback routes through set_mouse_mode(), which clears the other
        // tools' selected flags.
        const ImGuiKeyChord tool_chords[MouseMode_COUNT] = {ImGuiKey_P, ImGuiKey_M, ImGuiKey_I};
        const char *tool_icons[MouseMode_COUNT] = {ICON_MY_PAN_ZOOM_TOOL, ICON_MY_SELECT, ICON_MY_WATCHED_PIXEL};
        for (int i = 0; i < MouseMode_COUNT; ++i)
            add(Action{{mouse_mode_action_name(i)},
                       tool_icons[i],
                       tool_chords[i],
                       0,
                       [this, i]() { set_mouse_mode(i); },
                       always_enabled,
                       false,
                       &m_mouse_mode_enabled[i]});

        // below actions are only available if there is an image

        add(Action{{"Reload image"},
                   ICON_MY_RELOAD,
                   ImGuiMod_Ctrl | ImGuiKey_R,
                   0,
                   [this]() { reload_image(current_image()); },
                   [this]() { return can_reload(current_image()); }});
        add(Action{{"Duplicate image", "Make a copy"},
                   ICON_MY_DUPLICATE,
                   ImGuiMod_Alt | ImGuiMod_Shift | ImGuiKey_D,
                   0,
                   [this]() { duplicate_image(); },
                   if_img});
        add(Action{{"Reload all images"},
                   ICON_MY_RELOAD,
                   ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_R,
                   0,
                   [this]()
                   {
                       for (auto &i : m_images) reload_image(i);
                   },
                   [this]() {
                       return std::any_of(m_images.begin(), m_images.end(),
                                          [this](const ImagePtr &i) { return can_reload(i); });
                   }});

#if !defined(__EMSCRIPTEN__)
        add(Action{{"Watch for changes"},
                   ICON_MY_WATCH_CHANGES,
                   ImGuiKey_None,
                   0,
                   []() {},
                   always_enabled,
                   false,
                   &m_watch_files_for_changes,
                   "Regularly monitor opened files and folders, loading new files, and reloading existing files when "
                   "changes are detected."});
#if HDRVIEW_ENABLE_IPC
        // The generic action-to-palette mapping flips p_selected and then calls this, so the callback acts
        // on the flipped value and set_ipc_listening() settles it back if the port could not be bound.
        add(Action{{"Listen for image updates"},
                   ICON_MY_WATCH_CHANGES,
                   ImGuiKey_None,
                   0,
                   [this]() { set_ipc_listening(m_ipc_listen_requested); },
                   always_enabled,
                   false,
                   &m_ipc_listen_requested,
                   "Accept images pushed in by a renderer while it works, so a render appears here tile by "
                   "tile. Nothing outside this machine can connect."});
#endif
        add(Action{{"Add watched folder..."},
                   ICON_MY_ADD_WATCHED_FOLDER,
                   ImGuiKey_None,
                   0,
                   [this]()
                   {
                       if (m_image_loader.add_watched_directory(
                               pfd::select_folder("Open images in folder", "").result(), true))
                           m_watch_files_for_changes = true;
                   },
                   always_enabled,
                   false,
                   nullptr,
                   "Do not load the selected folder, but monitor it for new files and load those as they are "
                   "created.\nUseful if you plan to periodically write images into a folder (e.g. renderings) and "
                   "want HDRView to automatically load them as they appear."});
#endif
        add(Action{{"Save as..."},
                   ICON_MY_SAVE_AS,
                   ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S,
                   0,
                   [this]() { dialog("Save as...").open = true; },
                   if_img});

#if !defined(__EMSCRIPTEN__)
        add(Action{{"Save session..."}, ICON_MY_SAVE_AS, ImGuiKey_None, 0, [this]() { save_session(); }, if_img});
        add(Action{{"Load session..."}, ICON_MY_OPEN_IMAGE, ImGuiKey_None, 0, [this]() { load_session(); }});
        add(Action{{"Export session bundle..."},
                   ICON_MY_SAVE_AS,
                   ImGuiKey_None,
                   0,
                   [this]() { export_session_bundle(); },
                   if_img,
                   false,
                   nullptr,
                   "Exports the current session as a single self-contained .zip (manifest plus copies of "
                   "every loaded image) that can be shared or opened on the web build, where a plain "
                   ".hsess file's relative paths can't be resolved."});
#endif

        add(Action{{"Normalize exposure"},
                   ICON_MY_NORMALIZE_EXPOSURE,
                   ImGuiKey_N,
                   0,
                   [this]()
                   {
                       if (auto img = current_image())
                       {
                           float minimum = numeric_limits<float>::max();
                           float maximum = numeric_limits<float>::lowest();
                           auto &group   = img->groups[img->selected_group];

                           bool3 should_include[Channels_COUNT] = {
                               {true, true, true},   // RGBA
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
                               // A summary's min/max already leave out NaNs, infinities and FLT_MAX-style
                               // markers (see PixelStats::is_marker()); a channel with no measurements at
                               // all still holds the infinities its summary starts at.
                               const auto &s = img->channels[group.channels[c]].get_stats()->summary;
                               if (!isfinite(s.minimum) || !isfinite(s.maximum))
                                   continue;
                               minimum = std::min(minimum, s.minimum);
                               maximum = std::max(maximum, s.maximum);
                           }

                           // Nothing measured, or every measurement the same value: dividing by that span
                           // sends the exposure to infinity or NaN, from which no adjustment recovers.
                           if (!(maximum > minimum))
                               return;

                           float factor    = 1.0f / (maximum - minimum);
                           m_exposure_live = m_exposure = log2(factor);
                           m_offset_live = m_offset = -minimum * factor;
                       }
                   },
                   if_img,
                   false,
                   nullptr,
                   "Adjust the exposure and blackpoint offset to fit image values to the range [0, 1]."});

        add(Action{{"Play forward"},
                   ICON_MY_PLAY_FORWARD,
                   ImGuiKey_Space,
                   0,
                   [this]
                   {
                       m_play_backward &= !m_play_forward;
                       m_play_stopped             = !(m_play_forward || m_play_backward);
                       m_params.fpsIdling.fpsIdle = m_play_stopped ? 9.f : 0.f;
                   },
                   always_enabled,
                   false,
                   &m_play_forward});
        add(Action{{"Stop playback"},
                   ICON_MY_STOP,
                   ImGuiKey_Space,
                   0,
                   [this]
                   {
                       m_play_forward &= !m_play_stopped;
                       m_play_backward &= !m_play_stopped;
                       m_params.fpsIdling.fpsIdle = m_play_stopped ? 9.f : 0.f;
                   },
                   [this] { return m_play_forward || m_play_backward; },
                   false,
                   &m_play_stopped});
        add(Action{{"Play backward"},
                   ICON_MY_PLAY_BACKWARD,
                   ImGuiMod_Shift | ImGuiKey_Space,
                   0,
                   [this]
                   {
                       m_play_forward &= !m_play_backward;
                       m_play_stopped             = !(m_play_forward || m_play_backward);
                       m_params.fpsIdling.fpsIdle = m_play_stopped ? 9.f : 0.f;
                   },
                   always_enabled,
                   false,
                   &m_play_backward});

        // switch the current image using the image number (one-based indexing)
        for (int n = 1; n <= 10; ++n)
            add(Action{{fmt::format("Go to image {}", n)},
                       ICON_MY_IMAGE,
                       ImGuiKey_0 + mod(n, 10),
                       0,
                       [this, n]()
                       {
                           set_current_image_index(nth_visible_image_index(mod(n - 1, 10)));
                           m_scroll_to_next_frame = 0.5f;
                       },
                       [this, n]()
                       {
                           auto i = nth_visible_image_index(mod(n - 1, 10));
                           return is_valid(i) && i != m_current;
                       }});

        // select the reference image using Cmd + image number (one-based indexing)
        for (int n = 1; n <= 10; ++n)
            add(Action{{fmt::format("Set image {} as reference", n)},
                       ICON_MY_REFERENCE_IMAGE,
                       ImGuiMod_Ctrl | (ImGuiKey_0 + mod(n, 10)),
                       0,
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
            add(Action{{fmt::format("Go to channel group {}", n)},
                       ICON_MY_CHANNEL_GROUP,
                       modKey | ImGuiKey(ImGuiKey_0 + mod(n, 10)),
                       0,
                       [this, n]()
                       {
                           auto img               = current_image();
                           img->selected_group    = img->nth_visible_group_index(mod(n - 1, 10));
                           m_scroll_to_next_frame = 0.5f;
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
            add(Action{{fmt::format("Set channel group {} as reference", n)},
                       ICON_MY_REFERENCE_IMAGE,
                       ImGuiMod_Shift | modKey | ImGuiKey(ImGuiKey_0 + mod(n, 10)),
                       0,
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

        add(Action{{"Close"},
                   ICON_MY_CLOSE,
                   k_browser_reserved ? ImGuiKey_None : (ImGuiMod_Ctrl | ImGuiKey_W),
                   ImGuiInputFlags_Repeat,
                   [this]() { close_image(); },
                   if_img});
        add(Action{{"Close all"},
                   ICON_MY_CLOSE_ALL,
                   k_browser_reserved ? ImGuiKey_None : (ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_W),
                   0,
                   [this]() { close_all_images(); },
                   if_img});

        //
        // Editing. Every one of these goes through modify_image(); see src/app-edit.cpp.
        //
        add(Action{{"Undo"},
                   ICON_MY_UNDO,
                   ImGuiMod_Ctrl | ImGuiKey_Z,
                   ImGuiInputFlags_Repeat,
                   [this]() { undo(); },
                   [this]()
                   {
                       auto img = current_image();
                       return can_edit(img) && img->history.has_undo();
                   }});
        add(Action{{"Redo"},
                   ICON_MY_REDO,
                   ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z,
                   ImGuiInputFlags_Repeat,
                   [this]() { redo(); },
                   [this]()
                   {
                       auto img = current_image();
                       return can_edit(img) && img->history.has_redo();
                   }});

        // Every edit, from the one list that owns them. What each is called, what it looks like and what
        // it does are the command's own business; all that is registered here is the way in.
        for (auto &cmd : m_edit_commands)
        {
            EditCommand *c    = cmd.get();
            const auto  &info = c->info();
            add(Action{info.names, info.icon, info.chord, ImGuiInputFlags_None,
                       [this, c]() { invoke_edit_command(*c); }, [this, c]() { return edit_command_enabled(*c); }});
        }

        add(Action{{"Select all", "Select the entire image"},
                   ICON_MY_SELECT_ALL,
                   ImGuiMod_Ctrl | ImGuiKey_A,
                   0,
                   [this]()
                   {
                       if (auto img = current_image())
                           set_selection(img->data_window);
                   },
                   if_img});
        add(Action{{"Deselect", "Clear the selection"},
                   ICON_MY_DESELECT,
                   ImGuiMod_Ctrl | ImGuiKey_D,
                   0,
                   [this]() { set_selection(Box2i{}); },
                   [this]() { return m_roi.has_volume(); }});

        add(Action{{"Go to next image"},
                   ICON_MY_BLANK,
                   ImGuiKey_DownArrow,
                   ImGuiInputFlags_Repeat,
                   [this]()
                   {
                       set_current_image_index(next_visible_image_index(m_current, Direction_Forward));
                       m_scroll_to_next_frame = 1.f;
                   },
                   [this]()
                   {
                       auto i = next_visible_image_index(m_current, Direction_Forward);
                       return is_valid(i) && i != m_current;
                   }});
        add(Action{{"Go to previous image"},
                   ICON_MY_BLANK,
                   ImGuiKey_UpArrow,
                   ImGuiInputFlags_Repeat,
                   [this]()
                   {
                       set_current_image_index(next_visible_image_index(m_current, Direction_Backward));
                       m_scroll_to_next_frame = 0.f;
                   },
                   [this]()
                   {
                       auto i = next_visible_image_index(m_current, Direction_Backward);
                       return is_valid(i) && i != m_current;
                   }});
        add(Action{{"Make next image the reference"},
                   ICON_MY_BLANK,
                   ImGuiMod_Shift | ImGuiKey_DownArrow,
                   ImGuiInputFlags_Repeat,
                   [this]() { set_reference_image_index(next_visible_image_index(m_reference, Direction_Forward)); },
                   [this]()
                   {
                       auto i = next_visible_image_index(m_reference, Direction_Forward);
                       return is_valid(i) && i != m_reference;
                   }});
        add(Action{{"Make previous image the reference"},
                   ICON_MY_BLANK,
                   ImGuiMod_Shift | ImGuiKey_UpArrow,
                   ImGuiInputFlags_Repeat,
                   [this]() { set_reference_image_index(next_visible_image_index(m_reference, Direction_Backward)); },
                   [this]()
                   {
                       auto i = next_visible_image_index(m_reference, Direction_Backward);
                       return is_valid(i) && i != m_reference;
                   }});
        add(Action{{"Go to next channel group"},
                   ICON_MY_BLANK,
                   ImGuiKey_RightArrow,
                   ImGuiInputFlags_Repeat,
                   [this]()
                   {
                       auto img               = current_image();
                       img->selected_group    = img->next_visible_group_index(img->selected_group, Direction_Forward);
                       m_scroll_to_next_frame = 1.f;
                   },
                   [this]() { return current_image() != nullptr; }});
        add(Action{{"Go to previous channel group"},
                   ICON_MY_BLANK,
                   ImGuiKey_LeftArrow,
                   ImGuiInputFlags_Repeat,
                   [this]()
                   {
                       auto img               = current_image();
                       img->selected_group    = img->next_visible_group_index(img->selected_group, Direction_Backward);
                       m_scroll_to_next_frame = 0.f;
                   },
                   [this]() { return current_image() != nullptr; }});
        add(Action{{"Go to next channel group in reference"},
                   ICON_MY_BLANK,
                   ImGuiMod_Shift | ImGuiKey_RightArrow,
                   ImGuiInputFlags_Repeat,
                   [this]()
                   {
                       // if no reference image is selected, use the current image
                       if (!reference_image())
                           m_reference = m_current;
                       auto img             = reference_image();
                       img->reference_group = img->next_visible_group_index(img->reference_group, Direction_Forward);
                   },
                   [this]() { return reference_image() || current_image(); }});
        add(Action{{"Go to previous channel group in reference"},
                   ICON_MY_BLANK,
                   ImGuiMod_Shift | ImGuiKey_LeftArrow,
                   ImGuiInputFlags_Repeat,
                   [this]()
                   {
                       // if no reference image is selected, use the current image
                       if (!reference_image())
                           m_reference = m_current;
                       auto img             = reference_image();
                       img->reference_group = img->next_visible_group_index(img->reference_group, Direction_Backward);
                   },
                   [this]() { return reference_image() || current_image(); }});

        add(Action{{"Zoom out"},
                   ICON_MY_ZOOM_OUT,
                   ImGuiKey_Minus,
                   ImGuiInputFlags_Repeat,
                   [this]()
                   {
                       zoom_out();
                       cancel_autofit();
                   },
                   if_img});
        add(Action{{"Zoom in"},
                   ICON_MY_ZOOM_IN,
                   ImGuiKey_Equal,
                   ImGuiInputFlags_Repeat,
                   [this]()
                   {
                       zoom_in();
                       cancel_autofit();
                   },
                   if_img});
        add(Action{{"100%"},
                   ICON_MY_ZOOM_100,
                   0,
                   0,
                   [this]()
                   {
                       set_zoom_level(0.f);
                       cancel_autofit();
                   },
                   if_img});
        add(Action{{"Center"},
                   ICON_MY_CENTER,
                   ImGuiKey_C,
                   0,
                   [this]()
                   {
                       center();
                       cancel_autofit();
                   },
                   if_img});
        add(Action{{"Fit display window"},
                   ICON_MY_FIT_TO_WINDOW,
                   ImGuiKey_F,
                   0,
                   [this]()
                   {
                       fit_display_window();
                       cancel_autofit();
                   },
                   if_img});
        add(Action{{"Auto fit display window"},
                   ICON_MY_FIT_TO_WINDOW,
                   ImGuiMod_Shift | ImGuiKey_F,
                   0,
                   [this]() { m_auto_fit_selection = m_auto_fit_data = false; },
                   if_img,
                   false,
                   &m_auto_fit_display});
        add(Action{{"Fit data window"},
                   ICON_MY_FIT_TO_WINDOW,
                   ImGuiMod_Alt | ImGuiKey_F,
                   0,
                   [this]()
                   {
                       fit_data_window();
                       cancel_autofit();
                   },
                   if_img});
        add(Action{{"Auto fit data window"},
                   ICON_MY_FIT_TO_WINDOW,
                   ImGuiMod_Shift | ImGuiMod_Alt | ImGuiKey_F,
                   0,
                   [this]() { m_auto_fit_selection = m_auto_fit_display = false; },
                   if_img,
                   false,
                   &m_auto_fit_data});
        add(Action{{"Fit selection"},
                   ICON_MY_FIT_TO_WINDOW,
                   ImGuiKey_None,
                   0,
                   [this]()
                   {
                       fit_selection();
                       cancel_autofit();
                   },
                   [if_img, this]() { return if_img() && m_roi.has_volume(); }});
        add(Action{{"Auto fit selection"},
                   ICON_MY_FIT_TO_WINDOW,
                   ImGuiKey_None,
                   0,
                   [this]() { m_auto_fit_display = m_auto_fit_data = false; },
                   [if_img, this]() { return if_img() && m_roi.has_volume(); },
                   false,
                   &m_auto_fit_selection});
        add(Action{{"Flip horizontally"}, ICON_MY_FLIP_HORIZ, ImGuiKey_H, 0, []() {}, if_img, false, &m_flip.x});
        add(Action{{"Flip vertically"}, ICON_MY_FLIP_VERT, ImGuiKey_V, 0, []() {}, if_img, false, &m_flip.y});
    }
}

void HDRViewApp::process_shortcuts()
{
    // spdlog::trace("Processing shortcuts (frame: {})", ImGui::GetFrameCount());

    for (auto &a : m_actions)
        if (a.second.chord)
            // Held off only while something is taking typed characters, so a letter meant for a text field
            // is not also read as a shortcut. Not conditioned on keyboard navigation being idle: arriving
            // from the command palette or a dialog leaves navigation showing.
            if (a.second.enabled() && !ImGui::GetIO().WantTextInput &&
                ImGui::GlobalShortcut(a.second.chord, a.second.flags))
            {
                spdlog::trace("Processing shortcut for action '{}' (frame: {})", a.second.names[0],
                              ImGui::GetFrameCount());
                if (a.second.p_selected)
                    *a.second.p_selected = !*a.second.p_selected;
                a.second.callback();
                break;
            }

    set_image_textures();
}