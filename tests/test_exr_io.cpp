//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "image.h"
#include "imageio/exr.h"
#include "imageio/exr_save_options.h"
#include "imageio/exr_streams.h"
#include "imageio/image_loader.h"
#include "imageio/streams.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfOutputFile.h>
#include <ImfRationalAttribute.h>
#include <ImfStdIO.h>

#ifdef HDRVIEW_TEST_OPENEXR_DIR
#include <set>
#endif

namespace
{

// An RGB image with a value per pixel/channel that uniquely identifies it. data_window/display_window default
// to the whole image at the origin if left empty, as Image::finalize() does.
ImagePtr make_test_image(int2 size, Box2i data_window = {}, Box2i display_window = {})
{
    auto img = std::make_shared<Image>();
    for (auto name : {"R", "G", "B"})
    {
        img->channels.emplace_back(name, size);
        auto &c = img->channels.back();
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x) c(x, y) = float(x + 100 * y) + (name[0] - 'R') * 0.01f;
    }
    img->data_window    = data_window;
    img->display_window = display_window;
    img->finalize();
    return img;
}

// Saves img to an in-memory buffer and reloads it, returning the reloaded parts; by default every channel group
// is saved.
std::vector<ImagePtr> save_and_reload(const Image &img, EXRSaveOptions opts)
{
    std::ostringstream out(std::ios::binary);
    save_exr_image(img, out, "test.exr", &opts);

    std::istringstream in(out.str(), std::ios::binary);
    return load_exr_image(in, "test.exr");
}

std::vector<ImagePtr> save_and_reload(const Image &img) { return save_and_reload(img, exr_default_save_options(img)); }

} // namespace

TEST_CASE("EXR save/load round-trips a non-origin data window and an independent display window exactly")
{
    auto img = make_test_image(int2{4, 3}, Box2i{int2{10, 20}, int2{14, 23}}, Box2i{int2{0, 0}, int2{14, 23}});

    auto reloaded = save_and_reload(*img);
    REQUIRE(reloaded.size() == 1);

    CHECK(reloaded[0]->data_window == img->data_window);
    CHECK(reloaded[0]->display_window == img->display_window);
}

TEST_CASE("An EXR saved without chromaticities or alpha claims neither")
{
    // a reader that invented either would convert or composite the pixels by something the file never said
    auto img = make_test_image(int2{2, 2});
    REQUIRE_FALSE(img->chromaticities.has_value());

    auto reloaded = save_and_reload(*img);
    REQUIRE(reloaded.size() == 1);
    CHECK_FALSE(reloaded[0]->chromaticities.has_value());
    CHECK(reloaded[0]->transparency == TransparencyType_None);
}

TEST_CASE("EXR half samples are floating point, not quantized, and not exact")
{
    // 0.1f has no exact half-precision representation
    auto img = std::make_shared<Image>();
    img->channels.emplace_back("Y", int2{1, 1});
    img->channels.back()(0, 0) = 0.1f;
    img->finalize();

    auto opts       = exr_default_save_options(*img);
    opts.pixel_type = 1; // HALF
    auto reloaded   = save_and_reload(*img, opts);
    REQUIRE(reloaded.size() == 1);
    CHECK(reloaded[0]->channels[0].bits_per_sample == 0); //< floating point, so no quantization lattice
    float value = reloaded[0]->channels[0](0, 0);
    CHECK(value != doctest::Approx(0.1f).epsilon(1e-6)); // not exact
    CHECK(value == doctest::Approx(0.1f).epsilon(1e-3)); // but close, within half precision
}

