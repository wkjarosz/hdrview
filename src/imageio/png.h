#pragma once

#include "fwd.h"
#include <istream>
#include <memory>
#include <string>

// should not throw
bool is_png_image(std::istream &is) noexcept;
// throws on error
std::vector<ImagePtr> load_png_image(std::istream &is, const std::string &filename);
// throws on error
void save_png_image(const Image &img, std::ostream &os, const std::string &filename, float gain = 1.f, bool sRGB = true,
                    bool dither = true);