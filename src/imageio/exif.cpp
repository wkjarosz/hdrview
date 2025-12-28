#include "exif.h"
#include "common.h"
#include "json.h"
#include <cstdint>
#include <cstdio>
#include <cstring> // for memcmp
#include <libexif/exif-data.h>
#include <libexif/exif-ifd.h>
#include <libexif/exif-mnote-data.h>
#include <libexif/exif-tag.h>
#include <memory>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <type_traits>

using std::string;
using std::vector;

static const vector<uint8_t> FOURCC = {'E', 'x', 'i', 'f', 0, 0};

// Forward declarations for helper functions:
static json exif_data_to_json(ExifData *ed);

static std::string entry_to_string(ExifEntry *e)
{
    if (!e)
        return "";
    char buf[1024] = {0};
    if (auto p = exif_entry_get_value(e, buf, sizeof(buf)))
    {
        std::string str = p;
        if (str.length() >= sizeof(buf) - 1)
            str += "…";
        return str;
    }
    else
        return "";
}

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

json entry_to_json(void *entry_v, int boi, unsigned int ifd_idx)
{
    if (ifd_idx >= EXIF_IFD_COUNT)
        throw std::invalid_argument("Invalid IFD index");
    if (boi > EXIF_BYTE_ORDER_INTEL)
        throw std::invalid_argument("Invalid byte order");

    auto          entry = static_cast<ExifEntry *>(entry_v);
    auto          ifd   = static_cast<ExifIfd>(ifd_idx);
    ExifByteOrder bo    = static_cast<ExifByteOrder>(boi);
    auto          tag   = std::underlying_type<ExifTag>::type(entry->tag);

    Endian data_endian = (bo == EXIF_BYTE_ORDER_INTEL) ? Endian::Little : Endian::Big;

    if (tag == EXIF_TAG_INTEROPERABILITY_IFD_POINTER && ifd == EXIF_IFD_EXIF)
        return json::object(); // Ignore since this isn't useful to show to user

    const char *tag_name_ptr = exif_tag_get_title_in_ifd(entry->tag, ifd);
    string      tag_name;
    if (tag_name_ptr)
        tag_name = tag_name_ptr;

    json value = json::object();

    if (auto desc_ptr = exif_tag_get_description_in_ifd(entry->tag, ifd))
        value["description"] = std::string(desc_ptr);

    string str = entry_to_string(entry);
    if (str.empty())
        str = "n/a";
    value["string"] = str;

    if (auto format_name = exif_format_get_name(entry->format))
        value["type"] = format_name;
    else
        value["type"] = "unknown";

    value["tag"] = tag;
    value["ifd"] = ifd_idx;

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

    default: value = nullptr; break;
    }

    if (ifd == EXIF_IFD_0 || ifd == EXIF_IFD_1)
    {
        // Add special handling for certain tags
        switch (tag)
        {
        case 259: // Compression
            switch (value["value"].get<int>())
            {
            // these codes and names are copied from libtiff typedefs and comments
            case 1: value["string"] = "Uncompressed"; break;
            case 2: value["string"] = "CCITT modified Huffman RLE"; break;
            case 3: value["string"] = "CCITT Group 3 fax encoding"; break;
            case 4: value["string"] = "CCITT Group 4 fax encoding"; break;
            case 5: value["string"] = "Lempel-Ziv & Welch (LZW)"; break;
            case 6: value["string"] = "JPEG"; break;
            case 7: value["string"] = "JPEG"; break;
            case 8: value["string"] = "Deflate/ZIP compression, as recognized by Adobe"; break;
            case 9: value["string"] = "T.85 JBIG compression"; break;
            case 10: value["string"] = "T.43 color by layered JBIG compression"; break;
            case 32766: value["string"] = "NeXT 2-bit RLE"; break;
            case 32771: value["string"] = "Uncompressed w/ word alignment"; break;
            case 32773: value["string"] = "Macintosh RLE"; break;
            case 32809: value["string"] = "ThunderScan RLE"; break;
            /* codes 32895-32898 are reserved for ANSI IT8 TIFF/IT <dkelly@apago.com) */
            case 32895: value["string"] = "IT8 CT w/padding"; break;
            case 32896: value["string"] = "IT8 Linework RLE"; break;
            case 32897: value["string"] = "IT8 Monochrome picture"; break;
            case 32898: value["string"] = "IT8 Binary line art"; break;
            /* compression codes 32908-32911 are reserved for Pixar */
            case 32908: value["string"] = "Pixar Film (10bit LZW)"; break;
            case 32909: value["string"] = "Pixar Log (11bit ZIP)"; break;
            case 32910: [[fallthrough]];
            case 32911: value["string"] = "Unknown Pixar compression"; break;
            case 32946: value["string"] = "Deflate/ZIP compression, legacy tag"; break;
            case 32947: value["string"] = "Kodak DCS encoding"; break;
            case 34661: value["string"] = "ISO JBIG"; break;
            case 34676: value["string"] = "SGI Log Luminance RLE"; break;
            case 34677: value["string"] = "SGI Log 24-bit packed"; break;
            case 34712: value["string"] = "Leadtools JPEG2000"; break;
            case 34887: [[fallthrough]];
            case 34888: [[fallthrough]];
            case 34889: value["string"] = "ESRI Lerc codec: https://github.com/Esri/lerc"; break;
            case 34925: value["string"] = "LZMA2"; break;
            case 50000: value["string"] = "ZSTD"; break;
            case 50001: value["string"] = "WEBP"; break;
            case 50002: value["string"] = "JPEGXL"; break;
            case 52546: value["string"] = "JPEGXL from DNG 1.7 specification"; break;
            default: break;
            }
            break;
        case 262: // PhotometricInterpretation
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
            break;
        case 284: // PlanarConfiguration
            if (value["value"].get<int>() == 1)
                value["string"] = "Single (interleaved) plane";
            else if (value["value"].get<int>() == 2)
                value["string"] = "Separate planes";
            else
                value["string"] = "Unrecognized";
            break;
        case 322: tag_name = "Tile Width"; break;
        case 323: tag_name = "Tile Length"; break;
        case 324: tag_name = "Tile Offsets"; break;
        case 325: tag_name = "Tile Byte Counts"; break;
        case 513:
            tag_name = "JPEG Interchange Format";
            return json::object();
            break;
        case 514:
            tag_name = "JPEG Interchange Format Length";
            return json::object();
            break;
        case 700:
        {
            // Special handling for XMP data
            string xmp_data(reinterpret_cast<char *>(entry->data), entry->size);
            value = {
                {"value", xmp_data}, {"string", xmp_data}, {"type", "string"}, {"description", "XMP metadata packet"}};
            tag_name = "XMP Metadata";
        }
        break;
        case 34665:
            tag_name = "Exif IFD Pointer";
            return json::object();
            break;
        case 34853:
            tag_name = "GPS Info IFD Pointer";
            return json::object();
            break;
        case 37399: // SensorMethod
            tag_name             = "Sensing Method";
            value["description"] = "Indicates the type of image sensor used to capture the image.";
            switch (value["value"].get<int>())
            {
            case 1: value["string"] = "Undefined sensing method"; break;
            case 2: value["string"] = "One chip color area sensor"; break;
            case 3: value["string"] = "Two chip color area sensor"; break;
            case 4: value["string"] = "Three chip color area sensor"; break;
            case 5: value["string"] = "Color sequential area sensor"; break;
            case 7: value["string"] = "Trilinear sensor"; break;
            case 8: value["string"] = "Color sequential linear sensor"; break;
            default: break;
            }
            break;
        case 37393: tag_name = "Image Number"; break;
        case 36867: tag_name = "Date Time Original"; break;
        case 40965: tag_name = "Interoperability IFD Pointer"; break;
        case 50706: // DNGVersion
            tag_name = "DNG Version";
            value["description"] =
                "The DNG four-tier version number. Files compliant with e.g. version 1.6.0.0 of the DNG "
                "spec should contain the bytes: 1, 6, 0, 0.";
            break;
        case 50707: // DNGBackwardVersion
            tag_name = "DNG Backward Version";
            value["description"] =
                "Specifies the oldest version of the DNG spec for which a file is compatible. Readers should not "
                "attempt "
                "to read a file if this tag specifies a version number that is higher than the version number of the "
                "specification the reader was based on.";
            break;
        case 50708: // UniqueCameraModel
            tag_name             = "Unique Camera Model";
            value["description"] = "Defines a unique, non-localized name for the camera model that created the image "
                                   "in the raw file. This "
                                   "name should include the manufacturer's name to avoid conflicts, and should not be "
                                   "localized, even if the "
                                   "camera name itself is localized for different markets.";
            break;
        case 50709: // LocalizedCameraModel
            tag_name             = "Localized Camera Model";
            value["description"] = "Localized camera model name";
            break;
        case 50710: // CFAPlaneColor
            tag_name = "CFA Plane Color";
            value["description"] =
                "Provides a mapping between the values in the CFAPattern tag and the plane numbers in "
                "LinearRaw space. This is a required tag for non-RGB CFA images.";
            break;
        case 50711: // CFALayout
            tag_name             = "CFA Layout";
            value["description"] = "Describes the spatial layout of the CFA.";
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
            tag_name = "Linearization Table";
            value["description"] =
                "Describes a lookup table that maps stored values into linear values. This tag is typically used to "
                "increase compression ratios by storing the raw data in a non-linear, more visually uniform space with "
                "fewer total encoding levels. If SamplesPerPixel is not equal to one, this single table applies to all "
                "the "
                "samples for each pixel.";
            break;
        case 50713: // BlackLevelRepeatDim
            tag_name             = "Black Level Repeat Dim";
            value["description"] = "Specifies repeat pattern size for the BlackLevel tag.";
            break;
        case 50714: // BlackLevel
            tag_name = "Black Level";
            value["description"] =
                "Specifies the zero light (a.k.a. thermal black or black current) encoding level, as a "
                "repeating pattern. The origin of this pattern is the top-left corner of the ActiveArea "
                "rectangle. The values are stored in row-column-sample scan order.";
            break;
        case 50715: // BlackLevelDeltaH
            tag_name             = "Black Level Delta H";
            value["description"] = "Horizontal black level delta per column.";
            break;
        case 50716: // BlackLevelDeltaV
            tag_name             = "Black Level Delta V";
            value["description"] = "Vertical black level delta per row.";
            break;
        case 50717: // WhiteLevel
            tag_name             = "White Level";
            value["description"] = "Per-channel white/saturation level.";
            break;
        case 50718: // DefaultScale
            tag_name             = "Default Scale";
            value["description"] = "Default scale factors for X and Y dimensions.";
            break;
        case 50719: // DefaultCropOrigin
            tag_name             = "Default Crop Origin";
            value["description"] = "Origin of final image area in raw coordinates.";
            break;
        case 50720: // DefaultCropSize
            tag_name             = "Default Crop Size";
            value["description"] = "Size of final image area in raw coordinates.";
            break;
        case 50721: // ColorMatrix1
            tag_name             = "Color Matrix 1";
            value["description"] = "Color transform matrix from camera color space to reference illuminant 1.";
            break;
        case 50722: // ColorMatrix2
            tag_name             = "Color Matrix 2";
            value["description"] = "Color transform matrix from camera color space to reference illuminant 2.";
            break;
        case 50723: // CameraCalibration1
            tag_name             = "Camera Calibration 1";
            value["description"] = "Camera calibration matrix for illuminant 1.";
            break;
        case 50724: // CameraCalibration2
            tag_name             = "Camera Calibration 2";
            value["description"] = "Camera calibration matrix for illuminant 2.";
            break;
        case 50725: // ReductionMatrix1
            tag_name             = "Reduction Matrix 1";
            value["description"] = "Dimensionality reduction matrix for illuminant 1.";
            break;
        case 50726: // ReductionMatrix2
            tag_name             = "Reduction Matrix 2";
            value["description"] = "Dimensionality reduction matrix for illuminant 2.";
            break;
        case 50727: // AnalogBalance
            tag_name             = "Analog Balance";
            value["description"] = "Per-channel analog gain applied before digitization.";
            break;
        case 50728: // AsShotNeutral
            tag_name             = "As Shot Neutral";
            value["description"] = "Selected white balance at time of capture in inverse format.";
            break;
        case 50729: // AsShotWhiteXY
            tag_name             = "As Shot White XY";
            value["description"] = "Selected white balance at time of capture in chromaticity coordinates.";
            break;
        case 50730: // BaselineExposure
            tag_name             = "Baseline Exposure";
            value["description"] = "Camera model-specific baseline exposure compensation.";
            break;
        case 50731: // BaselineNoise
            tag_name             = "Baseline Noise";
            value["description"] = "Camera model-specific noise level at ISO 100.";
            break;
        case 50732: // BaselineSharpness
            tag_name             = "Baseline Sharpness";
            value["description"] = "Camera model-specific sharpness level.";
            break;
        case 50733: // BayerGreenSplit
            tag_name             = "Bayer Green Split";
            value["description"] = "Bayer green channel split quality metric.";
            break;
        case 50734: // LinearResponseLimit
            tag_name             = "Linear Response Limit";
            value["description"] = "Fraction of encoded range above which response may be non-linear.";
            break;
        case 50735: // CameraSerialNumber
            tag_name             = "Camera Serial Number";
            value["description"] = "Camera serial number.";
            break;
        case 50736: // LensInfo
            tag_name             = "Lens Info";
            value["description"] = "Lens information: min focal length, max focal length, min F-stop, max F-stop.";
            break;
        case 50737: // ChromaBlurRadius
            tag_name             = "Chroma Blur Radius";
            value["description"] = "Chroma blur radius for anti-aliasing.";
            break;
        case 50738: // AntiAliasStrength
            tag_name             = "Anti Alias Strength";
            value["description"] = "Anti-aliasing filter strength.";
            break;
        case 50739: // ShadowScale
            tag_name             = "Shadow Scale";
            value["description"] = "Shadow scale factor hint.";
            break;
        case 50740: // DNGPrivateData
            tag_name             = "DNG Private Data";
            value["description"] = "Private DNG data block.";
            break;
        case 50741: // MakerNoteSafety
            tag_name             = "Maker Note Safety";
            value["description"] = "MakerNote data safety indicator.";
            switch (value["value"].get<int>())
            {
            case 0: value["string"] = "Unsafe - may require original file for processing"; break;
            case 1: value["string"] = "Safe - can be processed without original file"; break;
            default: break;
            }
            break;
        case 50778: // CalibrationIlluminant1
            tag_name             = "Calibration Illuminant 1";
            value["description"] = "Illuminant type for ColorMatrix1 and CameraCalibration1.";
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
            tag_name             = "Calibration Illuminant 2";
            value["description"] = "Illuminant type for ColorMatrix2 and CameraCalibration2.";
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
            tag_name             = "Best Quality Scale";
            value["description"] = "Best quality multiplier for final image size.";
            break;
        case 50781: // RawDataUniqueID
            tag_name             = "Raw Data Unique ID";
            value["description"] = "Unique identifier for raw image data.";
            break;
        case 50827: // OriginalRawFileName
            tag_name             = "Original Raw File Name";
            value["description"] = "Original raw file name before conversion.";
            break;
        case 50828: // OriginalRawFileData
            tag_name             = "Original Raw File Data";
            value["description"] = "Original raw file embedded data.";
            break;
        case 50829: // ActiveArea
            tag_name = "Active Area";
            value["description"] =
                "This rectangle defines the active (non-masked) pixels of the sensor. The order of the "
                "rectangle coordinates is: top, left, bottom, right.";
            break;
        case 50830: // MaskedAreas
            tag_name = "Masked Areas";
            value["description"] =
                "A list of non-overlapping rectangle coordinates of fully masked pixels, which can be "
                "optionally used by DNG readers to measure the black encoding level.The order of each "
                "rectangle's coordinates is: top, left, bottom, right.";
            break;
        case 50831: // AsShotICCProfile
            tag_name             = "As Shot ICC Profile";
            value["description"] = "ICC profile for as-shot color space.";
            break;
        case 50832: // AsShotPreProfileMatrix
            tag_name             = "As Shot Pre Profile Matrix";
            value["description"] = "Matrix applied before ICC profile for as-shot rendering.";
            break;
        case 50833: // CurrentICCProfile
            tag_name             = "Current ICC Profile";
            value["description"] = "ICC profile for current rendering.";
            break;
        case 50834: // CurrentPreProfileMatrix
            tag_name             = "Current Pre Profile Matrix";
            value["description"] = "Matrix applied before ICC profile for current rendering.";
            break;
        case 50879: // ColorimetricReference
            tag_name             = "Colorimetric Reference";
            value["description"] = "Colorimetric reference for camera color space.";
            switch (value["value"].get<int>())
            {
            case 0: value["string"] = "Scene-referred (default)"; break;
            case 1: value["string"] = "Output-referred"; break;
            default: break;
            }
            break;
        case 50931: // CameraCalibrationSignature
            tag_name             = "Camera Calibration Signature";
            value["description"] = "Digital signature for camera calibration data.";
            break;
        case 50932: // ProfileCalibrationSignature
            tag_name             = "Profile Calibration Signature";
            value["description"] = "Digital signature for profile calibration data.";
            break;
        case 50934: // AsShotProfileName
            tag_name             = "As Shot Profile Name";
            value["description"] = "Name of as-shot camera profile.";
            break;
        case 50935: // NoiseReductionApplied
            tag_name             = "Noise Reduction Applied";
            value["description"] = "Amount of noise reduction already applied.";
            break;
        case 50936: // ProfileName
            tag_name             = "Profile Name";
            value["description"] = "Name of camera profile.";
            break;
        case 50937: // ProfileHueSatMapDims
            tag_name             = "Profile Hue Sat Map Dims";
            value["description"] = "Dimensions of ProfileHueSatMapData arrays.";
            break;
        case 50938: // ProfileHueSatMapData1
            tag_name             = "Profile Hue Sat Map Data 1";
            value["description"] = "Hue/saturation/value mapping table for illuminant 1.";
            break;
        case 50939: // ProfileHueSatMapData2
            tag_name             = "Profile Hue Sat Map Data 2";
            value["description"] = "Hue/saturation/value mapping table for illuminant 2.";
            break;
        case 50940: // ProfileToneCurve
            tag_name             = "Profile Tone Curve";
            value["description"] = "Default tone curve for camera profile.";
            break;
        case 50941: // ProfileEmbedPolicy
            tag_name             = "Profile Embed Policy";
            value["description"] = "Profile embedding policy.";
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
            tag_name             = "Profile Copyright";
            value["description"] = "Camera profile copyright string.";
            break;
        case 50964: // ForwardMatrix1
            tag_name             = "Forward Matrix 1";
            value["description"] = "Matrix mapping XYZ values to camera color space for illuminant 1.";
            break;
        case 50965: // ForwardMatrix2
            tag_name             = "Forward Matrix 2";
            value["description"] = "Matrix mapping XYZ values to camera color space for illuminant 2.";
            break;
        case 50966: // PreviewApplicationName
            tag_name             = "Preview Application Name";
            value["description"] = "Name of application used to create preview.";
            break;
        case 50967: // PreviewApplicationVersion
            tag_name             = "Preview Application Version";
            value["description"] = "Version of application used to create preview.";
            break;
        case 50968: // PreviewSettingsName
            tag_name             = "Preview Settings Name";
            value["description"] = "Name of preview settings.";
            break;
        case 50969: // PreviewSettingsDigest
            tag_name             = "Preview Settings Digest";
            value["description"] = "MD5 digest of preview settings.";
            break;
        case 50970: // PreviewColorSpace
            tag_name             = "Preview Color Space";
            value["description"] = "Color space of preview image.";
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
            tag_name             = "Preview Date Time";
            value["description"] = "Date/time preview was created.";
            break;
        case 50972: // RawImageDigest
            tag_name             = "Raw Image Digest";
            value["description"] = "MD5 digest of raw image data.";
            break;
        case 50973: // OriginalRawFileDigest
            tag_name             = "Original Raw File Digest";
            value["description"] = "MD5 digest of original raw file data.";
            break;
        case 50974: // SubTileBlockSize
            tag_name             = "Sub Tile Block Size";
            value["description"] = "Tile block size for sub-tile access.";
            break;
        case 50975: // RowInterleaveFactor
            tag_name             = "Row Interleave Factor";
            value["description"] = "Number of interleaved fields per row.";
            break;
        case 50981: // ProfileLookTableDims
            tag_name             = "Profile Look Table Dims";
            value["description"] = "Dimensions of ProfileLookTableData.";
            break;
        case 50982: // ProfileLookTableData
            tag_name             = "Profile Look Table Data";
            value["description"] = "3D lookup table for profile color transform.";
            break;
        case 51008: // OpcodeList1
            tag_name             = "Opcode List 1";
            value["description"] = "Processing operations applied to raw data.";
            break;
        case 51009: // OpcodeList2
            tag_name             = "Opcode List 2";
            value["description"] = "Processing operations applied after demosaicing.";
            break;
        case 51022: // OpcodeList3
            tag_name             = "Opcode List 3";
            value["description"] = "Processing operations applied after color correction.";
            break;
        case 51041: // NoiseProfile
            tag_name             = "Noise Profile";
            value["description"] = "Noise model parameters for each channel.";
            break;
        case 51043: // TimeCodes
            tag_name             = "Time Codes";
            value["description"] = "SMPTE time codes for video frames.";
            break;
        case 51044: // FrameRate
            tag_name             = "Frame Rate";
            value["description"] = "Video frame rate as rational number.";
            break;
        case 51058: // TStop
            tag_name             = "T Stop";
            value["description"] = "T-stop value for lens transmission loss.";
            break;
        case 51081: // ReelName
            tag_name             = "Reel Name";
            value["description"] = "Film reel or video tape identifier.";
            break;
        case 51089: // OriginalDefaultFinalSize
            tag_name             = "Original Default Final Size";
            value["description"] = "Default final image size before cropping.";
            break;
        case 51090: // OriginalBestQualityFinalSize
            tag_name             = "Original Best Quality Final Size";
            value["description"] = "Best quality final image size before cropping.";
            break;
        case 51091: // OriginalDefaultCropSize
            tag_name             = "Original Default Crop Size";
            value["description"] = "Default crop size in original coordinates.";
            break;
        case 51105: // ProfileHueSatMapEncoding
            tag_name             = "Profile Hue Sat Map Encoding";
            value["description"] = "Encoding method for hue/saturation/value maps.";
            switch (value["value"].get<int>())
            {
            case 0: value["string"] = "Linear"; break;
            case 1: value["string"] = "sRGB"; break;
            default: break;
            }
            break;
        case 51107: // ProfileLookTableEncoding
            tag_name             = "Profile Look Table Encoding";
            value["description"] = "Encoding method for profile lookup tables.";
            switch (value["value"].get<int>())
            {
            case 0: value["string"] = "Linear"; break;
            case 1: value["string"] = "sRGB"; break;
            default: break;
            }
            break;
        case 51108: // BaselineExposureOffset
            tag_name             = "Baseline Exposure Offset";
            value["description"] = "Baseline exposure offset for DNG 1.4.";
            break;
        case 51109: // DefaultBlackRender
            tag_name             = "Default Black Render";
            value["description"] = "Preferred black rendering method.";
            switch (value["value"].get<int>())
            {
            case 0: value["string"] = "Auto"; break;
            case 1: value["string"] = "None"; break;
            default: break;
            }
            break;
        case 51110: // NewRawImageDigest
            tag_name             = "New Raw Image Digest";
            value["description"] = "Enhanced MD5 digest of raw image data.";
            break;
        case 51111: // RawToPreviewGain
            tag_name             = "Raw To Preview Gain";
            value["description"] = "Gain factor from raw to preview linear space.";
            break;
        case 51112: // CacheBlob
            tag_name             = "Cache Blob";
            value["description"] = "Cached data for faster processing (DNG 1.4).";
            break;
        case 51114: // CacheVersion
            tag_name             = "Cache Version";
            value["description"] = "Version of cached data format (DNG 1.4).";
            break;
        case 51125: // DefaultUserCrop
            tag_name             = "Default User Crop";
            value["description"] = "Default user crop rectangle.";
            break;
        case 51157: // DepthFormat
            tag_name             = "Depth Format";
            value["description"] = "Format of depth map data.";
            switch (value["value"].get<int>())
            {
            case 0: value["string"] = "Unknown"; break;
            case 1: value["string"] = "Linear"; break;
            case 2: value["string"] = "Inverse"; break;
            default: break;
            }
            break;
        case 51158: // DepthNear
            tag_name             = "Depth Near";
            value["description"] = "Distance to nearest object in depth map.";
            break;
        case 51159: // DepthFar
            tag_name             = "Depth Far";
            value["description"] = "Distance to farthest object in depth map.";
            break;
        case 51160: // DepthUnits
            tag_name             = "Depth Units";
            value["description"] = "Measurement units for depth values.";
            switch (value["value"].get<int>())
            {
            case 0: value["string"] = "Unknown"; break;
            case 1: value["string"] = "Meters"; break;
            default: break;
            }
            break;
        case 51161: // DepthMeasureType
            tag_name             = "Depth Measure Type";
            value["description"] = "Type of depth measurement.";
            switch (value["value"].get<int>())
            {
            case 0: value["string"] = "Unknown"; break;
            case 1: value["string"] = "Optical axis"; break;
            case 2: value["string"] = "Optical ray"; break;
            default: break;
            }
            break;
        case 51162: // EnhanceParams
            tag_name             = "Enhance Params";
            value["description"] = "Parameters for image enhancement.";
            break;
        case 52525: // ProfileGainTableMap
            tag_name             = "Profile Gain Table Map";
            value["description"] = "Gain table map for sensor variations (DNG 1.6).";
            break;
        case 52526: // SemanticName
            tag_name             = "Semantic Name";
            value["description"] = "Semantic label for image content (DNG 1.6).";
            break;
        case 52528: // SemanticInstanceID
            tag_name             = "Semantic Instance ID";
            value["description"] = "Instance identifier for semantic content (DNG 1.6).";
            break;
        case 52529: // CalibrationIlluminant3
            tag_name             = "Calibration Illuminant 3";
            value["description"] = "Illuminant type for third calibration set (DNG 1.6).";
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
            tag_name             = "Camera Calibration 3";
            value["description"] = "Camera calibration matrix for illuminant 3 (DNG 1.6).";
            break;
        case 52531: // ColorMatrix3
            tag_name             = "Color Matrix 3";
            value["description"] = "Color transform matrix for illuminant 3 (DNG 1.6).";
            break;
        case 52532: // ForwardMatrix3
            tag_name             = "Forward Matrix 3";
            value["description"] = "Forward matrix for illuminant 3 (DNG 1.6).";
            break;
        case 52533: // IlluminantData1
            tag_name             = "Illuminant Data 1";
            value["description"] = "Spectral data for illuminant 1 (DNG 1.6).";
            break;
        case 52534: // IlluminantData2
            tag_name             = "Illuminant Data 2";
            value["description"] = "Spectral data for illuminant 2 (DNG 1.6).";
            break;
        case 52535: // IlluminantData3
            tag_name             = "Illuminant Data 3";
            value["description"] = "Spectral data for illuminant 3 (DNG 1.6).";
            break;
        case 52536: // MaskSubArea
            tag_name             = "Mask Sub Area";
            value["description"] = "Sub-area for mask or matte (DNG 1.6).";
            break;
        case 52537: // ProfileHueSatMapData3
            tag_name             = "Profile Hue Sat Map Data 3";
            value["description"] = "Hue/saturation/value mapping table for illuminant 3 (DNG 1.6).";
            break;
        case 52538: // ReductionMatrix3
            tag_name             = "Reduction Matrix 3";
            value["description"] = "Dimensionality reduction matrix for illuminant 3 (DNG 1.6).";
            break;
        case 52539: // RGBTables
            tag_name             = "RGB Tables";
            value["description"] = "RGB lookup tables for color correction (DNG 1.6).";
            break;
        case 52541: // ProfileGainTableMap2
            tag_name             = "Profile Gain Table Map 2";
            value["description"] = "Second gain table map for sensor variations (DNG 1.6).";
            break;
        case 52544: // ColumnInterleaveFactor
            tag_name             = "Column Interleave Factor";
            value["description"] = "Number of interleaved fields per column (DNG 1.7).";
            break;
        case 52545: // ImageSequenceInfo
            tag_name             = "Image Sequence Info";
            value["description"] = "Information about image sequence or burst (DNG 1.7).";
            break;
        case 52546: // ImageStats
            tag_name             = "Image Stats";
            value["description"] = "Statistical information about image data (DNG 1.7).";
            break;
        case 52547: // ProfileDynamicRange
            tag_name             = "Profile Dynamic Range";
            value["description"] = "Dynamic range of camera profile (DNG 1.7).";
            break;
        case 52548: // ProfileGroupName
            tag_name             = "Profile Group Name";
            value["description"] = "Group name for related camera profiles (DNG 1.7).";
            break;
        case 52550: // JXLDistance
            tag_name             = "JXL Distance";
            value["description"] = "JPEG XL compression distance parameter (DNG 1.7).";
            break;
        case 52551: // JXLEffort
            tag_name             = "JXL Effort";
            value["description"] = "JPEG XL encoding effort level (DNG 1.7).";
            break;
        case 52552: // JXLDecodeSpeed
            tag_name             = "JXL Decode Speed";
            value["description"] = "JPEG XL decode speed tier (DNG 1.7).";
            break;

            // FujiFilm tag value mappings (from https://exiftool.org/TagNames/FujiFilm.html)
        case 4097: // Quality (0x1001)
            tag_name = "Quality";
            switch (value["value"].get<int>())
            {
            case 0x0: value["string"] = "Normal"; break;
            case 0x1: value["string"] = "Fine"; break;
            case 0x2: value["string"] = "Super Fine"; break;
            default: value["string"] = "Unknown"; break;
            }
            break;
        case 4098: // Sharpness (0x1002)
            tag_name = "Sharpness";
            switch (value["value"].get<int>())
            {
            case 0x0: value["string"] = "-4 (softest)"; break;
            case 0x1: value["string"] = "-3 (very soft)"; break;
            case 0x2: value["string"] = "-2 (soft)"; break;
            case 0x3: value["string"] = "0 (normal)"; break;
            case 0x4: value["string"] = "+2 (hard)"; break;
            case 0x5: value["string"] = "+3 (very hard)"; break;
            case 0x6: value["string"] = "+4 (hardest)"; break;
            case 0x82: value["string"] = "-1 (medium soft)"; break;
            case 0x84: value["string"] = "+1 (medium hard)"; break;
            case 0x8000: value["string"] = "Film Simulation"; break;
            case 0xffff: value["string"] = "n/a"; break;
            default: value["string"] = "Unknown"; break;
            }
            break;
        case 4099: // WhiteBalance (0x1003)
            tag_name = "White Balance";
            switch (value["value"].get<int>())
            {
            case 0x0: value["string"] = "Auto"; break;
            case 0x1: value["string"] = "Auto (white priority)"; break;
            case 0x2: value["string"] = "Auto (ambiance priority)"; break;
            case 0x100: value["string"] = "Daylight"; break;
            case 0x200: value["string"] = "Cloudy"; break;
            case 0x300: value["string"] = "Daylight Fluorescent"; break;
            case 0x301: value["string"] = "Day White Fluorescent"; break;
            case 0x302: value["string"] = "White Fluorescent"; break;
            case 0x303: value["string"] = "Warm White Fluorescent"; break;
            case 0x304: value["string"] = "Living Room Warm White Fluorescent"; break;
            case 0x400: value["string"] = "Incandescent"; break;
            case 0x500: value["string"] = "Flash"; break;
            case 0x600: value["string"] = "Underwater"; break;
            case 0xf00: value["string"] = "Custom"; break;
            case 0xf01: value["string"] = "Custom2"; break;
            case 0xf02: value["string"] = "Custom3"; break;
            case 0xf03: value["string"] = "Custom4"; break;
            case 0xf04: value["string"] = "Custom5"; break;
            case 0xff0: value["string"] = "Kelvin"; break;
            default: value["string"] = "Unknown"; break;
            }
            break;
        case 4146: // ExposureCount (0x1032)
            tag_name             = "Exposure Count";
            value["description"] = "Number of exposures used for this image.";
            break;
        case 4147: // EXRAuto (0x1033)
            tag_name = "EXR Auto";
            switch (value["value"].get<int>())
            {
            case 0: value["string"] = "Auto"; break;
            case 1: value["string"] = "Manual"; break;
            default: value["string"] = "Unknown"; break;
            }
            break;
        case 4148: // EXRMode (0x1034)
            tag_name = "EXR Mode";
            switch (value["value"].get<int>())
            {
            case 0x100: value["string"] = "HR (High Resolution)"; break;
            case 0x200: value["string"] = "SN (Signal to Noise priority)"; break;
            case 0x300: value["string"] = "DR (Dynamic Range priority)"; break;
            default: value["string"] = "Unknown"; break;
            }
            break;
        case 4164: // ShadowTone (0x1040)
            tag_name = "Shadow Tone";
            switch (value["value"].get<int>())
            {
            case -64: value["string"] = "+4 (hardest)"; break;
            case -48: value["string"] = "+3 (very hard)"; break;
            case -32: value["string"] = "+2 (hard)"; break;
            case -16: value["string"] = "+1 (medium hard)"; break;
            case 0: value["string"] = "0 (normal)"; break;
            case 16: value["string"] = "-1 (medium soft)"; break;
            case 32: value["string"] = "-2 (soft)"; break;
            default: value["string"] = "Unknown"; break;
            }
            break;
        case 4165: // HighlightTone (0x1041)
            tag_name = "Highlight Tone";
            switch (value["value"].get<int>())
            {
            case -64: value["string"] = "+4 (hardest)"; break;
            case -48: value["string"] = "+3 (very hard)"; break;
            case -32: value["string"] = "+2 (hard)"; break;
            case -16: value["string"] = "+1 (medium hard)"; break;
            case 0: value["string"] = "0 (normal)"; break;
            case 16: value["string"] = "-1 (medium soft)"; break;
            case 32: value["string"] = "-2 (soft)"; break;
            default: value["string"] = "Unknown"; break;
            }
            break;
        case 4176: // ShutterType (0x1050)
            tag_name = "Shutter Type";
            switch (value["value"].get<int>())
            {
            case 0: value["string"] = "Mechanical"; break;
            case 1: value["string"] = "Electronic"; break;
            case 2: value["string"] = "Electronic (long shutter speed)"; break;
            case 3: value["string"] = "Electronic Front Curtain"; break;
            default: value["string"] = "Unknown"; break;
            }
            break;
        case 5121: // DynamicRange (0x1400)
            tag_name = "Dynamic Range";
            switch (value["value"].get<int>())
            {
            case 1: value["string"] = "Standard"; break;
            case 3: value["string"] = "Wide"; break;
            default: value["string"] = "Unknown"; break;
            }
            break;
        case 5122: // FilmMode (0x1401)
            tag_name = "Film Mode";
            switch (value["value"].get<int>())
            {
            case 0x0: value["string"] = "F0/Standard (Provia)"; break;
            case 0x100: value["string"] = "F1/Studio Portrait"; break;
            case 0x110: value["string"] = "F1a/Studio Portrait Enhanced Saturation"; break;
            case 0x120: value["string"] = "F1b/Studio Portrait Smooth Skin Tone (Astia)"; break;
            case 0x130: value["string"] = "F1c/Studio Portrait Increased Sharpness"; break;
            case 0x200: value["string"] = "F2/Fujichrome (Velvia)"; break;
            case 0x300: value["string"] = "F3/Studio Portrait Ex"; break;
            case 0x400: value["string"] = "F4/Velvia"; break;
            case 0x500: value["string"] = "Pro Neg. Std"; break;
            case 0x501: value["string"] = "Pro Neg. Hi"; break;
            case 0x600: value["string"] = "Classic Chrome"; break;
            case 0x700: value["string"] = "Eterna"; break;
            case 0x800: value["string"] = "Classic Negative"; break;
            case 0x900: value["string"] = "Bleach Bypass"; break;
            case 0xa00: value["string"] = "Nostalgic Neg"; break;
            case 0xb00: value["string"] = "Reala ACE"; break;
            default: value["string"] = "Unknown"; break;
            }
            break;
        case 5123: // DynamicRangeSetting (0x1402)
            tag_name = "Dynamic Range Setting";
            switch (value["value"].get<int>())
            {
            case 0x0: value["string"] = "Auto"; break;
            case 0x1: value["string"] = "Manual"; break;
            case 0x100: value["string"] = "Standard (100%)"; break;
            case 0x200: value["string"] = "Wide1 (230%)"; break;
            case 0x201: value["string"] = "Wide2 (400%)"; break;
            case 0x8000: value["string"] = "Film Simulation"; break;
            default: value["string"] = "Unknown"; break;
            }
            break;
        case 4145: // PictureMode (0x1031)
            tag_name             = "Picture Mode";
            value["description"] = "Picture mode used by FujiFilm camera.";
            switch (value["value"].get<int>())
            {
            case 0x0: value["string"] = "Auto"; break;
            case 0x1: value["string"] = "Portrait"; break;
            case 0x2: value["string"] = "Landscape"; break;
            case 0x3: value["string"] = "Macro"; break;
            case 0x4: value["string"] = "Sports"; break;
            case 0x5: value["string"] = "Night Scene"; break;
            case 0x6: value["string"] = "Program AE"; break;
            case 0x7: value["string"] = "Natural Light"; break;
            case 0x8: value["string"] = "Anti-blur"; break;
            case 0x9: value["string"] = "Beach & Snow"; break;
            case 0xa: value["string"] = "Sunset"; break;
            case 0xb: value["string"] = "Museum"; break;
            case 0xc: value["string"] = "Party"; break;
            case 0xd: value["string"] = "Flower"; break;
            case 0xe: value["string"] = "Text"; break;
            case 0xf: value["string"] = "Natural Light & Flash"; break;
            case 0x10: value["string"] = "Beach"; break;
            case 0x11: value["string"] = "Snow"; break;
            case 0x12: value["string"] = "Fireworks"; break;
            case 0x13: value["string"] = "Underwater"; break;
            case 0x14: value["string"] = "Portrait with Skin Correction"; break;
            case 0x16: value["string"] = "Panorama"; break;
            case 0x17: value["string"] = "Night (tripod)"; break;
            case 0x18: value["string"] = "Pro Low-light"; break;
            case 0x19: value["string"] = "Pro Focus"; break;
            case 0x1a: value["string"] = "Portrait 2"; break;
            case 0x1b: value["string"] = "Dog Face Detection"; break;
            case 0x1c: value["string"] = "Cat Face Detection"; break;
            case 0x30: value["string"] = "HDR"; break;
            case 0x40: value["string"] = "Advanced Filter"; break;
            case 0x100: value["string"] = "Aperture Priority AE"; break;
            case 0x200: value["string"] = "Shutter Priority AE"; break;
            case 0x300: value["string"] = "Manual Exposure"; break;
            default: value["string"] = "Unknown"; break;
            }
            break;
        // // FujiFilm IFD/RAF tags (from https://exiftool.org/TagNames/FujiFilm.html)
        // case 61441: tag_name = "Raw Image Full Width"; break;                       // 0xf001
        // case 61442: tag_name = "Raw Image Full Height"; break;                      // 0xf002
        // case 61443: tag_name = "Bits Per Sample"; break;                            // 0xf003
        // case 61447: tag_name = "Strip Offsets"; break;                              // 0xf007
        // case 61448: tag_name = "Strip Byte Counts"; break;                          // 0xf008
        // case 61450: tag_name = "Black Level"; break;                                // 0xf00a
        // case 61451: tag_name = "Geometric Distortion Params"; break;                // 0xf00b
        // case 61452: tag_name = "WB GRB Levels Standard"; break;                     // 0xf00c
        // case 61453: tag_name = "WB GRB Levels Auto"; break;                         // 0xf00d
        // case 61454: tag_name = "WB GRB Levels"; break;                              // 0xf00e
        // case 61455: tag_name = "Chromatic Aberration Params"; break;                // 0xf00f
        // case 61456: tag_name = "Vignetting Params"; break;                          // 0xf010
        // case 256: tag_name = "Raw Image Full Size"; break;                          // 0x0100
        // case 272: tag_name = "Raw Image Crop Top Left"; break;                      // 0x0110
        // case 273: tag_name = "Raw Image Cropped Size"; break;                       // 0x0111
        // case 277: tag_name = "Raw Image Aspect Ratio"; break;                       // 0x0115
        // case 279: tag_name = "Raw Zoom Active"; break;                              // 0x0117
        // case 280: tag_name = "Raw Zoom Top Left"; break;                            // 0x0118
        // case 281: tag_name = "Raw Zoom Size"; break;                                // 0x0119
        // case 289: tag_name = "Raw Image Size"; break;                               // 0x0121
        // case 304: tag_name = "Fuji Layout"; break;                                  // 0x0130
        // case 305: tag_name = "XTrans Layout"; break;                                // 0x0131
        // case 8192: tag_name = "WB GRGB Levels Auto"; break;                         // 0x2000
        // case 8448: tag_name = "WB GRGB Levels Daylight"; break;                     // 0x2100
        // case 8704: tag_name = "WB GRGB Levels Cloudy"; break;                       // 0x2200
        // case 8960: tag_name = "WB GRGB Levels Daylight Fluor"; break;               // 0x2300
        // case 8961: tag_name = "WB GRGB Levels Day White Fluor"; break;              // 0x2301
        // case 8962: tag_name = "WB GRGB Levels White Fluorescent"; break;            // 0x2302
        // case 8976: tag_name = "WB GRGB Levels Warm White Fluor"; break;             // 0x2310
        // case 8977: tag_name = "WB GRGB Levels Living Room Warm White Fluor"; break; // 0x2311
        // case 9216: tag_name = "WB GRGB Levels Tungsten"; break;                     // 0x2400
        // case 12272: tag_name = "WB GRGB Levels"; break;                             // 0x2ff0
        // case 37376: tag_name = "Relative Exposure"; break;                          // 0x9200
        // case 38480: tag_name = "Raw Exposure Bias"; break;                          // 0x9650
        // case 49152: tag_name = "RAFData"; break; // 0xc000

        // // Sony-specific tags (from https://exiftool.org/TagNames/Sony.html)
        // case 4096:
        //     tag_name = "Multi Burst Mode";
        //     break; // 0x1000
        // // case 4097: tag_name = "Multi Burst Image Width"; break;          // 0x1001
        // // case 4098: tag_name = "Multi Burst Image Height"; break;         // 0x1002
        // // case 4099: tag_name = "Panorama"; break;                         // 0x1003
        // case 8193: tag_name = "Preview Image"; break;                    // 0x2001
        // case 8194: tag_name = "Rating"; break;                           // 0x2002
        // case 8196: tag_name = "Contrast"; break;                         // 0x2004
        // case 8197: tag_name = "Saturation"; break;                       // 0x2005
        // case 8198: tag_name = "Sharpness"; break;                        // 0x2006
        // case 8199: tag_name = "Brightness"; break;                       // 0x2007
        // case 8200: tag_name = "Long Exposure Noise Reduction"; break;    // 0x2008
        // case 8201: tag_name = "High ISO Noise Reduction"; break;         // 0x2009
        // case 8202: tag_name = "HDR"; break;                              // 0x200A
        // case 8206: tag_name = "Picture Effect"; break;                   // 0x200E
        // case 8207: tag_name = "Soft Skin Effect"; break;                 // 0x200F
        // case 8209: tag_name = "Vignetting Correction"; break;            // 0x2011
        // case 8210: tag_name = "Lateral Chromatic Aberration"; break;     // 0x2012
        // case 8211: tag_name = "Distortion Correction Setting"; break;    // 0x2013
        // case 8212: tag_name = "WB Shift AB GM"; break;                   // 0x2014
        // case 8214: tag_name = "Auto Portrait Framed"; break;             // 0x2016
        // case 8215: tag_name = "Flash Action"; break;                     // 0x2017
        // case 8218: tag_name = "Electronic Front Curtain Shutter"; break; // 0x201A
        // case 8219: tag_name = "Focus Mode"; break;                       // 0x201B
        // case 8220: tag_name = "AF Area Mode Setting"; break;             // 0x201C
        // case 8221: tag_name = "Flexible Spot Position"; break;           // 0x201D
        // case 8222: tag_name = "AF Point Selected"; break;                // 0x201E
        // case 8224: tag_name = "AF Points Used"; break;                   // 0x2020
        // case 8225: tag_name = "AF Tracking"; break;                      // 0x2021
        // case 8226: tag_name = "Focal Plane AF Points Used"; break;       // 0x2022
        // case 8227: tag_name = "Multi Frame NR Effect"; break;            // 0x2023
        // case 8230: tag_name = "WB Shift AB GM Precise"; break;           // 0x2026
        // case 8231: tag_name = "Focus Location"; break;                   // 0x2027
        // case 8232: tag_name = "Variable Low Pass Filter"; break;         // 0x2028
        // case 8233: tag_name = "RAW File Type"; break;                    // 0x2029
        // case 8235: tag_name = "Priority Set In AWB"; break;              // 0x202B
        // case 8236: tag_name = "Metering Mode 2"; break;                  // 0x202C
        // case 8237: tag_name = "Exposure Standard Adjustment"; break;     // 0x202D
        // case 8238: tag_name = "Quality"; break;                          // 0x202E
        // case 8239: tag_name = "Pixel Shift Info"; break;                 // 0x202F
        // case 8241: tag_name = "Serial Number"; break;                    // 0x2031
        // case 8242: tag_name = "Shadows"; break;                          // 0x2032
        // case 8243: tag_name = "Highlights"; break;                       // 0x2033
        // case 8244: tag_name = "Fade"; break;                             // 0x2034
        // case 8245: tag_name = "Sharpness Range"; break;                  // 0x2035
        // case 8246: tag_name = "Clarity"; break;                          // 0x2036
        // case 8247: tag_name = "Focus Frame Size"; break;                 // 0x2037
        // case 8249: tag_name = "JPEG-HEIF Switch"; break;                 // 0x2039
        // case 8268: tag_name = "Hidden Info"; break;                      // 0x2044
        // case 8266: tag_name = "Focus Location 2"; break;                 // 0x204A
        // case 8284: tag_name = "Step Crop Shooting"; break;               // 0x205C
        // case 36880: tag_name = "Shot Info"; break;                       // 0x3000
        // case 36875: tag_name = "Tag 900B"; break;                        // 0x900B
        // case 36912: tag_name = "Tag 9050A"; break;                       // 0x9050
        // case 37888: tag_name = "Tag 9400A"; break;                       // 0x9400
        // case 37889: tag_name = "Tag 9401"; break;                        // 0x9401
        // case 37890: tag_name = "Tag 9402"; break;                        // 0x9402
        // case 37891: tag_name = "Tag 9403"; break;                        // 0x9403
        // case 37892: tag_name = "Tag 9404A"; break;                       // 0x9404
        // case 37893: tag_name = "Tag 9405A"; break;                       // 0x9405
        // case 45056: tag_name = "File Format"; break;                     // 0xB000
        // case 45057: tag_name = "Model ID"; break;                        // 0xB001
        // case 45088: tag_name = "Creative Style"; break;                  // 0xB020
        // case 45089: tag_name = "Color Temperature"; break;               // 0xB021
        // case 45090: tag_name = "Color Compensation Filter"; break;       // 0xB022
        // case 45091: tag_name = "Scene Mode"; break;                      // 0xB023
        // case 45092: tag_name = "Zone Matching"; break;                   // 0xB024
        // case 45093: tag_name = "Dynamic Range Optimizer"; break;         // 0xB025
        // case 45094: tag_name = "Image Stabilization"; break;             // 0xB026
        // case 45095: tag_name = "Lens Type"; break;                       // 0xB027
        // case 45096: tag_name = "Minolta Maker Note"; break;              // 0xB028
        // case 45097: tag_name = "Color Mode"; break;                      // 0xB029
        // case 45098: tag_name = "Lens Spec"; break;                       // 0xB02A
        // case 45099: tag_name = "Full Image Size"; break;                 // 0xB02B
        // case 45121: tag_name = "Exposure Mode"; break;                   // 0xB041
        // case 45122: tag_name = "Focus Mode"; break;                      // 0xB042
        // case 45123: tag_name = "AF Area Mode"; break;                    // 0xB043
        // case 45124: tag_name = "AF Illuminator"; break;                  // 0xB044
        // case 45127: tag_name = "JPEG Quality"; break;                    // 0xB047
        // case 45128: tag_name = "Flash Level"; break;                     // 0xB048
        // case 45129: tag_name = "Release Mode"; break;                    // 0xB049
        // case 45130: tag_name = "Sequence Number"; break;                 // 0xB04A
        // case 45140: tag_name = "White Balance"; break;                   // 0xB054
        default: break;
        }
    }

    if (tag_name.empty())
    {
        tag_name = fmt::format("Unknown Tag {:05}", tag);
        spdlog::debug("EXIF: Encountered {}", tag_name);
    }

    json ret;
    ret[tag_name] = value;
    return ret;
}

