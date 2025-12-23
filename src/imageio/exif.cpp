#include "exif.h"
#include <cstdio>
#include <cstring> // for memcmp
#include <libexif/exif-data.h>
#include <libexif/exif-tag.h>
#include <stdexcept>

using std::string;
using std::vector;

static const vector<uint8_t> FOURCC = {'E', 'x', 'i', 'f', 0, 0};

// Forward declarations for helper functions:
static json exif_data_to_json(ExifData *ed);

json exif_to_json(const uint8_t *data_ptr, size_t data_size)
{
    // 1) Prepare data buffer and prepend FOURCC if missing
    vector<uint8_t> data;
    if (data_size < FOURCC.size() || memcmp(data_ptr, FOURCC.data(), FOURCC.size()) != 0)
    {
        data.reserve(data_size + FOURCC.size());
        data.insert(data.end(), FOURCC.begin(), FOURCC.end());
        data.insert(data.end(), data_ptr, data_ptr + data_size);
    }
    else
        data.assign(data_ptr, data_ptr + data_size);

    // 2) Create ExifData and ExifLog with custom log function
    bool error = false;

    std::unique_ptr<ExifData, decltype(&exif_data_unref)> exif_data(exif_data_new(), &exif_data_unref);
    if (!exif_data)
        throw std::invalid_argument("Failed to allocate ExifData.");

    std::unique_ptr<ExifLog, decltype(&exif_log_unref)> exif_log(exif_log_new(), &exif_log_unref);
    if (!exif_log)
        throw std::invalid_argument("Failed to allocate ExifLog.");

    exif_log_set_func(
        exif_log.get(),
        [](ExifLog *log, ExifLogCode kind, const char *domain, const char *format, va_list args, void *user_data)
        {
            bool *error = static_cast<bool *>(user_data);
            char  buf[1024];
            vsnprintf(buf, sizeof(buf), format, args);

            switch (kind)
            {
            case EXIF_LOG_CODE_NONE: spdlog::info("{}: {}", domain, buf); break;
            case EXIF_LOG_CODE_DEBUG: spdlog::debug("{}: {}", domain, buf); break;
            case EXIF_LOG_CODE_NO_MEMORY:
            case EXIF_LOG_CODE_CORRUPT_DATA:
                *error = true;
                spdlog::error("log: {}: {}", domain, buf);
                break;
            }
        },
        &error);

    exif_data_log(exif_data.get(), exif_log.get());

    // 3) Load the EXIF data from memory buffer
    exif_data_load_data(exif_data.get(), data.data(), data.size());

    if (!exif_data || error)
        throw std::invalid_argument{"Failed to decode EXIF data."};

    // 4) Convert to JSON
    return exif_data_to_json(exif_data.get());
}

