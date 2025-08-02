#include "dds.h"
#include "image.h"
#include "texture.h"
#include <fmt/core.h>
#include <iostream>
#include <memory>
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

#define TINYDDSLOADER_IMPLEMENTATION
#include "tinyddsloader.h"

#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

using namespace std;
using namespace tinyddsloader;

namespace
{

enum Types
{
    Float32,
    Float16,
    SInt8,
    SInt16,
    SInt32,
    UInt8,
    UInt16,
    UInt32,
    SNorm8,
    SNorm16,
    UNorm8,
    UNorm16,
    Typeless,
};

json dxgi_format_to_json(DDSFile::DXGIFormat fmt)
{
    static const char *dxgi_format_names[] = {"Unknown",
                                              "R32G32B32A32_Typeless",
                                              "R32G32B32A32_Float",
                                              "R32G32B32A32_UInt",
                                              "R32G32B32A32_SInt",
                                              "R32G32B32_Typeless",
                                              "R32G32B32_Float",
                                              "R32G32B32_UInt",
                                              "R32G32B32_SInt",
                                              "R16G16B16A16_Typeless",
                                              "R16G16B16A16_Float",
                                              "R16G16B16A16_UNorm",
                                              "R16G16B16A16_UInt",
                                              "R16G16B16A16_SNorm",
                                              "R16G16B16A16_SInt",
                                              "R32G32_Typeless",
                                              "R32G32_Float",
                                              "R32G32_UInt",
                                              "R32G32_SInt",
                                              "R32G8X24_Typeless",
                                              "D32_Float_S8X24_UInt",
                                              "R32_Float_X8X24_Typeless",
                                              "X32_Typeless_G8X24_UInt",
                                              "R10G10B10A2_Typeless",
                                              "R10G10B10A2_UNorm",
                                              "R10G10B10A2_UInt",
                                              "R11G11B10_Float",
                                              "R8G8B8A8_Typeless",
                                              "R8G8B8A8_UNorm",
                                              "R8G8B8A8_UNorm_SRGB",
                                              "R8G8B8A8_UInt",
                                              "R8G8B8A8_SNorm",
                                              "R8G8B8A8_SInt",
                                              "R16G16_Typeless",
                                              "R16G16_Float",
                                              "R16G16_UNorm",
                                              "R16G16_UInt",
                                              "R16G16_SNorm",
                                              "R16G16_SInt",
                                              "R32_Typeless",
                                              "D32_Float",
                                              "R32_Float",
                                              "R32_UInt",
                                              "R32_SInt",
                                              "R24G8_Typeless",
                                              "D24_UNorm_S8_UInt",
                                              "R24_UNorm_X8_Typeless",
                                              "X24_Typeless_G8_UInt",
                                              "R8G8_Typeless",
                                              "R8G8_UNorm",
                                              "R8G8_UInt",
                                              "R8G8_SNorm",
                                              "R8G8_SInt",
                                              "R16_Typeless",
                                              "R16_Float",
                                              "D16_UNorm",
                                              "R16_UNorm",
                                              "R16_UInt",
                                              "R16_SNorm",
                                              "R16_SInt",
                                              "R8_Typeless",
                                              "R8_UNorm",
                                              "R8_UInt",
                                              "R8_SNorm",
                                              "R8_SInt",
                                              "A8_UNorm",
                                              "R1_UNorm",
                                              "R9G9B9E5_SHAREDEXP",
                                              "R8G8_B8G8_UNorm",
                                              "G8R8_G8B8_UNorm",
                                              "BC1_Typeless",
                                              "BC1_UNorm",
                                              "BC1_UNorm_SRGB",
                                              "BC2_Typeless",
                                              "BC2_UNorm",
                                              "BC2_UNorm_SRGB",
                                              "BC3_Typeless",
                                              "BC3_UNorm",
                                              "BC3_UNorm_SRGB",
                                              "BC4_Typeless",
                                              "BC4_UNorm",
                                              "BC4_SNorm",
                                              "BC5_Typeless",
                                              "BC5_UNorm",
                                              "BC5_SNorm",
                                              "B5G6R5_UNorm",
                                              "B5G5R5A1_UNorm",
                                              "B8G8R8A8_UNorm",
                                              "B8G8R8X8_UNorm",
                                              "R10G10B10_XR_BIAS_A2_UNorm",
                                              "B8G8R8A8_Typeless",
                                              "B8G8R8A8_UNorm_SRGB",
                                              "B8G8R8X8_Typeless",
                                              "B8G8R8X8_UNorm_SRGB",
                                              "BC6H_Typeless",
                                              "BC6H_UF16",
                                              "BC6H_SF16",
                                              "BC7_Typeless",
                                              "BC7_UNorm",
                                              "BC7_UNorm_SRGB",
                                              "AYUV",
                                              "Y410",
                                              "Y416",
                                              "NV12",
                                              "P010",
                                              "P016",
                                              "YUV420_OPAQUE",
                                              "YUY2",
                                              "Y210",
                                              "Y216",
                                              "NV11",
                                              "AI44",
                                              "IA44",
                                              "P8",
                                              "A8P8",
                                              "B4G4R4A4_UNorm"};

    uint32_t    value = static_cast<uint32_t>(fmt);
    std::string name;
    if (value <= 115)
        name = dxgi_format_names[value];
    else if (value == 130)
        name = "P208";
    else if (value == 131)
        name = "V208";
    else if (value == 132)
        name = "V408";
    else
        name = "Unknown";

    json j;
    j["value"]  = value;
    j["type"]   = "int";
    j["string"] = name;
    return j;
}

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

void get_channel_specs(DDSFile::DXGIFormat format, int &num_channels, int &bytes_per_channel, Types &type,
                       bool is_normal)
{
    using DXGI = DDSFile::DXGIFormat;

    bytes_per_channel = 0;

    switch (format)
    {
    // Compressed formats
    case DXGI::BC1_UNorm:
    case DXGI::BC1_UNorm_SRGB:
    case DXGI::BC2_UNorm:
    case DXGI::BC2_UNorm_SRGB:
    case DXGI::BC7_UNorm:
    case DXGI::BC7_UNorm_SRGB:
        num_channels      = 4;
        type              = UNorm8;
        bytes_per_channel = 1;
        break;
    case DXGI::BC3_UNorm:
    case DXGI::BC3_UNorm_SRGB:
        num_channels      = is_normal ? 3 : 4;
        type              = UNorm8;
        bytes_per_channel = 1;
        break;
    case DXGI::BC4_UNorm:
        num_channels      = 1;
        type              = UNorm8;
        bytes_per_channel = 1;
        break;
    case DXGI::BC4_SNorm:
        num_channels      = 1;
        type              = SNorm8;
        bytes_per_channel = 1;
        break;
    case DXGI::BC5_UNorm:
        num_channels      = is_normal ? 3 : 2;
        type              = UNorm8;
        bytes_per_channel = 1;
        break;
    case DXGI::BC5_SNorm:
        num_channels      = is_normal ? 3 : 2;
        type              = SNorm8;
        bytes_per_channel = 1;
        break;
    case DXGI::BC6H_UF16:
    case DXGI::BC6H_SF16:
        num_channels      = 3;
        type              = Float16;
        bytes_per_channel = 2;
        break;

    // Uncompressed formats
    case DXGI::R32G32B32A32_Float:
        num_channels = 4;
        type         = Float32;
        break;
    case DXGI::R32G32B32_Float:
        num_channels = 3;
        type         = Float32;
        break;
    case DXGI::R32G32_Float:
        num_channels = 2;
        type         = Float32;
        break;
    case DXGI::R32_Float:
    case DXGI::D32_Float:
        num_channels = 1;
        type         = Float32;
        break;

    case DXGI::R16G16B16A16_Float:
        num_channels = 4;
        type         = Float16;
        break;
    case DXGI::R16G16_Float:
        num_channels = 2;
        type         = Float16;
        break;
    case DXGI::R16_Float:
        num_channels = 1;
        type         = Float16;
        break;

    case DXGI::R32G32B32A32_UInt:
        num_channels = 4;
        type         = UInt32;
        break;
    case DXGI::R32G32B32_UInt:
        num_channels = 3;
        type         = UInt32;
        break;
    case DXGI::R32G32_UInt:
        num_channels = 2;
        type         = UInt32;
        break;
    case DXGI::R32_UInt:
        num_channels = 1;
        type         = UInt32;
        break;

    case DXGI::R16G16B16A16_UInt:
        num_channels = 4;
        type         = UInt16;
        break;
    case DXGI::R16G16_UInt:
        num_channels = 2;
        type         = UInt16;
        break;
    case DXGI::R16_UInt:
        num_channels = 1;
        type         = UInt16;
        break;

    case DXGI::R8G8B8A8_UInt:
        num_channels = 4;
        type         = UInt8;
        break;
    case DXGI::R8G8_UInt:
        num_channels = 2;
        type         = UInt8;
        break;
    case DXGI::R8_UInt:
        num_channels = 1;
        type         = UInt8;
        break;

    case DXGI::R32G32B32A32_SInt:
        num_channels = 4;
        type         = SInt32;
        break;
    case DXGI::R32G32B32_SInt:
        num_channels = 3;
        type         = SInt32;
        break;
    case DXGI::R32G32_SInt:
        num_channels = 2;
        type         = SInt32;
        break;
    case DXGI::R32_SInt:
        num_channels = 1;
        type         = SInt32;
        break;

    case DXGI::R16G16B16A16_SInt:
        num_channels = 4;
        type         = SInt16;
        break;
    case DXGI::R16G16_SInt:
        num_channels = 2;
        type         = SInt16;
        break;
    case DXGI::R16_SInt:
        num_channels = 1;
        type         = SInt16;
        break;

    case DXGI::R8G8B8A8_SInt:
        num_channels = 4;
        type         = SInt8;
        break;
    case DXGI::R8G8_SInt:
        num_channels = 2;
        type         = SInt8;
        break;
    case DXGI::R8_SInt:
        num_channels = 1;
        type         = SInt8;
        break;

    case DXGI::R16G16B16A16_SNorm:
        num_channels = 4;
        type         = SNorm16;
        break;
    case DXGI::R16G16_SNorm:
        num_channels = 2;
        type         = SNorm16;
        break;
    case DXGI::R16_SNorm:
        num_channels = 1;
        type         = SNorm16;
        break;

    case DXGI::R8G8B8A8_SNorm:
        num_channels = 4;
        type         = SNorm8;
        break;
    case DXGI::R8G8_SNorm:
        num_channels = 2;
        type         = SNorm8;
        break;
    case DXGI::R8_SNorm:
        num_channels = 1;
        type         = SNorm8;
        break;

    case DXGI::R16G16B16A16_UNorm:
        num_channels = 4;
        type         = UNorm16;
        break;
    case DXGI::R16G16_UNorm:
        num_channels = 2;
        type         = UNorm16;
        break;
    case DXGI::R16_UNorm:
    case DXGI::D16_UNorm:
        num_channels = 1;
        type         = UNorm16;
        break;

    case DXGI::R8G8B8A8_UNorm:
    case DXGI::R8G8B8A8_UNorm_SRGB:
    case DXGI::B8G8R8A8_UNorm:
    case DXGI::B8G8R8A8_UNorm_SRGB:
        num_channels = 4;
        type         = UNorm8;
        break;
    case DXGI::R8G8_UNorm:
        num_channels = 2;
        type         = UNorm8;
        break;
    case DXGI::R8_UNorm:
        num_channels = 1;
        type         = UNorm8;
        break;
    case DXGI::B8G8R8X8_UNorm:
        num_channels = 4;
        type         = UNorm8;
        break;

    case DXGI::R11G11B10_Float:
        num_channels = 3;
        type         = Float32;
        break;

    default:
        num_channels      = 0;
        type              = Typeless;
        bytes_per_channel = 0;
        break;
    }

    // Set bytes_per_channel for uncompressed formats based on type
    if (bytes_per_channel == 0 && num_channels > 0)
    {
        switch (type)
        {
        case Float32:
        case UInt32:
        case SInt32: bytes_per_channel = 4; break;
        case Float16:
        case UInt16:
        case SInt16:
        case SNorm16:
        case UNorm16: bytes_per_channel = 2; break;
        case UInt8:
        case SInt8:
        case SNorm8:
        case UNorm8: bytes_per_channel = 1; break;
        default: bytes_per_channel = 0; break;
        }
    }
}

ImagePtr load_uncompressed(const DDSFile::ImageData *data, int nc, Types type, bool is_srgb)
{
    int  w     = data->width;
    int  h     = data->height;
    auto m     = data->mem;
    auto image = make_shared<Image>(int2(w, h), nc);
    for (int c = 0; c < nc; ++c)
    {
        Channel &ch = image->channels[c];
        switch (type)
        {
        case Float32: ch.copy_from_interleaved((const float *)m, w, h, nc, c, [](float v) { return v; }); break;
        case Float16: ch.copy_from_interleaved((const half *)m, w, h, nc, c, [](half v) { return float(v); }); break;
        case SInt32:
            ch.copy_from_interleaved((const int32_t *)m, w, h, nc, c, [](int32_t v) { return float(v); });
            break;
        case SInt16:
            ch.copy_from_interleaved((const int16_t *)m, w, h, nc, c, [](int16_t v) { return float(v); });
            break;
        case SInt8: ch.copy_from_interleaved((const int8_t *)m, w, h, nc, c, [](int8_t v) { return float(v); }); break;
        case UInt32:
            ch.copy_from_interleaved((const uint32_t *)m, w, h, nc, c, [](uint32_t v) { return float(v); });
            break;
        case UInt16:
            ch.copy_from_interleaved((const uint16_t *)m, w, h, nc, c, [](uint16_t v) { return float(v); });
            break;
        case UInt8:
            ch.copy_from_interleaved((const uint8_t *)m, w, h, nc, c, [](uint8_t v) { return float(v); });
            break;
        case SNorm16:
            ch.copy_from_interleaved((const int16_t *)m, w, h, nc, c, [](int16_t v) { return dequantize_full(v); });
            break;
        case SNorm8:
            ch.copy_from_interleaved((const int8_t *)m, w, h, nc, c, [](int8_t v) { return dequantize_full(v); });
            break;
        case UNorm16:
            ch.copy_from_interleaved((const uint16_t *)m, w, h, nc, c, [](uint16_t v) { return dequantize_full(v); });
            break;
        case UNorm8:
            ch.copy_from_interleaved((const uint8_t *)m, w, h, nc, c, [](uint8_t v) { return dequantize_full(v); });
            break;
        default: break;
        }
    }
    // sRGB to linear for uncompressed sRGB formats
    if (is_srgb)
    {
        spdlog::info("Converting sRGB to linear for uncompressed image.");
        for (int c = 0; c < std::min(3, nc); ++c)
            image->channels[c].apply([](float v, int, int) { return sRGB_to_linear(v); });
    }
    return image;
}

ImagePtr load_bc_compressed(const DDSFile::ImageData *data, int num_channels, DDSFile::Compression cmp, bool is_signed,
                            bool is_normal, bool is_srgb, bool is_rxgb)
{
    int  width  = data->width;
    int  height = data->height;
    auto image  = make_shared<Image>(int2{width, height}, num_channels);

    constexpr int block_width      = 4;
    const int     width_in_blocks  = (width + block_width - 1) / block_width;
    const int     height_in_blocks = (height + block_width - 1) / block_width;
    const size_t  block_size       = cmp == DDSFile::Compression::DXT1 || cmp == DDSFile::Compression::BC4 ? 8 : 16;

    bool is_float = (cmp == DDSFile::Compression::BC6HU || cmp == DDSFile::Compression::BC6HS);

    parallel_for(
        blocked_range<int>(0, height_in_blocks, 1024 * 1024 / block_width / block_width),
        [&](int start_y, int end_y, int, int)
        {
            for (int by = start_y; by < end_y; ++by)
            {
                for (int bx = 0; bx < width_in_blocks; ++bx)
                {
                    const uint8_t *block =
                        static_cast<const uint8_t *>(data->mem) + (by * width_in_blocks + bx) * block_size;

                    // Use separate buffers for float and uint8_t types
                    float   float_out[4 * 4 * 3] = {0};
                    uint8_t uint8_out[4 * 4 * 4] = {0};

                    switch (cmp)
                    {
                    case DDSFile::Compression::DXT1: bcdec_bc1(block, uint8_out, block_width * 4); break;
                    case DDSFile::Compression::DXT2:
                    case DDSFile::Compression::DXT3: bcdec_bc2(block, uint8_out, block_width * 4); break;
                    case DDSFile::Compression::DXT4:
                    case DDSFile::Compression::DXT5: bcdec_bc3(block, uint8_out, block_width * 4); break;
                    case DDSFile::Compression::BC4: bcdec_bc4(block, uint8_out, block_width); break;
                    case DDSFile::Compression::BC5: bcdec_bc5(block, uint8_out, block_width * 2); break;
                    case DDSFile::Compression::BC6HU:
                    case DDSFile::Compression::BC6HS:
                        bcdec_bc6h_float(block, float_out, block_width * 3, cmp == DDSFile::Compression::BC6HS);
                        break;
                    case DDSFile::Compression::BC7: bcdec_bc7(block, uint8_out, block_width * 4); break;
                    default: throw std::invalid_argument("Unsupported BC format for decompression.");
                    }

                    // RXGB swizzle: swap R and A channels for each pixel in the block
                    if (is_rxgb)
                    {
                        for (int i = 0; i < block_width * block_width; ++i)
                            std::swap(uint8_out[i * 4 + 0], uint8_out[i * 4 + 3]);
                    }

                    // If normal map, convert to RGB normal map in-place
                    if (!is_float && is_normal)
                    {
                        if (cmp == DDSFile::Compression::BC5)
                            compute_normal_rg(uint8_out, block_width * block_width);
                        else if (cmp == DDSFile::Compression::DXT5)
                            compute_normal_ag(uint8_out, block_width * block_width);
                    }

                    for (int py = 0; py < block_width; ++py)
                    {
                        int y = by * block_width + py;
                        if (y >= height)
                            continue;
                        for (int px = 0; px < block_width; ++px)
                        {
                            int x = bx * block_width + px;
                            if (x >= width)
                                continue;
                            int src_idx = (py * block_width + px) * num_channels;
                            int dst_idx = y * width + x;
                            for (int c = 0; c < num_channels; ++c)
                                if (is_float)
                                    image->channels[c](dst_idx) = float_out[src_idx + c];
                                else
                                {
                                    float f                     = is_signed
                                                                      ? dequantize_full(reinterpret_cast<int8_t *>(uint8_out)[src_idx + c])
                                                                      : dequantize_full(uint8_out[src_idx + c]);
                                    image->channels[c](dst_idx) = (is_srgb && c < 3) ? sRGB_to_linear(f) : f;
                                }
                        }
                    }
                }
            }
        });

    return image;
}

} // namespace

