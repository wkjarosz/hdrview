//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "raw.h"
#include "app.h"
#include "colorspace.h"
#include "common.h"
#include "exif.h"
#include "icc.h"
#include "image.h"
#include "jpg.h"
#include <sstream>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <libexif/exif-tag.h>
#include <spdlog/fmt/fmt.h>
#include <stdexcept>

#include <smallthreadpool.h>

#ifndef HDRVIEW_ENABLE_LIBRAW

bool is_raw_image(std::std::istream &is) noexcept { return false; }

vector<ImagePtr> load_raw_image(std::std::istream &is, string_view filename, const ImageLoadOptions &opts)
{
    throw runtime_error("RAW support not enabled in this build.");
}

#else

#include <libraw/libraw.h>
#include <libraw/libraw_version.h>

#include <libexif/exif-data.h>

#if LIBRAW_VERSION < LIBRAW_MAKE_VERSION(0, 20, 0)
#error "HDRView requires LibRaw 0.20.0 or later"
#endif

// Some structure layouts changed mid-release on this snapshot
#define LIBRAW_VERSION_AT_LEAST_SNAPSHOT_202110                                                                        \
    (LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 21, 0) && LIBRAW_SHLIB_CURRENT >= 22)

namespace
{

// Custom LibRaw datastream that wraps std::std::istream
class LibRawIStream : public LibRaw_abstract_datastream
{
public:
    LibRawIStream(std::istream &stream) : m_stream(stream) {}

    int valid() override { return m_stream.good() ? 1 : 0; }

    int read(void *ptr, size_t size, size_t nmemb) override
    {
        m_stream.read(static_cast<char *>(ptr), size * nmemb);
        return static_cast<int>(m_stream.gcount() / size);
    }

    int seek(INT64 offset, int whence) override
    {
        std::ios_base::seekdir dir;
        switch (whence)
        {
        case SEEK_SET: dir = std::ios_base::beg; break;
        case SEEK_CUR: dir = std::ios_base::cur; break;
        case SEEK_END: dir = std::ios_base::end; break;
        default: return -1;
        }
        m_stream.clear(); // Clear any eof flags
        m_stream.seekg(offset, dir);
        return m_stream.good() ? 0 : -1;
    }

    INT64 tell() override { return m_stream.tellg(); }

    INT64 size() override
    {
        auto current_pos = m_stream.tellg();
        m_stream.seekg(0, std::ios_base::end);
        auto stream_size = m_stream.tellg();
        m_stream.seekg(current_pos, std::ios_base::beg);
        return stream_size;
    }

    int get_char() override { return m_stream.get(); }

    char *gets(char *str, int sz) override
    {
        m_stream.getline(str, sz);
        return m_stream.good() ? str : nullptr;
    }

    int scanf_one(const char * /*fmt*/, void * /*val*/) override { return -1; }

    int eof() override { return m_stream.eof() ? 1 : 0; }

    int jpeg_src(void * /*jpegdata*/) override { return -1; }

    const char *fname() override { return nullptr; }

private:
    std::istream &m_stream;
};

// Context for LibRaw EXIF callback
struct ExifContext
{
    ExifContext()
    {
        content         = exif_content_new();
        data            = exif_data_new();
        content->parent = data;
    }

    ~ExifContext()
    {
        if (content)
            exif_content_free(content);
        if (data)
            exif_data_unref(data);
    }

