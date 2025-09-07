#include "app.h"

#include "dithermatrix256.h"
#include "fonts.h"
#include "hello_imgui/dpi_aware.h"
#include "hello_imgui/hello_imgui.h"
#include "image.h"
#include "imageio/stb.h"
#include "imgui.h"
#include "imgui_ext.h"
#include "platform_utils.h"

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
}

HDRViewApp *hdrview() { return g_hdrview; }

HDRViewApp::HDRViewApp(optional<float> force_exposure, optional<float> force_gamma, optional<bool> force_dither,
                       optional<bool> force_sdr, optional<bool> force_apple_keys, vector<string> in_files)
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

    bool use_edr = hasEdrSupport() && !force_sdr;

    if (force_sdr)
        spdlog::info("Forcing SDR display mode (display {} support EDR mode)",
                     hasEdrSupport() ? "would otherwise" : "would not anyway");

    m_params.rendererBackendOptions.requestFloatBuffer = use_edr;
    spdlog::info("Launching GUI with {} display support.", use_edr ? "EDR" : "SDR");
    spdlog::info("Creating a {} framebuffer.", m_params.rendererBackendOptions.requestFloatBuffer
                                                   ? "floating-point precision"
                                                   : "standard precision");

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

    //
    // Dockable windows
    //

    DockableWindow histogram_window{"Histogram", "HistogramSpace", [this]
                                    {
                                        if (auto img = current_image())
                                            img->draw_histogram();
                                    }};
    DockableWindow channel_stats_window{"Channel statistics", "HistogramSpace", [this]
                                        {
                                            if (auto img = current_image())
                                                return img->draw_channel_stats();
                                        }};
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
    colorspace_window.imGuiWindowFlags =
        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar;

    DockableWindow pixel_inspector_window{"Pixel inspector", "RightBottomSpace",
                                          [this] { draw_pixel_inspector_window(); }};
    DockableWindow log_window{
        "Log", "LogSpace",
        [this] { ImGui::GlobalSpdLogWindow().draw(font("mono regular"), ImGui::GetStyle().FontSizeBase); }, false};

#if !defined(__EMSCRIPTEN__)
    DockableWindow watched_folders_window{"Watched Folders", "ImagesSpace", [this] { m_image_loader.draw_gui(); }};
#endif

#ifdef _WIN32
    ImGuiKey modKey = ImGuiMod_Alt;
#else
    ImGuiKey modKey = ImGuiMod_Super;