json exif_data_to_json(ExifData *ed)
{
    json j;

    static const char *ExifIfdTable[] = {"TIFF IFD0", "TIFF IFD1", "EXIF", "GPS", "Interoperability"};

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

    // Handle MakerNotes
    ExifMnoteData *md = exif_data_get_mnote_data(ed);
    if (md)
    {
        ExifContent *ifd0 = ed->ifd[EXIF_IFD_0];
        std::string  make = entry_to_string(exif_content_get_entry(ifd0, EXIF_TAG_MAKE));

        auto &mn = j[make.empty() ? "Maker Note" : "Maker Note (" + make + ")"] = json::object();

        unsigned int n = exif_mnote_data_count(md);
        for (unsigned int i = 0; i < n; ++i)
        {
            char   buf[1024] = {0};
            auto   tag       = exif_mnote_data_get_id(md, i);
            auto   name      = exif_mnote_data_get_name(md, i);
            auto   title     = exif_mnote_data_get_title(md, i);
            auto   desc      = exif_mnote_data_get_description(md, i);
            string str;

            string key;
            if (title && strlen(title) > 0)
                key = title;
            else if (name && strlen(name) > 0)
                key = name;
            else
                key = fmt::format("Tag {:05}", tag);

            if (auto p = exif_mnote_data_get_value(md, i, buf, sizeof(buf)))
            {
                str = p;
                if (str.length() >= sizeof(buf) - 1)
                    str += "…";
            }
            else
                str = "n/a";

            mn[key] = {{"string", str}, {"type", "MakerNote"}, {"tag", tag}};
            if (desc && strlen(desc) > 0)
                mn[key]["description"] = desc;
        }
    }

    return j;
}