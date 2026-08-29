/** \file test_gui_loader.cpp
    \author Wojciech Jarosz

    BackgroundImageLoader driven through a running app: which directories it watches and what stops it
    watching them, and what it does with an archive whose contents it has to discover for itself.

    Distinct from test_gui_image_io.cpp, which loads a file the caller named. Here the loader is the one
    deciding what to open, so the tests are about the decisions rather than the decode.
*/

#include "app.h"
#include "image.h"
#include "test_gui_registry.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"

#include <miniz.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>

#ifndef HDRVIEW_GUI_TEST_IMAGE
#error "HDRVIEW_GUI_TEST_IMAGE must be defined by CMake to a small fixture image path"
#endif

namespace fs = std::filesystem;

namespace
{

//! A fresh, empty directory under the system temp dir, canonicalized the way the loader stores paths.
fs::path make_temp_dir(const char *stem)
{
    std::error_code ec;
    fs::path        d = fs::temp_directory_path(ec) / fmt::format("hdrview_watch_test_{}", stem);
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return fs::weakly_canonical(d, ec);
}

//! Copies the fixture into `dir` under `name`, returning its path.
fs::path place_fixture(const fs::path &dir, const char *name)
{
    std::error_code ec;
    fs::path        dst = dir / name;
    fs::copy_file(HDRVIEW_GUI_TEST_IMAGE, dst, fs::copy_options::overwrite_existing, ec);
    return dst;
}

bool is_watched(const fs::path &dir) { return hdrview()->image_loader().watched_directories().count(dir) != 0; }

//! Collects everything logged while it is in scope. See the same helper in tests/test_loader_limits.cpp:
//! refusing an entry up front and failing to extract it look identical from outside, so the warning is the
//! only thing that tells them apart.
struct LogCapture
{
    struct Sink : spdlog::sinks::base_sink<std::mutex>
    {
        std::vector<std::string> messages;
        void                     sink_it_(const spdlog::details::log_msg &msg) override
        {
            messages.emplace_back(msg.payload.data(), msg.payload.size());
        }
        void flush_() override {}
    };

    std::shared_ptr<Sink> sink = std::make_shared<Sink>();

    LogCapture() { spdlog::default_logger()->sinks().push_back(sink); }
    ~LogCapture()
    {
        auto &sinks = spdlog::default_logger()->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), sink), sinks.end());
    }

    bool saw(const std::string &substring) const
    {
        return std::any_of(sink->messages.begin(), sink->messages.end(),
                           [&](const std::string &m) { return m.find(substring) != std::string::npos; });
    }
};

//! Writes a zip holding `contents` under "inside.png". A non-zero `declared` overwrites the entry's
//! uncompressed size in both of its headers, so the archive claims more than it stores; zero leaves the
//! real archive alone. See the equivalent fixture in tests/test_loader_limits.cpp for the header offsets.
fs::path write_zip(const fs::path &dir, const char *name, uint32_t declared, const std::string &contents)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    void  *buf  = nullptr;
    size_t size = 0;
    if (!mz_zip_writer_init_heap(&zip, 0, 0) ||
        !mz_zip_writer_add_mem(&zip, "inside.png", contents.data(), contents.size(), MZ_BEST_COMPRESSION) ||
        !mz_zip_writer_finalize_heap_archive(&zip, &buf, &size))
    {
        mz_zip_writer_end(&zip);
        return {}; // the caller checks for this
    }
    std::string bytes(reinterpret_cast<char *>(buf), size);
    mz_zip_writer_end(&zip);

    if (declared)
    {
        auto patch_at = [&](const char *sig, size_t field_offset)
        {
            size_t pos = bytes.find(sig, 0, 4);
            if (pos == std::string::npos)
                return;
            for (int b = 0; b < 4; ++b) bytes[pos + field_offset + b] = char((declared >> (8 * b)) & 0xff);
        };
        patch_at("PK\x03\x04", 22);
        patch_at("PK\x01\x02", 24);
    }

    fs::path      out = dir / name;
    std::ofstream os{out, std::ios::binary};
    os.write(bytes.data(), (std::streamsize)bytes.size());
    return out;
}

void reset_images(ImGuiTestContext *ctx)
{
    hdrview()->close_all_images();
    for (int frame = 0; frame < 60 && hdrview()->num_images() != 0; ++frame) ctx->Yield();
}