#endif

    // docking layouts
    struct DockableWindowExtraInfo
    {
        ImGuiKeyChord chord = ImGuiKey_None;
        const char   *icon  = nullptr;
    };

    m_params.dockingParams.layoutName      = "Standard";
    m_params.dockingParams.dockableWindows = {histogram_window,
                                              channel_stats_window,
                                              file_window,
                                              info_window,
                                              colorspace_window,
                                              pixel_inspector_window,
                                              log_window
#if !defined(__EMSCRIPTEN__)
                                              ,
                                              watched_folders_window
#endif
    };
    DockableWindowExtraInfo window_info[] = {{ImGuiKey_F5, ICON_MY_HISTOGRAM_WINDOW},
                                             {ImGuiKey_F6, ICON_MY_STATISTICS_WINDOW},
                                             {ImGuiKey_F7, ICON_MY_FILES_WINDOW},
                                             {ImGuiMod_Ctrl | ImGuiKey_I, ICON_MY_INFO_WINDOW},
                                             {ImGuiKey_F8, ICON_MY_COLORSPACE_WINDOW},
                                             {ImGuiKey_F9, ICON_MY_INSPECTOR_WINDOW},
                                             {modKey | ImGuiKey_GraveAccent, ICON_MY_LOG_WINDOW}
#if !defined(__EMSCRIPTEN__)
                                             ,
                                             {ImGuiKey_None, ICON_MY_ADD_WATCHED_FOLDER}
#endif
    };

    m_params.dockingParams.dockingSplits = {DockingSplit{"MainDockSpace", "HistogramSpace", ImGuiDir_Left, 0.2f},
                                            DockingSplit{"HistogramSpace", "ImagesSpace", ImGuiDir_Down, 0.75f},
                                            DockingSplit{"MainDockSpace", "LogSpace", ImGuiDir_Down, 0.25f},
                                            DockingSplit{"MainDockSpace", "RightSpace", ImGuiDir_Right, 0.25f},
                                            DockingSplit{"RightSpace", "RightBottomSpace", ImGuiDir_Down, 0.5f}};

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
            const char *const *opened_files = glfwGetCocoaOpenedFilenames();
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

    m_params.iniFolderType      = IniFolderType::AppUserConfigFolder;
    m_params.iniFilename        = "HDRView/settings.ini";
    m_params.callbacks.PostInit = [this, force_exposure, force_gamma, force_dither, force_apple_keys]
    {
        setup_imgui_clipboard();

        spdlog::info("Loading user settings from '{}'", IniSettingsLocation(m_params));

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
                m_gamma_live = m_gamma = j.value<float>("gamma", m_gamma);
                m_tonemap              = j.value<Tonemap_>("tonemap", m_tonemap);
                m_clamp_to_LDR         = j.value<bool>("clamp to LDR", m_clamp_to_LDR);
                m_dither               = j.value<bool>("dither", m_dither);
                m_file_list_mode       = j.value<int>("file list mode", m_file_list_mode);
                m_short_names          = j.value<bool>("short names", m_short_names);
                m_draw_clip_warnings   = j.value<bool>("draw clip warnings", m_draw_clip_warnings);
                m_show_FPS             = j.value<bool>("show FPS", m_show_FPS);
                m_clip_range           = j.value<float2>("clip range", m_clip_range);
                m_playback_speed       = j.value<float>("playback speed", m_playback_speed);
                m_colormap_index       = clamp<int>(j.value<int>("colormap index", 0), 0, std::size(m_colormaps));
                m_show_developer_menu  = j.value<bool>("show developer menu", m_show_developer_menu);
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

        spdlog::info("Saving user settings to '{}'", IniSettingsLocation(m_params));

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
        j["draw clip warnings"]      = m_draw_clip_warnings;
        j["show FPS"]                = m_show_FPS;
        j["clip range"]              = m_clip_range;
        j["show developer menu"]     = m_show_developer_menu;
        j["playback speed"]          = m_playback_speed;
        j["colormap index"]          = m_colormap_index;

        m_theme.save(j);

        SaveUserPref("UserSettings", j.dump(4));
    };

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

    m_params.callbacks.ShowGui = [this]()
    {
        process_shortcuts();

        for (auto &[key, value] : m_dialogs) value->draw(value->open);

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
#if !defined(__EMSCRIPTEN__)
                std::error_code ec;
                auto            path = fs::weakly_canonical(new_image->filename, ec);
                if (ec)
                    return;

                m_active_directories.insert(path.parent_path());
#endif

                int idx = (to_replace) ? image_index(to_replace) : -1;
                if (is_valid(idx))
                    m_images[idx] = new_image;
                else
                    m_images.push_back(new_image);

                if (should_select)
                    m_current = is_valid(idx) ? idx : int(m_images.size() - 1);
                update_visibility(); // this also calls set_image_textures();
                m_request_sort = true;
            });

        draw_tweak_window();
        draw_develop_windows();
    };
    m_params.callbacks.CustomBackground        = [this]() { draw_background(); };
    m_params.callbacks.AnyBackendEventCallback = [this](void *event) { return process_event(event); };

    m_dialogs["About"] = make_unique<PopupDialog>([this](bool &open) { draw_about_dialog(open); }, in_files.empty());
    m_dialogs["Command palette..."] = make_unique<PopupDialog>([this](bool &open) { draw_command_palette(open); });
    m_dialogs["Save as..."]         = make_unique<PopupDialog>([this](bool &open) { draw_save_as_dialog(open); });
    m_dialogs["Image loading options..."] =
        make_unique<PopupDialog>([this](bool &open) { draw_open_options_dialog(open); });
    m_dialogs["Custom background color picker"] =
        make_unique<PopupDialog>([this](bool &open) { draw_color_picker(open); });
    m_dialogs["Create dither image..."] = make_unique<PopupDialog>(
        [this](bool &open)
        {
            if (open)
                ImGui::OpenPopup("Create dither image...");

            static int2  size = {256, 256};
            static bool  tent = false;
            static Box1f range{0.0f, 1.0f};
            ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
            if (ImGui::BeginPopupModal("Create dither image...", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                open = false;
                ImGui::InputInt2("Size", &size.x);
                ImGui::Checkbox("Tent dither", &tent);
                ImGui::DragFloatRange2("Value range", &range.min.x, &range.max.x, 0.01f, -FLT_MAX, FLT_MAX, "min: %.3f",
                                       "max: %.3f");

                if (ImGui::Button("Cancel", EmToVec2(6.f, 0.f)) ||
                    (!ImGui::GetIO().NavVisible &&
                     (ImGui::Shortcut(ImGuiKey_Escape) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Period))))
                    ImGui::CloseCurrentPopup();

                ImGui::SameLine();

                if (ImGui::Button("Create", EmToVec2(6.f, 0.f)) ||
                    (!ImGui::GetIO().NavVisible && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Enter)))
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
        });
    m_dialogs["Create gradient image..."] = make_unique<PopupDialog>(
        [this](bool &open)
        {
            if (open)
                ImGui::OpenPopup("Create gradient image...");

            static int2   res    = {256, 256};
            static float  dither = 1.f;
            static int    levels = 1;
            static float4 c00{0.f, 0.f, 1.f, 1.f}, c10{1.f, 0.f, 0.f, 1.f}, c11{1.f, 1.f, 0.f, 1.f},
                c01{0.f, 1.f, 0.f, 1.f};
            ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
            if (ImGui::BeginPopupModal("Create gradient image...", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                open = false;
                ImGui::InputInt2("Resolution", &res.x);
                static int channel_mode = 1; // Default to RGB
                static int num_channels = 3;
                if (ImGui::Combo("Channels", &channel_mode, "Gray\0RGB\0RGBA\0"))
                    num_channels = channel_mode == 0 ? 1 : channel_mode == 1 ? 3 : 4;
                ImGui::SliderInt("Quantization levels", &levels, 2, 256);
                ImGui::WrappedTooltip("If >= 2, quantize the result to this many discrete levels.");
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

                if (ImGui::Button("Cancel", EmToVec2(6.f, 0.f)) ||
                    (!ImGui::GetIO().NavVisible &&
                     (ImGui::Shortcut(ImGuiKey_Escape) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Period))))
                    ImGui::CloseCurrentPopup();

                ImGui::SameLine();

                if (ImGui::Button("Create", EmToVec2(6.f, 0.f)) ||
                    (!ImGui::GetIO().NavVisible && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Enter)))
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

                                             for (size_t c = 0; c < img->channels.size(); ++c)
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
        });

    //
    // Actions and command palette
    //
    {
        const auto always_enabled = []() { return true; };
        const auto if_img         = [this]() { return current_image() != nullptr; };
        using ImGui::Action;
        auto add = [this](const Action &a) { m_actions[a.name] = a; };
        add(Action{"Open image...", ICON_MY_OPEN_IMAGE, ImGuiMod_Ctrl | ImGuiKey_O, 0, [this]() { open_image(); }});

        add(Action{"Create gradient image...", ICON_MY_DITHER, ImGuiKey_None, 0,
                   [this]() { m_dialogs["Create gradient image..."]->open = true; }});
        add(Action{"Create dither image...", ICON_MY_DITHER, ImGuiKey_None, 0,
                   [this]() { m_dialogs["Create dither image..."]->open = true; }});

        add(Action{"Image loading options...", ICON_MY_SETTINGS_WINDOW, ImGuiKey_None, 0,
                   [this]() { m_dialogs["Image loading options..."]->open = true; }});

#if !defined(__EMSCRIPTEN__)
        add(Action{"Open folder...", ICON_MY_OPEN_FOLDER, ImGuiKey_None, 0, [this]() { open_folder(); }});

        add(Action{reveal_in_file_manager_text(), ICON_MY_OPEN_FOLDER, ImGuiKey_None, 0,
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
        add(Action{"Open URL...", ICON_MY_OPEN_IMAGE, ImGuiKey_None, 0,
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
                   always_enabled, true});
#endif

        add(Action{"Show help", ICON_MY_ABOUT, ImGuiMod_Shift | ImGuiKey_Slash, 0, []() {}, always_enabled, false,
                   &m_dialogs["About"]->open});
        add(Action{"Quit", ICON_MY_QUIT, ImGuiMod_Ctrl | ImGuiKey_Q, 0, [this]() { m_params.appShallExit = true; }});

        add(Action{"Command palette...", ICON_MY_COMMAND_PALETTE, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, 0,
                   []() {}, always_enabled, false, &m_dialogs["Command palette..."]->open});

        static bool toolbar_on =
            m_params.callbacks.edgesToolbars.find(EdgeToolbarType::Top) != m_params.callbacks.edgesToolbars.end();
        add(Action{"Show top toolbar", ICON_MY_TOOLBAR, 0, 0,
                   [this]()
                   {
                       if (!toolbar_on)
                           m_params.callbacks.edgesToolbars.erase(EdgeToolbarType::Top);
                       else
                           m_params.callbacks.AddEdgeToolbar(
                               EdgeToolbarType::Top, [this]() { draw_top_toolbar(); }, m_top_toolbar_options);
                   },
                   always_enabled, false, &toolbar_on});
        add(Action{"Show menu bar", ICON_MY_HIDE_ALL_WINDOWS, 0, 0, []() {}, always_enabled, false,
                   &m_params.imGuiWindowParams.showMenuBar});
        add(Action{"Show status bar", ICON_MY_STATUSBAR, 0, 0, []() {}, always_enabled, false,
                   &m_params.imGuiWindowParams.showStatusBar});
        add(Action{"Show FPS in status bar", ICON_MY_FPS, 0, 0, []() {}, always_enabled, false, &m_show_FPS});
        add(Action{"Enable idling", ICON_MY_BLANK, 0, 0, []() {}, always_enabled, false,
                   &m_params.fpsIdling.enableIdling});

        auto any_window_hidden = [this]()
        {
            for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                if (dockableWindow.canBeClosed && !dockableWindow.isVisible)
                    return true;
            return false;
        };

        add(Action{"Show all windows", ICON_MY_SHOW_ALL_WINDOWS, ImGuiKey_Tab, 0,
                   [this]()
                   {
                       for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                           if (dockableWindow.canBeClosed)
                               dockableWindow.isVisible = true;
                   },
                   any_window_hidden});

        add(Action{"Hide all windows", ICON_MY_HIDE_ALL_WINDOWS, ImGuiKey_Tab, 0,
                   [this]()
                   {
                       for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                           if (dockableWindow.canBeClosed)
                               dockableWindow.isVisible = false;
                   },
                   [any_window_hidden]() { return !any_window_hidden(); }});

        add(Action{"Show entire GUI", ICON_MY_SHOW_ALL_WINDOWS, ImGuiMod_Shift | ImGuiKey_Tab, 0,
                   [this]()
                   {
                       for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                           if (dockableWindow.canBeClosed)
                               dockableWindow.isVisible = true;
                       m_params.imGuiWindowParams.showMenuBar   = true;
                       m_params.imGuiWindowParams.showStatusBar = true;
                       m_params.callbacks.AddEdgeToolbar(
                           EdgeToolbarType::Top, [this]() { draw_top_toolbar(); }, m_top_toolbar_options);
                       toolbar_on = true;
                   },
                   [this, any_window_hidden]()
                   {
                       return any_window_hidden() || !m_params.imGuiWindowParams.showMenuBar ||
                              !m_params.imGuiWindowParams.showStatusBar || !toolbar_on;
                   }});

        add(Action{"Hide entire GUI", ICON_MY_HIDE_GUI, ImGuiMod_Shift | ImGuiKey_Tab, 0,
                   [this]()
                   {
                       for (auto &dockableWindow : m_params.dockingParams.dockableWindows)
                           if (dockableWindow.canBeClosed)
                               dockableWindow.isVisible = false;
                       m_params.imGuiWindowParams.showMenuBar   = false;
                       m_params.imGuiWindowParams.showStatusBar = false;
                       m_params.callbacks.edgesToolbars.erase(EdgeToolbarType::Top);
                       toolbar_on = false;
                   },
                   [this, any_window_hidden]()
                   {
                       return !any_window_hidden() || m_params.imGuiWindowParams.showMenuBar ||
                              m_params.imGuiWindowParams.showStatusBar || toolbar_on;
                   }});

        add(Action{"Restore default layout", ICON_MY_RESTORE_LAYOUT, 0, 0,
                   [this]() { m_params.dockingParams.layoutReset = true; },
                   [this]() { return !m_params.dockingParams.dockableWindows.empty(); }});

        add(Action{"Show developer menu", ICON_MY_DEVELOPER_WINDOW, 0, 0, []() {}, always_enabled, false,
                   &m_show_developer_menu});
        add(Action{"Show Dear ImGui demo window", ICON_MY_DEMO_WINDOW, 0, 0, []() {}, always_enabled, false,
                   &m_show_demo_window});
        add(Action{"Show debug window", ICON_MY_LOG_LEVEL_DEBUG, 0, 0, []() {}, always_enabled, false,
                   &m_show_debug_window});
        add(Action{"Theme tweak window", ICON_MY_TWEAK_THEME, 0, 0, []() {}, always_enabled, false,
                   &m_show_tweak_window});
        add(Action{"Locate settings file", ICON_MY_DEVELOPER_WINDOW, 0, 0,
                   [this]() { show_in_file_manager(IniSettingsLocation(m_params).c_str()); }, always_enabled, false});

        for (size_t i = 0; i < m_params.dockingParams.dockableWindows.size(); ++i)
        {
            DockableWindow &w = m_params.dockingParams.dockableWindows[i];
            add(Action{fmt::format("Show {} window", w.label).c_str(), window_info[i].icon, window_info[i].chord, 0,
                       []() {}, [&w]() { return w.canBeClosed; }, false, &w.isVisible});
        }

        add(Action{"Decrease exposure", ICON_MY_DECREASE_EXPOSURE, ImGuiKey_E, ImGuiInputFlags_Repeat,
                   [this]() { m_exposure_live = m_exposure -= 0.25f; }});
        add(Action{"Increase exposure", ICON_MY_INCREASE_EXPOSURE, ImGuiMod_Shift | ImGuiKey_E, ImGuiInputFlags_Repeat,
                   [this]() { m_exposure_live = m_exposure += 0.25f; }});
        add(Action{"Reset tonemapping", ICON_MY_RESET_TONEMAPPING, 0, 0,
                   [this]()
                   {
                       m_exposure_live = m_exposure = 0.f;
                       m_offset_live = m_offset = 0.f;
                       m_gamma_live = m_gamma = 1.f;
                       m_tonemap              = Tonemap_Gamma;
                   },
                   always_enabled, false, nullptr, "Reset the exposure and blackpoint offset to 0."});
        add(Action{"Reverse colormap", ICON_MY_INVERT_COLORMAP, 0, 0, []() {}, always_enabled, false,
                   &m_reverse_colormap});
        if (m_params.rendererBackendOptions.requestFloatBuffer)
            add(Action{"Clamp to LDR", ICON_MY_CLAMP_TO_LDR, ImGuiMod_Ctrl | ImGuiKey_L, 0, []() {}, always_enabled,
                       false, &m_clamp_to_LDR});
        add(Action{"Dither", ICON_MY_DITHER, 0, 0, []() {}, always_enabled, false, &m_dither});
        add(Action{"Clip warnings", ICON_MY_ZEBRA_STRIPES, 0, 0, []() {}, always_enabled, false,
                   &m_draw_clip_warnings});

        add(Action{"Draw pixel grid", ICON_MY_SHOW_GRID, ImGuiMod_Ctrl | ImGuiKey_G, 0, []() {}, always_enabled, false,
                   &m_draw_grid});
        add(Action{"Draw pixel values", ICON_MY_SHOW_PIXEL_VALUES, ImGuiMod_Ctrl | ImGuiKey_P, 0, []() {},
                   always_enabled, false, &m_draw_pixel_info});

        add(Action{"Draw data window", ICON_MY_DATA_WINDOW, ImGuiKey_None, 0, []() {}, always_enabled, false,
                   &m_draw_data_window});
        add(Action{"Draw display window", ICON_MY_DISPLAY_WINDOW, ImGuiKey_None, 0, []() {}, always_enabled, false,
                   &m_draw_display_window});

        add(Action{"Decrease gamma/Previous colormap", ICON_MY_DECREASE_GAMMA, ImGuiKey_G, ImGuiInputFlags_Repeat,
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
        add(Action{"Increase gamma/Next colormap", ICON_MY_INCREASE_GAMMA, ImGuiMod_Shift | ImGuiKey_G,
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

        static bool s_mouse_mode_enabled[MouseMode_COUNT] = {true, false, false};

        add(Action{"Pan and zoom", ICON_MY_PAN_ZOOM_TOOL, ImGuiKey_P, 0,
                   [this]()
                   {
                       for (int i = 0; i < MouseMode_COUNT; ++i) s_mouse_mode_enabled[i] = false;
                       m_mouse_mode                            = MouseMode_PanZoom;
                       s_mouse_mode_enabled[MouseMode_PanZoom] = true;
                   },
                   always_enabled, false, &s_mouse_mode_enabled[MouseMode_PanZoom]});
        add(Action{"Rectangular select", ICON_MY_SELECT, ImGuiKey_M, 0,
                   [this]()
                   {
                       for (int i = 0; i < MouseMode_COUNT; ++i) s_mouse_mode_enabled[i] = false;
                       m_mouse_mode                                         = MouseMode_RectangularSelection;
                       s_mouse_mode_enabled[MouseMode_RectangularSelection] = true;
                   },
                   always_enabled, false, &s_mouse_mode_enabled[MouseMode_RectangularSelection]});
        add(Action{"Pixel/color inspector", ICON_MY_WATCHED_PIXEL, ImGuiKey_I, 0,
                   [this]()
                   {
                       for (int i = 0; i < MouseMode_COUNT; ++i) s_mouse_mode_enabled[i] = false;
                       m_mouse_mode                                   = MouseMode_ColorInspector;
                       s_mouse_mode_enabled[MouseMode_ColorInspector] = true;
                   },
                   always_enabled, false, &s_mouse_mode_enabled[MouseMode_ColorInspector]});

        // below actions are only available if there is an image

#if !defined(__EMSCRIPTEN__)
        add(Action{"Reload image", ICON_MY_RELOAD, ImGuiMod_Ctrl | ImGuiKey_R, 0,
                   [this]() { reload_image(current_image()); }, if_img});
        add(Action{"Reload all images", ICON_MY_RELOAD, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_R, 0,
                   [this]()
                   {
                       for (auto &i : m_images) reload_image(i);
                   },
                   if_img});
        add(Action{"Watch for changes", ICON_MY_WATCH_CHANGES, ImGuiKey_None, 0, []() {}, always_enabled, false,
                   &m_watch_files_for_changes,
                   "Regularly monitor opened files and folders, loading new files, and reloading existing files when "
                   "changes are detected."});
        add(Action{"Add watched folder...", ICON_MY_ADD_WATCHED_FOLDER, ImGuiKey_None, 0,
                   [this]()
                   {
                       if (m_image_loader.add_watched_directory(
                               pfd::select_folder("Open images in folder", "").result(), true))
                           m_watch_files_for_changes = true;
                   },
                   always_enabled, false, nullptr,
                   "Do not load the selected folder, but monitor it for new files and load those as they are "
                   "created.\nUseful if you plan to periodically write images into a folder (e.g. renderings) and "
                   "want HDRView to automatically load them as they appear."});
#endif
        add(Action{"Save as...", ICON_MY_SAVE_AS, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, 0,
                   [this]() { m_dialogs["Save as..."]->open = true; }, if_img});

        add(Action{"Normalize exposure", ICON_MY_NORMALIZE_EXPOSURE, ImGuiKey_N, 0,
                   [this]()
                   {
                       if (auto img = current_image())
                       {
                           float minimum = numeric_limits<float>::max();
                           float maximum = numeric_limits<float>::min();
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
                               minimum =
                                   std::min(minimum, img->channels[group.channels[c]].get_stats()->summary.minimum);
                               maximum =
                                   std::max(maximum, img->channels[group.channels[c]].get_stats()->summary.maximum);
                           }

                           float factor    = 1.0f / (maximum - minimum);
                           m_exposure_live = m_exposure = log2(factor);
                           m_offset_live = m_offset = -minimum * factor;
                       }
                   },
                   if_img, false, nullptr,
                   "Adjust the exposure and blackpoint offset to fit image values to the range [0, 1]."});

        add(Action{"Play forward", ICON_MY_PLAY_FORWARD, ImGuiKey_Space, 0,
                   [this]
                   {
                       m_play_backward &= !m_play_forward;
                       m_play_stopped             = !(m_play_forward || m_play_backward);
                       m_params.fpsIdling.fpsIdle = m_play_stopped ? 9.f : 0.f;
                   },
                   always_enabled, false, &m_play_forward});
        add(Action{"Stop playback", ICON_MY_STOP, ImGuiKey_Space, 0,
                   [this]
                   {
                       m_play_forward &= !m_play_stopped;
                       m_play_backward &= !m_play_stopped;
                       m_params.fpsIdling.fpsIdle = m_play_stopped ? 9.f : 0.f;
                   },
                   [this] { return m_play_forward || m_play_backward; }, false, &m_play_stopped});
        add(Action{"Play backward", ICON_MY_PLAY_BACKWARD, ImGuiMod_Shift | ImGuiKey_Space, 0,
                   [this]
                   {
                       m_play_forward &= !m_play_backward;
                       m_play_stopped             = !(m_play_forward || m_play_backward);
                       m_params.fpsIdling.fpsIdle = m_play_stopped ? 9.f : 0.f;
                   },
                   always_enabled, false, &m_play_backward});

        // switch the current image using the image number (one-based indexing)
        for (int n = 1; n <= 10; ++n)
            add(Action{fmt::format("Go to image {}", n), ICON_MY_IMAGE, ImGuiKey_0 + mod(n, 10), 0,
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
            add(Action{fmt::format("Set image {} as reference", n), ICON_MY_REFERENCE_IMAGE,
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
            add(Action{fmt::format("Go to channel group {}", n), ICON_MY_CHANNEL_GROUP,
                       modKey | ImGuiKey(ImGuiKey_0 + mod(n, 10)), 0,
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
            add(Action{fmt::format("Set channel group {} as reference", n), ICON_MY_REFERENCE_IMAGE,
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

        add(Action{"Close", ICON_MY_CLOSE, ImGuiMod_Ctrl | ImGuiKey_W, ImGuiInputFlags_Repeat,
                   [this]() { close_image(); }, if_img});
        add(Action{"Close all", ICON_MY_CLOSE_ALL, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_W, 0,
                   [this]() { close_all_images(); }, if_img});

        add(Action{"Go to next image", ICON_MY_BLANK, ImGuiKey_DownArrow, ImGuiInputFlags_Repeat,
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
        add(Action{"Go to previous image", ICON_MY_BLANK, ImGuiKey_UpArrow, ImGuiInputFlags_Repeat,
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
        add(Action{"Make next image the reference", ICON_MY_BLANK, ImGuiMod_Shift | ImGuiKey_DownArrow,
                   ImGuiInputFlags_Repeat,
                   [this]() { set_reference_image_index(next_visible_image_index(m_reference, Direction_Forward)); },
                   [this]()
                   {
                       auto i = next_visible_image_index(m_reference, Direction_Forward);
                       return is_valid(i) && i != m_reference;
                   }});
        add(Action{"Make previous image the reference", ICON_MY_BLANK, ImGuiMod_Shift | ImGuiKey_UpArrow,
                   ImGuiInputFlags_Repeat,
                   [this]() { set_reference_image_index(next_visible_image_index(m_reference, Direction_Backward)); },
                   [this]()
                   {
                       auto i = next_visible_image_index(m_reference, Direction_Backward);
                       return is_valid(i) && i != m_reference;
                   }});
        add(Action{"Go to next channel group", ICON_MY_BLANK, ImGuiKey_RightArrow, ImGuiInputFlags_Repeat,
                   [this]()
                   {
                       auto img               = current_image();
                       img->selected_group    = img->next_visible_group_index(img->selected_group, Direction_Forward);
                       m_scroll_to_next_frame = 1.f;
                   },
                   [this]() { return current_image() != nullptr; }});
        add(Action{"Go to previous channel group", ICON_MY_BLANK, ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat,
                   [this]()
                   {
                       auto img               = current_image();
                       img->selected_group    = img->next_visible_group_index(img->selected_group, Direction_Backward);
                       m_scroll_to_next_frame = 0.f;
                   },
                   [this]() { return current_image() != nullptr; }});
        add(Action{"Go to next channel group in reference", ICON_MY_BLANK, ImGuiMod_Shift | ImGuiKey_RightArrow,
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
        add(Action{"Go to previous channel group in reference", ICON_MY_BLANK, ImGuiMod_Shift | ImGuiKey_LeftArrow,
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

        add(Action{"Zoom out", ICON_MY_ZOOM_OUT, ImGuiKey_Minus, ImGuiInputFlags_Repeat,
                   [this]()
                   {
                       zoom_out();
                       cancel_autofit();
                   },
                   if_img});
        add(Action{"Zoom in", ICON_MY_ZOOM_IN, ImGuiKey_Equal, ImGuiInputFlags_Repeat,
                   [this]()
                   {
                       zoom_in();
                       cancel_autofit();
                   },
                   if_img});
        add(Action{"100%", ICON_MY_ZOOM_100, 0, 0,
                   [this]()
                   {
                       set_zoom_level(0.f);
                       cancel_autofit();
                   },
                   if_img});
        add(Action{"Center", ICON_MY_CENTER, ImGuiKey_C, 0,
                   [this]()
                   {
                       center();
                       cancel_autofit();
                   },
                   if_img});
        add(Action{"Fit display window", ICON_MY_FIT_TO_WINDOW, ImGuiKey_F, 0,
                   [this]()
                   {
                       fit_display_window();
                       cancel_autofit();
                   },
                   if_img});
        add(Action{"Auto fit display window", ICON_MY_FIT_TO_WINDOW, ImGuiMod_Shift | ImGuiKey_F, 0,
                   [this]() { m_auto_fit_selection = m_auto_fit_data = false; }, if_img, false, &m_auto_fit_display});
        add(Action{"Fit data window", ICON_MY_FIT_TO_WINDOW, ImGuiMod_Alt | ImGuiKey_F, 0,
                   [this]()
                   {
                       fit_data_window();
                       cancel_autofit();
                   },
                   if_img});
        add(Action{"Auto fit data window", ICON_MY_FIT_TO_WINDOW, ImGuiMod_Shift | ImGuiMod_Alt | ImGuiKey_F, 0,
                   [this]() { m_auto_fit_selection = m_auto_fit_display = false; }, if_img, false, &m_auto_fit_data});
        add(Action{"Fit selection", ICON_MY_FIT_TO_WINDOW, ImGuiKey_None, 0,
                   [this]()
                   {
                       fit_selection();
                       cancel_autofit();
                   },
                   [if_img, this]() { return if_img() && m_roi.has_volume(); }});
        add(Action{"Auto fit selection", ICON_MY_FIT_TO_WINDOW, ImGuiKey_None, 0,
                   [this]() { m_auto_fit_display = m_auto_fit_data = false; },
                   [if_img, this]() { return if_img() && m_roi.has_volume(); }, false, &m_auto_fit_selection});
        add(Action{"Flip horizontally", ICON_MY_FLIP_HORIZ, ImGuiKey_H, 0, []() {}, if_img, false, &m_flip.x});
        add(Action{"Flip vertically", ICON_MY_FLIP_VERT, ImGuiKey_V, 0, []() {}, if_img, false, &m_flip.y});
    }

    // load any passed-in images
    load_images(in_files);
}