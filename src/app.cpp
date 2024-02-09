/** \file app.cpp
    \author Wojciech Jarosz
*/

#include "app.h"

#include "hello_imgui/hello_imgui.h"
#include "imgui_ext.h"
#include "imgui_internal.h"
#include "immapp/immapp.h"
#include "implot/implot.h"
#include "implot/implot_internal.h"

// Taken from https://raw.githubusercontent.com/juliettef/IconFontCppHeaders/main/IconsFontAwesome6.h
#include "IconsFontAwesome6.h"

#include "opengl_check.h"

#include "colorspace.h"

#include "sviewstream.h"
#include "texture.h"
#include "timer.h"
#include "version.h"

#include <spdlog/spdlog.h>

#include <cmath>
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
#include "portable_file_dialogs/portable_file_dialogs.h"
#endif

#ifdef HELLOIMGUI_USE_SDL_OPENGL3
#include <SDL.h>
#endif

using namespace linalg::ostream_overloads;

using std::to_string;
using std::unique_ptr;

#ifdef __EMSCRIPTEN__
EM_JS(int, screen_width, (), { return screen.width; });
EM_JS(int, screen_height, (), { return screen.height; });
EM_JS(int, window_width, (), { return window.innerWidth; });
EM_JS(int, window_height, (), { return window.innerHeight; });
#endif

static std::mt19937     g_rand(53);
static constexpr float  MIN_ZOOM     = 0.01f;
static constexpr float  MAX_ZOOM     = 512.f;
static constexpr size_t g_max_recent = 15;
static bool             g_open_help  = false;

// static const vector<std::pair<vector<int>, string>> g_help_strings2 = {
//     {{ImGuiKey_H}, "Toggle this help window"},
//     {{ImGuiKey_MouseLeft}, "Pan image"},
//     {{ImGuiKey_MouseWheelX}, "Zoom in and out continuously"},
//     {{ImGuiMod_Shortcut | ImGuiKey_O}, "Open image"},
//     {{ImGuiMod_Shortcut | ImGuiKey_W}, "Close image"},
//     {{ImGuiMod_Shortcut | ImGuiMod_Shift | ImGuiKey_H}, "Close image"},
//     {{ImGuiKey_UpArrow}, "Switch to previous image"},
//     {{ImGuiKey_DownArrow}, "Switch to next image"},
//     {{ImGuiKey_LeftArrow}, "Switch to previous channel group"},
//     {{ImGuiKey_RightArrow}, "Switch to next channel group"},
//     {{ImGuiKey_1}, "Go to image 1"},
//     {{ImGuiKey_2}, "Go to image 2"},
//     {{ImGuiKey_3}, "Go to image 3"},
//     {{ImGuiKey_4}, "Go to image 4"},
//     {{ImGuiKey_5}, "Go to image 5"},
//     {{ImGuiKey_6}, "Go to image 6"},
//     {{ImGuiKey_7}, "Go to image 7"},
//     {{ImGuiKey_8}, "Go to image 8"},
//     {{ImGuiKey_9}, "Go to image 9"},
//     {{ImGuiKey_0}, "Go to image 10"},
//     {{ImGuiMod_Shortcut | ImGuiKey_1}, "Go to channel group 1"},
//     {{ImGuiMod_Shortcut | ImGuiKey_2}, "Go to channel group 2"},
//     {{ImGuiMod_Shortcut | ImGuiKey_3}, "Go to channel group 3"},
//     {{ImGuiMod_Shortcut | ImGuiKey_4}, "Go to channel group 4"},
//     {{ImGuiMod_Shortcut | ImGuiKey_5}, "Go to channel group 5"},
//     {{ImGuiMod_Shortcut | ImGuiKey_6}, "Go to channel group 6"},
//     {{ImGuiMod_Shortcut | ImGuiKey_7}, "Go to channel group 7"},
//     {{ImGuiMod_Shortcut | ImGuiKey_8}, "Go to channel group 8"},
//     {{ImGuiMod_Shortcut | ImGuiKey_9}, "Go to channel group 9"},
//     {{ImGuiMod_Shortcut | ImGuiKey_0}, "Go to channel group 10"},
//     {{ImGuiKey_Equal, ImGuiMod_Shift | ImGuiKey_Equal}, "Zoom in"},
//     {{ImGuiKey_Minus}, "Zoom out"},
//     {{ImGuiKey_E}, "Decrease exposure"},
//     {{ImGuiMod_Shift | ImGuiKey_E}, "Increase exposure"},
//     {{ImGuiKey_G}, "Decrease gamma"},
//     {{ImGuiMod_Shift | ImGuiKey_G}, "Increase gamma"},
//     {{ImGuiKey_F}, "Fit image"},
//     {{ImGuiKey_C}, "Center image"},
// };

static const vector<std::pair<string, string>> g_help_strings = {
    {"h", "Toggle this help window"},
    {"Left click+drag", "Pan image"},
    {"Scroll mouse/pinch", "Zoom in and out continuously"},
    {"Cmd+O", "Open image"},
    {"Cmd+W", "Close image"},
    {"Cmd+Shift+W", "Close image"},
    {ICON_FA_ARROW_DOWN "," ICON_FA_ARROW_UP,
     "Switch to previous (" ICON_FA_ARROW_UP ") or next (" ICON_FA_ARROW_DOWN ") image"},
    {ICON_FA_ARROW_LEFT "," ICON_FA_ARROW_RIGHT,
     "Switch to previous (" ICON_FA_ARROW_LEFT ") or next (" ICON_FA_ARROW_RIGHT ") channel group"},
    {"1, 2, ...", "Switch to image number 1, 2, ..."},
    {ICON_FA_CHEVRON_UP "+ 1, " ICON_FA_CHEVRON_UP "+ 2, ...", "Switch to channel group number 1, 2, ..."},
    {"- , +", "Zoom out ( - ) of or into ( + ) the image"},
    {"e , E", "Decrease ( e ) or increase ( E ) the image exposure/gain"},
    {"g , G", "Decrease ( g ) or increase ( G ) the gamma value"},
    {"f", "Fit the image to the window"},
    {"c", "Center the image in the window"},
};
// static const map<string, string> g_tooltip_map(g_help_strings.begin(), g_help_strings.end());

