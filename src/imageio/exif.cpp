#include "exif.h"
#include "common.h"
#include "json.h"
#include <cstdint>
#include <cstdio>
#include <cstring> // for memcmp
#include <libexif/exif-data.h>
#include <libexif/exif-tag.h>
#include <memory>
#include <stdexcept>
#include <type_traits>

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

json entry_to_json(void *entry_v, int boi, unsigned int ifd_idx_i)
{
    if (ifd_idx_i >= EXIF_IFD_COUNT)
        throw std::invalid_argument("Invalid IFD index");
    if (boi > EXIF_BYTE_ORDER_INTEL)
        throw std::invalid_argument("Invalid byte order");

    auto          entry   = static_cast<ExifEntry *>(entry_v);
    auto          ifd_idx = static_cast<ExifIfd>(ifd_idx_i);
    ExifByteOrder bo      = static_cast<ExifByteOrder>(boi);
    auto          tag     = std::underlying_type<ExifTag>::type(entry->tag);

    json   ret;
    Endian data_endian = (bo == EXIF_BYTE_ORDER_INTEL) ? Endian::Little : Endian::Big;

    const char *tag_name_ptr = exif_tag_get_title_in_ifd(entry->tag, ifd_idx);
    string      tag_name;
    if (tag_name_ptr)
        tag_name = tag_name_ptr;
    else
    {
        // Check if this is a known DNG tag or other TIFF/EXIF tag
        switch (tag)
        {
        case 322: tag_name = "Tile Width"; break;
        case 323: tag_name = "Tile Length"; break;
        case 324: tag_name = "Tile Offsets"; break;
        case 325: tag_name = "Tile Byte Counts"; break;
        case 34665: tag_name = "Exif IFD Pointer"; break;
        case 37393: tag_name = "Image Number"; break;
        case 50706: tag_name = "DNG Version"; break;
        case 50707: tag_name = "DNG Backward Version"; break;
        case 50708: tag_name = "Unique Camera Model"; break;
        case 50709: tag_name = "Localized Camera Model"; break;
        case 50710: tag_name = "CFA Plane Color"; break;
        case 50711: tag_name = "CFA Layout"; break;
        case 50712: tag_name = "Linearization Table"; break;
        case 50713: tag_name = "Black Level Repeat Dim"; break;
        case 50714: tag_name = "Black Level"; break;
        case 50715: tag_name = "Black Level Delta H"; break;
        case 50716: tag_name = "Black Level Delta V"; break;
        case 50717: tag_name = "White Level"; break;
        case 50718: tag_name = "Default Scale"; break;
        case 50719: tag_name = "Default Crop Origin"; break;
        case 50720: tag_name = "Default Crop Size"; break;
        case 50721: tag_name = "Color Matrix 1"; break;
        case 50722: tag_name = "Color Matrix 2"; break;
        case 50723: tag_name = "Camera Calibration 1"; break;
        case 50724: tag_name = "Camera Calibration 2"; break;
        case 50725: tag_name = "Reduction Matrix 1"; break;
        case 50726: tag_name = "Reduction Matrix 2"; break;
        case 50727: tag_name = "Analog Balance"; break;
        case 50728: tag_name = "As Shot Neutral"; break;
        case 50729: tag_name = "As Shot White XY"; break;
        case 50730: tag_name = "Baseline Exposure"; break;
        case 50731: tag_name = "Baseline Noise"; break;
        case 50732: tag_name = "Baseline Sharpness"; break;
        case 50733: tag_name = "Bayer Green Split"; break;
        case 50734: tag_name = "Linear Response Limit"; break;
        case 50735: tag_name = "Camera Serial Number"; break;
        case 50736: tag_name = "Lens Info"; break;
        case 50737: tag_name = "Chroma Blur Radius"; break;
        case 50738: tag_name = "Anti Alias Strength"; break;
        case 50739: tag_name = "Shadow Scale"; break;
        case 50740: tag_name = "DNG Private Data"; break;
        case 50741: tag_name = "Maker Note Safety"; break;
        case 50778: tag_name = "Calibration Illuminant 1"; break;
        case 50779: tag_name = "Calibration Illuminant 2"; break;
        case 50780: tag_name = "Best Quality Scale"; break;
        case 50781: tag_name = "Raw Data Unique ID"; break;
        case 50827: tag_name = "Original Raw File Name"; break;
        case 50828: tag_name = "Original Raw File Data"; break;
        case 50829: tag_name = "Active Area"; break;
        case 50830: tag_name = "Masked Areas"; break;
        case 50831: tag_name = "As Shot ICC Profile"; break;
        case 50832: tag_name = "As Shot Pre Profile Matrix"; break;
        case 50833: tag_name = "Current ICC Profile"; break;
        case 50834: tag_name = "Current Pre Profile Matrix"; break;
        case 50879: tag_name = "Colorimetric Reference"; break;
        case 50931: tag_name = "Camera Calibration Signature"; break;
        case 50932: tag_name = "Profile Calibration Signature"; break;
        case 50934: tag_name = "As Shot Profile Name"; break;
        case 50935: tag_name = "Noise Reduction Applied"; break;
        case 50936: tag_name = "Profile Name"; break;
        case 50937: tag_name = "Profile Hue Sat Map Dims"; break;
        case 50938: tag_name = "Profile Hue Sat Map Data 1"; break;
        case 50939: tag_name = "Profile Hue Sat Map Data 2"; break;
        case 50940: tag_name = "Profile Tone Curve"; break;
        case 50941: tag_name = "Profile Embed Policy"; break;
        case 50942: tag_name = "Profile Copyright"; break;
        case 50964: tag_name = "Forward Matrix 1"; break;
        case 50965: tag_name = "Forward Matrix 2"; break;
        case 50966: tag_name = "Preview Application Name"; break;
        case 50967: tag_name = "Preview Application Version"; break;
        case 50968: tag_name = "Preview Settings Name"; break;
        case 50969: tag_name = "Preview Settings Digest"; break;
        case 50970: tag_name = "Preview Color Space"; break;
        case 50971: tag_name = "Preview Date Time"; break;
        case 50972: tag_name = "Raw Image Digest"; break;
        case 50973: tag_name = "Original Raw File Digest"; break;
        case 50974: tag_name = "Sub Tile Block Size"; break;
        case 50975: tag_name = "Row Interleave Factor"; break;
        case 50981: tag_name = "Profile Look Table Dims"; break;
        case 50982: tag_name = "Profile Look Table Data"; break;
        case 51008: tag_name = "Opcode List 1"; break;
        case 51009: tag_name = "Opcode List 2"; break;
        case 51022: tag_name = "Opcode List 3"; break;
        case 51041: tag_name = "Noise Profile"; break;
        case 51043: tag_name = "Time Codes"; break;
        case 51044: tag_name = "Frame Rate"; break;
        case 51058: tag_name = "T Stop"; break;
        case 51081: tag_name = "Reel Name"; break;
        case 51089: tag_name = "Original Default Final Size"; break;
        case 51090: tag_name = "Original Best Quality Final Size"; break;
        case 51091: tag_name = "Original Default Crop Size"; break;
        case 51105: tag_name = "Profile Hue Sat Map Encoding"; break;
        case 51107: tag_name = "Profile Look Table Encoding"; break;
        case 51108: tag_name = "Baseline Exposure Offset"; break;
        case 51109: tag_name = "Default Black Render"; break;
        case 51110: tag_name = "New Raw Image Digest"; break;
        case 51111: tag_name = "Raw To Preview Gain"; break;
        case 51112: tag_name = "Cache Blob"; break;
        case 51114: tag_name = "Cache Version"; break;
        case 51125: tag_name = "Default User Crop"; break;
        case 51157: tag_name = "Depth Format"; break;
        case 51158: tag_name = "Depth Near"; break;
        case 51159: tag_name = "Depth Far"; break;
        case 51160: tag_name = "Depth Units"; break;
        case 51161: tag_name = "Depth Measure Type"; break;
        case 51162: tag_name = "Enhance Params"; break;
        case 52525: tag_name = "Profile Gain Table Map"; break;
        case 52526: tag_name = "Semantic Name"; break;
        case 52528: tag_name = "Semantic Instance ID"; break;
        case 52529: tag_name = "Calibration Illuminant 3"; break;
        case 52530: tag_name = "Camera Calibration 3"; break;
        case 52531: tag_name = "Color Matrix 3"; break;
        case 52532: tag_name = "Forward Matrix 3"; break;
        case 52533: tag_name = "Illuminant Data 1"; break;
        case 52534: tag_name = "Illuminant Data 2"; break;
        case 52535: tag_name = "Illuminant Data 3"; break;
        case 52536: tag_name = "Mask Sub Area"; break;
        case 52537: tag_name = "Profile Hue Sat Map Data 3"; break;
        case 52538: tag_name = "Reduction Matrix 3"; break;
        case 52539: tag_name = "RGB Tables"; break;
        case 52541: tag_name = "Profile Gain Table Map 2"; break;
        case 52544: tag_name = "Column Interleave Factor"; break;
        case 52545: tag_name = "Image Sequence Info"; break;
        case 52546: tag_name = "Image Stats"; break;
        case 52547: tag_name = "Profile Dynamic Range"; break;
        case 52548: tag_name = "Profile Group Name"; break;
        case 52550: tag_name = "JXL Distance"; break;
        case 52551: tag_name = "JXL Effort"; break;
        case 52552: tag_name = "JXL Decode Speed"; break;
        default: tag_name = "Unknown Tag " + std::to_string(entry->tag); break;
        }
    }

    ret[tag_name] = json::object();
    json &value   = ret[tag_name];

    string tag_desc = fmt::format(" (ifd:tag {}:{})", std::underlying_type<ExifTag>::type(ifd_idx),
                                  std::underlying_type<ExifTag>::type(entry->tag));
    if (const char *desc_ptr = exif_tag_get_description_in_ifd(entry->tag, ifd_idx))
        value["description"] = fmt::format("{}{}", desc_ptr, tag_desc);
    else
        value["description"] = tag_desc;

    char   buf[256] = {0};
    string str      = exif_entry_get_value(entry, buf, sizeof(buf));
    if (str.empty())
        str = "n/a";
    else if (str.length() >= 255)
        str += "…";
    value["string"] = str;

    if (auto format_name = exif_format_get_name(entry->format))
        value["type"] = format_name;
    else
        value["type"] = "unknown";

    switch (entry->format)
    {
    case EXIF_FORMAT_ASCII:
    {
        // EXIF ASCII strings are null-terminated, so we need to exclude the null terminator
        size_t len = entry->components;
        if (len > 0 && entry->data[len - 1] == '\0')
            len--;
        value["value"] = string(reinterpret_cast<char *>(entry->data), len);
        break;
    }

    case EXIF_FORMAT_BYTE:
    case EXIF_FORMAT_UNDEFINED:
    {
        vector<uint8_t> vals(entry->data, entry->data + entry->components);

        if (vals.size() == 1)
            value["value"] = vals[0];
        else
            value["value"] = vals;
        break;
    }

    case EXIF_FORMAT_SHORT:
    {
        vector<uint16_t> vals(entry->components);
        read_array(vals.data(), entry->data, entry->components, data_endian);

        if (vals.size() == 1)
            value["value"] = vals[0];
        else
            value["value"] = vals;
        break;
    }

    case EXIF_FORMAT_LONG:
    {
        vector<uint32_t> vals(entry->components);
        read_array(vals.data(), entry->data, entry->components, data_endian);

        if (vals.size() == 1)
            value["value"] = vals[0];
        else
            value["value"] = vals;
        break;
    }

    case EXIF_FORMAT_SBYTE:
    {
        vector<int8_t> sbytes(reinterpret_cast<int8_t *>(entry->data),
                              reinterpret_cast<int8_t *>(entry->data) + entry->components);
        if (sbytes.size() == 1)
            value["value"] = sbytes[0];
        else
            value["value"] = sbytes;
        break;
    }

    case EXIF_FORMAT_SSHORT:
    {
        vector<int16_t> vals(entry->components);
        read_array(vals.data(), entry->data, entry->components, data_endian);

        if (vals.size() == 1)
            value["value"] = vals[0];
        else
            value["value"] = vals;
        break;
    }

    case EXIF_FORMAT_SLONG:
    {
        vector<int32_t> vals(entry->components);
        read_array(vals.data(), entry->data, entry->components, data_endian);

        if (vals.size() == 1)
            value["value"] = vals[0];
        else
            value["value"] = vals;
        break;
    }

    case EXIF_FORMAT_RATIONAL:
    {
        vector<double> vals;
        for (unsigned int i = 0; i < entry->components; i++)
        {
            auto r = read_as<uint2>(&entry->data[8 * i], data_endian);
            vals.push_back(double(r.x) / r.y);
        }

        if (vals.size() == 1)
            value["value"] = vals[0];
        else
            value["value"] = vals;
        break;
    }

    case EXIF_FORMAT_SRATIONAL:
    {
        vector<double> vals;
        for (unsigned int i = 0; i < entry->components; i++)
        {
            auto r = read_as<int2>(&entry->data[8 * i], data_endian);
            vals.push_back(double(r.x) / r.y);
        }

        if (vals.size() == 1)
            value["value"] = vals[0];
        else
            value["value"] = vals;
        break;
    }

    case EXIF_FORMAT_FLOAT:
    {
        vector<float> vals(entry->components);
        read_array(vals.data(), entry->data, entry->components, data_endian);

        if (vals.size() == 1)
            value["value"] = vals[0];
        else
            value["value"] = vals;
        break;
    }

    case EXIF_FORMAT_DOUBLE:
    {
        vector<double> vals(entry->components);
        read_array(vals.data(), entry->data, entry->components, data_endian);

        if (vals.size() == 1)
            value["value"] = vals[0];
        else
            value["value"] = vals;
        break;
    }

    default: value = nullptr; return ret; // unknown or unsupported format
    }

    // special handling of some tags
    // special handling of compression tag since libexif doesn't know about many compression formats
    if (entry->tag == EXIF_TAG_COMPRESSION)
    {
        string compression_name;
        switch (value["value"].get<int>())
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
        value["string"] = compression_name;
    }
    else if (entry->tag == EXIF_TAG_PHOTOMETRIC_INTERPRETATION)
    {
        string photo_interp;
        switch (value["value"].get<int>())
        {
        case 4: value["string"] = "Transparency Mask"; break;
        case 9: value["string"] = "ICCLab"; break;
        case 10: value["string"] = "ITULab"; break;
        case 32803: value["string"] = "Color Filter Array"; break; // DNG ext
        case 32844: value["string"] = "CIE Log2(L)"; break;
        case 32845: value["string"] = "CIE Log2(L) (u',v')"; break;
        case 34892: value["string"] = "Linear RAW"; break; // DNG ext
        default: break;
        }
    }
    else if (entry->tag == EXIF_TAG_PLANAR_CONFIGURATION)
    {
        if (value["value"].get<int>() == 1)
            value["string"] = "Single (interleaved) plane";
        else if (value["value"].get<int>() == 2)
            value["string"] = "Separate planes";
    }

    // DNG-specific TIFF tags (Adobe DNG Specification versions 1.0-1.7)
    switch (tag)
    {
    case 50706: // DNGVersion
        value["description"] = "The DNG four-tier version number. Files compliant with e.g. version 1.6.0.0 of the DNG "
                               "spec should contain the bytes: 1, 6, 0, 0." +
                               tag_desc;
        break;
    case 50707: // DNGBackwardVersion
        value["description"] =
            "Specifies the oldest version of the DNG spec for which a file is compatible. Readers should not attempt "
            "to read a file if this tag specifies a version number that is higher than the version number of the "
            "specification the reader was based on." +
            tag_desc;
        break;
    case 50708: // UniqueCameraModel
        value["description"] =
            "Defines a unique, non-localized name for the camera model that created the image in the raw file. This "
            "name should include the manufacturer's name to avoid conflicts, and should not be localized, even if the "
            "camera name itself is localized for different markets." +
            tag_desc;
        break;
    case 50709: // LocalizedCameraModel
        value["description"] = "Localized camera model name" + tag_desc;
        break;
    case 50710: // CFAPlaneColor
        value["description"] = "Provides a mapping between the values in the CFAPattern tag and the plane numbers in "
                               "LinearRaw space. This is a required tag for non-RGB CFA images." +
                               tag_desc;
        break;
    case 50711: // CFALayout
        value["description"] = "Describes the spatial layout of the CFA." + tag_desc;
        switch (value["value"].get<int>())
        {
        case 1: value["string"] = "Rectangular (or square) layout"; break;
        case 2: value["string"] = "Staggered layout A: even columns offset down by 1/2 row"; break;
        case 3: value["string"] = "Staggered layout B: even columns offset up by 1/2 row"; break;
        case 4: value["string"] = "Staggered layout C: even rows offset right by 1/2 column"; break;
        case 5: value["string"] = "Staggered layout D: even rows offset left by 1/2 column"; break;
        case 6:
            value["string"] =
                "Staggered layout E: even rows offset up by 1/2 row, even columns offset left by 1/2 column";
            break;
        case 7:
            value["string"] =
                "Staggered layout F: even rows offset up by 1/2 row, even columns offset right by 1/2 column";
            break;
        case 8:
            value["string"] =
                "Staggered layout G: even rows offset down by 1/2 row, even columns offset left by 1/2 column";
            break;
        case 9:
            value["string"] =
                "Staggered layout H: even rows offset down by 1/2 row, even columns offset right by 1/2 column";
            break;
        default: break;
        }
        break;
    case 50712: // LinearizationTable
        value["description"] =
            "Describes a lookup table that maps stored values into linear values. This tag is typically used to "
            "increase compression ratios by storing the raw data in a non-linear, more visually uniform space with "
            "fewer total encoding levels. If SamplesPerPixel is not equal to one, this single table applies to all the "
            "samples for each pixel." +
            tag_desc;
        break;
    case 50713: // BlackLevelRepeatDim
        value["description"] = "Specifies repeat pattern size for the BlackLevel tag." + tag_desc;
        break;
    case 50714: // BlackLevel
        value["description"] = "Specifies the zero light (a.k.a. thermal black or black current) encoding level, as a "
                               "repeating pattern. The origin of this pattern is the top-left corner of the ActiveArea "
                               "rectangle. The values are stored in row-column-sample scan order." +
                               tag_desc;
        break;
    case 50715: // BlackLevelDeltaH
        value["description"] = "Horizontal black level delta per column." + tag_desc;
        break;
    case 50716: // BlackLevelDeltaV
        value["description"] = "Vertical black level delta per row." + tag_desc;
        break;
    case 50717: // WhiteLevel
        value["description"] = "Per-channel white/saturation level." + tag_desc;
        break;
    case 50718: // DefaultScale
        value["description"] = "Default scale factors for X and Y dimensions." + tag_desc;
        break;
    case 50719: // DefaultCropOrigin
        value["description"] = "Origin of final image area in raw coordinates." + tag_desc;
        break;
    case 50720: // DefaultCropSize
        value["description"] = "Size of final image area in raw coordinates." + tag_desc;
        break;
    case 50721: // ColorMatrix1
        value["description"] = "Color transform matrix from camera color space to reference illuminant 1." + tag_desc;
        break;
    case 50722: // ColorMatrix2
        value["description"] = "Color transform matrix from camera color space to reference illuminant 2." + tag_desc;
        break;
    case 50723: // CameraCalibration1
        value["description"] = "Camera calibration matrix for illuminant 1." + tag_desc;
        break;
    case 50724: // CameraCalibration2
        value["description"] = "Camera calibration matrix for illuminant 2." + tag_desc;
        break;
    case 50725: // ReductionMatrix1
        value["description"] = "Dimensionality reduction matrix for illuminant 1." + tag_desc;
        break;
    case 50726: // ReductionMatrix2
        value["description"] = "Dimensionality reduction matrix for illuminant 2." + tag_desc;
        break;
    case 50727: // AnalogBalance
        value["description"] = "Per-channel analog gain applied before digitization." + tag_desc;
        break;
    case 50728: // AsShotNeutral
        value["description"] = "Selected white balance at time of capture in inverse format." + tag_desc;
        break;
    case 50729: // AsShotWhiteXY
        value["description"] = "Selected white balance at time of capture in chromaticity coordinates." + tag_desc;
        break;
    case 50730: // BaselineExposure
        value["description"] = "Camera model-specific baseline exposure compensation." + tag_desc;
        break;
    case 50731: // BaselineNoise
        value["description"] = "Camera model-specific noise level at ISO 100." + tag_desc;
        break;
    case 50732: // BaselineSharpness
        value["description"] = "Camera model-specific sharpness level." + tag_desc;
        break;
    case 50733: // BayerGreenSplit
        value["description"] = "Bayer green channel split quality metric." + tag_desc;
        break;
    case 50734: // LinearResponseLimit
        value["description"] = "Fraction of encoded range above which response may be non-linear." + tag_desc;
        break;
    case 50735: // CameraSerialNumber
        value["description"] = "Camera serial number." + tag_desc;
        break;
    case 50736: // LensInfo
        value["description"] =
            "Lens information: min focal length, max focal length, min F-stop, max F-stop." + tag_desc;
        break;
    case 50737: // ChromaBlurRadius
        value["description"] = "Chroma blur radius for anti-aliasing." + tag_desc;
        break;
    case 50738: // AntiAliasStrength
        value["description"] = "Anti-aliasing filter strength." + tag_desc;
        break;
    case 50739: // ShadowScale
        value["description"] = "Shadow scale factor hint." + tag_desc;
        break;
    case 50740: // DNGPrivateData
        value["description"] = "Private DNG data block." + tag_desc;
        break;
    case 50741: // MakerNoteSafety
        value["description"] = "MakerNote data safety indicator." + tag_desc;
        switch (value["value"].get<int>())
        {
        case 0: value["string"] = "Unsafe - may require original file for processing"; break;
        case 1: value["string"] = "Safe - can be processed without original file"; break;
        default: break;
        }
        break;
    case 50778: // CalibrationIlluminant1
        value["description"] = "Illuminant type for ColorMatrix1 and CameraCalibration1." + tag_desc;
        switch (value["value"].get<int>())
        {
        case 0: value["string"] = "Unknown"; break;
        case 1: value["string"] = "Daylight"; break;
        case 2: value["string"] = "Fluorescent"; break;
        case 3: value["string"] = "Tungsten (incandescent light)"; break;
        case 4: value["string"] = "Flash"; break;
        case 9: value["string"] = "Fine weather"; break;
        case 10: value["string"] = "Cloudy weather"; break;
        case 11: value["string"] = "Shade"; break;
        case 12: value["string"] = "Daylight fluorescent (D 5700 - 7100K)"; break;
        case 13: value["string"] = "Day white fluorescent (N 4600 - 5500K)"; break;
        case 14: value["string"] = "Cool white fluorescent (W 3800 - 4500K)"; break;
        case 15: value["string"] = "White fluorescent (WW 3250 - 3800K)"; break;
        case 16: value["string"] = "Warm white fluorescent (L 2600 - 3250K)"; break;
        case 17: value["string"] = "Standard light A"; break;
        case 18: value["string"] = "Standard light B"; break;
        case 19: value["string"] = "Standard light C"; break;
        case 20: value["string"] = "D55"; break;
        case 21: value["string"] = "D65"; break;
        case 22: value["string"] = "D75"; break;
        case 23: value["string"] = "D50"; break;
        case 24: value["string"] = "ISO studio tungsten"; break;
        case 255: value["string"] = "Other light source"; break;
        default: break;
        }
        break;
    case 50779: // CalibrationIlluminant2
        value["description"] = "Illuminant type for ColorMatrix2 and CameraCalibration2." + tag_desc;
        switch (value["value"].get<int>())
        {
        case 0: value["string"] = "Unknown"; break;
        case 1: value["string"] = "Daylight"; break;
        case 2: value["string"] = "Fluorescent"; break;
        case 3: value["string"] = "Tungsten (incandescent light)"; break;
        case 4: value["string"] = "Flash"; break;
        case 9: value["string"] = "Fine weather"; break;
        case 10: value["string"] = "Cloudy weather"; break;
        case 11: value["string"] = "Shade"; break;
        case 12: value["string"] = "Daylight fluorescent (D 5700 - 7100K)"; break;
        case 13: value["string"] = "Day white fluorescent (N 4600 - 5500K)"; break;
        case 14: value["string"] = "Cool white fluorescent (W 3800 - 4500K)"; break;
        case 15: value["string"] = "White fluorescent (WW 3250 - 3800K)"; break;
        case 16: value["string"] = "Warm white fluorescent (L 2600 - 3250K)"; break;
        case 17: value["string"] = "Standard light A"; break;
        case 18: value["string"] = "Standard light B"; break;
        case 19: value["string"] = "Standard light C"; break;
        case 20: value["string"] = "D55"; break;
        case 21: value["string"] = "D65"; break;
        case 22: value["string"] = "D75"; break;
        case 23: value["string"] = "D50"; break;
        case 24: value["string"] = "ISO studio tungsten"; break;
        case 255: value["string"] = "Other light source"; break;
        default: break;
        }
        break;
    case 50780: // BestQualityScale
        value["description"] = "Best quality multiplier for final image size." + tag_desc;
        break;
    case 50781: // RawDataUniqueID
        value["description"] = "Unique identifier for raw image data." + tag_desc;
        break;
    case 50827: // OriginalRawFileName
        value["description"] = "Original raw file name before conversion." + tag_desc;
        break;
    case 50828: // OriginalRawFileData
        value["description"] = "Original raw file embedded data." + tag_desc;
        break;
    case 50829: // ActiveArea
        value["description"] = "This rectangle defines the active (non-masked) pixels of the sensor. The order of the "
                               "rectangle coordinates is: top, left, bottom, right." +
                               tag_desc;
        break;
    case 50830: // MaskedAreas
        value["description"] = "A list of non-overlapping rectangle coordinates of fully masked pixels, which can be "
                               "optionally used by DNG readers to measure the black encoding level.The order of each "
                               "rectangle's coordinates is: top, left, bottom, right." +
                               tag_desc;
        break;
    case 50831: // AsShotICCProfile
        value["description"] = "ICC profile for as-shot color space." + tag_desc;
        break;
    case 50832: // AsShotPreProfileMatrix
        value["description"] = "Matrix applied before ICC profile for as-shot rendering." + tag_desc;
        break;
    case 50833: // CurrentICCProfile
        value["description"] = "ICC profile for current rendering." + tag_desc;
        break;
    case 50834: // CurrentPreProfileMatrix
        value["description"] = "Matrix applied before ICC profile for current rendering." + tag_desc;
        break;
    case 50879: // ColorimetricReference
        value["description"] = "Colorimetric reference for camera color space." + tag_desc;
        switch (value["value"].get<int>())
        {
        case 0: value["string"] = "Scene-referred (default)"; break;
        case 1: value["string"] = "Output-referred"; break;
        default: break;
        }
        break;
    case 50931: // CameraCalibrationSignature
        value["description"] = "Digital signature for camera calibration data." + tag_desc;
        break;
    case 50932: // ProfileCalibrationSignature
        value["description"] = "Digital signature for profile calibration data." + tag_desc;
        break;
    case 50934: // AsShotProfileName
        value["description"] = "Name of as-shot camera profile." + tag_desc;
        break;
    case 50935: // NoiseReductionApplied
        value["description"] = "Amount of noise reduction already applied." + tag_desc;
        break;
    case 50936: // ProfileName
        value["description"] = "Name of camera profile." + tag_desc;
        break;
    case 50937: // ProfileHueSatMapDims
        value["description"] = "Dimensions of ProfileHueSatMapData arrays." + tag_desc;
        break;
    case 50938: // ProfileHueSatMapData1
        value["description"] = "Hue/saturation/value mapping table for illuminant 1." + tag_desc;
        break;
    case 50939: // ProfileHueSatMapData2
        value["description"] = "Hue/saturation/value mapping table for illuminant 2." + tag_desc;
        break;
    case 50940: // ProfileToneCurve
        value["description"] = "Default tone curve for camera profile." + tag_desc;
        break;
    case 50941: // ProfileEmbedPolicy
        value["description"] = "Profile embedding policy." + tag_desc;
        switch (value["value"].get<int>())
        {
        case 0: value["string"] = "Allow copying"; break;
        case 1: value["string"] = "Embed if used"; break;
        case 2: value["string"] = "Never embed"; break;
        case 3: value["string"] = "No restrictions"; break;
        default: break;
        }
        break;
    case 50942: // ProfileCopyright
        value["description"] = "Camera profile copyright string." + tag_desc;
        break;
    case 50964: // ForwardMatrix1
        value["description"] = "Matrix mapping XYZ values to camera color space for illuminant 1." + tag_desc;
        break;
    case 50965: // ForwardMatrix2
        value["description"] = "Matrix mapping XYZ values to camera color space for illuminant 2." + tag_desc;
        break;
    case 50966: // PreviewApplicationName
        value["description"] = "Name of application used to create preview." + tag_desc;
        break;
    case 50967: // PreviewApplicationVersion
        value["description"] = "Version of application used to create preview." + tag_desc;
        break;
    case 50968: // PreviewSettingsName
        value["description"] = "Name of preview settings." + tag_desc;
        break;
    case 50969: // PreviewSettingsDigest
        value["description"] = "MD5 digest of preview settings." + tag_desc;
        break;
    case 50970: // PreviewColorSpace
        value["description"] = "Color space of preview image." + tag_desc;
        switch (value["value"].get<int>())
        {
        case 0: value["string"] = "Unknown"; break;
        case 1: value["string"] = "Gray Gamma 2.2"; break;
        case 2: value["string"] = "sRGB"; break;
        case 3: value["string"] = "Adobe RGB"; break;
        case 4: value["string"] = "ProPhoto RGB"; break;
        default: break;
        }
        break;
    case 50971: // PreviewDateTime
        value["description"] = "Date/time preview was created." + tag_desc;
        break;
    case 50972: // RawImageDigest
        value["description"] = "MD5 digest of raw image data." + tag_desc;
        break;
    case 50973: // OriginalRawFileDigest
        value["description"] = "MD5 digest of original raw file data." + tag_desc;
        break;
    case 50974: // SubTileBlockSize
        value["description"] = "Tile block size for sub-tile access." + tag_desc;
        break;
    case 50975: // RowInterleaveFactor
        value["description"] = "Number of interleaved fields per row." + tag_desc;
        break;
    case 50981: // ProfileLookTableDims
        value["description"] = "Dimensions of ProfileLookTableData." + tag_desc;
        break;
    case 50982: // ProfileLookTableData
        value["description"] = "3D lookup table for profile color transform." + tag_desc;
        break;
    case 51008: // OpcodeList1
        value["description"] = "Processing operations applied to raw data." + tag_desc;
        break;
    case 51009: // OpcodeList2
        value["description"] = "Processing operations applied after demosaicing." + tag_desc;
        break;
    case 51022: // OpcodeList3
        value["description"] = "Processing operations applied after color correction." + tag_desc;
        break;
    case 51041: // NoiseProfile
        value["description"] = "Noise model parameters for each channel." + tag_desc;
        break;
    case 51043: // TimeCodes
        value["description"] = "SMPTE time codes for video frames." + tag_desc;
        break;
    case 51044: // FrameRate
        value["description"] = "Video frame rate as rational number." + tag_desc;
        break;
    case 51058: // TStop
        value["description"] = "T-stop value for lens transmission loss." + tag_desc;
        break;
    case 51081: // ReelName
        value["description"] = "Film reel or video tape identifier." + tag_desc;
        break;
    case 51089: // OriginalDefaultFinalSize
        value["description"] = "Default final image size before cropping." + tag_desc;
        break;
    case 51090: // OriginalBestQualityFinalSize
        value["description"] = "Best quality final image size before cropping." + tag_desc;
        break;
    case 51091: // OriginalDefaultCropSize
        value["description"] = "Default crop size in original coordinates." + tag_desc;
        break;
    case 51105: // ProfileHueSatMapEncoding
        value["description"] = "Encoding method for hue/saturation/value maps." + tag_desc;
        switch (value["value"].get<int>())
        {
        case 0: value["string"] = "Linear"; break;
        case 1: value["string"] = "sRGB"; break;
        default: break;
        }
        break;
    case 51107: // ProfileLookTableEncoding
        value["description"] = "Encoding method for profile lookup tables." + tag_desc;
        switch (value["value"].get<int>())
        {
        case 0: value["string"] = "Linear"; break;
        case 1: value["string"] = "sRGB"; break;
        default: break;
        }
        break;
    case 51108: // BaselineExposureOffset
        value["description"] = "Baseline exposure offset for DNG 1.4." + tag_desc;
        break;
    case 51109: // DefaultBlackRender
        value["description"] = "Preferred black rendering method." + tag_desc;
        switch (value["value"].get<int>())
        {
        case 0: value["string"] = "Auto"; break;
        case 1: value["string"] = "None"; break;
        default: break;
        }
        break;
    case 51110: // NewRawImageDigest
        value["description"] = "Enhanced MD5 digest of raw image data." + tag_desc;
        break;
    case 51111: // RawToPreviewGain
        value["description"] = "Gain factor from raw to preview linear space." + tag_desc;
        break;
    case 51112: // CacheBlob
        value["description"] = "Cached data for faster processing (DNG 1.4)." + tag_desc;
        break;
    case 51114: // CacheVersion
        value["description"] = "Version of cached data format (DNG 1.4)." + tag_desc;
        break;
    case 51125: // DefaultUserCrop
        value["description"] = "Default user crop rectangle." + tag_desc;
        break;
    case 51157: // DepthFormat
        value["description"] = "Format of depth map data." + tag_desc;
        switch (value["value"].get<int>())
        {
        case 0: value["string"] = "Unknown"; break;
        case 1: value["string"] = "Linear"; break;
        case 2: value["string"] = "Inverse"; break;
        default: break;
        }
        break;
    case 51158: // DepthNear
        value["description"] = "Distance to nearest object in depth map." + tag_desc;
        break;
    case 51159: // DepthFar
        value["description"] = "Distance to farthest object in depth map." + tag_desc;
        break;
    case 51160: // DepthUnits
        value["description"] = "Measurement units for depth values." + tag_desc;
        switch (value["value"].get<int>())
        {
        case 0: value["string"] = "Unknown"; break;
        case 1: value["string"] = "Meters"; break;
        default: break;
        }
        break;
    case 51161: // DepthMeasureType
        value["description"] = "Type of depth measurement." + tag_desc;
        switch (value["value"].get<int>())
        {
        case 0: value["string"] = "Unknown"; break;
        case 1: value["string"] = "Optical axis"; break;
        case 2: value["string"] = "Optical ray"; break;
        default: break;
        }
        break;
    case 51162: // EnhanceParams
        value["description"] = "Parameters for image enhancement." + tag_desc;
        break;
    case 52525: // ProfileGainTableMap
        value["description"] = "Gain table map for sensor variations (DNG 1.6)." + tag_desc;
        break;
    case 52526: // SemanticName
        value["description"] = "Semantic label for image content (DNG 1.6)." + tag_desc;
        break;
    case 52528: // SemanticInstanceID
        value["description"] = "Instance identifier for semantic content (DNG 1.6)." + tag_desc;
        break;
    case 52529: // CalibrationIlluminant3
        value["description"] = "Illuminant type for third calibration set (DNG 1.6)." + tag_desc;
        switch (value["value"].get<int>())
        {
        case 0: value["string"] = "Unknown"; break;
        case 1: value["string"] = "Daylight"; break;
        case 2: value["string"] = "Fluorescent"; break;
        case 3: value["string"] = "Tungsten (incandescent light)"; break;
        case 4: value["string"] = "Flash"; break;
        case 9: value["string"] = "Fine weather"; break;
        case 10: value["string"] = "Cloudy weather"; break;
        case 11: value["string"] = "Shade"; break;
        case 12: value["string"] = "Daylight fluorescent (D 5700 - 7100K)"; break;
        case 13: value["string"] = "Day white fluorescent (N 4600 - 5500K)"; break;
        case 14: value["string"] = "Cool white fluorescent (W 3800 - 4500K)"; break;
        case 15: value["string"] = "White fluorescent (WW 3250 - 3800K)"; break;
        case 16: value["string"] = "Warm white fluorescent (L 2600 - 3250K)"; break;
        case 17: value["string"] = "Standard light A"; break;
        case 18: value["string"] = "Standard light B"; break;
        case 19: value["string"] = "Standard light C"; break;
        case 20: value["string"] = "D55"; break;
        case 21: value["string"] = "D65"; break;
        case 22: value["string"] = "D75"; break;
        case 23: value["string"] = "D50"; break;
        case 24: value["string"] = "ISO studio tungsten"; break;
        case 255: value["string"] = "Other light source"; break;
        default: break;
        }
        break;
    case 52530: // CameraCalibration3
        value["description"] = "Camera calibration matrix for illuminant 3 (DNG 1.6)." + tag_desc;
        break;
    case 52531: // ColorMatrix3
        value["description"] = "Color transform matrix for illuminant 3 (DNG 1.6)." + tag_desc;
        break;
    case 52532: // ForwardMatrix3
        value["description"] = "Forward matrix for illuminant 3 (DNG 1.6)." + tag_desc;
        break;
    case 52533: // IlluminantData1
        value["description"] = "Spectral data for illuminant 1 (DNG 1.6)." + tag_desc;
        break;
    case 52534: // IlluminantData2
        value["description"] = "Spectral data for illuminant 2 (DNG 1.6)." + tag_desc;
        break;
    case 52535: // IlluminantData3
        value["description"] = "Spectral data for illuminant 3 (DNG 1.6)." + tag_desc;
        break;
    case 52536: // MaskSubArea
        value["description"] = "Sub-area for mask or matte (DNG 1.6)." + tag_desc;
        break;
    case 52537: // ProfileHueSatMapData3
        value["description"] = "Hue/saturation/value mapping table for illuminant 3 (DNG 1.6)." + tag_desc;
        break;
    case 52538: // ReductionMatrix3
        value["description"] = "Dimensionality reduction matrix for illuminant 3 (DNG 1.6)." + tag_desc;
        break;
    case 52539: // RGBTables
        value["description"] = "RGB lookup tables for color correction (DNG 1.6)." + tag_desc;
        break;
    case 52541: // ProfileGainTableMap2
        value["description"] = "Second gain table map for sensor variations (DNG 1.6)." + tag_desc;
        break;
    case 52544: // ColumnInterleaveFactor
        value["description"] = "Number of interleaved fields per column (DNG 1.7)." + tag_desc;
        break;
    case 52545: // ImageSequenceInfo
        value["description"] = "Information about image sequence or burst (DNG 1.7)." + tag_desc;
        break;
    case 52546: // ImageStats
        value["description"] = "Statistical information about image data (DNG 1.7)." + tag_desc;
        break;
    case 52547: // ProfileDynamicRange
        value["description"] = "Dynamic range of camera profile (DNG 1.7)." + tag_desc;
        break;
    case 52548: // ProfileGroupName
        value["description"] = "Group name for related camera profiles (DNG 1.7)." + tag_desc;
        break;
    case 52550: // JXLDistance
        value["description"] = "JPEG XL compression distance parameter (DNG 1.7)." + tag_desc;
        break;
    case 52551: // JXLEffort
        value["description"] = "JPEG XL encoding effort level (DNG 1.7)." + tag_desc;
        break;
    case 52552: // JXLDecodeSpeed
        value["description"] = "JPEG XL decode speed tier (DNG 1.7)." + tag_desc;
        break;
    default: break;
    }

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

    return ret;
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

        json &ifd_json = j[ExifIfdTable[ifd_idx]];

        for (unsigned int i = 0; i < content->count; ++i)
        {
            ExifEntry *entry = content->entries[i];
            if (!entry)
                continue;

            ifd_json.update(entry_to_json(entry, exif_data_get_byte_order(ed), ifd_idx));
        }
    }

    return j;
}