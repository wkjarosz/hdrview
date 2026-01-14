#include <cstdint>
#include <iostream>
#include <vector>

struct PSDMetadata
{
    uint16_t num_channels = uint16_t(-1); // Supported range is 1 to 56.
    uint32_t width        = uint32_t(-1); // Supported range is 1 to 30,000. (PSB max of 300,000.)
    uint32_t height       = uint32_t(-1); // Supported range is 1 to 30,000. (PSB max of 300,000.)
    uint16_t depth        = uint16_t(-1); // Supported values are 1, 8, 16 and 32.
    enum ColorMode : uint16_t
    {
        Bitmap       = 0,
        Grayscale    = 1,
        Indexed      = 2,
        RGB          = 3,
        CMYK         = 4,
        Multichannel = 7,
        Duotone      = 8,
        Lab          = 9,
        NotSet       = uint16_t(-1),
    };
    // Image properties
    ColorMode color_mode = NotSet;

    static const char *color_mode_names[10];

    // Metadata blocks
    std::vector<uint8_t> exif, exif3, xmp, iptc, icc_profile, thumbnail;

    // Flags
    uint8_t is_copyright    = uint8_t(-1); // arbitrary value meaning "not set"
    uint8_t is_icc_untagged = uint8_t(-1); // arbitrary value meaning "not set"

    std::string url;

    void read(std::istream &stream);
};