TEST_CASE("EXR channels report their own sample depths independently")
{
    // A part can mix pixel types, so the depth that sets the histogram's bin count lives on the channel, not
    // the image. Only UINT is quantized; half and float report 0.
    const int2 size{4, 4};

    Imf::Header header(size.x, size.y);
    header.channels().insert("H", Imf::Channel(Imf::HALF));
    header.channels().insert("F", Imf::Channel(Imf::FLOAT));
    header.channels().insert("U", Imf::Channel(Imf::UINT));

    std::vector<half>     halves(size.x * size.y, half(0.5f));
    std::vector<float>    floats(size.x * size.y, 0.5f);
    std::vector<uint32_t> uints(size.x * size.y, 7u);

    std::ostringstream out(std::ios::binary);
    {
        Imf::StdOSStream os;
        Imf::FrameBuffer fb;
        fb.insert("H", Imf::Slice(Imf::HALF, (char *)halves.data(), sizeof(half), sizeof(half) * size.x));
        fb.insert("F", Imf::Slice(Imf::FLOAT, (char *)floats.data(), sizeof(float), sizeof(float) * size.x));
        fb.insert("U", Imf::Slice(Imf::UINT, (char *)uints.data(), sizeof(uint32_t), sizeof(uint32_t) * size.x));

        Imf::OutputFile file(os, header);
        file.setFrameBuffer(fb);
        file.writePixels(size.y);
        out << os.str();
    }

    std::istringstream in(out.str(), std::ios::binary);
    auto               images = load_exr_image(in, "mixed.exr");
    REQUIRE(images.size() == 1);

    std::map<std::string, int> depths;
    for (const auto &c : images[0]->channels) depths[c.name] = c.bits_per_sample;

    REQUIRE(depths.size() == 3);
    CHECK(depths["H"] == 0);
    CHECK(depths["F"] == 0);
    CHECK(depths["U"] == 32);
}

// The tests below need real-world EXR files save_exr_image cannot produce (it writes neither multi-part files
// nor subsampled channels), so they use OpenEXR's own test image set. Only compiled in when CMake found that
// data, i.e. for the -cpm and -universal presets that fetch OpenEXR from source.
#ifdef HDRVIEW_TEST_OPENEXR_DIR

namespace
{

std::ifstream open_test_exr(const char *filename)
{
    std::string   path = std::string(HDRVIEW_TEST_OPENEXR_DIR) + "/" + filename;
    std::ifstream file(path, std::ios::binary);
    REQUIRE_MESSAGE(file.good(), "Could not open vendored test EXR: ", path);
    return file;
}

} // namespace

TEST_CASE("EXR load splits a real multi-part file into one Image per part, with correct part names")
{
    auto file     = open_test_exr("multipart.0001.exr");
    auto reloaded = load_exr_image(file, "multipart.0001.exr");

    REQUIRE(reloaded.size() == 10);

    std::set<std::string> expected_names = {"rgba_right", "depth_left",        "forward_left",  "whitebarmask_left",
                                            "rgba_left",  "depth_right",       "forward_right", "disparityL",
                                            "disparityR", "whitebarmask_right"};
    std::set<std::string> actual_names;
    for (const auto &img : reloaded) actual_names.insert(img->partname);
    CHECK(actual_names == expected_names);

    auto depth_left = std::find_if(reloaded.begin(), reloaded.end(),
                                   [](const ImagePtr &img) { return img->partname == "depth_left"; });
    REQUIRE(depth_left != reloaded.end());
    REQUIRE((*depth_left)->channels.size() == 1);
    CHECK((*depth_left)->channels[0].name == "Z");
}

