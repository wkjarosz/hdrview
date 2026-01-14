#include "psd.h"
#include <cstring>

static uint16_t read_uint16_be(std::istream &stream)
{
    uint8_t bytes[2];
    stream.read(reinterpret_cast<char *>(bytes), 2);
    if (!stream)
        throw std::runtime_error("Unexpected end of file");
    return (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1];
}

static uint32_t read_uint32_be(std::istream &stream)
{
    uint8_t bytes[4];
    stream.read(reinterpret_cast<char *>(bytes), 4);
    if (!stream)
        throw std::runtime_error("Unexpected end of file");
    return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
}

static void skip_bytes(std::istream &stream, std::streamsize count)
{
    stream.seekg(count, std::ios::cur);
    if (!stream)
        throw std::runtime_error("Failed to skip bytes");
}

static std::vector<uint8_t> read_bytes(std::istream &stream, size_t count)
{
    std::vector<uint8_t> data(count);
    stream.read(reinterpret_cast<char *>(data.data()), count);
    if (!stream)
        throw std::runtime_error("Failed to read bytes");
    return data;
}

const char *PSDMetadata::color_mode_names[10] = {
    "Bitmap", "Grayscale", "Indexed", "RGB", "CMYK", "Invalid5", "Invalid6", "Multichannel", "Duotone", "Lab",
};

// See https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/
void PSDMetadata::read(std::istream &stream)
{
    // Read and verify PSD signature
    char signature[4];
    stream.read(signature, 4);
    if (!stream || memcmp(signature, "8BPS", 4) != 0)
        throw std::runtime_error("Not a valid PSD file");

    // Read version (1 for PSD, 2 for PSB)
    uint16_t version = read_uint16_be(stream);
    if (version != 1 && version != 2)
        throw std::runtime_error("Unsupported PSD version");

    // Skip reserved bytes (6 bytes) - must be zero
    skip_bytes(stream, 6);

    // Channels (2), height (4), width (4), depth (2), color mode (2) = 14 bytes
    num_channels = read_uint16_be(stream);
    height       = read_uint32_be(stream);
    width        = read_uint32_be(stream);
    depth        = read_uint16_be(stream);
    color_mode   = static_cast<ColorMode>(read_uint16_be(stream));

    // Read and skip Color Mode Data section
    uint32_t color_mode_data_length = read_uint32_be(stream);
    skip_bytes(stream, color_mode_data_length);

    // Read Image Resources section length
    uint32_t image_resources_length = read_uint32_be(stream);

    if (image_resources_length == 0)
        return; // No metadata

    // Parse Image Resources section
    std::streampos section_start = stream.tellg();
    std::streampos section_end   = section_start + static_cast<std::streamoff>(image_resources_length);

    while (stream.tellg() < section_end && stream.good())
    {
        // Verify we have at least enough bytes for a minimal resource block
        std::streampos current_pos = stream.tellg();
        if (section_end - current_pos < 12) // Minimum: 4+2+2+4 = 12 bytes
            break;

        // Read resource block signature (must be '8BIM')
        char res_signature[4];
        stream.read(res_signature, 4);
        if (!stream || memcmp(res_signature, "8BIM", 4) != 0)
            break; // Invalid signature

        // Read resource ID
        uint16_t resource_id = read_uint16_be(stream);

        // Read Pascal string name
        // Format: [1 byte length][N bytes name][padding to make total even]
        uint8_t name_length;
        stream.read(reinterpret_cast<char *>(&name_length), 1);
        if (!stream)
            break;

        // Skip name bytes
        if (name_length > 0)
            skip_bytes(stream, name_length);

        // Add padding if needed to make (1 + name_length) even
        if ((1 + name_length) % 2 == 1)
            skip_bytes(stream, 1);

        // Read resource data size
        uint32_t data_size = read_uint32_be(stream);

        // Sanity check
        if (data_size > 100 * 1024 * 1024) // 100MB limit
            throw std::runtime_error("Resource data size too large");

        // Extract metadata based on resource ID
        if (resource_id == 1028 && iptc.empty()) // IPTC-NAA
            iptc = read_bytes(stream, data_size);
        else if (resource_id == 1034) // Copyright flag
        {
            if (data_size > 0)
            {
                uint8_t flag;
                stream.read(reinterpret_cast<char *>(&flag), 1);
                is_copyright = (flag != 0);
                skip_bytes(stream, data_size - 1);
            }
        }
        else if (resource_id == 1035) // URL
        {
            if (data_size > 0)
            {
                auto url_data = read_bytes(stream, data_size);
                url           = std::string(url_data.begin(), url_data.end());
            }
        }
        else if (resource_id == 1039 && icc_profile.empty()) // ICC Profile
            icc_profile = read_bytes(stream, data_size);
        else if (resource_id == 1041) // ICC Untagged
        {
            if (data_size >= 1)
            {
                uint8_t flag;
                stream.read(reinterpret_cast<char *>(&flag), 1);
                is_icc_untagged = (flag != 0);
                skip_bytes(stream, data_size - 1);
            }
        }
        else if (resource_id == 1036 && thumbnail.empty()) // Thumbnail
            thumbnail = read_bytes(stream, data_size);
        else if (resource_id == 1058 && exif.empty()) // EXIF data 1
            exif = read_bytes(stream, data_size);
        else if (resource_id == 1059 && exif3.empty()) // EXIF data 3
            exif3 = read_bytes(stream, data_size);
        else if (resource_id == 1060 && xmp.empty()) // XMP (0x0424 in hex)
            xmp = read_bytes(stream, data_size);
        else
            skip_bytes(stream, data_size);

        // Resource data is padded to even length
        if (data_size % 2 == 1)
            skip_bytes(stream, 1);
    }
}