json entry_to_json(ExifEntry *entry, ExifByteOrder bo)
{
    json value;

    char   buf[256] = {0};
    string str      = exif_entry_get_value(entry, buf, sizeof(buf));
    if (str.empty())
        str = "n/a";
    else if (str.length() >= 255)
        str += "…";

    value["string"] = str;

    switch (entry->format)
    {
    case EXIF_FORMAT_ASCII:
    {
        // EXIF ASCII strings are null-terminated, so we need to exclude the null terminator
        size_t len = entry->components;
        if (len > 0 && entry->data[len - 1] == '\0')
            len--;
        value["value"] = string(reinterpret_cast<char *>(entry->data), len);
        value["type"]  = "ascii";
        break;
    }

    case EXIF_FORMAT_BYTE:
    case EXIF_FORMAT_UNDEFINED:
    {
        vector<uint8_t> bytes(entry->data, entry->data + entry->components);
        value["value"] = bytes;
        value["type"]  = "byte";
        break;
    }

    case EXIF_FORMAT_SHORT:
    {
        vector<uint16_t> shorts;
        const uint16_t  *data = reinterpret_cast<const uint16_t *>(entry->data);
        for (unsigned int i = 0; i < entry->components; i++)
            shorts.push_back(exif_get_short(reinterpret_cast<const unsigned char *>(&data[i]), bo));

        value["type"] = "short";
        if (shorts.size() == 1)
            value["value"] = shorts[0];
        else
            value["value"] = shorts;
        break;
    }

    case EXIF_FORMAT_LONG:
    {
        vector<uint32_t> longs;
        const uint32_t  *data = reinterpret_cast<const uint32_t *>(entry->data);
        for (unsigned int i = 0; i < entry->components; i++)
            longs.push_back(exif_get_long(reinterpret_cast<const unsigned char *>(&data[i]), bo));

        value["type"] = "long";
        if (longs.size() == 1)
            value["value"] = longs[0];
        else
            value["value"] = longs;
        break;
    }

    case EXIF_FORMAT_SBYTE:
    {
        vector<int8_t> sbytes(reinterpret_cast<int8_t *>(entry->data),
                              reinterpret_cast<int8_t *>(entry->data) + entry->components);
        value["type"] = "sbyte";
        if (sbytes.size() == 1)
            value["value"] = sbytes[0];
        else
            value["value"] = sbytes;
        break;
    }

    case EXIF_FORMAT_SSHORT:
    {
        vector<int16_t> sshorts;
        const int16_t  *data = reinterpret_cast<const int16_t *>(entry->data);
        for (unsigned int i = 0; i < entry->components; i++)
            sshorts.push_back(exif_get_sshort(reinterpret_cast<const unsigned char *>(&data[i]), bo));

        value["type"] = "sshort";
        if (sshorts.size() == 1)
            value["value"] = sshorts[0];
        else
            value["value"] = sshorts;
        break;
    }

    case EXIF_FORMAT_SLONG:
    {
        vector<int32_t> slongs;
        const int32_t  *data = reinterpret_cast<const int32_t *>(entry->data);
        for (unsigned int i = 0; i < entry->components; i++)
            slongs.push_back(exif_get_slong(reinterpret_cast<const unsigned char *>(&data[i]), bo));

        value["type"] = "slong";
        if (slongs.size() == 1)
            value["value"] = slongs[0];
        else
            value["value"] = slongs;
        break;
    }

    case EXIF_FORMAT_RATIONAL:
    {
        vector<json> rationals;
        for (unsigned int i = 0; i < entry->components; i++)
        {
            auto r = exif_get_rational(reinterpret_cast<const unsigned char *>(&entry->data[8 * i]), bo);
            json jr;
            jr["numerator"]   = r.numerator;
            jr["denominator"] = r.denominator;
            jr["value"]       = double(r.numerator) / r.denominator;
            rationals.push_back(jr);
        }

        value["type"] = "rational";
        if (rationals.size() == 1)
            value["value"] = rationals[0];
        else
            value["value"] = rationals;
        break;
    }

    case EXIF_FORMAT_SRATIONAL:
    {
        vector<json> srationals;
        for (unsigned int i = 0; i < entry->components; i++)
        {
            auto r = exif_get_srational(reinterpret_cast<const unsigned char *>(&entry->data[8 * i]), bo);
            json jr;
            jr["numerator"]   = r.numerator;
            jr["denominator"] = r.denominator;
            jr["value"]       = double(r.numerator) / r.denominator;
            srationals.push_back(jr);
        }

        value["type"] = "srational";
        if (srationals.size() == 1)
            value["value"] = srationals[0];
        else
            value["value"] = srationals;
        break;
    }

    case EXIF_FORMAT_FLOAT:
    {
        vector<float> vals;
        for (unsigned int i = 0; i < entry->components; i++)
        {
            // For float, we need to handle endianness manually since libexif doesn't provide a utility
            uint32_t raw_bits = exif_get_long(reinterpret_cast<const unsigned char *>(&entry->data[4 * i]), bo);
            float    val;
            memcpy(&val, &raw_bits, sizeof(float));
            vals.push_back(val);
        }
        value["type"] = "float";
        if (vals.size() == 1)
            value["value"] = vals[0];
        else
            value["value"] = vals;
        break;
    }

    case EXIF_FORMAT_DOUBLE:
    {
        vector<double> vals;
        for (unsigned int i = 0; i < entry->components; i++)
        {
            // For double, we need to handle endianness manually since libexif doesn't provide a utility
            // Read as two 32-bit words and reconstruct the double
            uint32_t low  = exif_get_long(reinterpret_cast<const unsigned char *>(&entry->data[8 * i]), bo);
            uint32_t high = exif_get_long(reinterpret_cast<const unsigned char *>(&entry->data[8 * i + 4]), bo);

            uint64_t raw_bits = (bo == EXIF_BYTE_ORDER_INTEL) ? (uint64_t)high << 32 | low : (uint64_t)low << 32 | high;
            double   val;
            memcpy(&val, &raw_bits, sizeof(double));
            vals.push_back(val);
        }
        value["type"] = "double";
        if (vals.size() == 1)
            value["value"] = vals[0];
        else
            value["value"] = vals;
        break;
    }

    default: value = nullptr; // unknown or unsupported format
    }

    return value;
}

