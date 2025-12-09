#include "dds.h"
#include "colorspace.h"
#include "image.h"
#include <fmt/core.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// these pragmas ignore warnings about unused static functions
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#define SMALLDDS_IMPLEMENTATION
#include "smalldds.h"

#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

#include "astc_decomp.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

using namespace std;
using namespace smalldds;
using namespace basisu;

namespace
{

// Helper: Compute normal Z from X/Y (as in OIIO)
inline uint8_t compute_normal_z(uint8_t x, uint8_t y)
{
    float nx  = 2.0f * (x / 255.0f) - 1.0f;
    float ny  = 2.0f * (y / 255.0f) - 1.0f;
    float nz2 = 1.0f - nx * nx - ny * ny;
    float nz  = nz2 > 0.0f ? sqrtf(nz2) : 0.0f;
    int   z   = int(255.0f * (nz + 1.0f) / 2.0f);
    return (uint8_t)std::clamp(z, 0, 255);
}

// expand from RG into RGB, computing B from RG
void compute_normal_rg(uint8_t *rgba, int count)
{
    for (int i = count - 1; i >= 0; --i)
    {
        uint8_t x       = rgba[i * 2 + 0];
        uint8_t y       = rgba[i * 2 + 1];
        rgba[i * 3 + 0] = x;
        rgba[i * 3 + 1] = y;
        rgba[i * 3 + 2] = compute_normal_z(x, y);
    }
}

// contract from RGBA (R & B unused) to RGB, computing B from GA
void compute_normal_ag(uint8_t *rgba, int count)
{
    for (int i = 0; i < count; ++i)
    {
        uint8_t x       = rgba[i * 4 + 3];
        uint8_t y       = rgba[i * 4 + 1];
        rgba[i * 3 + 0] = x;
        rgba[i * 3 + 1] = y;
        rgba[i * 3 + 2] = compute_normal_z(x, y);
    }
}

void name_channels(const ImagePtr &image, const DDSFile &dds)
{
    using DXGI = DDSFile::DXGIFormat;
    auto fmt   = dds.format();
    if (image->channels.size() == 1 && dds.color_transform != DDSFile::ColorTransform::eLuminance)
    {
        string name = "Y";
        switch (fmt)
        {
        default:
        case DXGI::R32_Float:
        case DXGI::R32_UInt:
        case DXGI::R16_UInt:
        case DXGI::R32_SInt:
        case DXGI::R16_SInt:
        case DXGI::R8_SInt:
        case DXGI::R16_SNorm:
        case DXGI::R8_SNorm:
        case DXGI::R16_UNorm:
        case DXGI::R8_UInt:
        case DXGI::R8_UNorm: name = "R"; break;

        case DXGI::D32_Float:
        case DXGI::D16_UNorm: name = "D"; break;

        case DXGI::A8_UNorm: name = "A"; break;

        case DXGI::BC4_UNorm: name = "R"; break;
        case DXGI::BC4_SNorm: name = "R"; break;
        }

        if (dds.bitmasked && !dds.bitmask_has_rgb && dds.bitmask_has_alpha)
            name = "A"; // if we have a bitmask, we assume it's alpha

        if (name != "Y")
            image->channels[0].name = name;
    }
    else if (image->channels.size() == 2)
    {
        if (fmt == DDSFile::R8G8_UInt || fmt == DDSFile::R8G8_SInt || fmt == DDSFile::R8G8_UNorm ||
            fmt == DDSFile::R8G8_SNorm || fmt == DDSFile::R8G8_Typeless || fmt == DDSFile::R32G32_Float ||
            fmt == DDSFile::R16G16_Float || fmt == DDSFile::R16G16_UNorm || fmt == DDSFile::R16G16_SNorm ||
            fmt == DDSFile::R16G16_UInt)
        {
            image->channels[0].name = "u";
            image->channels[1].name = "v";
        }
        // else if (fmt == DDSFile::R32G32_Float)
        // {
        //     image->channels[0].name = "R";
        //     image->channels[1].name = "G";
        // }
    }
}

vector<ImagePtr> load_uncompressed(const DDSFile::ImageData *data, DDSFile &dds, DDSFile::DataType type)
{
    using Type = DDSFile::DataType;
    int nc     = dds.num_channels;

    int  w     = data->width;
    int  h     = data->height;
    auto m     = data->bytes();
    auto image = make_shared<Image>(int2(w, h), nc);

    using DXGI = DDSFile::DXGIFormat;
    auto fmt   = dds.format();

    int Bpp = (dds.header.pixel_format.bit_count + 7) / 8;

    if (type == Type::Packed || dds.bitmasked)
    {
        auto masks  = dds.header.pixel_format.masks;
        auto shifts = dds.right_shifts;
        // special cases
        if (fmt == DXGI::R9G9B9E5_SHAREDEXP)
        {
            if (nc != 3 || image->channels.size() != 3)
                throw logic_error("R11G11B10_Float format must have 3 channels");
            int u8_index = 0;
            for (int i = 0; i < w * h; ++i, u8_index += Bpp)
            {
                uint32_t packed = reinterpret_cast<const uint32_t *>(data->bytes() + u8_index)[0];
                auto     r      = (packed & masks[0]) >> shifts[0];
                auto     g      = (packed & masks[1]) >> shifts[1];
                auto     b      = (packed & masks[2]) >> shifts[2];
                auto     e      = (packed & masks[3]) >> shifts[3];

                float fr = decode_float9_exp_5(r, e);
                float fg = decode_float9_exp_5(g, e);
                float fb = decode_float9_exp_5(b, e);

                image->channels[0](i) = fr;
                image->channels[1](i) = fg;
                image->channels[2](i) = fb;
            }
        }
        else if (fmt == DXGI::R1_UNorm)
        {
            // each row must be at least 1 byte, and must start on a byte boundary
            int w_bytes = (w + 7) / 8; // number of bytes needed to store the bits
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w_bytes; ++x)
                {
                    uint8_t byte = data->bytes()[y * w_bytes + x];
                    for (int bit_idx = 0; bit_idx < 8 && (x * 8 + bit_idx) < w; ++bit_idx)
                    {
                        int idx                 = y * w + (x * 8 + bit_idx);
                        image->channels[0](idx) = (byte >> (7 - bit_idx)) & 0x1;
                    }
                }
            }
        }
        else if (fmt == DXGI::R11G11B10_Float)
        {
            if (nc != 3 || image->channels.size() != 3)
                throw logic_error("R11G11B10_Float format must have 3 channels");
            for (int i = 0; i < w * h; ++i)
            {
                auto packed           = reinterpret_cast<const uint32_t *>(m)[i];
                image->channels[0](i) = decode_float11((packed >> 0) & 0x7FF);  // 11 bits for red
                image->channels[1](i) = decode_float11((packed >> 11) & 0x7FF); // 11 bits for green
                image->channels[2](i) = decode_float10((packed >> 22) & 0x3FF); // 10 bits for blue
            }
        }
        else if (fmt == DXGI::R10G10B10_XR_BIAS_A2_UNorm)
        {
            if (nc != 4 || image->channels.size() != 4)
                throw logic_error("R10G10B10_XR_BIAS_A2_UNorm format must have 4 channels");
            int u8_index = 0;
            for (int i = 0; i < w * h; ++i, u8_index += Bpp)
            {
                uint32_t packed = reinterpret_cast<const uint32_t *>(data->bytes() + u8_index)[0];

                image->channels[0](i) = xr_bias_to_float((packed & masks[0]) >> shifts[0]);
                image->channels[1](i) = xr_bias_to_float((packed & masks[1]) >> shifts[1]);
                image->channels[2](i) = xr_bias_to_float((packed & masks[2]) >> shifts[2]);
                image->channels[3](i) = ((packed & masks[3]) >> shifts[3]) / 3.f;
            }
        }
        else if (dds.bitmasked)
        {
            int mask_c = 0;
            for (int c = 0; c < nc; ++c, ++mask_c)
            {
                Channel &ch       = image->channels[c];
                int      u8_index = 0;

                // there might be empty channel masks, so find the c-th non-empty channel
                // mask_c = max(c, mask_c);
                while (mask_c < 4 && masks[mask_c] == 0) ++mask_c;

                bool snorm = dds.bitmask_was_bump_du_dv;

                float multiplier = snorm ? 1.f / float((1 << (dds.bit_counts[mask_c] - 1)) - 1)
                                         : 1.f / float((1 << (dds.bit_counts[mask_c])) - 1);

                for (int i = 0; i < w * h; ++i, u8_index += Bpp)
                {
                    uint32_t packed = reinterpret_cast<const uint32_t *>(data->bytes() + u8_index)[0];

                    // shift everything to the right end of a 32-bit int
                    auto shifted = (packed & masks[mask_c]) << (32 - shifts[mask_c] - dds.bit_counts[mask_c]);
                    if (snorm)
                    {
                        auto value = reinterpret_cast<const int32_t *>(&shifted)[0];
                        value      = arithmetic_right_shift(value, 32 - dds.bit_counts[mask_c]);
                        ch(i)      = std::max(-1.f, multiplier * value);
                    }
                    else
                    {
                        auto value = reinterpret_cast<const uint32_t *>(&shifted)[0];
                        value      = arithmetic_right_shift(value, 32 - dds.bit_counts[mask_c]);
                        ch(i)      = multiplier * value;
                    }
                }
            }
        }
    }
    else
    {
        int file_nc = (dds.bpp == 0) ? dds.num_channels : dds.bpp / 8 / DDSFile::data_type_size(type);
        for (int c = 0; c < nc; ++c)
        {
            Channel &ch = image->channels[c];
            switch (type)
            {
            case Type::Float32:
                spdlog::warn("Handling Float32 format.");
                ch.copy_from_interleaved((const float *)m, w, h, file_nc, c, [](float v) { return v; });
                break;
            case Type::Float16:
                spdlog::warn("Handling Float16 format.");
                ch.copy_from_interleaved((const half *)m, w, h, file_nc, c, [](half v) { return float(v); });
                break;
            case Type::SInt32:
                ch.copy_from_interleaved((const int32_t *)m, w, h, file_nc, c, [](int32_t v) { return float(v); });
                break;
            case Type::SInt16:
                ch.copy_from_interleaved((const int16_t *)m, w, h, file_nc, c, [](int16_t v) { return float(v); });
                break;
            case Type::SInt8:
                ch.copy_from_interleaved((const int8_t *)m, w, h, file_nc, c, [](int8_t v) { return float(v); });
                break;
            case Type::UInt32:
                ch.copy_from_interleaved((const uint32_t *)m, w, h, file_nc, c, [](uint32_t v) { return float(v); });
                break;
            case Type::UInt16:
                ch.copy_from_interleaved((const uint16_t *)m, w, h, file_nc, c, [](uint16_t v) { return float(v); });
                break;
            case Type::UInt8:
                ch.copy_from_interleaved((const uint8_t *)m, w, h, file_nc, c, [](uint8_t v) { return float(v); });
                break;
            case Type::SNorm16:
                ch.copy_from_interleaved((const int16_t *)m, w, h, file_nc, c,
                                         [](int16_t v) { return dequantize_full(v); });
                break;
            case Type::SNorm8:
                ch.copy_from_interleaved((const int8_t *)m, w, h, file_nc, c,
                                         [](int8_t v) { return dequantize_full(v); });
                break;
            case Type::UNorm16:
                ch.copy_from_interleaved((const uint16_t *)m, w, h, file_nc, c,
                                         [](uint16_t v) { return dequantize_full(v); });
                break;
            case Type::UNorm8:
                ch.copy_from_interleaved((const uint8_t *)m, w, h, file_nc, c,
                                         [](uint8_t v) { return dequantize_full(v); });
                break;
            default: break;
            }
        }
    }

    // swap the R and G channels
    if (dds.color_transform == DDSFile::ColorTransform::eSwapRG && nc >= 2)
    {
        spdlog::info("Swapping R and G channels.");
        std::swap(image->channels[0], image->channels[1]);
        std::swap(image->channels[0].name, image->channels[1].name);
    }
    else if (dds.color_transform == DDSFile::ColorTransform::eSwapRB && nc >= 3)
    {
        spdlog::info("Swapping R and B channels.");
        std::swap(image->channels[0], image->channels[2]);
        std::swap(image->channels[0].name, image->channels[2].name);
    }
    return {image};
}

