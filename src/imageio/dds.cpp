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

// Helper: Expand BC5 RG to RGB normal map
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

// Helper: Expand BC3 AG to RGB normal map
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
        type              = Types::UNorm8;
        bytes_per_channel = 1;
        break;
    case DXGI::BC3_UNorm:
    case DXGI::BC3_UNorm_SRGB:
        num_channels      = is_normal ? 3 : 4;
        type              = Types::UNorm8;
        bytes_per_channel = 1;
        break;
    case DXGI::BC4_UNorm:
        num_channels      = 1;
        type              = Types::UNorm8;
        bytes_per_channel = 1;
        break;
    case DXGI::BC4_SNorm:
        num_channels      = 1;
        type              = Types::SNorm8;
        bytes_per_channel = 1;
        break;
    case DXGI::BC5_UNorm:
        num_channels      = is_normal ? 3 : 2;
        type              = Types::UNorm8;
        bytes_per_channel = 1;
        break;
    case DXGI::BC5_SNorm:
        num_channels      = is_normal ? 3 : 2;
        type              = Types::SNorm8;
        bytes_per_channel = 1;
        break;
    case DXGI::BC6H_UF16:
    case DXGI::BC6H_SF16:
        num_channels      = 3;
        type              = Types::Float16;
        bytes_per_channel = 2;
        break;

    // Uncompressed formats
    case DXGI::R32G32B32A32_Float:
        num_channels = 4;
        type         = Types::Float32;
        break;
    case DXGI::R32G32B32_Float:
        num_channels = 3;
        type         = Types::Float32;
        break;
    case DXGI::R32G32_Float:
        num_channels = 2;
        type         = Types::Float32;
        break;
    case DXGI::R32_Float:
    case DXGI::D32_Float:
        num_channels = 1;
        type         = Types::Float32;
        break;

    case DXGI::R16G16B16A16_Float:
        num_channels = 4;
        type         = Types::Float16;
        break;
    case DXGI::R16G16_Float:
        num_channels = 2;
        type         = Types::Float16;
        break;
    case DXGI::R16_Float:
        num_channels = 1;
        type         = Types::Float16;
        break;

    case DXGI::R32G32B32A32_UInt:
        num_channels = 4;
        type         = Types::UInt32;
        break;
    case DXGI::R32G32B32_UInt:
        num_channels = 3;
        type         = Types::UInt32;
        break;
    case DXGI::R32G32_UInt:
        num_channels = 2;
        type         = Types::UInt32;
        break;
    case DXGI::R32_UInt:
        num_channels = 1;
        type         = Types::UInt32;
        break;

    case DXGI::R16G16B16A16_UInt:
        num_channels = 4;
        type         = Types::UInt16;
        break;
    case DXGI::R16G16_UInt:
        num_channels = 2;
        type         = Types::UInt16;
        break;
    case DXGI::R16_UInt:
        num_channels = 1;
        type         = Types::UInt16;
        break;

    case DXGI::R8G8B8A8_UInt:
        num_channels = 4;
        type         = Types::UInt8;
        break;
    case DXGI::R8G8_UInt:
        num_channels = 2;
        type         = Types::UInt8;
        break;
    case DXGI::R8_UInt:
        num_channels = 1;
        type         = Types::UInt8;
        break;

    case DXGI::R32G32B32A32_SInt:
        num_channels = 4;
        type         = Types::SInt32;
        break;
    case DXGI::R32G32B32_SInt:
        num_channels = 3;
        type         = Types::SInt32;
        break;
    case DXGI::R32G32_SInt:
        num_channels = 2;
        type         = Types::SInt32;
        break;
    case DXGI::R32_SInt:
        num_channels = 1;
        type         = Types::SInt32;
        break;

    case DXGI::R16G16B16A16_SInt:
        num_channels = 4;
        type         = Types::SInt16;
        break;
    case DXGI::R16G16_SInt:
        num_channels = 2;
        type         = Types::SInt16;
        break;
    case DXGI::R16_SInt:
        num_channels = 1;
        type         = Types::SInt16;
        break;

    case DXGI::R8G8B8A8_SInt:
        num_channels = 4;
        type         = Types::SInt8;
        break;
    case DXGI::R8G8_SInt:
        num_channels = 2;
        type         = Types::SInt8;
        break;
    case DXGI::R8_SInt:
        num_channels = 1;
        type         = Types::SInt8;
        break;

    case DXGI::R16G16B16A16_SNorm:
        num_channels = 4;
        type         = Types::SNorm16;
        break;
    case DXGI::R16G16_SNorm:
        num_channels = 2;
        type         = Types::SNorm16;
        break;
    case DXGI::R16_SNorm:
        num_channels = 1;
        type         = Types::SNorm16;
        break;

    case DXGI::R8G8B8A8_SNorm:
        num_channels = 4;
        type         = Types::SNorm8;
        break;
    case DXGI::R8G8_SNorm:
        num_channels = 2;
        type         = Types::SNorm8;
        break;
    case DXGI::R8_SNorm:
        num_channels = 1;
        type         = Types::SNorm8;
        break;

    case DXGI::R16G16B16A16_UNorm:
        num_channels = 4;
        type         = Types::UNorm16;
        break;
    case DXGI::R16G16_UNorm:
        num_channels = 2;
        type         = Types::UNorm16;
        break;
    case DXGI::R16_UNorm:
    case DXGI::D16_UNorm:
        num_channels = 1;
        type         = Types::UNorm16;
        break;

    case DXGI::R8G8B8A8_UNorm:
    case DXGI::R8G8B8A8_UNorm_SRGB:
    case DXGI::B8G8R8A8_UNorm:
    case DXGI::B8G8R8A8_UNorm_SRGB:
        num_channels = 4;
        type         = Types::UNorm8;
        break;
    case DXGI::R8G8_UNorm:
        num_channels = 2;
        type         = Types::UNorm8;
        break;
    case DXGI::R8_UNorm:
        num_channels = 1;
        type         = Types::UNorm8;
        break;
    case DXGI::B8G8R8X8_UNorm:
        num_channels = 4;
        type         = Types::UNorm8;
        break;

    case DXGI::R11G11B10_Float:
        num_channels = 3;
        type         = Types::Float32;
        break;

    default:
        num_channels      = 0;
        type              = Types::Typeless;
        bytes_per_channel = 0;
        break;
    }

    // Set bytes_per_channel for uncompressed formats based on type
    if (bytes_per_channel == 0 && num_channels > 0)
    {
        switch (type)
        {
        case Types::Float32:
        case Types::UInt32:
        case Types::SInt32: bytes_per_channel = 4; break;
        case Types::Float16:
        case Types::UInt16:
        case Types::SInt16:
        case Types::SNorm16:
        case Types::UNorm16: bytes_per_channel = 2; break;
        case Types::UInt8:
        case Types::SInt8:
        case Types::SNorm8:
        case Types::UNorm8: bytes_per_channel = 1; break;
        default: bytes_per_channel = 0; break;
        }
    }
}