    json         metadata;
    ExifContent *content = nullptr;
    ExifData    *data    = nullptr;
};

// LibRaw EXIF callback handler
void exif_handler(void *context, int tag, int type, int len, unsigned int ord, void *ifp, INT64 base)
{
    ExifContext &exif = *(ExifContext *)context;

    // LibRaw encodes IFD information in bits 16-23 of the tag parameter:
    // 0x00 (0) = EXIF sub-IFD tags (from parse_exif)
    // 0x20 (2) = Kodak maker notes (from parse_kodak_ifd)
    // 0x40 (4) = Interoperability IFD (from parse_exif_interop)
    // 0x50 (5) = GPS IFD (from parse_gps_libraw)
    int libraw_ifd_idx = (tag >> 16) & 0xFF;

    // Get actual tag value (lower 16 bits)
    int actual_tag = tag & 0xFFFF;

    // Map LibRaw IFD indices to libexif ExifIfd enum and IFD name
    const char *ifd_name;
    ExifIfd     ifd_enum;

    switch (libraw_ifd_idx)
    {
    default: // Unknown, treat as EXIF_IFD_0
        ifd_name = "TIFF";
        ifd_enum = EXIF_IFD_0;
        break;
    case 0x00: // EXIF sub-IFD
        ifd_name = "EXIF";
        ifd_enum = EXIF_IFD_EXIF;
        break;
    case 0x02: // Kodak maker notes - treat as EXIF_IFD_0
        ifd_name = "TIFF";
        ifd_enum = EXIF_IFD_0;
        break;
    case 0x04: // Interoperability IFD
        ifd_name = "Interoperability";
        ifd_enum = EXIF_IFD_INTEROPERABILITY;
        break;
    case 0x05: // GPS IFD
        ifd_name = "GPS";
        ifd_enum = EXIF_IFD_GPS;
        break;
    }

    ExifEntry *entry = exif_entry_new();
    if (!entry)
        return;

    const auto guard = ScopeGuard([&]() { exif_entry_unref(entry); });

    exif_data_set_byte_order(exif.data, ord == 0x4949 ? EXIF_BYTE_ORDER_INTEL : EXIF_BYTE_ORDER_MOTOROLA);
    entry->parent     = exif.content;
    entry->tag        = static_cast<ExifTag>(actual_tag);
    entry->format     = static_cast<ExifFormat>(type);
    entry->components = len;

    const int sizePerComponent = exif_format_get_size(entry->format);
    if (sizePerComponent == 0)
        return;

    entry->size = len * sizePerComponent;
    entry->data = (unsigned char *)malloc(entry->size); // Will get freed by exif_entry_unref

    // LibRaw has already positioned the stream at the correct location
    // The base parameter is for TIFF offset calculations, not for seeking
    auto *stream = (LibRaw_abstract_datastream *)ifp;
    stream->read(entry->data, sizePerComponent, len);

    try
    {
        exif.metadata[ifd_name].update(entry_to_json(entry, ord == 0x4949 ? 1 : 0, ifd_enum));
    }
    catch (const std::exception &e)
    {
        // ignore errors
        spdlog::warn("Error processing EXIF tag {}: {}", actual_tag, e.what());
    }
}

// Use LibRaw's parsed maker-notes structure (if available) and store
// the fields into our JSON `maker_notes` object. This mirrors the
// approach used in OpenImageIO's RawInput::get_makernotes* helpers.
void add_maker_notes(const unique_ptr<LibRaw> &processor, json &metadata)
{
    std::string make;
    if (processor->imgdata.idata.make[0])
        make = std::string(processor->imgdata.idata.make);

    std::string maker_key = make.empty() ? "Maker Notes" : fmt::format("Maker Notes ({})", make);

    json maker_notes = json::object();

    // Helper: add a maker-note field to the maker_notes JSON. If `force` is false and the value equals `ignore`, the
    // field is skipped. The stored object contains `value` and `string` keys.
    auto maker_add = [&](const std::string &name, const json &val, bool force = true, const json &ignore = json())
    {
        if (!force && val == ignore)
            return;
        json entry;
        entry["value"] = val;
        if (entry["value"].is_string())
            entry["string"] = entry["value"].get<std::string>();
        else
            entry["string"] = entry["value"].dump();
        maker_notes[name] = entry;
    };

    // helper function to move private tags from metadata to maker_notes
    auto move_private_tags = [](const std::vector<int> &tags, json &metadata, json &maker_notes)
    {
        for (int tagnum : tags)
        {
            for (auto it_ifd = metadata.begin(); it_ifd != metadata.end(); ++it_ifd)
            {
                if (!it_ifd->is_object())
                    continue;
                json                    &ifd_obj = *it_ifd;
                std::vector<std::string> keys_to_erase;
                for (auto it = ifd_obj.begin(); it != ifd_obj.end(); ++it)
                {
                    if (it.value().is_object() && it.value().contains("tag") && it.value()["tag"] == tagnum)
                    {
                        json entry = it.value();
                        if (!entry.contains("string"))
                            entry["string"] = entry.dump();
                        maker_notes[it.key()] = entry;
                        keys_to_erase.push_back(it.key());
                    }
                }
                for (const auto &k : keys_to_erase) ifd_obj.erase(k);
            }
        }
    };

    try
    {
        const auto &makernotes = processor->imgdata.makernotes;
        const auto &common     = makernotes.common;

        std::string lc_make = make;
        for (auto &c : lc_make) c = (char)std::tolower((unsigned char)c);

        // Common makernotes structure (humidity, pressure, etc.)
        maker_add("Humidity", common.exifHumidity, false, 0.0f);
        maker_add("Pressure", common.exifPressure, false, 0.0f);
        maker_add("Water Depth", common.exifWaterDepth, false, 0.0f);
        maker_add("Acceleration", common.exifAcceleration, false, 0.0f);
        maker_add("Camera Elevation Angle", common.exifCameraElevationAngle, false, 0.0f);

        // Lens-related makernotes
        {
            const auto &ln = processor->imgdata.lens;
            maker_add("Min Focal", ln.MinFocal, false, 0.0f);
            maker_add("Max Focal", ln.MaxFocal, false, 0.0f);
            maker_add("Max Ap 4 Min Focal", ln.MaxAp4MinFocal, false, 0.0f);
            maker_add("Max Ap 4 Max Focal", ln.MaxAp4MaxFocal, false, 0.0f);
            maker_add("EXIF Max Ap", ln.EXIF_MaxAp, false, 0.0f);
            maker_add("Lens Make", std::string(ln.LensMake), false, std::string());
            maker_add("Lens", std::string(ln.Lens), false, std::string());
            maker_add("Lens Serial", std::string(ln.LensSerial), false, std::string());
            maker_add("Internal Lens Serial", std::string(ln.InternalLensSerial), false, std::string());
            maker_add("Focal Length In 35mm Format", ln.FocalLengthIn35mmFormat, false, 0.0f);
        }
        {
            const auto &lnmn = processor->imgdata.lens.makernotes;
            maker_add("Lens ID", lnmn.LensID, false, (unsigned long long)(-1));
            maker_add("Lens", std::string(lnmn.Lens), false, std::string());
            maker_add("Lens Format", lnmn.LensFormat, false, 0);
            maker_add("Lens Mount", lnmn.LensMount, false, 0);
            maker_add("Cam ID", lnmn.CamID, false, 0ULL);
            maker_add("Camera Format", lnmn.CameraFormat, false, 0);
            maker_add("Camera Mount", lnmn.CameraMount, false, 0);
            maker_add("Body", std::string(lnmn.body), false, std::string());
            maker_add("Focal Type", lnmn.FocalType, false, 0);
            maker_add("Lens Features Pre", std::string(lnmn.LensFeatures_pre), false, std::string());
            maker_add("Lens Features Suf", std::string(lnmn.LensFeatures_suf), false, std::string());
            maker_add("Min Focal", lnmn.MinFocal, false, 0.0f);
            maker_add("Max Focal", lnmn.MaxFocal, false, 0.0f);
            maker_add("Max Ap 4 Min Focal", lnmn.MaxAp4MinFocal, false, 0.0f);
            maker_add("Max Ap 4 Max Focal", lnmn.MaxAp4MaxFocal, false, 0.0f);
            maker_add("Min Ap 4 Min Focal", lnmn.MinAp4MinFocal, false, 0.0f);
            maker_add("Min Ap 4 Max Focal", lnmn.MinAp4MaxFocal, false, 0.0f);
            maker_add("Max Ap", lnmn.MaxAp, false, 0.0f);
            maker_add("Min Ap", lnmn.MinAp, false, 0.0f);
            maker_add("Cur Focal", lnmn.CurFocal, false, 0.0f);
            maker_add("Cur Ap", lnmn.CurAp, false, 0.0f);
            maker_add("Max Ap 4 Cur Focal", lnmn.MaxAp4CurFocal, false, 0.0f);
            maker_add("Min Ap 4 Cur Focal", lnmn.MinAp4CurFocal, false, 0.0f);
            maker_add("Min Focus Distance", lnmn.MinFocusDistance, false, 0.0f);
            maker_add("Focus Range Index", lnmn.FocusRangeIndex, false, 0.0f);
            maker_add("Lens F Stops", lnmn.LensFStops, false, 0.0f);
            maker_add("Teleconverter ID", lnmn.TeleconverterID, false, 0ULL);
            maker_add("Teleconverter", std::string(lnmn.Teleconverter), false, std::string());
            maker_add("Adapter ID", lnmn.AdapterID, false, 0ULL);
            maker_add("Adapter", std::string(lnmn.Adapter), false, std::string());
            maker_add("Attachment ID", lnmn.AttachmentID, false, 0ULL);
            maker_add("Attachment", std::string(lnmn.Attachment), false, std::string());
            maker_add("Focal Units", lnmn.FocalUnits, false, 0);
            maker_add("Focal Length In 35mm Format", lnmn.FocalLengthIn35mmFormat, false, 0.0f);
        }
        // Vendor-specific lens makernotes
        if (lc_make.rfind("nikon", 0) == 0)
        {
            const auto &lnn = processor->imgdata.lens.nikon;
            maker_add("Effective Max Ap", lnn.EffectiveMaxAp);
            maker_add("Lens ID Number", lnn.LensIDNumber);
            maker_add("Lens F-Stops", lnn.LensFStops);
            maker_add("MCU Version", lnn.MCUVersion);
            maker_add("Lens Type", lnn.LensType);
        }
        if (lc_make.rfind("dng", 0) == 0)
        {
            const auto &lnd = processor->imgdata.lens.dng;
            maker_add("Max Ap 4 Max Focal", lnd.MaxAp4MaxFocal, false, 0.0f);
            maker_add("Max Ap 4 Min Focal", lnd.MaxAp4MinFocal, false, 0.0f);
            maker_add("Max Focal", lnd.MaxFocal, false, 0.0f);
            maker_add("Min Focal", lnd.MinFocal, false, 0.0f);
        }

        // shooting info
        {
            auto const &mn(processor->imgdata.shootinginfo);
            maker_add("Drive Mode", mn.DriveMode, false, -1);
            maker_add("Focus Mode", mn.FocusMode, false, -1);
            maker_add("Metering Mode", mn.MeteringMode, false, -1);
            maker_add("AF Point", mn.AFPoint, false, -1);
            maker_add("Exposure Mode", mn.ExposureMode, false, -1);
            maker_add("Image Stabilization", mn.ImageStabilization, false, -1);
            maker_add("Body Serial", std::string(mn.BodySerial), false, std::string());
            maker_add("Internal Body Serial", std::string(mn.InternalBodySerial), false, std::string());
        }

        if (lc_make.rfind("canon", 0) == 0)
        {
            const auto &mn = makernotes.canon;
            maker_add("Specular White Level", mn.SpecularWhiteLevel);
            maker_add("Channel Black Level", mn.ChannelBlackLevel);
            maker_add("Average Black Level", mn.AverageBlackLevel);
            maker_add("Metering Mode", mn.MeteringMode);
            maker_add("Spot Metering Mode", mn.SpotMeteringMode);
            maker_add("Flash Metering Mode", mn.FlashMeteringMode);
            maker_add("Flash Exposure Lock", mn.FlashExposureLock);
            maker_add("Exposure Mode", mn.ExposureMode);
            maker_add("AE Setting", mn.AESetting);
            maker_add("Image Stabilization", mn.ImageStabilization);
#if LIBRAW_VERSION < LIBRAW_MAKE_VERSION(0, 21, 0)
            maker_add("Highlight Tone Priority", mn.HighlightTonePriority);
            maker_add("Focus Mode", mn.FocusMode);
            maker_add("AF Point", mn.AFPoint);
            maker_add("Focus Continuous", mn.FocusContinuous);
            maker_add("AF Area Mode", mn.AFAreaMode);
            if (mn.AFAreaMode)
            {
                maker_add("Num AF Points", mn.NumAFPoints);
                maker_add("Valid AF Points", mn.ValidAFPoints);
                maker_add("AF Image Width", mn.AFImageWidth);
                maker_add("AF Image Height", mn.AFImageHeight);
            }
#endif
            maker_add("Flash Mode", mn.FlashMode);
            maker_add("Flash Activity", mn.FlashActivity);
            maker_add("Flash Bits", mn.FlashBits, false, 0);
            maker_add("Manual Flash Output", mn.ManualFlashOutput, false, 0);
            maker_add("Flash Output", mn.FlashOutput, false, 0);
            maker_add("Flash Guide Number", mn.FlashGuideNumber, false, 0);
            maker_add("Continuous Drive", mn.ContinuousDrive);
            maker_add("Sensor Width", mn.SensorWidth, false, 0);
            maker_add("Sensor Height", mn.SensorHeight, false, 0);
#if LIBRAW_VERSION_AT_LEAST_SNAPSHOT_202110
            maker_add("Sensor Left Border", mn.DefaultCropAbsolute.l, false, 0);
            maker_add("Sensor Top Border", mn.DefaultCropAbsolute.t, false, 0);
            maker_add("Sensor Right Border", mn.DefaultCropAbsolute.r, false, 0);
            maker_add("Sensor Bottom Border", mn.DefaultCropAbsolute.b, false, 0);
            maker_add("Black Mask Left Border", mn.LeftOpticalBlack.l, false, 0);
            maker_add("Black Mask Top Border", mn.LeftOpticalBlack.t, false, 0);
            maker_add("Black Mask Right Border", mn.LeftOpticalBlack.r, false, 0);
            maker_add("Black Mask Bottom Border", mn.LeftOpticalBlack.b, false, 0);
#else
            maker_add("Sensor Left Border", mn.DefaultCropAbsolute.l, 0);
            maker_add("Sensor Top Border", mn.DefaultCropAbsolute.t, 0);
            maker_add("Sensor Right Border", mn.DefaultCropAbsolute.r, 0);
            maker_add("Sensor Bottom Border", mn.DefaultCropAbsolute.b, 0);
            maker_add("Black Mask Left Border", mn.LeftOpticalBlack.l, 0);
            maker_add("Black Mask Top Border", mn.LeftOpticalBlack.t, 0);
            maker_add("Black Mask Right Border", mn.LeftOpticalBlack.r, 0);
            maker_add("Black Mask Bottom Border", mn.LeftOpticalBlack.b, 0);
#endif
            // Extra added with libraw 0.19:
            // unsigned int mn.multishot[4]
            maker_add("AF Micro Adj Mode", mn.AFMicroAdjMode, 0);
            maker_add("AF Micro Adj Value", mn.AFMicroAdjValue, 0.0f);
        }
        else if (lc_make.rfind("nikon", 0) == 0)
        {
            const auto &mn = makernotes.nikon;
            maker_add("Flash Exposure Bracket Value", mn.FlashExposureBracketValue, false, 0.f);
            maker_add("Active D Lighting", mn.ActiveDLighting);
            maker_add("Shooting Mode", mn.ShootingMode);
            maker_add("Image Stabilization", mn.ImageStabilization);
            maker_add("Vibration Reduction", mn.VibrationReduction, false, 0);
            maker_add("VR Mode", mn.VRMode);
#if LIBRAW_VERSION < LIBRAW_MAKE_VERSION(0, 21, 0)
            maker_add("Focus Mode", mn.FocusMode, false, 0);
            maker_add("AF Point", mn.AFPoint);
            maker_add("AF Points In Focus", mn.AFPointsInFocus, false, 0);
            maker_add("Contrast Detect AF", mn.ContrastDetectAF);
            maker_add("AF Area Mode", mn.AFAreaMode);
            maker_add("Phase Detect AF", mn.PhaseDetectAF);
            if (mn.PhaseDetectAF)
            {
                maker_add("Primary AF Point", mn.PrimaryAFPoint, false, 0);
            }
            if (mn.ContrastDetectAF)
            {
                maker_add("AF Image Width", mn.AFImageWidth, false, 0);
                maker_add("AF Image Height", mn.AFImageHeight, false, 0);
                maker_add("AF Area X Position", mn.AFAreaXPposition, false, 0);
                maker_add("AF Area Y Position", mn.AFAreaYPosition, false, 0);
                maker_add("AF Area Width", mn.AFAreaWidth, false, 0);
                maker_add("AF Area Height", mn.AFAreaHeight, false, 0);
                maker_add("Contrast Detect AF In Focus", mn.ContrastDetectAFInFocus, false, 0);
            }
#endif
            maker_add("Flash Setting", mn.FlashSetting, false, 0);
            maker_add("Flash Type", mn.FlashType, false, 0);
            maker_add("Flash Exposure Compensation", mn.FlashExposureCompensation);
            maker_add("External Flash Exposure Comp", mn.ExternalFlashExposureComp);
            maker_add("Flash Mode", mn.FlashMode);
            maker_add("Flash Source", mn.FlashSource);
            maker_add("Flash Firmware", mn.FlashFirmware);
            maker_add("External Flash Flags", mn.ExternalFlashFlags);
            maker_add("Flash Control Commander Mode", mn.FlashControlCommanderMode);
            maker_add("Flash Output And Compensation", mn.FlashOutputAndCompensation, false, 0);
            maker_add("Flash Focal Length", mn.FlashFocalLength, false, 0);
            maker_add("Flash GN Distance", mn.FlashGNDistance, false, 0);
            maker_add("Flash Group Control Mode", mn.FlashGroupControlMode);
            maker_add("Flash Group Output And Compensation", mn.FlashGroupOutputAndCompensation);
            maker_add("Flash Color Filter", mn.FlashColorFilter, false, 0);

            maker_add("NEF Compression", mn.NEFCompression, false, 0);
            maker_add("Exposure Mode", mn.ExposureMode, false, -1);
            maker_add("n ME shots", mn.nMEshots, false, 0);
            maker_add("ME gain On", mn.MEgainOn, false, 0);
            maker_add("ME WB", mn.ME_WB);
            maker_add("AF Fine Tune", mn.AFFineTune);
            maker_add("AF Fine Tune Index", mn.AFFineTuneIndex);
            maker_add("AF Fine Tune Adj", mn.AFFineTuneAdj);
        }
        else if (lc_make.rfind("olympus", 0) == 0)
        {
            const auto &mn = makernotes.olympus;
            maker_add("Sensor Calibration", mn.SensorCalibration);
            maker_add("Focus Mode", mn.FocusMode);
            maker_add("Auto Focus", mn.AutoFocus);
            maker_add("AF Point", mn.AFPoint);
            maker_add("AF Point Selected", mn.AFPointSelected);
            maker_add("AF Result", mn.AFResult);
            maker_add("Color Space", mn.ColorSpace);
            maker_add("AF Fine Tune", mn.AFFineTune);
            if (mn.AFFineTune)
                maker_add("AF Fine Tune Adj", mn.AFFineTuneAdj);
        }
        else if (lc_make.rfind("panasonic", 0) == 0)
        {
            const auto &mn = makernotes.panasonic;
            maker_add("Compression", mn.Compression);
            maker_add("Black Level Dim", mn.BlackLevelDim, false, 0);
            maker_add("Black Level", mn.BlackLevel);
        }
        else if (lc_make.rfind("pentax", 0) == 0)
        {
            const auto &mn = makernotes.pentax;
            maker_add("Focus Mode", mn.FocusMode);
            maker_add("AF Points In Focus", mn.AFPointsInFocus);
            maker_add("Drive Mode", mn.DriveMode);
            maker_add("AF Point Selected", mn.AFPointSelected);
            maker_add("Focus Position", mn.FocusPosition);
            maker_add("AF Adjustment", mn.AFAdjustment);
        }
        else if (lc_make.rfind("kodak", 0) == 0)
        {
            const auto &mn = makernotes.kodak;
            maker_add("Black Level Top", mn.BlackLevelTop);
            maker_add("Black Level Bottom", mn.BlackLevelBottom);
            maker_add("Offset Left", mn.offset_left);
            maker_add("Offset Top", mn.offset_top);
            maker_add("Clip Black", mn.clipBlack);
            maker_add("Clip White", mn.clipWhite);
        }
        else if (lc_make.rfind("fuji", 0) == 0)
        {
            const auto &mn = makernotes.fuji;
            maker_add("Expo Mid Point Shift", mn.ExpoMidPointShift);
            maker_add("Dynamic Range", mn.DynamicRange);
            maker_add("Film Mode", mn.FilmMode);
            maker_add("Dynamic Range Setting", mn.DynamicRangeSetting);
            maker_add("Development Dynamic Range", mn.DevelopmentDynamicRange);
            maker_add("Auto Dynamic Range", mn.AutoDynamicRange);

            maker_add("Focus Mode", mn.FocusMode);
            maker_add("AF Mode", mn.AFMode);
            maker_add("Focus Pixel", mn.FocusPixel);
            maker_add("Image Stabilization", mn.ImageStabilization);
            maker_add("Flash Mode", mn.FlashMode);
            maker_add("WB Preset", mn.WB_Preset);
            maker_add("Shutter Type", mn.ShutterType);
            maker_add("Exr Mode", mn.ExrMode);
            maker_add("Macro", mn.Macro);
            maker_add("Rating", mn.Rating);
#if LIBRAW_VERSION < LIBRAW_MAKE_VERSION(0, 21, 0)
            maker_add("Frame Rate", mn.FrameRate);
            maker_add("Frame Width", mn.FrameWidth);
            maker_add("Frame Height", mn.FrameHeight);
#endif
        }
        else if (lc_make.rfind("sony", 0) == 0)
        {
            const auto &mn = makernotes.sony;
            maker_add("Camera Type", mn.CameraType);
            maker_add("AF Micro Adj Value", mn.AFMicroAdjValue);
            maker_add("AF Micro Adj On", mn.AFMicroAdjOn);
            maker_add("AF Micro Adj Registered Lenses", mn.AFMicroAdjRegisteredLenses, false, 0);
            maker_add("Group 2010", mn.group2010);
            if (mn.real_iso_offset != 0xffff)
                maker_add("Real ISO Offset", mn.real_iso_offset);
            maker_add("Firmware", mn.firmware);
            maker_add("Image Count 3 Offset", mn.ImageCount3_offset);
            maker_add("Image Count 3", mn.ImageCount3, false, 0);
            if (mn.ElectronicFrontCurtainShutter == 0 || mn.ElectronicFrontCurtainShutter == 1)
                maker_add("Electronic Front Curtain Shutter", mn.ElectronicFrontCurtainShutter);
            maker_add("Metering Mode 2", mn.MeteringMode2, false, 0);
            if (mn.SonyDateTime[0])
                maker_add("Date Time", std::string(mn.SonyDateTime));
            maker_add("Shot Number Since Power Up", mn.ShotNumberSincePowerUp, false, 0);
        }

        // List of numeric Fuji and Sony private tag codes observed in the wild.
        const std::vector<int> private_tags = {// sony tags
                                               28672, 28673, 28688, 28689, 28704, 28721, 28722, 28724, 28725, 28726,
                                               28727, 29184, 29185, 29186, 29216, 29217, 29248, 29249, 29264, 29265,
                                               // fuji tags
                                               61441, 61442, 61443, 61444, 61445, 61446, 61447, 61448, 61449, 61450,
                                               61451, 61452, 61453, 61454, 61455, 61456};
        move_private_tags(private_tags, metadata, maker_notes);
    }
    catch (...)
    {
        spdlog::warn("Maker-notes extraction via LibRaw failed");
    }

    if (!maker_notes.empty())
        metadata[maker_key] = maker_notes;
}

// Robust display window logic adapted from OIIO
Box2i get_display_window(const libraw_data_t &idata)
{
    // int flip       = idata.sizes.flip;
    int crop_width = 0, crop_height = 0, crop_left = 0, crop_top = 0;
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 21, 0)
    if (idata.sizes.raw_inset_crops[0].cwidth != 0)
    {
        crop_width  = idata.sizes.raw_inset_crops[0].cwidth;
        crop_height = idata.sizes.raw_inset_crops[0].cheight;
        crop_left   = idata.sizes.raw_inset_crops[0].cleft;
        crop_top    = idata.sizes.raw_inset_crops[0].ctop;
    }
#else
    if (idata.sizes.raw_inset_crop.cwidth != 0)
    {
        crop_width  = idata.sizes.raw_inset_crop.cwidth;
        crop_height = idata.sizes.raw_inset_crop.cheight;
        crop_left   = idata.sizes.raw_inset_crop.cleft;
        crop_top    = idata.sizes.raw_inset_crop.ctop;
    }
#endif
    int image_width  = idata.sizes.iwidth;
    int image_height = idata.sizes.iheight;
    int left_margin  = idata.sizes.left_margin;
    int top_margin   = idata.sizes.top_margin;