vector<ImagePtr> load_compressed(const DDSFile::ImageData *data, const DDSFile &dds, bool is_signed, bool is_normal)
{
    int  num_channels = dds.num_channels;
    int  width        = data->width;
    int  height       = data->height;
    int  depth        = dds.depth();
    auto cmp          = dds.compression;

    int file_num_channels = num_channels;
    switch (cmp)
    {
    case DDSFile::Compression::BC1_DXT1:
    case DDSFile::Compression::BC2_DXT2:
    case DDSFile::Compression::BC2_DXT3:
    case DDSFile::Compression::BC7: file_num_channels = 4; break;
    case DDSFile::Compression::BC3_DXT4:
    case DDSFile::Compression::BC3_DXT5: file_num_channels = is_normal ? 3 : 4; break;
    case DDSFile::Compression::BC4: file_num_channels = 1; break;
    case DDSFile::Compression::BC5: file_num_channels = 2; break;
    case DDSFile::Compression::BC6HS:
    case DDSFile::Compression::BC6HU: file_num_channels = 3; break;
    case DDSFile::Compression::ASTC: file_num_channels = 4; break;
    default: throw std::invalid_argument("Unsupported BC format for decompression.");
    }

    int block_width  = dds.block_width();
    int block_height = dds.block_height();

    if (block_width <= 1 || block_height <= 1)
        throw std::invalid_argument("Invalid block size for compression.");

    const int    width_in_blocks  = (width + block_width - 1) / block_width;
    const int    height_in_blocks = (height + block_height - 1) / block_height;
    const size_t block_size       = cmp == DDSFile::Compression::BC1_DXT1 || cmp == DDSFile::Compression::BC4 ? 8 : 16;

    bool is_float = (cmp == DDSFile::Compression::BC6HU || cmp == DDSFile::Compression::BC6HS);

    // Use separate buffers for float and uint8_t types
    vector<float>   float_out(block_width * block_height * 3, 0);
    vector<uint8_t> uint8_out(block_width * block_height * 4, 0);

    vector<ImagePtr> images;

    for (int d = 0; d < depth; ++d)
    {
        auto image = make_shared<Image>(int2{width, height}, num_channels);

        if (depth > 1)
        {
            spdlog::info("Decompressing depth slice {}/{}", d + 1, depth);
            image->partname = fmt::format("z={:0>{}}", d, int(ceil(std::log10(depth))));
        }

        if (cmp == DDSFile::Compression::BC5)
        {
            if (is_normal && num_channels == 3)
            {
                image->channels[0].name = "x";
                image->channels[1].name = "y";
                image->channels[2].name = "z";
            }
            else if (num_channels == 2)
            {
                image->channels[0].name = "u";
                image->channels[1].name = "v";
            }
        }

        auto start_of_slice = data->bytes() + d * (height_in_blocks * width_in_blocks * block_size);

        parallel_for(
            blocked_range<int>(0, height_in_blocks, 1024 * 1024 / block_width / block_height),
            [&](int start_y, int end_y, int, int)
            {
                for (int by = start_y; by < end_y; ++by)
                {
                    for (int bx = 0; bx < width_in_blocks; ++bx)
                    {
                        auto block = start_of_slice + (by * width_in_blocks + bx) * block_size;

                        switch (cmp)
                        {
                        case DDSFile::Compression::BC1_DXT1: bcdec_bc1(block, uint8_out.data(), block_width * 4); break;
                        case DDSFile::Compression::BC2_DXT2:
                        case DDSFile::Compression::BC2_DXT3: bcdec_bc2(block, uint8_out.data(), block_width * 4); break;
                        case DDSFile::Compression::BC3_DXT4:
                        case DDSFile::Compression::BC3_DXT5: bcdec_bc3(block, uint8_out.data(), block_width * 4); break;
                        case DDSFile::Compression::BC4: bcdec_bc4(block, uint8_out.data(), block_width); break;
                        case DDSFile::Compression::BC5: bcdec_bc5(block, uint8_out.data(), block_width * 2); break;
                        case DDSFile::Compression::BC6HU:
                        case DDSFile::Compression::BC6HS:
                            bcdec_bc6h_float(block, float_out.data(), block_width * 3,
                                             cmp == DDSFile::Compression::BC6HS);
                            break;
                        case DDSFile::Compression::BC7: bcdec_bc7(block, uint8_out.data(), block_width * 4); break;
                        case DDSFile::Compression::ASTC:
                            astc::decompress(uint8_out.data(), block, dds.is_sRGB(), block_width, block_height);
                            break;
                        default: throw std::invalid_argument("Unsupported format for decompression.");
                        }

                        // swap the R and G channels
                        if (dds.color_transform == DDSFile::ColorTransform::eSwapRG && file_num_channels >= 2)
                        {
                            for (int i = 0; i < block_width * block_height; ++i)
                                std::swap(uint8_out[i * 2 + 0], uint8_out[i * 2 + 1]);
                        }
                        // swap the R and B channels
                        else if (dds.color_transform == DDSFile::ColorTransform::eSwapRB && file_num_channels >= 3)
                        {
                            for (int i = 0; i < block_width * block_height; ++i)
                                std::swap(uint8_out[i * file_num_channels + 0], uint8_out[i * file_num_channels + 2]);
                        }
                        // RXGB swizzle: swap R and A channels
                        else if (dds.color_transform == DDSFile::ColorTransform::eAGBR && file_num_channels == 4)
                        {
                            for (int i = 0; i < block_width * block_height; ++i)
                                std::swap(uint8_out[i * 4 + 0], uint8_out[i * 4 + 3]);
                        }

                        // If normal map, convert to RGB normal map in-place
                        if (is_normal)
                        {
                            if (cmp == DDSFile::Compression::BC5)
                                compute_normal_rg(uint8_out.data(), block_width * block_height);
                            else if (cmp == DDSFile::Compression::BC3_DXT5)
                                compute_normal_ag(uint8_out.data(), block_width * block_height);
                        }

                        for (int py = 0; py < block_height; ++py)
                        {
                            int y = by * block_height + py;
                            if (y >= height)
                                continue;
                            for (int px = 0; px < block_width; ++px)
                            {
                                int x = bx * block_width + px;
                                if (x >= width)
                                    continue;
                                int src_idx = (py * block_width + px) * (is_normal ? 3 : file_num_channels);
                                int dst_idx = y * width + x;
                                for (int c = 0; c < num_channels; ++c)
                                    if (is_float)
                                        image->channels[c](dst_idx) = float_out[src_idx + c];
                                    else
                                    {
                                        float f = is_signed ? dequantize_full(reinterpret_cast<int8_t *>(
                                                                  uint8_out.data())[src_idx + c])
                                                            : dequantize_full(uint8_out[src_idx + c]);
                                        image->channels[c](dst_idx) = f;
                                    }
                            }
                        }
                    }
                }
            });
        images.push_back(image);
    }

    return images;
}