TEST_CASE("EXR load up-samples subsampled channels to full resolution with block-consistent nearest-neighbor values")
{
    auto file     = open_test_exr("Flowers.exr");
    auto reloaded = load_exr_image(file, "Flowers.exr");

    REQUIRE(reloaded.size() == 1);
    auto &img = *reloaded[0];

    auto find_channel = [&](const char *name) -> Channel &
    {
        auto it =
            std::find_if(img.channels.begin(), img.channels.end(), [&](const Channel &c) { return c.name == name; });
        REQUIRE(it != img.channels.end());
        return *it;
    };

    Channel &y  = find_channel("Y");  // full resolution (sampling 1x1)
    Channel &ry = find_channel("RY"); // subsampled 2x2 in the file
    Channel &by = find_channel("BY"); // subsampled 2x2 in the file

    // every channel ends up at the full data-window resolution whatever its sampling rate in the file
    CHECK(y.size().x == ry.size().x);
    CHECK(y.size().y == ry.size().y);
    CHECK(y.size().x == by.size().x);
    CHECK(y.size().y == by.size().y);

    // nearest-neighbor up-res gives each 2x2 block of output pixels one source value
    for (int2 block_origin : {int2{10, 10}, int2{40, 60}})
    {
        INFO("block_origin = ", block_origin.x, ",", block_origin.y);
        float v00 = ry(block_origin.x, block_origin.y);
        CHECK(ry(block_origin.x + 1, block_origin.y) == doctest::Approx(v00));
        CHECK(ry(block_origin.x, block_origin.y + 1) == doctest::Approx(v00));
        CHECK(ry(block_origin.x + 1, block_origin.y + 1) == doctest::Approx(v00));
    }
}

TEST_CASE("EXR load reads every vendored real-world file identically from disk and from memory")
{
    namespace fs = std::filesystem;

    // load_exr_image() lets OpenEXR open a FileIStream's path itself and reads any other stream's bytes from
    // memory; both must decode the same pixels
    auto decode = [](std::istream &is, const std::string &name)
    {
        std::vector<std::pair<std::string, std::vector<float>>> parts;
        for (const auto &img : load_exr_image(is, name))
        {
            std::vector<float> samples;
            std::string        names = img->partname;
            for (const auto &c : img->channels)
            {
                names += fmt::format(" {}:{}x{}", c.name, c.size().x, c.size().y);
                for (int i = 0; i < c.num_elements(); ++i) samples.push_back(c(i));
            }
            parts.emplace_back(names, std::move(samples));
        }
        return parts;
    };

    int checked = 0;
    for (const auto &entry : fs::directory_iterator(HDRVIEW_TEST_OPENEXR_DIR))
    {
        if (entry.path().extension() != ".exr")
            continue;
        // deep (multi-sample-per-pixel) images are a different EXR image type; HDRView reads only scanline
        // and tiled images, through the regular FrameBuffer API
        if (entry.path().filename().string().find(".deep.") != std::string::npos)
            continue;

        const auto name = entry.path().filename().string();
        INFO("file = ", name);

        FileIStream file{entry.path()};
        REQUIRE(file.good());
        auto from_disk = decode(file, name);
        REQUIRE_FALSE(from_disk.empty());
        for (const auto &[names, samples] : from_disk) CHECK_FALSE(samples.empty());

        file.clear();
        file.seekg(0);
        std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

        SpanIStream span{bytes};
        const auto  from_memory = decode(span, name);
        REQUIRE(from_memory.size() == from_disk.size());
        for (size_t p = 0; p < from_memory.size(); ++p)
        {
            CHECK(from_memory[p].first == from_disk[p].first);
            // bitwise, so NaNs compare equal
            CHECK((from_memory[p].second.size() == from_disk[p].second.size() &&
                   std::memcmp(from_memory[p].second.data(), from_disk[p].second.data(),
                               from_memory[p].second.size() * sizeof(float)) == 0));
        }
        ++checked;
    }
    CHECK(checked > 0); // sanity: make sure the directory iteration actually found and checked something
}

#endif // HDRVIEW_TEST_OPENEXR_DIR