void load_and_wait(ImGuiTestContext *ctx, const fs::path &file)
{
    const int before = hdrview()->num_images();
    hdrview()->load_images({file.u8string()});
    for (int frame = 0; frame < 240 && hdrview()->num_images() <= before; ++frame) ctx->Yield();
}

} // namespace

void RegisterTests_Loader(ImGuiTestEngine *engine)
{
    /*
        "Add watched folder..." exists to watch a folder you have *not* opened -- its whole point is to pick
        up files that do not exist yet. So no loaded image ever lives in it, and any rule phrased as "drop
        the directories no loaded image came from" throws it away. Closing an unrelated image must not.
    */
    ImGuiTest *t = IM_REGISTER_TEST(engine, "loader", "explicit_watch_outlives_the_images");
    t->TestFunc  = [](ImGuiTestContext *ctx)
    {
        const fs::path watch_dir = make_temp_dir("explicit");
        const fs::path image_dir = make_temp_dir("images");
        const fs::path image     = place_fixture(image_dir, "a.png");
        IM_CHECK(fs::exists(image));

        reset_images(ctx);
        hdrview()->image_loader().remove_watched_directories([](const fs::path &) { return true; });

        IM_CHECK(hdrview()->image_loader().add_watched_directory(watch_dir, true));
        IM_CHECK(is_watched(watch_dir));

        // An image from somewhere else entirely; the watched folder stays empty throughout.
        load_and_wait(ctx, image);
        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK(is_watched(watch_dir));

        hdrview()->close_image(0);
        IM_CHECK_EQ(hdrview()->num_images(), 0);
        IM_CHECK(is_watched(watch_dir));

        // Nor may closing everything, for the same reason: the folder was asked for in its own right.
        load_and_wait(ctx, image);
        hdrview()->close_all_images();
        IM_CHECK(is_watched(watch_dir));

        // The X button beside it in the watched-folders table is what removes it, and must still work.
        hdrview()->image_loader().remove_watched_directories([&](const fs::path &p) { return p == watch_dir; });
        IM_CHECK(!is_watched(watch_dir));

        // The other half of the rule, so this does not simply pin every directory forever: a folder watched
        // only because its images were opened is still dropped once none of them is loaded.
        reset_images(ctx);
        hdrview()->load_images({image_dir.u8string()});
        for (int frame = 0; frame < 240 && hdrview()->num_images() == 0; ++frame) ctx->Yield();
        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK(is_watched(image_dir));
        hdrview()->close_image(0);
        IM_CHECK(!is_watched(image_dir));

        std::error_code ec;
        fs::remove_all(watch_dir, ec);
        fs::remove_all(image_dir, ec);
    };

    /*
        Opening a zip is the path where the loader discovers the entries itself, so the sizes it allocates
        from are whatever the archive claims. A claim its own stored bytes cannot back has to be refused
        before the allocation, not after the extraction fails -- the buffer is value-initialized, so the
        pages are really touched and overcommit does not soften it. tests/test_loader_limits.cpp covers the
        same guard on the two session-manifest paths; this is the one a user reaches by opening a file.
    */
    t           = IM_REGISTER_TEST(engine, "loader", "an_archive_lying_about_an_entry_is_refused_up_front");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        const fs::path dir = make_temp_dir("zip");
        // Well past deflate's 1032:1 ceiling, but small enough that a build without the guard merely wastes
        // the allocation rather than exhausting the machine.
        // A small, highly compressible payload: the claim has to exceed what deflate could have expanded
        // the *stored* bytes to, and an already-compressed entry (a PNG, say) barely shrinks, so the same
        // declared size against one would be a ratio deflate can legitimately reach.
        const fs::path lying = write_zip(dir, "lying.zip", 64u << 20, std::string(64, 'x'));
        IM_CHECK(!lying.empty());

        reset_images(ctx);
        {
            LogCapture log;
            hdrview()->load_images({lying.u8string()});
            for (int frame = 0; frame < 120; ++frame) ctx->Yield();
            IM_CHECK_EQ(hdrview()->num_images(), 0);
            IM_CHECK(log.saw("Skipping zip entry 'inside.png'"));
        }

        // The same image in an honest archive still opens, so the guard is refusing the claim rather than
        // the format.
        std::ifstream     in{HDRVIEW_GUI_TEST_IMAGE, std::ios::binary};
        const std::string fixture{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        const fs::path    honest = write_zip(dir, "honest.zip", 0, fixture);
        IM_CHECK(!honest.empty());
        reset_images(ctx);
        hdrview()->load_images({honest.u8string()});
        for (int frame = 0; frame < 240 && hdrview()->num_images() == 0; ++frame) ctx->Yield();
        IM_CHECK_EQ(hdrview()->num_images(), 1);

        reset_images(ctx);
        std::error_code ec;
        fs::remove_all(dir, ec);
    };

    /*
        Two reloads in flight for one image. The watch loop schedules a reload whenever a file's timestamp
        moves, and it polls four times a second, so a file being written repeatedly -- which is exactly what
        a watched render folder is -- outruns the load. "Reload all images" pressed twice does the same.

        The arrival is matched to its slot by pointer identity, so once the first reload has replaced the
        image, the second no longer finds the object it was told to replace.
    */
    t           = IM_REGISTER_TEST(engine, "loader", "overlapping_reloads_replace_rather_than_accumulate");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        const fs::path dir  = make_temp_dir("reload");
        const fs::path file = place_fixture(dir, "a.png");

        reset_images(ctx);
        load_and_wait(ctx, file);
        IM_CHECK_EQ(hdrview()->num_images(), 1);

        auto original = hdrview()->image(0);
        IM_CHECK(original != nullptr);

        // Both scheduled before either can finish, the way the watch loop schedules them.
        hdrview()->reload_image(original);
        hdrview()->reload_image(original);
        for (int frame = 0; frame < 240; ++frame) ctx->Yield();

        // One file, one entry -- whichever reload landed last.
        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK(hdrview()->image(0) != original);
        IM_CHECK(hdrview()->image(0)->path == fs::path(file));

        // However many pile up, not just two.
        auto current = hdrview()->image(0);
        for (int i = 0; i < 4; ++i) hdrview()->reload_image(current);
        for (int frame = 0; frame < 240; ++frame) ctx->Yield();
        IM_CHECK_EQ(hdrview()->num_images(), 1);
        IM_CHECK(hdrview()->image(0)->path == fs::path(file));

        reset_images(ctx);
        std::error_code ec;
        fs::remove_all(dir, ec);
    };

    /*
        An entry with no bytes in it. A zip can hold one, and the loader decided where to read from by
        asking whether the buffer it was handed was empty -- so a zero-byte entry fell through to "open
        this path", against a name assembled for display (archive.zip/inside.png) that no filesystem has.
        The complaint was that the file did not exist, about a file that was never supposed to.
    */
    t           = IM_REGISTER_TEST(engine, "loader", "an_empty_zip_entry_is_reported_as_empty");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        const fs::path dir   = make_temp_dir("empty_entry");
        const fs::path empty = write_zip(dir, "empty.zip", 0, std::string{});
        IM_CHECK(!empty.empty());

        reset_images(ctx);
        {
            LogCapture log;
            hdrview()->load_images({empty.u8string()});
            for (int frame = 0; frame < 120; ++frame) ctx->Yield();
            IM_CHECK_EQ(hdrview()->num_images(), 0);
            IM_CHECK(log.saw("is empty"));
            // Whatever it says, it must not claim something is missing from the filesystem: the name it
            // would be talking about was assembled for display and was never a path.
            IM_CHECK(!log.saw("doesn't exist"));
        }

        reset_images(ctx);
        std::error_code ec;
        fs::remove_all(dir, ec);
    };

    /*
        The same identity matching, in the other direction: an image closed while its reload was still in
        flight. The arrival has a slot to fill that no longer exists, and appending it puts the image the
        user just closed back in the list.
    */
    t           = IM_REGISTER_TEST(engine, "loader", "closing_an_image_mid_reload_does_not_resurrect_it");
    t->TestFunc = [](ImGuiTestContext *ctx)
    {
        const fs::path dir  = make_temp_dir("closed_reload");
        const fs::path file = place_fixture(dir, "a.png");

        reset_images(ctx);
        load_and_wait(ctx, file);
        IM_CHECK_EQ(hdrview()->num_images(), 1);

        hdrview()->reload_image(hdrview()->image(0));
        hdrview()->close_image(0);
        IM_CHECK_EQ(hdrview()->num_images(), 0);

        for (int frame = 0; frame < 240; ++frame) ctx->Yield();
        IM_CHECK_EQ(hdrview()->num_images(), 0);

        // An ordinary load still adds, so this is not refusing arrivals in general.
        load_and_wait(ctx, file);
        IM_CHECK_EQ(hdrview()->num_images(), 1);

        reset_images(ctx);
        std::error_code ec;
        fs::remove_all(dir, ec);
    };
}