    // Only use crop if width/height positive and less than image size
    if (crop_width > 0 && crop_height > 0 && crop_width <= image_width && crop_height <= image_height)
    {
        // If crop_left is undefined, assume central crop
        if (crop_left == 65535)
            crop_left = (image_width - crop_width) / 2;
        // If crop_top is undefined, assume central crop
        if (crop_top == 65535)
            crop_top = (image_height - crop_height) / 2;

        // // Apply flip corrections
        // if (flip & 1)
        //     crop_left = image_width - crop_width - crop_left;
        // if (flip & 2)
        //     crop_top = image_height - crop_height - crop_top;

        // Subtract margins if crop is within them
        if (crop_top >= top_margin && crop_left >= left_margin)
        {
            crop_top -= top_margin;
            crop_left -= left_margin;

            // Swap axes if flip & 4
            // if (flip & 4)
            // {
            //     std::swap(crop_left, crop_top);
            //     std::swap(crop_width, crop_height);
            //     std::swap(image_width, image_height);
            // }

            // Only use crop if it fits within the image
            if (crop_left >= 0 && crop_top >= 0 && crop_left + crop_width <= image_width &&
                crop_top + crop_height <= image_height)
            {
                spdlog::debug("Using RAW crop window: left={}, top={}, width={}, height={}", crop_left, crop_top,
                              crop_width, crop_height);
                return Box2i{{crop_left, crop_top}, {crop_left + crop_width, crop_top + crop_height}};
            }
        }
    }
    return Box2i{{0, 0}, {idata.sizes.iwidth, idata.sizes.iheight}};
}

} // namespace