size_t get_bc_block_size(DDSFile::DXGIFormat fmt)
{
    using DXGI = DDSFile::DXGIFormat;
    switch (fmt)
    {
    case DXGI::BC1_UNorm:
    case DXGI::BC1_UNorm_SRGB:
    case DXGI::BC4_UNorm:
    case DXGI::BC4_SNorm: return 8;
    case DXGI::BC2_UNorm:
    case DXGI::BC2_UNorm_SRGB:
    case DXGI::BC3_UNorm:
    case DXGI::BC3_UNorm_SRGB:
    case DXGI::BC5_UNorm:
    case DXGI::BC5_SNorm:
    case DXGI::BC6H_UF16:
    case DXGI::BC6H_SF16:
    case DXGI::BC7_UNorm:
    case DXGI::BC7_UNorm_SRGB: return 16;
    default: return 0;
    }
}

ImagePtr load_uncompressed(const DDSFile::ImageData *data, int num_channels, Types type, bool is_srgb)
{
    auto image = make_shared<Image>(int2(data->m_width, data->m_height), num_channels);
    for (int c = 0; c < num_channels; ++c)
    {
        Channel &channel = image->channels[c];
        if (type == Types::Float32)
            channel.copy_from_interleaved((const float *)data->m_mem, data->m_width, data->m_height, num_channels, c,
                                          [](float v) { return v; });
        else if (type == Types::Float16)
            channel.copy_from_interleaved((const half *)data->m_mem, data->m_width, data->m_height, num_channels, c,
                                          [](half v) { return v; });
        else if (type == Types::SInt32)
            channel.copy_from_interleaved((const int32_t *)data->m_mem, data->m_width, data->m_height, num_channels, c,
                                          [](int32_t v) { return dequantize_full(v); });
        else if (type == Types::SInt16)
            channel.copy_from_interleaved((const int16_t *)data->m_mem, data->m_width, data->m_height, num_channels, c,
                                          [](int16_t v) { return dequantize_full(v); });
        else if (type == Types::SInt8)
            channel.copy_from_interleaved((const int8_t *)data->m_mem, data->m_width, data->m_height, num_channels, c,
                                          [](int8_t v) { return dequantize_full(v); });
        else if (type == Types::UInt32)
            channel.copy_from_interleaved((const uint32_t *)data->m_mem, data->m_width, data->m_height, num_channels, c,
                                          [](uint32_t v) { return dequantize_full(v); });
        else if (type == Types::UInt16)
            channel.copy_from_interleaved((const uint16_t *)data->m_mem, data->m_width, data->m_height, num_channels, c,
                                          [](uint16_t v) { return dequantize_full(v); });
        else if (type == Types::UInt8)
            channel.copy_from_interleaved((const uint8_t *)data->m_mem, data->m_width, data->m_height, num_channels, c,
                                          [](uint8_t v) { return dequantize_full(v); });
        else if (type == Types::SNorm16)
            channel.copy_from_interleaved((const int16_t *)data->m_mem, data->m_width, data->m_height, num_channels, c,
                                          [](int16_t v) { return dequantize_full(v); });
        else if (type == Types::SNorm8)
            channel.copy_from_interleaved((const int8_t *)data->m_mem, data->m_width, data->m_height, num_channels, c,
                                          [](int8_t v) { return dequantize_full(v); });
        else if (type == Types::UNorm16)
            channel.copy_from_interleaved((const uint16_t *)data->m_mem, data->m_width, data->m_height, num_channels, c,
                                          [](uint16_t v) { return dequantize_full(v); });
        else if (type == Types::UNorm8)
            channel.copy_from_interleaved((const uint8_t *)data->m_mem, data->m_width, data->m_height, num_channels, c,
                                          [](uint8_t v) { return dequantize_full(v); });
    }
    // sRGB to linear for uncompressed sRGB formats
    if (is_srgb)
    {
        spdlog::info("Converting sRGB to linear for uncompressed image.");
        for (int c = 0; c < std::min(3, num_channels); ++c)
        {
            Channel &channel = image->channels[c];
            channel.apply([](float v, int, int) { return sRGB_to_linear(v); });
        }
    }
    return image;
}