json exif_data_to_json(ExifData *ed)
{
    json j;

    static const char *ExifIfdTable[] = {"TIFF", // Apple Preview
                                         "TIFF", // seems to combine the 0th and 1st IFDs into a "TIFF" section
                                         "EXIF", "GPS", "Interoperability"};

    for (int ifd_idx = 0; ifd_idx < EXIF_IFD_COUNT; ++ifd_idx)
    {
        ExifContent *content = ed->ifd[ifd_idx];
        if (!content || !content->count)
            continue;

        json ifd_json;

        for (unsigned int i = 0; i < content->count; ++i)
        {
            ExifEntry *entry = content->entries[i];
            if (!entry)
                continue;

            string tag_name = exif_tag_get_title_in_ifd(entry->tag, static_cast<ExifIfd>(ifd_idx));
            if (tag_name.empty())
                tag_name = "UnknownTag_" + std::to_string(entry->tag);

            // if (entry->tag == EXIF_TAG_XML_PACKET)
            // {
            //     // Special handling for XMP data
            //     string xmp_data(reinterpret_cast<char *>(entry->data), entry->size);
            //     ifd_json["XMP"] = {{"value", xmp_data},
            //                        {"string", xmp_data},
            //                        {"type", "string"},
            //                        {"description", "XMP metadata packet"}};
            //     continue;
            // }

            ifd_json[tag_name] = entry_to_json(entry, exif_data_get_byte_order(ed));

            string desc = exif_tag_get_description_in_ifd(entry->tag, static_cast<ExifIfd>(ifd_idx));
            if (!desc.empty())
                ifd_json[tag_name]["description"] = desc;

            // special handling of compression tag since libexif doesn't know about many compression formats
            if (entry->tag == EXIF_TAG_COMPRESSION)
            {
                string compression_name;
                switch (ifd_json[tag_name]["value"].get<int>())
                {
                // these codes and names are copied from libtiff typedefs and comments
                case 1: compression_name = "Uncompressed"; break;
                case 2: compression_name = "CCITT modified Huffman RLE"; break;
                case 3: compression_name = "CCITT Group 3 fax encoding"; break;
                case 4: compression_name = "CCITT Group 4 fax encoding"; break;
                case 5: compression_name = "Lempel-Ziv & Welch (LZW)"; break;
                case 6: compression_name = "JPEG"; break;
                case 7: compression_name = "JPEG"; break;
                case 8: compression_name = "Deflate/ZIP compression, as recognized by Adobe"; break;
                case 9: compression_name = "T.85 JBIG compression"; break;
                case 10: compression_name = "T.43 color by layered JBIG compression"; break;
                case 32766: compression_name = "NeXT 2-bit RLE"; break;
                case 32771: compression_name = "Uncompressed w/ word alignment"; break;
                case 32773: compression_name = "Macintosh RLE"; break;
                case 32809: compression_name = "ThunderScan RLE"; break;
                /* codes 32895-32898 are reserved for ANSI IT8 TIFF/IT <dkelly@apago.com) */
                case 32895: compression_name = "IT8 CT w/padding"; break;
                case 32896: compression_name = "IT8 Linework RLE"; break;
                case 32897: compression_name = "IT8 Monochrome picture"; break;
                case 32898: compression_name = "IT8 Binary line art"; break;
                /* compression codes 32908-32911 are reserved for Pixar */
                case 32908: compression_name = "Pixar Film (10bit LZW)"; break;
                case 32909: compression_name = "Pixar Log (11bit ZIP)"; break;
                case 32910: [[fallthrough]];
                case 32911: compression_name = "Unknown Pixar compression"; break;
                case 32946: compression_name = "Deflate/ZIP compression, legacy tag"; break;
                case 32947: compression_name = "Kodak DCS encoding"; break;
                case 34661: compression_name = "ISO JBIG"; break;
                case 34676: compression_name = "SGI Log Luminance RLE"; break;
                case 34677: compression_name = "SGI Log 24-bit packed"; break;
                case 34712: compression_name = "Leadtools JPEG2000"; break;
                case 34887: [[fallthrough]];
                case 34888: [[fallthrough]];
                case 34889: compression_name = "ESRI Lerc codec: https://github.com/Esri/lerc"; break;
                case 34925: compression_name = "LZMA2"; break;
                case 50000: compression_name = "ZSTD"; break;
                case 50001: compression_name = "WEBP"; break;
                case 50002: compression_name = "JPEGXL"; break;
                case 52546: compression_name = "JPEGXL from DNG 1.7 specification"; break;
                default: compression_name = "Other";
                }
                ifd_json[tag_name]["string"] = compression_name;
            }
        }

        if (j.contains(ExifIfdTable[ifd_idx]))
            j[ExifIfdTable[ifd_idx]].update(ifd_json);
        else
            j[ExifIfdTable[ifd_idx]] = ifd_json;
    }

    return j;
}