bool is_dds_image(std::istream &is) noexcept
{
    try
    {
        DDSFile dds;
        auto    result = dds.Load(is);
        is.clear();
        is.seekg(0);
        return result == tinyddsloader::Result::Success;
    }
    catch (...)
    {
        is.clear();
        is.seekg(0);
        return false;
    }
}

vector<ImagePtr> load_dds_image(istream &is, string_view filename, string_view channel_selector)
{
    DDSFile dds;
    if (dds.Load(is) != tinyddsloader::Result::Success || dds.PopulateImageDatas() != tinyddsloader::Result::Success)
        throw std::runtime_error("Failed to load DDS.");

    auto hdr      = dds.GetHeader();
    auto dxt10hdr = dds.GetHeaderDXT10();
    auto fmt      = dds.GetFormat();
    auto cmp      = dds.GetCompression();

    bool is_normal = (hdr.pixelFormat.flags & uint32_t(DDSFile::PixelFormatFlagBits::Normal)) != 0;
    bool is_srgb   = (fmt == DDSFile::DXGIFormat::BC1_UNorm_SRGB || fmt == DDSFile::DXGIFormat::BC2_UNorm_SRGB ||
                    fmt == DDSFile::DXGIFormat::BC3_UNorm_SRGB || fmt == DDSFile::DXGIFormat::BC7_UNorm_SRGB ||
                    fmt == DDSFile::DXGIFormat::R8G8B8A8_UNorm_SRGB ||
                    fmt == DDSFile::DXGIFormat::B8G8R8A8_UNorm_SRGB || fmt == DDSFile::DXGIFormat::B8G8R8X8_UNorm_SRGB);
    bool is_rxgb   = (cmp == DDSFile::Compression::DXT5 && hdr.pixelFormat.fourCC == DDSFile::RXGB_4CC);
    bool is_signed = dxt10hdr.format == DDSFile::BC5_SNorm || dxt10hdr.format == DDSFile::BC4_SNorm;

    spdlog::info("height = {}.", dds.GetHeight());
    spdlog::info("height = {}.", dds.GetHeight());
    spdlog::info("depth = {}.", dds.GetDepth());
    spdlog::info("mipCount = {}.", dds.GetMipCount());
    spdlog::info("arraySize = {}.", dds.GetArraySize());
    spdlog::info("bits per pixel = {}.", dds.GetBitsPerPixel(fmt));
    spdlog::info("format = {}.", (uint32_t)fmt);
    spdlog::info("isCubeMap = {}.", (uint32_t)dds.IsCubemap());
    spdlog::info("isCompressed = {}.", (uint32_t)dds.IsCompressed(fmt));
    spdlog::info("isNormalMap = {}.", is_normal);
    spdlog::info("isSRGB = {}.", is_srgb);
    spdlog::info("isRXGB = {}.", is_rxgb);
    spdlog::info("pixelFormat.size = {}.", hdr.pixelFormat.size);
    spdlog::info("pixelFormat.flags = {:#010x}.", hdr.pixelFormat.flags);
    spdlog::info("pixelFormat.fourCC = {:#010x}.", hdr.pixelFormat.fourCC);
    spdlog::info("pixelFormat.bitCount = {}.", hdr.pixelFormat.bitCount);
    spdlog::info("pixelFormat.masks = {:#010x}:{:#010x}:{:#010x}:{:#010x}.", hdr.pixelFormat.masks[0],
                 hdr.pixelFormat.masks[1], hdr.pixelFormat.masks[2], hdr.pixelFormat.masks[3]);

    int   num_channels      = 0;
    int   bytes_per_channel = 0;
    Types type              = Typeless;
    get_channel_specs(fmt, num_channels, bytes_per_channel, type, is_normal);

    if (num_channels == 0 || bytes_per_channel == 0)
        throw std::invalid_argument("DDS: Unsupported format or no channels detected.");

    // Set bit depth metadata based on type and number of channels
    std::string bit_depth_str;
    switch (type)
    {
    case Float32: bit_depth_str = fmt::format("{}-bit float (32 bpc)", num_channels * 32); break;
    case Float16: bit_depth_str = fmt::format("{}-bit half (16 bpc)", num_channels * 16); break;
    case SInt8: bit_depth_str = fmt::format("{}-bit int8 (8 bpc)", num_channels * 8); break;
    case SInt16: bit_depth_str = fmt::format("{}-bit int16 (16 bpc)", num_channels * 16); break;
    case SInt32: bit_depth_str = fmt::format("{}-bit int32 (32 bpc)", num_channels * 32); break;
    case UInt8: bit_depth_str = fmt::format("{}-bit uint8 (8 bpc)", num_channels * 8); break;
    case UInt16: bit_depth_str = fmt::format("{}-bit uint16 (16 bpc)", num_channels * 16); break;
    case UInt32: bit_depth_str = fmt::format("{}-bit uint32 (32 bpc)", num_channels * 32); break;
    case SNorm8: bit_depth_str = fmt::format("{}-bit snorm8 (8 bpc)", num_channels * 8); break;
    case SNorm16: bit_depth_str = fmt::format("{}-bit snorm16 (16 bpc)", num_channels * 16); break;
    case UNorm8: bit_depth_str = fmt::format("{}-bit unorm8 (8 bpc)", num_channels * 8); break;
    case UNorm16: bit_depth_str = fmt::format("{}-bit unorm16 (16 bpc)", num_channels * 16); break;
    default: bit_depth_str = "unknown"; break;
    }

    vector<ImagePtr> images;

    static const char *cubemap_face_names[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};

    json header;
    header["isCubemap"] = {{"value", dds.IsCubemap()}, {"string", dds.IsCubemap() ? "yes" : "no"}, {"type", "boolean"}};

    string cmp_str = "None";
    switch (cmp)
    {
    case DDSFile::Compression::None: break;
    case DDSFile::Compression::DXT1: cmp_str = "DXT1"; break;
    case DDSFile::Compression::DXT2: cmp_str = "DXT2"; break;
    case DDSFile::Compression::DXT3: cmp_str = "DXT3"; break;
    case DDSFile::Compression::DXT4: cmp_str = "DXT4"; break;
    case DDSFile::Compression::DXT5: cmp_str = "DXT5"; break;
    case DDSFile::Compression::BC4: cmp_str = "BC4"; break;
    case DDSFile::Compression::BC5: cmp_str = "BC5"; break;
    case DDSFile::Compression::BC6HU: cmp_str = "BC6HU"; break;
    case DDSFile::Compression::BC6HS: cmp_str = "BC6HS"; break;
    case DDSFile::Compression::BC7: cmp_str = "BC7"; break;
    }

    header["compression"] = {{"value", cmp}, {"string", cmp_str}, {"type", "enum"}};
    header["format"]      = dxgi_format_to_json(fmt);

    for (uint32_t p = 0; p < dds.GetArraySize(); ++p)
    {
        const DDSFile::ImageData *data = dds.GetImageData(0, p);
        if (!data)
            throw std::runtime_error("DDS: No image data found for the specified array index.");

        ImagePtr image;
        if (DDSFile::IsCompressed(fmt))
            image = load_bc_compressed(data, num_channels, cmp, is_signed, is_normal, is_srgb, is_rxgb);
        else
            image = load_uncompressed(data, num_channels, type, is_srgb);

        // Set shared metadata after image creation
        image->filename                = filename;
        image->partname                = dds.GetArraySize() > 1 ? cubemap_face_names[p % 6] : "";
        image->file_has_straight_alpha = num_channels == 2 || num_channels == 4;
        image->metadata["loader"]      = "tinyddsloader";
        image->metadata["bit depth"]   = bit_depth_str;
        image->metadata["transfer function"] =
            transfer_function_name(is_srgb ? TransferFunction_sRGB : TransferFunction_Linear);
        image->metadata["header"] = header;

        images.push_back(image);
    }

    return images;
}