bool is_raw_image(std::istream &is) noexcept
{
    try
    {
        unique_ptr<LibRaw> raw(new LibRaw());
        LibRawIStream      ds(is);

        auto ret = raw->open_datastream(&ds) == LIBRAW_SUCCESS;

        is.clear();
        is.seekg(0);
        return ret;
    }
    catch (...)
    {
        is.clear();
        is.seekg(0);
        return false;
    }
}

vector<ImagePtr> load_raw_image(std::istream &is, string_view filename, const ImageLoadOptions &opts)
{
    ScopedMDC mdc{"IO", "RAW"};

    // Create and configure LibRaw processor (use heap allocation for thread-safe version)
    unique_ptr<LibRaw> processor;

    {
        // See https://github.com/AcademySoftwareFoundation/OpenImageIO/issues/2630
        // Something inside LibRaw constructor is not thread safe. Use a
        // static mutex here to make sure only one thread is constructing a
        // LibRaw at a time. Cross fingers and hope all the rest of LibRaw
        // is re-entrant.
        static std::mutex           libraw_ctr_mutex;
        std::lock_guard<std::mutex> lock(libraw_ctr_mutex);
        processor.reset(new LibRaw());
    }

    // Set up EXIF callback handler to extract metadata
    ExifContext exif_ctx; // Constructor creates and initializes libexif structures
    processor->set_exifparser_handler(exif_handler, &exif_ctx);

    // Set processing parameters
    processor->imgdata.params.use_camera_matrix = 1; // Use camera color matrix
    processor->imgdata.params.use_camera_wb     = 1; // Use camera white balance
    processor->imgdata.params.use_auto_wb       = 0;
    processor->imgdata.params.no_auto_bright    = 1;   // Prevent exposure scaling
    processor->imgdata.params.gamm[0]           = 1.0; // Keep linear output
    processor->imgdata.params.gamm[1]           = 1.0;
    processor->imgdata.params.highlight         = 0;  // Disable highlight recovery / compression
    processor->imgdata.params.output_bps        = 16; // Full precision
    processor->imgdata.params.user_qual         = 3;  // demosaic algorithm/quality:
                                                      // 0 - linear
                                                      // 1 - VNG
                                                      // 2 - PPG
                                                      // 3 - AHD
                                                      // 4 - DCB
                                                      // 11 - DHT
                                                      // 12 - AAHD
    processor->imgdata.params.output_color = 1;       // Output color space (camera → XYZ → output)
                                                      // 0 Raw color (unique to each camera)
                                                      // 1 sRGB D65 (default)
                                                      // 2 Adobe RGB (1998) D65
                                                      // 3 Wide Gamut RGB D65
                                                      // 4 Kodak ProPhoto RGB D65
                                                      // 5 XYZ
                                                      // 6 ACES
                                                      // 7 DCI-P3
                                                      // 8 Rec2020

    // Create custom datastream from std::istream
    LibRawIStream libraw_stream(is);

    // Open the RAW file using datastream (avoids loading entire file into memory)
    if (auto ret = processor->open_datastream(&libraw_stream); ret != LIBRAW_SUCCESS)
        throw std::runtime_error(fmt::format("Failed to open RAW file: {}", libraw_strerror(ret)));

    add_maker_notes(processor, exif_ctx.metadata);

    // Attach EXIF/XMP metadata (if present) to all images (thumbnails + main)
    if (!exif_ctx.metadata.empty())
        spdlog::debug("Successfully extracted EXIF metadata");
    else
        spdlog::debug("No EXIF metadata extracted from RAW file");

    // Access image data directly from imgdata
    auto &idata = processor->imgdata;

    // Create HDRView image with oriented dimensions
    std::vector<ImagePtr> images;

    ImGuiTextFilter filter{opts.channel_selector.c_str()};
    filter.Build();

    auto &sizes = idata.sizes;

    if (filter.PassFilter("main"))
    {
        try
        {
            // Unpack the RAW data
            if (auto ret = processor->unpack(); ret != LIBRAW_SUCCESS)
                throw std::runtime_error(fmt::format("Failed to unpack RAW data: {}", libraw_strerror(ret)));

            // Now process the full image (demosaic, white balance, etc.)
            if (auto ret = processor->dcraw_process(); ret != LIBRAW_SUCCESS)
                throw std::runtime_error(fmt::format("Failed to process RAW image: {}", libraw_strerror(ret)));

            // Use iwidth/iheight for the processed image dimensions
            int2 size{sizes.iwidth, sizes.iheight};
            int  num_channels = 3; // Force RGB

            // Verify we have image data
            if (!idata.image)
                throw std::runtime_error("No image data available after processing");

            auto image                = std::make_shared<Image>(size, num_channels);
            image->filename           = filename;
            image->partname           = "main";
            image->metadata["loader"] = "LibRaw";
            if (!exif_ctx.metadata.empty())
                image->metadata["exif"] = exif_ctx.metadata;

            // Access image data as array of ushort[4]
            const auto *img_data = idata.image;

            // Allocate float buffer for all pixels
            vector<float> float_pixels((size_t)size.x * size.y * num_channels);

            // Convert from 16-bit to float [0,1] with flip handling
            // We include an ad-hoc scale factor here to make the exposure match the DNG preview better
            constexpr float scale = 2.0f / 65535.0f;

            stp::parallel_for(stp::blocked_range<int>(0, size.x * size.y, 1024),
                              [&](int begin, int end, int unit_index, int thread_index)
                              {
                                  for (int i = begin; i < end; ++i)
                                  {
                                      for (int c = 0; c < num_channels; ++c)
                                          float_pixels[i * num_channels + c] = img_data[i][c] * scale;
                                  }
                              });

            string profile_desc = color_profile_name(ColorGamut_Unspecified, TransferFunction::Unspecified);
            if (opts.override_profile)
            {
                spdlog::info("Ignoring embedded color profile with user-specified profile: {} {}",
                             color_gamut_name(opts.gamut_override), transfer_function_name(opts.tf_override));

                Chromaticities chr;
                if (linearize_pixels(float_pixels.data(), int3{size, num_channels},
                                     gamut_chromaticities(opts.gamut_override), opts.tf_override, opts.keep_primaries,
                                     &profile_desc, &chr))
                {
                    image->chromaticities = chr;
                    profile_desc += " (override)";
                }
            }
            else
            {
                Chromaticities chr;
                // We configured LibRaw to output to linear sRGB above
                if (linearize_pixels(float_pixels.data(), int3{size, num_channels},
                                     gamut_chromaticities(ColorGamut_sRGB_BT709), TransferFunction::Linear,
                                     opts.keep_primaries, &profile_desc, &chr))
                    image->chromaticities = chr;
            }
            image->metadata["color profile"] = profile_desc;

            // Copy data to image channels
            for (int c = 0; c < num_channels; ++c)
                image->channels[c].copy_from_interleaved(float_pixels.data(), size.x, size.y, num_channels, c,
                                                         [](float v) { return v; });

            // Set display window using LibRaw crop info
            image->display_window = get_display_window(idata);

            images.push_back(image);
        }
        catch (const std::exception &e)
        {
            throw std::runtime_error(fmt::format("Error processing RAW image: {}", e.what()));
        }
    }
    else
        spdlog::debug("Skipping main RAW image (filtered out by channel selector '{}')", opts.channel_selector);

    for (int ti = 0; ti < idata.thumbs_list.thumbcount; ti++)
    {
        string name = fmt::format("thumbnail:{}", ti);

        if (!filter.PassFilter(&name[0], &name[0] + name.size()))
        {
            spdlog::debug("Skipping thumbnail image {}: '{}' (filtered out by channel selector '{}')", ti, name,
                          opts.channel_selector);
            continue;
        }

        int tret = processor->unpack_thumb_ex(ti);
        if (tret != LIBRAW_SUCCESS)
            break; // no more thumbnails or error

        int                       err   = 0;
        libraw_processed_image_t *thumb = processor->dcraw_make_mem_thumb(&err);
        if (!thumb)
            continue;

        const auto guard = ScopeGuard([&]() { LibRaw::dcraw_clear_mem(thumb); });

        try
        {
            if (thumb->type == LIBRAW_IMAGE_JPEG)
            {
                std::string        s(reinterpret_cast<char *>(thumb->data), thumb->data_size);
                std::istringstream iss(s);
                auto               thumbs = load_jpg_image(iss, fmt::format("{}:thumb{}", filename, ti), opts);
                for (auto &ti_img : thumbs)
                {
                    ti_img->partname                           = fmt::format("thumbnail:{}", ti);
                    ti_img->metadata["loader"]                 = "LibRaw";
                    ti_img->metadata["header"]["Is thumbnail"] = {
                        {"value", true},
                        {"string", "Yes"},
                        {"type", "bool"},
                        {"description", "Indicates this image is a thumbnail"}};
                    if (!exif_ctx.metadata.empty())
                        ti_img->metadata["exif"] = exif_ctx.metadata;
                    images.push_back(ti_img);
                }
            }
            else if (thumb->type == LIBRAW_IMAGE_BITMAP)
            {
                int w = thumb->width;
                int h = thumb->height;
                int n = thumb->colors;

                auto timg                                = std::make_shared<Image>(int2{w, h}, n);
                timg->filename                           = filename;
                timg->partname                           = fmt::format("thumbnail:{}", ti);
                timg->metadata["pixel format"]           = fmt::format("{}-bit ({} bpc)", n * thumb->bits, thumb->bits);
                timg->metadata["loader"]                 = "LibRaw";
                timg->metadata["header"]["Is thumbnail"] = {{"value", true},
                                                            {"string", "Yes"},
                                                            {"type", "bool"},
                                                            {"description", "Indicates this image is a thumbnail"}};
                if (!exif_ctx.metadata.empty())
                    timg->metadata["exif"] = exif_ctx.metadata;

                // Load interleaved bytes/shorts into a float buffer, then linearize
                std::vector<float> float_pixels((size_t)w * h * n);
                auto               data8  = reinterpret_cast<uint8_t *>(thumb->data);
                auto               data16 = reinterpret_cast<uint16_t *>(thumb->data);
                stp::parallel_for(stp::blocked_range<int>(0, w * h, 1024),
                                  [&](int begin, int end, int unit_index, int thread_index)
                                  {
                                      for (int i = begin; i < end; ++i)
                                      {
                                          for (int c = 0; c < n; ++c)
                                              float_pixels[i * n + c] = (thumb->bits == 8)
                                                                            ? data8[i * n + c] / 255.0f
                                                                            : data16[i * n + c] / 65535.0f;
                                      }
                                  });

                // Apply sRGB->linear correction to bitmap thumbnails
                string profile_desc = color_profile_name(ColorGamut_Unspecified, TransferFunction::sRGB);
                if (opts.override_profile)
                {
                    spdlog::info("Ignoring embedded color profile with user-specified profile: {} {}",
                                 color_gamut_name(opts.gamut_override), transfer_function_name(opts.tf_override));

                    Chromaticities chr;
                    if (linearize_pixels(float_pixels.data(), int3{w, h, n}, gamut_chromaticities(opts.gamut_override),
                                         opts.tf_override, opts.keep_primaries, &profile_desc, &chr))
                    {
                        timg->chromaticities = chr;
                        profile_desc += " (override)";
                    }
                }
                else
                {
                    Chromaticities chr;
                    // LibRaw bitmap thumbnails are in sRGB color space
                    if (linearize_pixels(float_pixels.data(), int3{w, h, n},
                                         gamut_chromaticities(ColorGamut_sRGB_BT709), TransferFunction::sRGB,
                                         opts.keep_primaries, &profile_desc, &chr))
                        timg->chromaticities = chr;
                }
                timg->metadata["color profile"] = profile_desc;

                for (int c = 0; c < n; ++c)
                    timg->channels[c].copy_from_interleaved<float>(float_pixels.data(), w, h, n, c,
                                                                   [](float v) { return v; });

                images.push_back(timg);
            }
        }
        catch (const std::exception &e)
        {
            spdlog::warn("Error loading thumbnail {}: {}", ti, e.what());
        }
    }

    return images;
}

#endif