ImagePtr load_bc_compressed(const DDSFile::ImageData *data, int num_channels, DDSFile::DXGIFormat fmt, bool is_normal,
                            bool is_srgb)
{
    int  width  = data->m_width;
    int  height = data->m_height;
    auto image  = make_shared<Image>(int2{width, height}, num_channels);

    constexpr int block_width      = 4;
    const int     width_in_blocks  = (width + block_width - 1) / block_width;
    const int     height_in_blocks = (height + block_width - 1) / block_width;
    const size_t  block_size       = get_bc_block_size(fmt);

    bool is_float       = (fmt == DDSFile::DXGIFormat::BC6H_UF16 || fmt == DDSFile::DXGIFormat::BC6H_SF16);
    int  block_channels = is_float ? 3 : 4;

    // RXGB swizzle detection: DXT5 + fourCC == 'RXGB'
    bool is_rxgb = false;
    // if (fmt == DDSFile::DXGIFormat::BC3_UNorm && data->m_fourcc == 'RXGB')
    //     is_rxgb = true;

    parallel_for(blocked_range<int>(0, height_in_blocks, 1024 * 1024 / block_width / block_width),
                 [&](int start_y, int end_y, int, int)
                 {
                     for (int by = start_y; by < end_y; ++by)
                     {
                         for (int bx = 0; bx < width_in_blocks; ++bx)
                         {
                             const uint8_t *block =
                                 static_cast<const uint8_t *>(data->m_mem) + (by * width_in_blocks + bx) * block_size;

                             // Use separate buffers for float and uint8_t types
                             float   float_out[4 * 4 * 3] = {0};
                             uint8_t uint8_out[4 * 4 * 4] = {0};
                             int     stride               = block_width * block_channels;

                             using DXGI = DDSFile::DXGIFormat;
                             switch (fmt)
                             {
                             case DXGI::BC1_UNorm:
                             case DXGI::BC1_UNorm_SRGB: bcdec_bc1(block, uint8_out, stride); break;
                             case DXGI::BC2_UNorm:
                             case DXGI::BC2_UNorm_SRGB: bcdec_bc2(block, uint8_out, stride); break;
                             case DXGI::BC3_UNorm:
                             case DXGI::BC3_UNorm_SRGB: bcdec_bc3(block, uint8_out, stride); break;
                             case DXGI::BC4_UNorm: bcdec_bc4(block, uint8_out, stride); break;
                             case DXGI::BC5_UNorm: bcdec_bc5(block, uint8_out, stride); break;
                             case DXGI::BC6H_UF16: bcdec_bc6h_float(block, float_out, stride, false); break;
                             case DXGI::BC6H_SF16: bcdec_bc6h_float(block, float_out, stride, true); break;
                             case DXGI::BC7_UNorm: bcdec_bc7(block, uint8_out, stride); break;
                             case DXGI::BC7_UNorm_SRGB: bcdec_bc7(block, uint8_out, stride); break;
                             default: throw std::invalid_argument("Unsupported BC format for decompression.");
                             }

                             // RXGB swizzle: swap R and A channels for each pixel in the block
                             if (!is_float && is_rxgb)
                             {
                                 for (int i = 0; i < block_width * block_width; ++i)
                                     std::swap(uint8_out[i * 4 + 0], uint8_out[i * 4 + 3]);
                             }

                             // If normal map, convert to RGB normal map in-place
                             if (!is_float && is_normal)
                             {
                                 if (fmt == DXGI::BC5_UNorm || fmt == DXGI::BC5_SNorm)
                                     compute_normal_rg(uint8_out, block_width * block_width);
                                 else if (fmt == DXGI::BC3_UNorm || fmt == DXGI::BC3_UNorm_SRGB)
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
                                     int src_idx = (py * block_width + px) * block_channels;
                                     int dst_idx = y * width + x;
                                     if (is_float)
                                     {
                                         for (int c = 0; c < num_channels; ++c)
                                             image->channels[c](dst_idx) = float_out[src_idx + c];
                                     }
                                     else
                                     {
                                         for (int c = 0; c < num_channels; ++c)
                                         {
                                             float f                     = dequantize_full(uint8_out[src_idx + c]);
                                             image->channels[c](dst_idx) = (is_srgb && c < 3) ? sRGB_to_linear(f) : f;
                                         }
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
        char magic[4];
        is.read(reinterpret_cast<char *>(magic), 4);
        is.clear();
        is.seekg(0);
        return std::memcmp(magic, DDSFile::Magic, 4) == 0;
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
    Result  ret = dds.Load(is);
    if (ret != tinyddsloader::Result::Success)
        throw std::runtime_error("Failed to load DDS.");

    // Detect normal map (for now, just BC5 or BC3 with user selector "normal")
    auto fmt       = dds.GetFormat();
    bool is_normal = false;
    // fmt == DDSFile::DXGIFormat::BC5_UNorm || fmt == DDSFile::DXGIFormat::BC5_SNorm ||
    //     fmt == DDSFile::DXGIFormat::BC3_UNorm || fmt == DDSFile::DXGIFormat::BC3_UNorm_SRGB;

    spdlog::info("height = {}.", dds.GetHeight());
    spdlog::info("height = {}.", dds.GetHeight());
    spdlog::info("depth = {}.", dds.GetDepth());
    spdlog::info("mipCount = {}.", dds.GetMipCount());
    spdlog::info("arraySize = {}.", dds.GetArraySize());
    spdlog::info("bits per pixel = {}.", dds.GetBitsPerPixel(fmt));
    spdlog::info("format = {}.", (uint32_t)fmt);
    spdlog::info("isCubeMap = {}.", (uint32_t)dds.IsCubemap());
    spdlog::info("isCompressed = {}.", (uint32_t)dds.IsCompressed(fmt));

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
    case Types::Float32: bit_depth_str = fmt::format("{}-bit float (32 bpc)", num_channels * 32); break;
    case Types::Float16: bit_depth_str = fmt::format("{}-bit half (16 bpc)", num_channels * 16); break;
    case Types::SInt8: bit_depth_str = fmt::format("{}-bit int8 (8 bpc)", num_channels * 8); break;
    case Types::SInt16: bit_depth_str = fmt::format("{}-bit int16 (16 bpc)", num_channels * 16); break;
    case Types::SInt32: bit_depth_str = fmt::format("{}-bit int32 (32 bpc)", num_channels * 32); break;
    case Types::UInt8: bit_depth_str = fmt::format("{}-bit uint8 (8 bpc)", num_channels * 8); break;
    case Types::UInt16: bit_depth_str = fmt::format("{}-bit uint16 (16 bpc)", num_channels * 16); break;
    case Types::UInt32: bit_depth_str = fmt::format("{}-bit uint32 (32 bpc)", num_channels * 32); break;
    case Types::SNorm8: bit_depth_str = fmt::format("{}-bit snorm8 (8 bpc)", num_channels * 8); break;
    case Types::SNorm16: bit_depth_str = fmt::format("{}-bit snorm16 (16 bpc)", num_channels * 16); break;
    case Types::UNorm8: bit_depth_str = fmt::format("{}-bit unorm8 (8 bpc)", num_channels * 8); break;
    case Types::UNorm16: bit_depth_str = fmt::format("{}-bit unorm16 (16 bpc)", num_channels * 16); break;
    default: bit_depth_str = "unknown"; break;
    }

    vector<ImagePtr> images;

    // Detect if this is an sRGB format
    bool is_srgb = (fmt == DDSFile::DXGIFormat::BC1_UNorm_SRGB || fmt == DDSFile::DXGIFormat::BC2_UNorm_SRGB ||
                    fmt == DDSFile::DXGIFormat::BC3_UNorm_SRGB || fmt == DDSFile::DXGIFormat::BC7_UNorm_SRGB ||
                    fmt == DDSFile::DXGIFormat::R8G8B8A8_UNorm_SRGB ||
                    fmt == DDSFile::DXGIFormat::B8G8R8A8_UNorm_SRGB || fmt == DDSFile::DXGIFormat::B8G8R8X8_UNorm_SRGB);

    static const char *cubemap_face_names[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};

    json header;
    header["isCubemap"] = {{"value", dds.IsCubemap()}, {"string", dds.IsCubemap() ? "yes" : "no"}, {"type", "boolean"}};
    header["isCompressed"] = {{"value", DDSFile::IsCompressed(fmt)},
                              {"string", DDSFile::IsCompressed(fmt) ? "yes" : "no"},
                              {"type", "boolean"}};
    header["format"]       = dxgi_format_to_json(fmt);

    for (uint32_t p = 0; p < dds.GetArraySize(); ++p)
    {
        const DDSFile::ImageData *data = dds.GetImageData(0, p);
        if (!data)
            throw std::runtime_error("DDS: No image data found for the specified array index.");

        ImagePtr image;
        if (DDSFile::IsCompressed(fmt))
            image = load_bc_compressed(data, num_channels, fmt, is_normal, is_srgb);
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