// static auto tooltip(const char *text, float wrap_width = 400.f)
// {
//     if (ImGui::BeginItemTooltip())
//     {
//         ImGui::PushTextWrapPos(wrap_width);
//         ImGui::TextUnformatted(text);
//         ImGui::PopTextWrapPos();
//         ImGui::EndTooltip();
//     }
// }

// static auto hotkey_tooltip(const char *name, float wrap_width = 400.f)
// {
//     if (auto t = g_tooltip_map.find(name); t != g_tooltip_map.end())
//         tooltip(fmt::format("{}.\nKey: {}", t->second, t->first).c_str(), wrap_width);
// }

HDRViewApp *g_app()
{
    static HDRViewApp viewer;
    return &viewer;
}

HDRViewApp::HDRViewApp()
{
    m_params.rendererBackendOptions.requestFloatBuffer = HelloImGui::hasEdrSupport();
    spdlog::info("Launching GUI with {} display support.", HelloImGui::hasEdrSupport() ? "EDR" : "SDR");
    HelloImGui::Log(HelloImGui::LogLevel::Info, "Creating a %s framebuffer.",
                    HelloImGui::hasEdrSupport() ? "floating-point precision" : "standard precision");

    // set up HelloImGui parameters
    m_params.appWindowParams.windowGeometry.size     = {1200, 800};
    m_params.appWindowParams.windowTitle             = "HDRView";
    m_params.appWindowParams.restorePreviousGeometry = false;

    // Menu bar
    m_params.imGuiWindowParams.showMenuBar            = true;
    m_params.imGuiWindowParams.showStatusBar          = true;
    m_params.imGuiWindowParams.defaultImGuiWindowType = HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;
    m_params.imGuiWindowParams.backgroundColor        = float4{0.15f, 0.15f, 0.15f, 1.f};

    // Setting this to true allows multiple viewports where you can drag windows outside out the main window in order to
    // put their content into new native windows m_params.imGuiWindowParams.enableViewports = true;
    m_params.imGuiWindowParams.enableViewports = false;
    m_params.imGuiWindowParams.menuAppTitle    = "File";

    m_params.iniFolderType = HelloImGui::IniFolderType::AppUserConfigFolder;
    m_params.iniFilename   = "HDRView/settings.ini";

    //
    // Dockable windows
    {
        // the histogram window
        HelloImGui::DockableWindow histogram_window;
        histogram_window.label             = "Histogram";
        histogram_window.dockSpaceName     = "HistogramSpace";
        histogram_window.isVisible         = true;
        histogram_window.rememberIsVisible = true;
        histogram_window.GuiFunction       = [this] { draw_histogram_window(); };

        // the file window
        HelloImGui::DockableWindow file_window;
        file_window.label             = "File";
        file_window.dockSpaceName     = "FileSpace";
        file_window.isVisible         = true;
        file_window.rememberIsVisible = true;
        file_window.GuiFunction       = [this] { draw_file_window(); };

        // the channels window
        HelloImGui::DockableWindow channel_window;
        channel_window.label             = "Channels";
        channel_window.dockSpaceName     = "ChannelSpace";
        channel_window.isVisible         = true;
        channel_window.rememberIsVisible = true;
        channel_window.GuiFunction       = [this] { draw_channel_window(); };

        // A console window named "Console" will be placed in "ConsoleSpace". It uses the HelloImGui logger gui
        HelloImGui::DockableWindow console_window;
        console_window.label             = "Console";
        console_window.dockSpaceName     = "ConsoleSpace";
        console_window.isVisible         = false;
        console_window.rememberIsVisible = true;
        console_window.GuiFunction       = [] { HelloImGui::LogGui(); };

        // docking layouts
        m_params.dockingParams.layoutName      = "Standard";
        m_params.dockingParams.dockableWindows = {histogram_window, file_window, channel_window, console_window};
        m_params.dockingParams.dockingSplits   = {
            HelloImGui::DockingSplit{"MainDockSpace", "HistogramSpace", ImGuiDir_Left, 0.2f},
            HelloImGui::DockingSplit{"HistogramSpace", "FileSpace", ImGuiDir_Down, 0.75f},
            HelloImGui::DockingSplit{"FileSpace", "ChannelSpace", ImGuiDir_Down, 0.25f},
            HelloImGui::DockingSplit{"MainDockSpace", "ConsoleSpace", ImGuiDir_Down, 0.25f}};
    }

    m_params.callbacks.ShowStatus = [this]()
    {
        if (auto img = current_image())
        {
            auto &io = ImGui::GetIO();

            int2 p{pixel_at_app_pos(io.MousePos)};
            ImGui::PushFont(m_mono_regular[14]);

            if (img->contains(p))
            {
                float4 color32 = image_pixel(p);
                float4 color8  = linalg::clamp(color32 * pow(2.f, m_exposure) * 255, 0.f, 255.f);

                // ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetFontSize() * 0.15f);
                ImGui::TextUnformatted(
                    fmt::format("({:>4d},{:>4d}) = ({:>6.3f},{:>6.3f},{:>6.3f},{:>6.3f})/({:>3d},{:>3d},{:>3d},{:>3d})",
                                p.x, p.y, color32.x, color32.y, color32.z, color32.w, (int)round(color8.x),
                                (int)round(color8.y), (int)round(color8.z), (int)round(color8.w))
                        .c_str());
                // ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFontSize() * 0.15f);
            }

            // ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetFontSize() * 0.15f);
            float  real_zoom = m_zoom * pixel_ratio();
            int    numer     = (real_zoom < 1.0f) ? 1 : (int)round(real_zoom);
            int    denom     = (real_zoom < 1.0f) ? (int)round(1.0f / real_zoom) : 1;
            auto   text      = fmt::format("{:7.2f}% ({:d}:{:d})", real_zoom * 100, numer, denom);
            float2 text_size = ImGui::CalcTextSize(text.c_str());
            ImGui::SameLine(ImGui::GetIO().DisplaySize.x - text_size.x - 16.f * ImGui::GetFontSize());
            ImGui::TextUnformatted(text.c_str());
            // ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFontSize() * 0.15f);
            ImGui::PopFont();
        }
    };

    //
    // Toolbars
    //
    HelloImGui::EdgeToolbarOptions edgeToolbarOptions;
    edgeToolbarOptions.sizeEm          = 2.2f;
    edgeToolbarOptions.WindowPaddingEm = ImVec2(0.7f, 0.35f);
    m_params.callbacks.AddEdgeToolbar(
        HelloImGui::EdgeToolbarType::Top, [this]() { draw_top_toolbar(); }, edgeToolbarOptions);

    m_params.callbacks.LoadAdditionalFonts = [this]()
    {
        std::string sans_r = "fonts/Roboto/Roboto-Regular.ttf";
        std::string sans_b = "fonts/Roboto/Roboto-Bold.ttf";
        std::string mono_r = "fonts/Roboto/RobotoMono-Regular.ttf";
        std::string mono_b = "fonts/Roboto/RobotoMono-Bold.ttf";
        string      fa6    = "fonts/" FONT_ICON_FILE_NAME_FAS;

        if (!HelloImGui::AssetExists(sans_r) || !HelloImGui::AssetExists(sans_b) || !HelloImGui::AssetExists(mono_r) ||
            !HelloImGui::AssetExists(mono_b) || !HelloImGui::AssetExists(fa6))
            throw std::runtime_error("Cannot find some required fonts!");

        HelloImGui::FontLoadingParams iconFontParams;
        iconFontParams.mergeToLastFont   = true;
        iconFontParams.useFullGlyphRange = false;
        iconFontParams.glyphRanges.push_back({ICON_MIN_FA, ICON_MAX_16_FA});
        iconFontParams.fontConfig.PixelSnapH = true;

        for (auto font_size : {14, 10, 16, 18, 30})
        {
            auto icon_font_size                        = 0.8f * font_size;
            iconFontParams.fontConfig.GlyphMinAdvanceX = iconFontParams.fontConfig.GlyphMaxAdvanceX =
                2.5f * icon_font_size;

            m_sans_regular[font_size] = HelloImGui::LoadFont(sans_r, (float)font_size);
            HelloImGui::LoadFont(fa6, icon_font_size, iconFontParams); // Merge FontAwesome6 with the previous font

            m_sans_bold[font_size] = HelloImGui::LoadFont(sans_b, (float)font_size);
            HelloImGui::LoadFont(fa6, icon_font_size, iconFontParams); // Merge FontAwesome6 with the previous font

            m_mono_regular[font_size] = HelloImGui::LoadFont(mono_r, (float)font_size);
            m_mono_bold[font_size]    = HelloImGui::LoadFont(mono_b, (float)font_size);
        }
    };

    m_params.callbacks.SetupImGuiStyle = [this]()
    {
        try
        {
            // make things like radio buttons look nice and round
            ImGui::GetStyle().CircleTessellationMaxError = 0.15f;

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
            // bind to a black texture so that the shader doesn't print errors before we've selected an image
            Image::set_null_texture(*m_shader, "primary");
            Image::set_null_texture(*m_shader, "secondary");

            HelloImGui::Log(HelloImGui::LogLevel::Info, "Successfully initialized graphics API!");
        }
        catch (const std::exception &e)
        {
            fmt::print(stderr, "Shader initialization failed!:\n\t{}.", e.what());
            HelloImGui::Log(HelloImGui::LogLevel::Error, "Shader initialization failed!:\n\t%s.", e.what());
        }
    };

    //
    // Load user settings at `PostInit` and save them at `BeforeExit`
    //
    m_params.callbacks.PostInit = [this]
    {
        spdlog::info("Restoring recent file list...");
        auto               s = HelloImGui::LoadUserPref("Recent files");
        std::istringstream ss(s);

        int         i = 0;
        std::string line;
        m_recent_files.clear();
        while (std::getline(ss, line))
        {
            if (line.empty())
                continue;

            string prefix = fmt::format("File{}=", i);
            // check that the line starts with the prefix
            if (starts_with(line, prefix))
            {
                auto r = line.substr(prefix.size());
                if (r.length())
                {
                    m_recent_files.push_back(r);
                    spdlog::info("Got this one: {}", prefix);
                    spdlog::info("Adding recent file with length {}: '{}'", r.length(), r);
                }
            }

            i++;
        }
    };

    m_params.callbacks.BeforeExit = [this]
    {
        std::stringstream ss;
        for (size_t i = 0; i < m_recent_files.size(); ++i)
        {
            ss << "File" << i << "=" << m_recent_files[i];
            if (i < m_recent_files.size() - 1)
                ss << std::endl;
        }
        HelloImGui::SaveUserPref("Recent files", ss.str());
    };

    m_params.callbacks.ShowMenus = []()
    {
        string text = ICON_FA_CIRCLE_INFO;
        auto   posX = (ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - ImGui::CalcTextSize(text.c_str()).x -
                     ImGui::GetStyle().ItemSpacing.x);
        if (posX > ImGui::GetCursorPosX())
            ImGui::SetCursorPosX(posX);
        // align_cursor(text, 1.f);
        if (ImGui::MenuItem(text))
            g_open_help = true;
    };

    m_params.callbacks.ShowAppMenuItems = [this]()
    {
        if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Open image...", "Cmd+O"))
            open_image();

#if !defined(__EMSCRIPTEN__)
        ImGui::BeginDisabled(m_recent_files.empty());
        if (ImGui::BeginMenu(ICON_FA_FOLDER_OPEN " Open recent"))
        {
            size_t i = m_recent_files.size() - 1;
            for (auto f = m_recent_files.rbegin(); f != m_recent_files.rend(); ++f, --i)
            {
                string short_name = (f->length() < 100) ? *f : f->substr(0, 47) + "..." + f->substr(f->length() - 50);
                if (ImGui::MenuItem(fmt::format("{}##File{}", short_name, i).c_str()))
                {
                    std::ifstream is{*f, std::ios_base::binary};
                    load_image(is, *f);
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

        ImGui::BeginDisabled(!current_image());
#if !defined(__EMSCRIPTEN__)
        if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK " Save as...", "Cmd+Shift+S"))
        {
            string filename = pfd::save_file("Save as", "",
                                             {
                                                 "Supported image files",
                                                 fmt::format("*.{}", fmt::join(Image::savable_formats(), "*.")),
                                             })
                                  .result();

            if (!filename.empty())
                save_as(filename);
        }
#else
        if (ImGui::BeginMenu(ICON_FA_DOWNLOAD " Download as..."))
        {
            if (current_image())
            {
                string filename;
                string filter = fmt::format("*.{}", fmt::join(Image::savable_formats(), " *."));
                ImGui::Text("Please enter a filename. Format is deduced from the accepted extensions:");
                ImGui::TextUnformatted(fmt::format("\t{}", filter));
                ImGui::Separator();
                if (ImGui::InputTextWithHint("##Filename", "Enter a filename and press <return>", &filename,
                                             ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    ImGui::CloseCurrentPopup();

                    if (!filename.empty())
                        save_as(filename);
                }
            }
            ImGui::EndMenu();
        }
#endif
        ImGui::EndDisabled();

        ImGui::Separator();

        if (ImGui::MenuItem(ICON_FA_CIRCLE_XMARK " Close", "Cmd+W", false, current_image() != nullptr))
            close_image();
        if (ImGui::MenuItem(ICON_FA_CIRCLE_XMARK " Close all", "Cmd+Shift+W", false, current_image() != nullptr))
            close_all_images();
    };

    m_params.callbacks.ShowGui          = [this]() { draw_about_dialog(); };
    m_params.callbacks.CustomBackground = [this]() { draw_background(); };
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
        HelloImGui::Log(HelloImGui::LogLevel::Error,
                        fmt::format("An error occurred while saving to '{}':\n\t{}.", filename, e.what()).c_str());
    }
    catch (...)
    {
        spdlog::error("An unknown error occurred while saving to '{}'.", filename);
    }
}

void HDRViewApp::open_image()
{
#if defined(__EMSCRIPTEN__)
    auto handle_upload_file =
        [](const string &filename, const string &mime_type, string_view buffer, void *my_data = nullptr)
    {
        isviewstream is{buffer};
        g_app()->load_image(is, filename);
    };

    string extensions = fmt::format(".{}", fmt::join(Image::loadable_formats(), ",."));

    // open the browser's file selector, and pass the file to the upload handler
    emscripten_browser_file::upload(extensions, handle_upload_file, this);
    HelloImGui::Log(HelloImGui::LogLevel::Debug, "Requesting file from user");
#else
    string extensions = fmt::format("*.{}", fmt::join(Image::loadable_formats(), " *."));

    auto result = pfd::open_file("Open image", "", {"Image files", extensions}).result();
    if (!result.empty())
    {
        std::ifstream is{result.front(), std::ios_base::binary};
        load_image(is, result.front());
    }
#endif
    if (auto img = current_image())
        fmt::print("Loaded image of size: {}\n", img->size());
}

void HDRViewApp::load_image(std::istream &is, const string &f)
{
    // make a copy of f in case its an element of m_recent_files, which we modify
    string filename = f;
    HelloImGui::Log(HelloImGui::LogLevel::Debug, "Loading file '%s'...", f.c_str());
    try
    {
        auto new_images = Image::load(is, filename);
        if (!new_images.size())
            throw std::invalid_argument("Could not allocate a new image.");

        for (auto &i : new_images) m_images.push_back(i);

        m_current = int(m_images.size() - 1);

        // remove any instances of filename from the recent files list
        m_recent_files.erase(std::remove(m_recent_files.begin(), m_recent_files.end(), filename), m_recent_files.end());

        // if loading was successful, now add the filename to the recent list and limit to g_max_recent files
        m_recent_files.push_back(filename);
        if (m_recent_files.size() > g_max_recent)
            m_recent_files.erase(m_recent_files.begin(), m_recent_files.end() - g_max_recent);
    }
    catch (const std::exception &e)
    {
        HelloImGui::Log(HelloImGui::LogLevel::Error,
                        fmt::format("Could not load image \"{}\": {}.", filename, e.what()).c_str());
        return;
    }

    // now upload the textures
    try
    {
        current_image()->set_as_texture(current_image()->selected_group, *m_shader, "primary");
    }
    catch (const std::exception &e)
    {
        HelloImGui::Log(HelloImGui::LogLevel::Error,
                        fmt::format("Could not upload texture to graphics backend: {}.", e.what()).c_str());
    }
}

void HDRViewApp::close_image()
{
    m_images.erase(m_images.begin() + m_current);
    m_current   = m_images.empty() ? -1 : std::clamp(m_current, 0, num_images() - 1);
    m_reference = m_images.empty() ? -1 : std::clamp(m_reference, 0, num_images() - 1);
    if (auto img = current_image())
        img->set_as_texture(img->selected_group, *m_shader, "primary");
    else
        Image::set_null_texture(*m_shader, "primary");
    if (auto img = reference_image())
        img->set_as_texture(img->selected_group, *m_shader, "secondary");
    else
        Image::set_null_texture(*m_shader, "secondary");
}

void HDRViewApp::close_all_images()
{
    m_images.clear();
    m_current   = -1;
    m_reference = -1;
    Image::set_null_texture(*m_shader, "primary");
    Image::set_null_texture(*m_shader, "secondary");
}

void HDRViewApp::run()
{
    ImmApp::AddOnsParams addons{/* .withImplot = */ true, /*.withMarkdown = */ false};
    ImmApp::Run(m_params, addons);
}

HDRViewApp::~HDRViewApp() {}

void HDRViewApp::draw_histogram_window()
{
    if (auto img = current_image())
        img->draw_histogram(m_exposure);
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
                HelloImGui::Log(HelloImGui::LogLevel::Debug, "Switching to blend mode %d.", n);
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
                HelloImGui::Log(HelloImGui::LogLevel::Debug, "Switching to channel %d.", n);
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    if (num_images())
    {
        static bool show_channels = true;
        ImGui::Checkbox("Show channel groups", &show_channels);

        ImGuiSelectableFlags selectable_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
        static ImGuiTableFlags table_flags =
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_BordersH;

        if (ImGui::BeginTable("ImageList", 3, table_flags))
        {
            const float icon_width = ImGui::CalcTextSize(ICON_FA_EYE_LOW_VISION).x;
            ImGui::TableSetupColumn(ICON_FA_LIST_OL,
                                    ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable,
                                    1.75f * icon_width);
            ImGui::TableSetupColumn(ICON_FA_EYE, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable,
                                    icon_width);
            ImGui::TableSetupColumn(show_channels ? "File or channel group name" : "File name",
                                    ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_IndentEnable);
            ImGui::TableHeadersRow();

            int id = 0;
            for (int i = 0; i < num_images(); ++i)
            {
                auto &img          = m_images[i];
                bool  is_current   = m_current == i;
                bool  is_reference = m_reference == i;

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::PushRowColors(is_current, is_reference);
                if (ImGui::Selectable(fmt::format("##image_{}_selectable", i + 1).c_str(), is_current || is_reference,
                                      selectable_flags))
                {
                    if (ImGui::GetIO().KeyCtrl)
                    {
                        if (is_reference)
                        {
                            m_reference = -1;
                            Image::set_null_texture(*m_shader, "secondary");
                        }
                        else
                        {
                            m_reference = i;
                            img->set_as_texture(img->selected_group, *m_shader, "secondary");
                        }
                    }
                    else
                    {
                        m_current = i;
                        img->set_as_texture(img->selected_group, *m_shader, "primary");
                    }
                    spdlog::info("Setting image {} to the {} image", i, is_reference ? "reference" : "current");
                }
                ImGui::PopStyleColor(3);
                ImGui::SameLine();

                auto image_num_str = fmt::format("{}", i + 1);
                ImGui::AlignCursor(image_num_str, 1.0f);
                ImGui::TextUnformatted(image_num_str.c_str());

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(is_current ? ICON_FA_EYE : (is_reference ? ICON_FA_EYE_LOW_VISION : ""));

                // right-align the truncated file name
                ImGui::TableNextColumn();
                string filename = img->filename;
                if (img->partname.length())
                    filename = filename + "/" + img->partname;
                string ellipsis    = "";
                float  avail_width = ImGui::GetContentRegionAvail().x;
                while (ImGui::CalcTextSize((ellipsis + filename).c_str()).x > avail_width && filename.length() > 1)
                {
                    filename = filename.substr(1);
                    ellipsis = "...";
                }
                ImGui::AlignCursor(ellipsis + filename, 1.f);
                ImGui::TextUnformatted(ellipsis + filename);

                if (show_channels && img->groups.size() > 1)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    for (size_t l = 0; l < img->layers.size(); ++l)
                    {
                        auto &layer = img->layers[l];

                        for (size_t g = 0; g < layer.groups.size(); ++g)
                        {
                            auto  &group = img->groups[layer.groups[g]];
                            string name  = string(ICON_FA_LAYER_GROUP) + " " + layer.name + group.name;

                            bool is_selected_channel = is_current && img->selected_group == layer.groups[g];

                            ImGui::PushRowColors(is_selected_channel, false);
                            {

                                ImGui::TableNextRow();

                                ImGui::TableNextColumn();
                                string hotkey =
                                    is_current ? fmt::format(ICON_FA_ANGLE_UP "{}", layer.groups[g] + 1) : "";
                                ImGui::AlignCursor(hotkey, 1.0f);
                                ImGui::TextUnformatted(hotkey);

                                ImGui::TableNextColumn();
                                if (ImGui::Selectable(
                                        fmt::format("{}##{}", is_selected_channel ? ICON_FA_EYE : "", id++).c_str(),
                                        is_selected_channel, selectable_flags))
                                {
                                    img->selected_group = layer.groups[g];
                                    m_current           = i;
                                    img->set_as_texture(img->selected_group, *m_shader, "primary");
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
}

void HDRViewApp::draw_channel_window()
{
    if (auto img = current_image())
        img->draw_channels_list();
}

void HDRViewApp::center() { m_offset = float2(0.f, 0.f); }

void HDRViewApp::fit()
{
    // Calculate the appropriate scaling factor.
    m_zoom = minelem(viewport_size() / current_image()->display_window.size());
    center();
}

float HDRViewApp::zoom_level() const { return log(m_zoom * pixel_ratio()) / log(m_zoom_sensitivity); }

void HDRViewApp::set_zoom_level(float level)
{
    m_zoom = std::clamp(std::pow(m_zoom_sensitivity, level) / pixel_ratio(), MIN_ZOOM, MAX_ZOOM);
}

void HDRViewApp::zoom_by(float amount, float2 focus_app_pos)
{
    if (amount == 0.f)
        return;

    auto  focused_pixel = pixel_at_app_pos(focus_app_pos); // save focused pixel coord before modifying zoom
    float scale_factor  = std::pow(m_zoom_sensitivity, amount);
    m_zoom              = std::clamp(scale_factor * m_zoom, MIN_ZOOM, MAX_ZOOM);
    // reposition so focused_pixel is still under focus_app_pos
    reposition_pixel_to_vp_pos(vp_pos_at_app_pos(focus_app_pos), focused_pixel);
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

    if (alpha > 0.0f)
    {
        ImColor col_fg(1.0f, 1.0f, 1.0f, alpha);
        ImColor col_bg(0.2f, 0.2f, 0.2f, alpha);

        auto screen_bounds = Box2i{int2(pixel_at_vp_pos({0.f, 0.f})) - 1, int2(pixel_at_vp_pos(viewport_size())) + 1};
        auto bounds        = screen_bounds.intersect(current_image()->data_window);

        // draw vertical lines
        for (int x = bounds.min.x; x <= bounds.max.x; ++x)
            ImGui::GetBackgroundDrawList()->AddLine(app_pos_at_pixel(float2(x, bounds.min.y)),
                                                    app_pos_at_pixel(float2(x, bounds.max.y)), col_bg, 4.f);

        // draw horizontal lines
        for (int y = bounds.min.y; y <= bounds.max.y; ++y)
            ImGui::GetBackgroundDrawList()->AddLine(app_pos_at_pixel(float2(bounds.min.x, y)),
                                                    app_pos_at_pixel(float2(bounds.max.x, y)), col_bg, 4.f);

        // and now again with the foreground color
        for (int x = bounds.min.x; x <= bounds.max.x; ++x)
            ImGui::GetBackgroundDrawList()->AddLine(app_pos_at_pixel(float2(x, bounds.min.y)),
                                                    app_pos_at_pixel(float2(x, bounds.max.y)), col_fg, 2.f);
        for (int y = bounds.min.y; y <= bounds.max.y; ++y)
            ImGui::GetBackgroundDrawList()->AddLine(app_pos_at_pixel(float2(bounds.min.x, y)),
                                                    app_pos_at_pixel(float2(bounds.max.x, y)), col_fg, 2.f);
    }
}

void HDRViewApp::draw_pixel_info() const
{
    if (!current_image() || !m_draw_pixel_info)
        return;

    static constexpr float3 white     = float3{1.f};
    static constexpr float3 black     = float3{0.f};
    static constexpr float2 align     = {0.5f, 0.5f};
    constexpr int           font_size = 30;
    auto                    font      = m_mono_bold.at(font_size);

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

    ImGui::PushFont(font);
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

    if (alpha > 0.0f)
    {
        ImGui::PushFont(font);

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
        ImGui::PopFont();
    }
}

void HDRViewApp::draw_image_border() const
{
    if (!current_image() || minelem(current_image()->size()) == 0)
        return;

    constexpr float  thickness = 3.f;
    constexpr float2 fudge     = float2{thickness * 0.5f - 0.5f, -(thickness * 0.5f - 0.5f)};
    float2           pad       = HelloImGui::EmToVec2({0.25, 0.125});

    auto draw_image_window =
        [&](const Box2f &image_window, ImGuiCol col_idx, const string &text, const float2 &align, bool draw_label)
    {
        auto  draw_list = ImGui::GetBackgroundDrawList();
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

    bool non_trivial = current_image()->data_window != current_image()->display_window ||
                       current_image()->data_window.min != int2{0, 0};
    draw_image_window(Box2f{current_image()->data_window}, ImGuiCol_TabActive, "Data window", {0.f, 0.f}, non_trivial);
    if (non_trivial)
        draw_image_window(Box2f{current_image()->display_window}, ImGuiCol_TabUnfocused, "Display window", {1.f, 1.f},
                          true);
}

void HDRViewApp::draw_image() const
{
    if (current_image() && !current_image()->data_window.is_empty())
    {
        float2 randomness(std::generate_canonical<float, 10>(g_rand) * 255,
                          std::generate_canonical<float, 10>(g_rand) * 255);

        m_shader->set_uniform("randomness", randomness);
        m_shader->set_uniform("gain", powf(2.0f, m_exposure));
        m_shader->set_uniform("gamma", m_gamma);
        m_shader->set_uniform("sRGB", m_sRGB);
        m_shader->set_uniform("clamp_to_LDR", !m_hdr);
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
    ImGui::AlignTextToFramePadding();
    ImGui::Text("EV:");
    ImGui::SameLine();
    ImGui::PushItemWidth(HelloImGui::EmSize(8));

    ImGui::SliderFloat("##ExposureSlider", &m_exposure, -9.f, 9.f, "%5.2f");
    ImGui::SameLine();

    auto img = current_image();

    ImGui::BeginDisabled(!img);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(0, ImGui::GetStyle().FramePadding.y));  // Remove frame padding
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0)); // Remove frame padding
    if (ImGui::Button(ICON_FA_WAND_SPARKLES "##NormalizeExposure", {ImGui::GetFrameHeight(), ImGui::GetFrameHeight()}))
    {
        float m     = 0.f;
        auto &group = img->groups[img->selected_group];
        for (int c = 0; c < group.num_channels; ++c)
            m = std::max(m, img->channels[group.channels[c]].get_statistics()->maximum);

        m_exposure = log2(1.f / m);
    }
    ImGui::PopStyleVar(2);
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(0, ImGui::GetStyle().FramePadding.y));  // Remove frame padding
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0)); // Remove frame padding
    if (ImGui::Button(ICON_FA_ARROWS_ROTATE "##ResetTonemapping", {ImGui::GetFrameHeight(), ImGui::GetFrameHeight()}))
    {
        m_exposure = 0.f;
        m_gamma    = 2.2f;
        m_sRGB     = true;
    }
    ImGui::PopStyleVar(2);
    ImGui::SameLine();

    ImGui::Checkbox("sRGB", &m_sRGB);
    ImGui::SameLine();

    ImGui::BeginDisabled(m_sRGB);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Gamma:");
    ImGui::SameLine();
    ImGui::PushItemWidth(HelloImGui::EmSize(8));
    ImGui::SliderFloat("##GammaSlider", &m_gamma, 0.02f, 9.f, "%5.3f");
    ImGui::EndDisabled();
    ImGui::SameLine();

    if (m_params.rendererBackendOptions.requestFloatBuffer)
    {
        ImGui::Checkbox("HDR", &m_hdr);
        ImGui::SameLine();
    }

    ImGui::Checkbox("Grid", &m_draw_grid);
    ImGui::SameLine();

    ImGui::Checkbox("RGB values", &m_draw_pixel_info);
    ImGui::SameLine();
}

void HDRViewApp::draw_background()
{
    auto &io = ImGui::GetIO();
    process_hotkeys();

    try
    {
        //
        // calculate the viewport sizes
        // fbsize is the size of the window in physical pixels while accounting for dpi factor on retina screens.
        // For retina displays, io.DisplaySize is the size of the window in logical pixels so we it by
        // io.DisplayFramebufferScale to get the physical pixel size for the framebuffer.
        int2 fbscale    = io.DisplayFramebufferScale;
        int2 fbsize     = int2{io.DisplaySize} * fbscale;
        m_viewport_min  = {0.f, 0.f};
        m_viewport_size = io.DisplaySize;
        if (auto id = m_params.dockingParams.dockSpaceIdFromName("MainDockSpace"))
        {
            auto central_node = ImGui::DockBuilderGetCentralNode(*id);
            m_viewport_size   = central_node->Size;
            m_viewport_min    = central_node->Pos;
        }

        if (!io.WantCaptureMouse)
        {
            auto app_mouse_pos = float2{io.MousePos};
            auto vp_mouse_pos  = vp_pos_at_app_pos(app_mouse_pos);
            auto scroll        = float2{io.MouseWheelH, io.MouseWheel};
#if defined(__EMSCRIPTEN__)
            scroll *= 10.0f;
#endif
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                reposition_pixel_to_vp_pos(vp_mouse_pos + float2{ImGui::GetMouseDragDelta(ImGuiMouseButton_Left)},
                                           pixel_at_vp_pos(vp_mouse_pos));
                ImGui::ResetMouseDragDelta();
            }
            else if (ImGui::IsKeyDown(ImGuiMod_Shift))
                // panning
                reposition_pixel_to_vp_pos(vp_mouse_pos + scroll * 4.f, pixel_at_vp_pos(vp_mouse_pos));
            else
                zoom_by(scroll.y / 4.f, app_mouse_pos);
        }

        //
        // clear the framebuffer and set up the viewport
        //

        // RenderPass expects things in framebuffer coordinates
        m_render_pass->resize(fbsize);
        m_render_pass->set_viewport(int2(m_viewport_min) * fbscale, int2(m_viewport_size) * fbscale);

        m_render_pass->begin();

        draw_image();
        draw_pixel_info();
        draw_pixel_grid();
        draw_image_border();

        m_render_pass->end();
    }
    catch (const std::exception &e)
    {
        fmt::print(stderr, "Drawing failed:\n\t{}.", e.what());
        HelloImGui::Log(HelloImGui::LogLevel::Error, "Drawing failed:\n\t%s.", e.what());
    }
}

void HDRViewApp::process_hotkeys()
{
    if (ImGui::GetIO().WantCaptureKeyboard)
        return;

    if (ImGui::IsKeyChordPressed(ImGuiKey_O | ImGuiMod_Shortcut))
        open_image();
    else if (ImGui::IsKeyPressed(ImGuiKey_H))
        g_open_help = !g_open_help;

    auto img = current_image();
    if (!img)
        return;

    // below hotkeys only available if there is an image

    // switch the current image using the number keys
    for (int n = 0; n < 10; ++n)
        if (ImGui::IsKeyChordPressed(ImGuiKey(ImGuiKey_1 + n)))
            set_current_image_index(n);
    if (ImGui::IsKeyChordPressed(ImGuiKey(ImGuiKey_0)))
        set_current_image_index(9);

    // switch the selected channel group using Ctrl + number key
    for (int n = 0; n < 10; ++n)
        if (n < (int)img->groups.size() && ImGui::IsKeyChordPressed(ImGuiKey(ImGuiKey_1 + n) | ImGuiMod_Ctrl))
            img->selected_group = n;
    if (10 < (int)img->groups.size() && ImGui::IsKeyChordPressed(ImGuiKey(ImGuiKey_0) | ImGuiMod_Ctrl))
        img->selected_group = 9;

    if (ImGui::IsKeyChordPressed(ImGuiKey_W | ImGuiMod_Shortcut))
        close_image();
    else if (ImGui::IsKeyChordPressed(ImGuiKey_W | ImGuiMod_Shortcut | ImGuiMod_Shift))
        close_all_images();
    else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        m_current = mod(m_current + 1, num_images());
    else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        m_current = mod(m_current - 1, num_images());
    else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
        img->selected_group = mod(img->selected_group + 1, (int)img->groups.size());
    else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
        img->selected_group = mod(img->selected_group - 1, (int)img->groups.size());
    else if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
        zoom_out();
    else if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
        zoom_in();
    else if (ImGui::IsKeyPressed(ImGuiKey_E))
        m_exposure += ImGui::IsKeyDown(ImGuiMod_Shift) ? 0.25f : -0.25f;
    else if (ImGui::IsKeyPressed(ImGuiKey_G))
        m_gamma = std::max(0.02f, m_gamma + (ImGui::IsKeyDown(ImGuiMod_Shift) ? 0.02f : -0.02f));
    else if (ImGui::IsKeyPressed(ImGuiKey_F))
        fit();
    else if (ImGui::IsKeyPressed(ImGuiKey_C))
        center();

    // update which texture is shown
    if ((img = current_image()))
        img->set_as_texture(img->selected_group, *m_shader, "primary");
}

void HDRViewApp::draw_about_dialog()
{
    if (g_open_help)
        ImGui::OpenPopup("About");

    // Always center this window when appearing
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowFocus();
    constexpr float icon_size    = 128.f;
    float2          col_width    = {icon_size + HelloImGui::EmSize(), 32 * HelloImGui::EmSize()};
    float2          display_size = ImGui::GetIO().DisplaySize;
#ifdef __EMSCRIPTEN__
    display_size = float2{window_width(), window_height()};
#endif
    col_width[1] = std::clamp(col_width[1], 5 * HelloImGui::EmSize(),
                              display_size.x - ImGui::GetStyle().WindowPadding.x - 2 * ImGui::GetStyle().ItemSpacing.x -
                                  ImGui::GetStyle().ScrollbarSize - col_width[0]);

    ImGui::SetNextWindowContentSize(float2{col_width[0] + col_width[1] + ImGui::GetStyle().ItemSpacing.x, 0});

    bool about_open = true;
    if (ImGui::BeginPopupModal("About", &about_open,
                               ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoSavedSettings |
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

            ImGui::PushFont(m_sans_bold.at(30));
            ImGui::Text("HDRView");
            ImGui::PopFont();

            ImGui::PushFont(m_sans_bold.at(18));
            ImGui::Text(version());
            ImGui::PopFont();
            ImGui::PushFont(m_sans_regular.at(10));
#if defined(__EMSCRIPTEN__)
            ImGui::Text(fmt::format("Built with emscripten using the {} backend on {}.", backend(), build_timestamp()));
#else
            ImGui::Text(fmt::format("Built using the {} backend on {}.", backend(), build_timestamp()));
#endif
            ImGui::PopFont();

            ImGui::Spacing();

            ImGui::PushFont(m_sans_bold.at(16));
            ImGui::Text("HDRView is a simple research-oriented tool for examining, comparing, manipulating, and "
                        "converting high-dynamic range images.");
            ImGui::PopFont();

            ImGui::Spacing();

            ImGui::Text("It is developed by Wojciech Jarosz, and is available under a 3-clause BSD license.");

            ImGui::PopTextWrapPos();
            ImGui::EndTable();
        }

        auto item_and_description = [this, col_width](string name, string desc)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImGui::PushFont(m_sans_bold.at(14));
            ImGui::AlignCursor(name, 1.f);
            ImGui::Text(name);
            ImGui::PopFont();

            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[1] - HelloImGui::EmSize());
            ImGui::PushFont(m_sans_regular.at(14));
            ImGui::Text(desc);
            ImGui::PopFont();
        };

        if (ImGui::BeginTabBar("AboutTabBar"))
        {
            if (ImGui::BeginTabItem("Keybindings", nullptr))
            {
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[0] + col_width[1]);
                ImGui::Text("The following keyboard shortcuts are available (these are also described in tooltips over "
                            "their respective controls).");

                ImGui::Spacing();
                ImGui::PopTextWrapPos();

                if (ImGui::BeginTable("about_table3", 2))
                {
                    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, col_width[0]);
                    ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthFixed, col_width[1]);

                    for (auto item : g_help_strings) item_and_description(item.first, item.second);
                    // for (auto item : g_help_strings2)
                    // {
                    //     string chords;
                    //     for (auto alias : item.first)
                    //     {
                    //         char key_chord_name[64];
                    //         ImGui::GetKeyChordName(alias, key_chord_name, IM_ARRAYSIZE(key_chord_name));
                    //         chords = chords + (chords.empty() ? "" : "; ") + key_chord_name;
                    //     }

                    //     item_and_description(chords.c_str(), item.second);
                    // }

                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Credits"))
            {
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + col_width[0] + col_width[1]);
                ImGui::Text("HDRView additionally makes use of the following external libraries and techniques (in "
                            "alphabetical order):\n\n");
                ImGui::PopTextWrapPos();

                if (ImGui::BeginTable("about_table2", 2))
                {
                    ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_WidthFixed, col_width[0]);
                    ImGui::TableSetupColumn("two", ImGuiTableColumnFlags_WidthFixed, col_width[1]);

                    item_and_description("colormaps", "Matt Zucker's degree 6 polynomial colormaps");
                    item_and_description("Dear ImGui",
                                         "Omar Cornut's immediate-mode graphical user interface for C++.");
#ifdef __EMSCRIPTEN__
                    item_and_description("emscripten", "An MIT-licensed LLVM-to-WebAssembly compiler.");
                    item_and_description("emscripten-browser-file",
                                         "Armchair Software's MIT-licensed header-only C++ library "
                                         "to open and save files in the browser.");
#endif
                    item_and_description("{fmt}", "A modern formatting library.");
                    item_and_description("Hello ImGui", "Pascal Thomet's cross-platform starter-kit for Dear ImGui.");
                    item_and_description(
                        "linalg", "Sterling Orsten's public domain, single header short vector math library for C++.");
                    item_and_description("NanoGUI", "Bits of code from Wenzel Jakob's BSD-licensed NanoGUI library.");
                    item_and_description("OpenEXR", "High Dynamic-Range (HDR) image file format");
#ifndef __EMSCRIPTEN__
                    item_and_description("portable-file-dialogs",
                                         "Sam Hocevar's WTFPL portable GUI dialogs library, C++11, single-header.");
#endif
                    item_and_description("stb_image/write/resize",
                                         "Single-Header libraries for loading/writing/resizing images");
                    item_and_description("tev", "Some code is adapted from Thomas Müller's tev");
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        // ImGui::SetKeyboardFocusHere();
        if (ImGui::Button("Dismiss", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Space) ||
            (!g_open_help && ImGui::IsKeyPressed(ImGuiKey_H)))
        {
            ImGui::CloseCurrentPopup();
            // g_dismissed_version = version_combined();
        }
        // ImGui::SetItemDefaultFocus();

        ImGui::ScrollWhenDraggingOnVoid(ImVec2(0.0f, -ImGui::GetIO().MouseDelta.y), ImGuiMouseButton_Left);
        ImGui::EndPopup();
        g_open_help = false;
    }
}

int main(int argc, char **argv)
{
    vector<string> args;
    bool           help                 = false;
    bool           error                = false;
    bool           launched_from_finder = false;

    try
    {
        for (int i = 1; i < argc; ++i)
        {
            if (strcmp("--help", argv[i]) == 0 || strcmp("-h", argv[i]) == 0)
                help = true;
            else if (strncmp("-psn", argv[i], 4) == 0)
                launched_from_finder = true;
            else
            {
                if (strncmp(argv[i], "-", 1) == 0)
                {
                    fmt::print(stderr, "Invalid argument: \"{}\"!\n", argv[i]);
                    help  = true;
                    error = true;
                }
                args.push_back(argv[i]);
            }
        }
        (void)launched_from_finder;
    }
    catch (const std::exception &e)
    {
        fmt::print(stderr, "Error: {}\n", e.what());
        help  = true;
        error = true;
    }
    if (help)
    {
        fmt::print(error ? stderr : stdout, R"(Syntax: {} [options]
Options:
   -h, --help                Display this message
)",
                   argv[0]);
        return error ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    try
    {
        g_app()->run();
    }
    catch (const std::exception &e)
    {
        fmt::print(stderr, "Caught a fatal error: {}\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