TEST_CASE("An EXR rational attribute with a zero denominator does not divide by it")
{
    // Imf::Rational is an int over an unsigned int read straight out of the file, and nothing in the format
    // stops the denominator being zero; 0/0 is how some writers spell "not set".
    const std::string path = (std::filesystem::temp_directory_path() / "hdrview_rational.exr").string();

    {
        Imf::Header header{4, 4};
        header.insert("framesPerSecond", Imf::RationalAttribute{Imf::Rational{24, 0}});
        header.channels().insert("Y", Imf::Channel{Imf::FLOAT});

        std::vector<float> pixels(16, 0.5f);
        Imf::OutputFile    out{path.c_str(), header};
        Imf::FrameBuffer   fb;
        fb.insert("Y", Imf::Slice{Imf::FLOAT, (char *)pixels.data(), sizeof(float), sizeof(float) * 4});
        out.setFrameBuffer(fb);
        out.writePixels(4);
    }

    // scoped like the writer above: Windows refuses to delete a file that is still open
    std::vector<ImagePtr> images;
    {
        std::ifstream is{path, std::ios::binary};
        REQUIRE(is);
        CHECK_NOTHROW(images = load_image(is, path));
    }
    REQUIRE(images.size() == 1);

    // and the attribute is still reported, with both halves intact
    const json &header_json = images[0]->metadata["header"];
    REQUIRE(header_json.contains("framesPerSecond"));
    const json &fps = header_json["framesPerSecond"]["value"];
    CHECK(fps["numerator"] == 24);
    CHECK(fps["denominator"] == 0);

    std::filesystem::remove(path);
}

#if OPENEXR_VERSION_MAJOR > 3 || (OPENEXR_VERSION_MAJOR == 3 && OPENEXR_VERSION_MINOR >= 3)
TEST_CASE("MemoryIStream's concurrent reads return what exists at an offset and zero-fill the rest")
{
    // OpenEXR's chunk reads come from offsets in the file, which a damaged offset table can put anywhere
    std::string bytes(100, '\0');
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = char(i + 1);
    MemoryIStream is{bytes, "test.exr"};
    CHECK(is.isStatelessRead());
    CHECK(is.size() == int64_t(bytes.size()));

    for (uint64_t offset :
         {uint64_t(0), uint64_t(37), uint64_t(99), uint64_t(100), uint64_t(101), uint64_t(1) << 40, ~uint64_t(0)})
        for (uint64_t sz : {uint64_t(0), uint64_t(1), uint64_t(63), uint64_t(200)})
        {
            INFO("offset = ", offset, ", sz = ", sz);
            std::vector<char> buf(sz, 'x');
            uint64_t          want = offset < bytes.size() ? std::min<uint64_t>(sz, bytes.size() - offset) : 0;
            CHECK(is.read(buf.data(), sz, offset) == int64_t(want));
            for (uint64_t i = 0; i < sz; ++i)
                if (buf[i] != (i < want ? bytes[offset + i] : '\0'))
                {
                    FAIL_CHECK("byte ", i, " is wrong");
                    break;
                }
        }

    // the sequential interface refuses to run off the end instead
    char c[8];
    is.seekg(96);
    CHECK_THROWS(is.read(c, 8));
    CHECK_THROWS(is.seekg(101));
}
#endif

TEST_CASE("EXR load of a truncated file fails cleanly, from disk and from memory")
{
    auto img = make_test_image(int2{64, 48});
    for (bool tiled : {false, true})
    {
        INFO("tiled = ", tiled);
        auto opts        = exr_default_save_options(*img);
        opts.compression = Imf::ZIP_COMPRESSION;
        opts.tiled       = tiled;
        opts.tile_width = opts.tile_height = 16;
        std::ostringstream out(std::ios::binary);
        save_exr_image(*img, out, "test.exr", &opts);
        const std::string full = out.str();

        const std::string path = (std::filesystem::temp_directory_path() / "hdrview_truncated.exr").string();
        for (size_t len = 0; len < full.size(); len += 1 + full.size() / 40)
        {
            INFO("len = ", len, " of ", full.size());
            const std::string bytes = full.substr(0, len);
            SpanIStream       span{bytes};
            CHECK_THROWS(load_exr_image(span, "test.exr"));

            std::ofstream{path, std::ios::binary}.write(bytes.data(), bytes.size());
            FileIStream file{path};
            CHECK_THROWS(load_exr_image(file, "test.exr"));
        }
        std::filesystem::remove(path);

        SpanIStream span{full};
        CHECK(load_exr_image(span, "test.exr").size() == 1);
    }
}
