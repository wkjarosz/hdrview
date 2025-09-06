#include "hello_imgui/hello_imgui_assets.h" // for AssetFileData, AssetExists
#include "stb_image.h"
#include <memory>
#include <random>
#include <spdlog/spdlog.h>
#include <string>

using namespace std;
using namespace HelloImGui;

static void deleter(uint8_t *p) { stbi_image_free(p); }
using DataBuffer = std::unique_ptr<uint8_t, void (*)(uint8_t *)>;
static DataBuffer g_dither_texture{nullptr, deleter};
static int        g_dither_texture_width = 1;

void create_dither_texture()
{
    try
    {
        auto filename = "dither-texture-256.png";
        int  width = 0, height = 0, channels_in_file = 0;

        if (!AssetExists(filename))
            throw std::runtime_error("Dither texture file does not exist");

        auto asset = LoadAssetFileData(filename);
        if (asset.data == nullptr)
            throw std::runtime_error("Cannot load dither texture from file");

        auto source = string((char *)asset.data, asset.dataSize);
        FreeAssetFileData(&asset);

        g_dither_texture = DataBuffer{
            stbi_load_from_memory((const stbi_uc *)source.data(), source.size(), &width, &height, &channels_in_file, 1),
            deleter};
        if (!g_dither_texture || width <= 0)
            throw std::runtime_error("Failed to load dither texture from memory");
        if (width != height)
            throw std::runtime_error("Unexpected non-square dither texture");
        g_dither_texture_width = width;
    }
    catch (const std::exception &e)
    {
        spdlog::error("Loading dither texture failed: {}. Falling back to a Bayer matrix.", e.what());
        // fallback to bayer
        g_dither_texture.reset();
        g_dither_texture_width = 16;
        g_dither_texture =
            DataBuffer{(uint8_t *)malloc(sizeof(uint8_t) * g_dither_texture_width * g_dither_texture_width), deleter};
        static const int bayer[16][16] = {{0, 128, 32, 160, 8, 136, 40, 168, 2, 130, 34, 162, 10, 138, 42, 170},
                                          {192, 64, 224, 96, 200, 72, 232, 104, 194, 66, 226, 98, 202, 74, 234, 106},
                                          {48, 176, 16, 144, 56, 184, 24, 152, 50, 178, 18, 146, 58, 186, 26, 154},
                                          {240, 112, 208, 80, 248, 120, 216, 88, 242, 114, 210, 82, 250, 122, 218, 90},
                                          {12, 140, 44, 172, 4, 132, 36, 164, 14, 142, 46, 174, 6, 134, 38, 166},
                                          {204, 76, 236, 108, 196, 68, 228, 100, 206, 78, 238, 110, 198, 70, 230, 102},
                                          {60, 188, 28, 156, 52, 180, 20, 148, 62, 190, 30, 158, 54, 182, 22, 150},
                                          {252, 124, 220, 92, 244, 116, 212, 84, 254, 126, 222, 94, 246, 118, 214, 86},
                                          {3, 131, 35, 163, 11, 139, 43, 171, 1, 129, 33, 161, 9, 137, 41, 169},
                                          {195, 67, 227, 99, 203, 75, 235, 107, 193, 65, 225, 97, 201, 73, 233, 105},
                                          {51, 179, 19, 147, 59, 187, 27, 155, 49, 177, 17, 145, 57, 185, 25, 153},
                                          {243, 115, 211, 83, 251, 123, 219, 91, 241, 113, 209, 81, 249, 121, 217, 89},
                                          {15, 143, 47, 175, 7, 135, 39, 167, 13, 141, 45, 173, 5, 133, 37, 165},
                                          {207, 79, 239, 111, 199, 71, 231, 103, 205, 77, 237, 109, 197, 69, 229, 101},
                                          {63, 191, 31, 159, 55, 183, 23, 151, 61, 189, 29, 157, 53, 181, 21, 149},
                                          {255, 127, 223, 95, 247, 119, 215, 87, 253, 125, 221, 93, 245, 117, 213, 85}};
        for (int y = 0; y < g_dither_texture_width; ++y)
            for (int x = 0; x < g_dither_texture_width; ++x)
                g_dither_texture.get()[y * g_dither_texture_width + x] = bayer[y][x];
        // static std::mt19937 rng(53);
        // g_dither_texture_width = 256;
        // for (int y = 0; y < g_dither_texture_width; ++y)
        //     for (int x = 0; x < g_dither_texture_width; ++x)
        //         g_dither_texture.get()[y * g_dither_texture_width + x] = std::uniform_int_distribution<uint8_t>(0,
        //         255)(rng);
    }
}

int dither_texture_width() { return g_dither_texture_width; }

const uint8_t *dither_texture_data()
{
    if (g_dither_texture == nullptr)
        create_dither_texture();
    return g_dither_texture.get();
}

float box_dither(int x, int y)
{
    x %= dither_texture_width();
    y %= dither_texture_width();
    return (dither_texture_data()[x + y * dither_texture_width()]) / 255.f - 0.5f;
}

float tent_dither(int x, int y)
{
    float r = 2.f * box_dither(x, y);
    return 0.5f * std::copysign(1.0f - std::sqrt(1.f - std::abs(r)), r);
}