json set_metadata(const DDSFile &dds)
{
    json header;

    auto       &hdr                 = dds.header;
    auto       &dxt10hdr            = dds.header_DXT10;
    auto        cmp                 = dds.compression;
    string      cmp_str             = compression_name(cmp);
    std::string alpha_mode_str      = alpha_mode_name(dds.alpha_mode);
    std::string color_transform_str = color_transform_name(dds.color_transform);

    header["is cubemap"]  = {{"value", dds.is_cubemap}, {"string", dds.is_cubemap ? "yes" : "no"}, {"type", "boolean"}};
    header["compression"] = {{"value", cmp}, {"string", cmp_str}, {"type", "enum"}};

    header["pf.flags"]   = {{"value", hdr.pixel_format.flags},
                            {"string", fmt::format("{:#010x}", hdr.pixel_format.flags)},
                            {"type", "uint32"}};
    header["has fourCC"] = {
        {"value", (hdr.pixel_format.flags & uint32_t(DDSFile::PixelFormatFlagBits::FourCC)) != 0},
        {"string", (hdr.pixel_format.flags & uint32_t(DDSFile::PixelFormatFlagBits::FourCC)) != 0 ? "yes" : "no"},
        {"type", "boolean"}};
    header["pf.fourCC"] = {
        {"value", hdr.pixel_format.fourCC},
        {"string", fmt::format("{:#010x} ({})", hdr.pixel_format.fourCC, fourCC_to_string(hdr.pixel_format.fourCC))},
        {"type", "uint32"}};
    header["pf.bit_count"] = {{"value", hdr.pixel_format.bit_count},
                              {"string", fmt::format("{}", hdr.pixel_format.bit_count)},
                              {"type", "uint32"}};

    header["flags"]  = {{"value", hdr.flags}, {"string", fmt::format("{:#010x}", hdr.flags)}, {"type", "uint32"}};
    header["height"] = {{"value", hdr.height}, {"string", fmt::format("{}", hdr.height)}, {"type", "uint32"}};
    header["width"]  = {{"value", hdr.width}, {"string", fmt::format("{}", hdr.width)}, {"type", "uint32"}};
    header["pitch or linear size"] = {{"value", hdr.pitch_or_linear_size},
                                      {"string", fmt::format("{}", hdr.pitch_or_linear_size)},
                                      {"type", "uint32"}};
    header["depth"]        = {{"value", hdr.depth}, {"string", fmt::format("{}", hdr.depth)}, {"type", "uint32"}};
    header["mipmap count"] = {
        {"value", hdr.mipmap_count}, {"string", fmt::format("{}", hdr.mipmap_count)}, {"type", "uint32"}};
    // for (int i = 0; i < 11; ++i)
    //     header[fmt::format("reserved1[{}]", i)] = {
    //         {"value", hdr.reserved1[i]}, {"string", fmt::format("{:#010x}", hdr.reserved1[i])}, {"type", "uint32"}};

    header["caps1"] = {{"value", hdr.caps1}, {"string", fmt::format("{:#010x}", hdr.caps1)}, {"type", "uint32"}};
    header["caps2"] = {{"value", hdr.caps2}, {"string", fmt::format("{:#010x}", hdr.caps2)}, {"type", "uint32"}};
    header["caps3"] = {{"value", hdr.caps3}, {"string", fmt::format("{:#010x}", hdr.caps3)}, {"type", "uint32"}};
    header["caps4"] = {{"value", hdr.caps4}, {"string", fmt::format("{:#010x}", hdr.caps4)}, {"type", "uint32"}};
    // header["reserved2"] = {
    //     {"value", hdr.reserved2}, {"string", fmt::format("{:#010x}", hdr.reserved2)}, {"type", "uint32"}};
    header["alpha mode"]      = {{"value", dds.alpha_mode}, {"string", alpha_mode_str}, {"type", "enum"}};
    header["color transform"] = {{"value", dds.color_transform}, {"string", color_transform_str}, {"type", "enum"}};
    header["DXT10 header"]    = {
        {"value", dds.has_DXT10_header}, {"string", dds.has_DXT10_header ? "yes" : "no"}, {"type", "boolean"}};

    header["DXT10.format"] = {
        {"value", dxt10hdr.format},
        {"string", fmt::format("{}: '{}'", (uint32_t)dxt10hdr.format, format_name(dxt10hdr.format))},
        {"type", "enum"}};
    header["DXT10.resource_dimension"] = {{"value", dxt10hdr.resource_dimension},
                                          {"string", fmt::format("{}", static_cast<int>(dxt10hdr.resource_dimension))},
                                          {"type", "enum"}};
    header["DXT10.misc_flag"]          = {
        {"value", dxt10hdr.misc_flag}, {"string", fmt::format("{:#010x}", dxt10hdr.misc_flag)}, {"type", "uint32"}};
    header["DXT10.array_size"] = {
        {"value", dxt10hdr.array_size}, {"string", fmt::format("{}", dxt10hdr.array_size)}, {"type", "uint32"}};
    header["DXT10.misc_flag2"] = {
        {"value", dxt10hdr.misc_flag2}, {"string", fmt::format("{:#010x}", dxt10hdr.misc_flag2)}, {"type", "uint32"}};

    header["bitmasked"] = {{"value", dds.bitmasked}, {"string", dds.bitmasked ? "yes" : "no"}, {"type", "boolean"}};
    header["bitmask_has_alpha"] = {
        {"value", dds.bitmask_has_alpha}, {"string", dds.bitmask_has_alpha ? "yes" : "no"}, {"type", "boolean"}};
    header["bitmask_has_rgb"] = {
        {"value", dds.bitmask_has_rgb}, {"string", dds.bitmask_has_rgb ? "yes" : "no"}, {"type", "boolean"}};
    header["bitmask_was_bump_du_dv"] = {{"value", dds.bitmask_was_bump_du_dv},
                                        {"string", dds.bitmask_was_bump_du_dv ? "yes" : "no"},
                                        {"type", "boolean"}};
    header["bit_counts"]   = {{"value", std::vector<uint32_t>(std::begin(dds.bit_counts), std::end(dds.bit_counts))},
                              {"string", fmt::format("[{}, {}, {}, {}]", dds.bit_counts[0], dds.bit_counts[1],
                                                     dds.bit_counts[2], dds.bit_counts[3])},
                              {"type", "array"}};
    header["right_shifts"] = {
        {"value", std::vector<uint32_t>(std::begin(dds.right_shifts), std::end(dds.right_shifts))},
        {"string", fmt::format("[{}, {}, {}, {}]", dds.right_shifts[0], dds.right_shifts[1], dds.right_shifts[2],
                               dds.right_shifts[3])},
        {"type", "array"}};
    header["num_channels"] = {
        {"value", dds.num_channels}, {"string", fmt::format("{}", dds.num_channels)}, {"type", "uint32"}};

    if (dds.bitmasked)
    {
        std::string bitmask_str;
        struct ChannelInfo
        {
            char     letter;
            uint32_t mask;
            uint32_t bit_count;
        };
        ChannelInfo channels[4] = {
            {'R', dds.header.pixel_format.masks[0], dds.bit_counts[0]},
            {'G', dds.header.pixel_format.masks[1], dds.bit_counts[1]},
            {'B', dds.header.pixel_format.masks[2], dds.bit_counts[2]},
            {'A', dds.header.pixel_format.masks[3], dds.bit_counts[3]},
        };
        if (dds.color_transform == DDSFile::ColorTransform::eLuminance)
            channels[0].letter = 'L';
        // Sort channels by mask (left-most = highest bit index)
        std::vector<ChannelInfo> sorted;
        for (int i = 0; i < 4; ++i)
            if (channels[i].bit_count > 0)
                sorted.push_back(channels[i]);
        std::sort(sorted.begin(), sorted.end(),
                  [](const ChannelInfo &a, const ChannelInfo &b) { return a.mask > b.mask; });
        for (const auto &ch : sorted) bitmask_str += fmt::format("{}{}", ch.letter, ch.bit_count);
        header["bitmask_string"] = {{"value", bitmask_str}, {"string", bitmask_str}, {"type", "string"}};
    }
    else
        spdlog::info("No bitmask detected in DDS file.");
    for (int i = 0; i < 4; ++i)
        header[fmt::format("pf.masks[{}]", i)] = {{"value", hdr.pixel_format.masks[i]},
                                                  {"string", fmt::format("{:#010x}", hdr.pixel_format.masks[i])},
                                                  {"type", "uint32"}};
    return header;
}

} // namespace

