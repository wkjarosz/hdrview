//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
// Times libheif's AV1 decoder plugins against each other on real files. libheif picks a decoder by
// priority unless heif_decoding_options::decoder_id names one, so a single build with both plugins
// compiled in can decode the same bitstream through each of them and report the difference.
//
// Usage: hdrview_bench_heif [-n iterations] [-y] [-v] [-c] <file-or-directory>...
//   -n  timed repetitions per file per decoder (default 5); the fastest is reported
//   -y  request the decoder's native YCbCr output instead of interleaved RGB
//   -v  compare the decoders' pixels instead of timing them; exits nonzero on any mismatch
//   -c  CSV instead of a table
//
// By default the decode is set up exactly as src/imageio/heif.cpp sets it up, so the numbers include
// the YCbCr->RGB conversion HDRView actually pays. That conversion is libheif's own and runs
// identically whichever plugin decoded the frame, so it dilutes the difference between them; -y
// leaves it out and measures the codecs alone. The gap between the two runs is what the conversion
// costs.
//

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <libheif/heif.h>
#include <libheif/heif_items.h>

namespace fs = std::filesystem;
using namespace std::chrono;

namespace
{

// Thrown for a file whose primary item is some other codec (HEVC in a .heic, say), which says nothing
// about the AV1 decoders and so is passed over rather than counted as a failure.
struct NotAV1
{
};

struct Timing
{
    double best_ms  = 0.0; // fastest of the repetitions
    double total_ms = 0.0; // sum over all repetitions, for a mean
    int    reps     = 0;
    bool   ok       = false;
};

struct Result
{
    std::string                   name;
    size_t                        bytes = 0;
    int                           width = 0, height = 0, depth = 0;
    std::string                   chroma;
    std::map<std::string, Timing> timings; // keyed by decoder id
};

std::vector<uint8_t> read_file(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open " + p.string());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// The AV1 decoder plugins libheif was built with, in the order it would fall back through them.
std::vector<std::string> av1_decoder_ids()
{
    const heif_decoder_descriptor *descriptors[16];
    int                            n = heif_get_decoder_descriptors(heif_compression_AV1, descriptors, 16);

    std::vector<std::string> ids;
    for (int i = 0; i < n; ++i)
        if (const char *id = heif_decoder_descriptor_get_id_name(descriptors[i]))
            ids.emplace_back(id);
    return ids;
}

const char *chroma_name(heif_chroma c)
{
    switch (c)
    {
    case heif_chroma_monochrome: return "mono";
    case heif_chroma_420: return "4:2:0";
    case heif_chroma_422: return "4:2:2";
    case heif_chroma_444: return "4:4:4";
    default: return "?";
    }
}

// One open+decode of an in-memory file through the named decoder. Unless `native` asks for whatever
// the codec produces, the request mirrors heif.cpp's: interleaved little-endian 16-bit RGB(A) with
// bilinear chroma upsampling. `pixels`, when given, receives the decoded interleaved plane so two
// decoders' output can be compared.
double decode_once(const std::vector<uint8_t> &data, const char *decoder_id, bool native, Result *info,
                   std::vector<uint8_t> *pixels = nullptr)
{
    heif_context *ctx = heif_context_alloc();
    if (!ctx)
        throw std::runtime_error("heif_context_alloc failed");
    struct CtxGuard
    {
        heif_context *c;
        ~CtxGuard() { heif_context_free(c); }
    } ctx_guard{ctx};

    if (auto err = heif_context_read_from_memory_without_copy(ctx, data.data(), data.size(), nullptr);
        err.code != heif_error_Ok)
        throw std::runtime_error(err.message);

    heif_image_handle *handle = nullptr;
    if (auto err = heif_context_get_primary_image_handle(ctx, &handle); err.code != heif_error_Ok)
        throw std::runtime_error(err.message);
    struct HandleGuard
    {
        heif_image_handle *h;
        ~HandleGuard() { heif_image_handle_release(h); }
    } handle_guard{handle};

    // These plugins only decode AV1, so anything else in a .heif/.heic container is not theirs to compare.
    if (heif_item_get_item_type(ctx, heif_image_handle_get_item_id(handle)) != heif_fourcc('a', 'v', '0', '1'))
        throw NotAV1{};

    heif_colorspace preferred_colorspace = heif_colorspace_undefined;
    heif_chroma     preferred_chroma     = heif_chroma_undefined;
    heif_image_handle_get_preferred_decoding_colorspace(handle, &preferred_colorspace, &preferred_chroma);

    const bool mono      = preferred_chroma == heif_chroma_monochrome;
    const bool has_alpha = heif_image_handle_has_alpha_channel(handle) != 0;

    heif_colorspace out_colorspace = heif_colorspace_undefined;
    heif_chroma     out_chroma     = heif_chroma_undefined;
    if (!native)
    {
        out_colorspace = mono ? heif_colorspace_monochrome : heif_colorspace_RGB;
        out_chroma     = mono ? heif_chroma_monochrome
                              : (has_alpha ? heif_chroma_interleaved_RRGGBBAA_LE : heif_chroma_interleaved_RRGGBB_LE);
    }

    heif_decoding_options *options = heif_decoding_options_alloc();
    struct OptGuard
    {
        heif_decoding_options *o;
        ~OptGuard() { heif_decoding_options_free(o); }
    } opt_guard{options};
    options->color_conversion_options.preferred_chroma_upsampling_algorithm = heif_chroma_upsampling_bilinear;
    options->color_conversion_options.only_use_preferred_chroma_algorithm   = true;
    options->decoder_id                                                     = decoder_id;

    heif_image *img = nullptr;

    auto t0  = steady_clock::now();
    auto err = heif_decode_image(handle, &img, out_colorspace, out_chroma, options);
    auto t1  = steady_clock::now();

    if (err.code != heif_error_Ok)
        throw std::runtime_error(err.message);

    if (info)
    {
        info->width  = heif_image_get_primary_width(img);
        info->height = heif_image_get_primary_height(img);
        info->depth  = heif_image_handle_get_luma_bits_per_pixel(handle);
        info->chroma = chroma_name(preferred_chroma);
    }

    if (pixels)
    {
        // Rows are padded to the decoder's stride, so copy the meaningful bytes of each one.
        const heif_channel channel = mono ? heif_channel_Y : heif_channel_interleaved;
        int                stride  = 0;
        const uint8_t     *plane   = heif_image_get_plane_readonly(img, channel, &stride);
        const int          h       = heif_image_get_height(img, channel);
        const int          w       = heif_image_get_width(img, channel);
        const int          bpp     = heif_image_get_bits_per_pixel(img, channel); // per pixel, all components
        const size_t       row     = (size_t)w * ((bpp + 7) / 8);

        pixels->resize(row * h);
        for (int y = 0; y < h; ++y) memcpy(pixels->data() + row * y, plane + (size_t)stride * y, row);
    }

    heif_image_release(img);

    return duration<double, std::milli>(t1 - t0).count();
}

void collect_files(const fs::path &p, std::vector<fs::path> &out)
{
    if (fs::is_directory(p))
    {
        for (auto &e : fs::recursive_directory_iterator(p))
        {
            auto ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (e.is_regular_file() && (ext == ".avif" || ext == ".heif" || ext == ".heic"))
                out.push_back(e.path());
        }
    }
    else
        out.push_back(p);
}

} // namespace

int main(int argc, char **argv)
{
    int                   reps   = 5;
    bool                  csv    = false;
    bool                  native = false;
    bool                  verify = false;
    std::vector<fs::path> inputs;

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "-n" && i + 1 < argc)
            reps = std::max(1, atoi(argv[++i]));
        else if (a == "-c")
            csv = true;
        else if (a == "-y")
            native = true;
        else if (a == "-v")
            verify = true;
        else if (a == "-h" || a == "--help")
        {
            printf("usage: %s [-n iterations] [-y] [-v] [-c] <file-or-directory>...\n", argv[0]);
            return 0;
        }
        else
            inputs.push_back(a);
    }

    if (inputs.empty())
    {
        fprintf(stderr, "no input files given; see --help\n");
        return 1;
    }

    auto decoders = av1_decoder_ids();
    if (decoders.empty())
    {
        fprintf(stderr, "libheif was built without any AV1 decoder plugin\n");
        return 1;
    }

    fprintf(stderr, "libheif %s, decoding to %s, AV1 decoders (priority order):\n", heif_get_version(),
            native ? "the codec's native output" : "interleaved 16-bit RGB");
    {
        const heif_decoder_descriptor *descriptors[16];
        int                            n = heif_get_decoder_descriptors(heif_compression_AV1, descriptors, 16);
        for (int i = 0; i < n; ++i)
            fprintf(stderr, "  %-8s %s\n", heif_decoder_descriptor_get_id_name(descriptors[i]),
                    heif_decoder_descriptor_get_name(descriptors[i]));
    }

    std::vector<fs::path> files;
    for (auto &p : inputs) collect_files(p, files);
    std::sort(files.begin(), files.end());

    // Comparison mode: every decoder has to reproduce the first one's pixels exactly. AV1 decoding is
    // specified bit-exactly, so anything else is a bug in one of them rather than a quality tradeoff.
    if (verify)
    {
        int identical = 0, differing = 0, unreadable = 0, skipped = 0;
        for (auto &f : files)
        {
            std::vector<uint8_t> data;
            try
            {
                data = read_file(f);
            }
            catch (const std::exception &e)
            {
                fprintf(stderr, "%-52.52s SKIP  %s\n", f.filename().string().c_str(), e.what());
                continue;
            }

            // Every decoder gets its turn even after one of them fails, because which of them failed is
            // the whole question: a file the container layer rejects fails identically for all of them
            // and says nothing, while one decoder failing alone is a real difference between them.
            std::vector<uint8_t> reference, other;
            std::vector<bool>    decoded(decoders.size(), false);
            bool                 mismatch = false, skip = false;
            std::string          detail, first_error;
            for (size_t i = 0; i < decoders.size(); ++i)
            {
                try
                {
                    decode_once(data, decoders[i].c_str(), native, nullptr, i == 0 ? &reference : &other);
                    decoded[i] = true;
                }
                catch (const NotAV1 &)
                {
                    skip = true;
                    break;
                }
                catch (const std::exception &e)
                {
                    if (first_error.empty())
                        first_error = e.what();
                    continue;
                }
                if (i == 0 || !decoded[0])
                    continue;

                if (other.size() != reference.size())
                {
                    mismatch = true;
                    detail   = "size " + std::to_string(other.size()) + " vs " + std::to_string(reference.size());
                }
                else if (memcmp(other.data(), reference.data(), reference.size()) != 0)
                {
                    size_t n = 0;
                    for (size_t b = 0; b < reference.size(); ++b) n += reference[b] != other[b];
                    mismatch = true;
                    detail   = std::to_string(n) + " of " + std::to_string(reference.size()) + " bytes differ";
                }
            }

            const int decoded_count = (int)std::count(decoded.begin(), decoded.end(), true);

            if (skip)
                ++skipped;
            else if (decoded_count == 0)
            {
                ++unreadable;
                printf("%-52.52s SKIP  no decoder could open it: %s\n", f.filename().string().c_str(),
                       first_error.c_str());
            }
            else if (decoded_count != (int)decoders.size())
            {
                ++differing;
                for (size_t i = 0; i < decoders.size(); ++i)
                    if (!decoded[i])
                        printf("%-52.52s FAIL  %s alone could not decode it: %s\n", f.filename().string().c_str(),
                               decoders[i].c_str(), first_error.c_str());
            }
            else if (mismatch)
            {
                ++differing;
                printf("%-52.52s DIFF  %s\n", f.filename().string().c_str(), detail.c_str());
            }
            else
                ++identical;
        }
        printf("\n%d identical, %d differing, %d unreadable by any decoder, %d not AV1 (%s vs %s)\n", identical,
               differing, unreadable, skipped, decoders.front().c_str(), decoders.back().c_str());
        return differing == 0 ? 0 : 1;
    }

    std::vector<Result> results;
    for (auto &f : files)
    {
        std::vector<uint8_t> data;
        try
        {
            data = read_file(f);
        }
        catch (const std::exception &e)
        {
            fprintf(stderr, "skipping %s: %s\n", f.string().c_str(), e.what());
            continue;
        }

        Result r;
        r.name  = f.filename().string();
        r.bytes = data.size();

        bool av1 = true;
        for (auto &id : decoders)
        {
            Timing t;
            try
            {
                decode_once(data, id.c_str(), native, &r); // warm up caches and fill in the image info
                t.best_ms = 1e30;
                for (int i = 0; i < reps; ++i)
                {
                    double ms = decode_once(data, id.c_str(), native, nullptr);
                    t.best_ms = std::min(t.best_ms, ms);
                    t.total_ms += ms;
                    ++t.reps;
                }
                t.ok = true;
            }
            catch (const NotAV1 &)
            {
                av1 = false;
                break;
            }
            catch (const std::exception &e)
            {
                // Off to the side so the table stays a table; the cell itself just reads "fail".
                fprintf(stderr, "%s: %s: %s\n", r.name.c_str(), id.c_str(), e.what());
            }
            r.timings[id] = t;
        }
        if (av1)
            results.push_back(std::move(r));
    }

    // Report. The last decoder in priority order is the baseline every other one is measured against.
    const std::string &baseline = decoders.back();

    if (csv)
    {
        printf("file,bytes,width,height,depth,chroma");
        for (auto &id : decoders) printf(",%s_best_ms,%s_mean_ms", id.c_str(), id.c_str());
        printf("\n");
        for (auto &r : results)
        {
            printf("%s,%zu,%d,%d,%d,%s", r.name.c_str(), r.bytes, r.width, r.height, r.depth, r.chroma.c_str());
            for (auto &id : decoders)
            {
                auto &t = r.timings[id];
                if (t.ok)
                    printf(",%.3f,%.3f", t.best_ms, t.total_ms / t.reps);
                else
                    printf(",,");
            }
            printf("\n");
        }
        return 0;
    }

    printf("\n%-44s %11s %6s %7s", "file", "size", "depth", "chroma");
    for (auto &id : decoders) printf(" %11s", id.c_str());
    printf("  speedup\n");

    std::map<std::string, double> totals;
    for (auto &r : results)
    {
        char dims[32];
        snprintf(dims, sizeof(dims), "%dx%d", r.width, r.height);
        printf("%-44.44s %11s %5db %7s", r.name.c_str(), dims, r.depth, r.chroma.c_str());

        for (auto &id : decoders)
        {
            auto &t = r.timings[id];
            if (t.ok)
            {
                printf(" %10.2f", t.best_ms);
                totals[id] += t.best_ms;
            }
            else
                printf(" %10s", "fail");
        }

        auto &base = r.timings[baseline];
        auto &fast = r.timings[decoders.front()];
        if (base.ok && fast.ok && fast.best_ms > 0)
            printf("  %6.2fx", base.best_ms / fast.best_ms);
        printf("\n");
    }

    printf("%-44s %11s %6s %7s", "TOTAL", "", "", "");
    for (auto &id : decoders) printf(" %10.2f", totals[id]);
    if (totals[decoders.front()] > 0)
        printf("  %6.2fx", totals[baseline] / totals[decoders.front()]);
    printf("\n(times are the fastest of %d decodes, in ms; speedup is %s vs %s)\n", reps, decoders.front().c_str(),
           baseline.c_str());

    return 0;
}
