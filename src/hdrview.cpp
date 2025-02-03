/** \file hdrview.cpp
    \author Wojciech Jarosz
*/

#include "app.h"
#include "cliformatter.h"
#include "imgui_ext.h"
#include "version.h"
#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <shellapi.h>
#include <windows.h>
#endif

int main(int argc, char **argv)
{
#ifdef _WIN32
    // Manually get the command line arguments, since we are not compiling in console mode
    LPWSTR    cmd_line = GetCommandLineW();
    wchar_t **win_argv = CommandLineToArgvW(cmd_line, &argc);

    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc) - 1U);
    for (auto i = static_cast<std::size_t>(argc) - 1U; i > 0U; --i) args.emplace_back(CLI::narrow(win_argv[i]));

    // This will enable console output if the app was started from a console. No extra console window is created if the
    // app was started any other way.
    bool consoleAvailable = AttachConsole(ATTACH_PARENT_PROCESS);

#ifndef NDEBUG
    // In Debug mode we create a console even if launched from within, e.g., Visual Studio
    bool customConsole = false;
    if (!consoleAvailable)
    {
        consoleAvailable = AllocConsole();
        customConsole    = consoleAvailable;
    }
#endif

    FILE *con_out = nullptr, *con_err = nullptr, *con_in = nullptr;
    if (consoleAvailable)
    {
        freopen_s(&con_out, "CONOUT$", "w", stdout);
        freopen_s(&con_err, "CONOUT$", "w", stderr);
        freopen_s(&con_in, "CONIN$", "r", stdin);
        std::cout.clear();
        std::clog.clear();
        std::cerr.clear();
        std::cin.clear();
    }
#endif

#ifdef ASSETS_LOCATION
    HelloImGui::SetAssetsFolder(ASSETS_LOCATION);
#endif

    constexpr spdlog::level::level_enum default_verbosity = spdlog::level::info;
    int                                 verbosity         = default_verbosity;

    std::optional<float> exposure, gamma;
    std::optional<bool>  dither, force_sdr, apple_keys;
    string               url;

    vector<string> in_files;

    try
    {
        string version_string =
            fmt::format("HDRView {}. (built using {} backend on {})", version(), backend(), build_timestamp());

        CLI::App app{R"(HDRView is a simple research-oriented tool for examining,
comparing, and converting high-dynamic range images. HDRView
is freely available under a 3-clause BSD license.
)",
                     "HDRView"};

        app.formatter(std::make_shared<ColorFormatter>());
        app.get_formatter()->column_width(20);
        app.get_formatter()->label("OPTIONS",
                                   fmt::format(fmt::emphasis::bold | fg(fmt::color::cornflower_blue), "OPTIONS"));

        app.add_option("-e,--exposure", exposure,
                       R"(Desired power of 2 EV or exposure value (gain = 2^exposure)
[default: 0].)")
            ->capture_default_str()
            ->group("Tone mapping and display");

        app.add_option("-g,--gamma", gamma,
                       R"(Desired gamma value for exposure+gamma tonemapping. An
sRGB curve is used if gamma is not specified.)")
            ->group("Tone mapping and display");

        app.add_flag("--dither,--no-dither{false}", dither,
                     "Enable/disable dithering when converting to LDR\n[default: on].")
            ->group("Tone mapping and display");

        app.add_flag("--sdr", force_sdr, "Force standard dynamic range (8-bit per channel) display.")
            ->group("Tone mapping and display");

        app.add_option("-v,--verbosity", verbosity,
                       R"(Set verbosity threshold T with lower values meaning more
verbose and higher values removing low-priority messages.
All messages with severity >= T are displayed, where the
severities are:
    trace    = 0
    debug    = 1
    info     = 2
    warn     = 3
    err      = 4
    critical = 5
    off      = 6
The default is 2 (info).)")
            ->check(CLI::Range(0, 6))
            ->option_text("INT in [0-6]")
            ->group("Misc");

        app.add_flag("--apple-keys,--non-apple-keys{false}", apple_keys,
                     "Apple-style keyboard behavior (Cmd key instead of Ctrl, etc.)")
            ->group("Misc");

        app.set_version_flag("--version", version_string, "Show the version and exit.")->group("Misc");

        app.set_help_flag("-h, --help", "Print this help message and exit.")->group("Misc");

        app.add_option("IMAGES", in_files, "The image files to load.")
            ->check(CLI::ExistingPath)
            ->option_text("PATH(existing) ...");
#if defined(__EMSCRIPTEN__)
        app.add_option("--url", url, "URL of an image to download and open");
#endif

        // enable all log messages globally, and for the default logger
        spdlog::set_level(spdlog::level::trace);
        spdlog::default_logger()->set_level(spdlog::level::trace);
        spdlog::set_pattern("%^[%T | %5l]: %$%v");

        // create a new sink.
        // new sinks default to allowing all levels (that are propagated by the parent logger)
        spdlog::default_logger()->sinks().push_back(ImGui::GlobalSpdLogWindow().sink());
        // spdlog::info("There are {} sinks", spdlog::default_logger()->sinks().size());
        ImGui::GlobalSpdLogWindow().set_pattern("%^%*[%T | %5l]: %$%v");

#ifdef _WIN32
        (void)argv; // prevent unused variable warning
        CLI11_PARSE(app, argc, win_argv);
#else
        CLI11_PARSE(app, argc, argv);
#endif

        spdlog::default_logger()->sinks().front()->set_level(spdlog::level::level_enum(verbosity));
        ImGui::GlobalSpdLogWindow().sink()->set_level(spdlog::level::level_enum(verbosity));

        if (argc > 1)
        {
            spdlog::debug("Launching with command line arguments:");
            for (int i = 1; i < argc; ++i)
#ifdef _WIN32
                spdlog::debug("\t" + string(CLI::narrow(win_argv[i])));
#else
                spdlog::debug("\t" + string(argv[i]));
#endif
        }
        else
            spdlog::debug("Launching HDRView with no command line arguments.");

        spdlog::info("Welcome to HDRView!");
        spdlog::info("Verbosity threshold set to level {:d}.", verbosity);
        if (exposure.has_value())
            spdlog::info("Forcing exposure to {:f} (intensity scale of {:f})", *exposure, powf(2.0f, *exposure));

        // gamma or sRGB
        if (gamma.has_value())
            spdlog::info("Forcing gamma correction to g={:f}.", *gamma);
        else
            spdlog::info("Using sRGB response curve.");

        // dithering
        if (dither.has_value())
            spdlog::info("Forcing dithering {}.", *dither ? "ON" : "OFF");

        if (apple_keys.has_value())
            spdlog::info("Turning Apple-style keyboard behavior {}.", *apple_keys ? "ON" : "OFF");

        init_hdrview(exposure, gamma, dither, force_sdr, apple_keys, in_files);
        hdrview()->load_url(url);
        hdrview()->run();
    }
    // Exceptions will only be thrown upon failed logger or sink construction (not during logging)
    catch (const spdlog::spdlog_ex &e)
    {
        fprintf(stderr, "Log init failed: %s\n", e.what());
        return 1;
    }
    catch (const std::exception &e)
    {
        spdlog::critical("Error: {}", e.what());
        // fprintf(stderr, "%s", USAGE);
        exit(EXIT_FAILURE);
    }

#ifdef _WIN32
    if (consoleAvailable)
    {
#ifndef NDEBUG
        if (customConsole)
            FreeConsole();
#endif

        fclose(con_out);
        fclose(con_err);
        fclose(con_in);
    }
#endif

    return EXIT_SUCCESS;
}