bool is_dds_image(std::istream &is) noexcept
{
    try
    {
        DDSFile dds;
        auto    result = dds.load(is);
        is.clear();
        is.seekg(0);
        return result.type != Result::Error;
    }
    catch (...)
    {
        is.clear();
        is.seekg(0);
        return false;
    }
}

vector<ImagePtr> load_dds_image(istream &is, string_view filename, const ImageLoadOptions &opts)
{
    ScopedMDC mdc{"IO", "DDS"};
    DDSFile   dds;
    auto      result = dds.load(is);
    if (result.type == Result::Error)
        throw std::invalid_argument(result.message);
    else if (result.type == Result::Warning)
        spdlog::warn(result.message);
    else if (result.type == Result::Info)
        spdlog::info(result.message);

    result = dds.populate_image_data();
    if (result.type == Result::Error)
        throw std::invalid_argument(result.message);
    else if (result.type == Result::Warning)
        spdlog::warn(result.message);
    else if (result.type == Result::Info)
        spdlog::info(result.message);

    auto &hdr      = dds.header;
    auto &dxt10hdr = dds.header_DXT10;
    auto  fmt      = dds.format();

    bool is_normal = (hdr.pixel_format.flags & uint32_t(DDSFile::PixelFormatFlagBits::Normal)) != 0;
    bool is_signed = dxt10hdr.format == DDSFile::BC5_SNorm || dxt10hdr.format == DDSFile::BC4_SNorm;

    DDSFile::DataType type = DDSFile::data_type(fmt);

    // deduce bitmasks for packed types

    if (type == DDSFile::DataType::Packed)
    {
        if (!dds.bitmasked)
            spdlog::error("Encountered packed format but no bitmasks are present!");

        spdlog::warn("masks: {:08x} {:08x} {:08x} {:08x}", dds.header.pixel_format.masks[0],
                     dds.header.pixel_format.masks[1], dds.header.pixel_format.masks[2],
                     dds.header.pixel_format.masks[3]);
    }

    json header = set_metadata(dds);

    if (dds.num_channels == 0)
    {
        spdlog::debug("File '{}': Unsupported format or no channels detected. This was the header:\n{}", filename,
                      header.dump(2));
        throw std::invalid_argument("Unsupported format or no channels detected.");
    }

    static const char *cubemap_face_names[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    vector<ImagePtr>   images;
    spdlog::debug("Loading {} images from DDS file: {}", dds.array_size(), filename);
    for (uint32_t p = 0; p < dds.array_size(); ++p)
    {
        const DDSFile::ImageData *data = dds.get_image_data(0, p);
        if (!data)
            throw std::runtime_error(fmt::format("No image data found for array index {}.", p));

        vector<ImagePtr> new_images;
        if (dds.compression != DDSFile::Compression::None)
            new_images = load_compressed(data, dds, is_signed, is_normal);
        else
            new_images = load_uncompressed(data, dds, type);

        for (size_t i = 0; i < new_images.size(); ++i)
        {
            ImagePtr image = new_images[i];

            // sRGB to linear for uncompressed sRGB formats
            if (dds.is_sRGB())
            {
                spdlog::info("Converting sRGB to linear for uncompressed image.");
                for (int c = 0; c < std::min(3, dds.num_channels); ++c)
                    image->channels[c].apply([](float v, int, int) { return sRGB_to_linear(v); });
            }

            // rename the channels according to the format type
            name_channels(image, dds);

            // Set shared metadata after image creation
            image->filename = filename;
            if (image->partname.empty())
                image->partname = dds.array_size() > 1 ? cubemap_face_names[p % 6] : "";
            image->alpha_type =
                image->channels.size() >= 4 || image->channels.size() == 2
                    ? (dds.alpha_mode == DDSFile::ALPHA_MODE_PREMULTIPLIED ? AlphaType_PremultipliedLinear
                                                                           : AlphaType_Straight)
                    : AlphaType_None;
            image->metadata["loader"] = "smalldds";
            image->metadata["pixel format"] =
                dds.bitmasked ? header["bitmask_string"]["string"].get<string>()
                              : fmt::format("{} ({})", format_name(dxt10hdr.format), (uint32_t)dxt10hdr.format);
            image->metadata["transfer function"] =
                transfer_function_name(dds.is_sRGB() ? TransferFunction::sRGB : TransferFunction::Linear);

            image->metadata["header"] = header;

            images.push_back(image);
        }
    }
    spdlog::debug("Loaded {} images from DDS file: {}", images.size(), filename);

    return images;
}
