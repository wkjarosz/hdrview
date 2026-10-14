//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

// Every library HDRView links has to be the one whose headers it was compiled against. An installed copy of a
// library the build also bundles can lend the build its headers, and a version apart from the linked library's
// lays out its structs differently.

#include <doctest/doctest.h>

#include <spdlog/fmt/fmt.h>

#include <ImfMisc.h>
#include <OpenEXRConfig.h>
#include <png.h>
#include <zlib.h>

#if HDRVIEW_ENABLE_LIBRAW
#include <libraw/libraw.h>
#endif
#if HDRVIEW_ENABLE_LIBHEIF
#include <libheif/heif.h>
#endif
#if HDRVIEW_ENABLE_LIBTIFF
#include <tiffio.h>
#include <tiffvers.h>
#endif
#if HDRVIEW_ENABLE_LCMS2
#include <lcms2.h>
#endif
#if HDRVIEW_ENABLE_LIBJXL
#include <jxl/decode.h>
#include <jxl/version.h>
#endif
#if HDRVIEW_ENABLE_J2K
#include <openjpeg.h>
#endif

#include <string>
#include <vector>

TEST_CASE("Every linked library is the version its headers say")
{
    struct Library
    {
        const char *name;
        std::string compiled, linked;
    };
    std::vector<Library> libraries{
        {"OpenEXR", OPENEXR_VERSION_STRING, Imf::getLibraryVersion()},
        {"libpng", std::to_string(PNG_LIBPNG_VER), std::to_string(png_access_version_number())},
        {"zlib", ZLIB_VERSION, zlibVersion()},
#if HDRVIEW_ENABLE_LIBRAW
        {"LibRaw", LIBRAW_VERSION_STR, LibRaw::version()},
#endif
#if HDRVIEW_ENABLE_LIBHEIF
        {"libheif", std::to_string(LIBHEIF_NUMERIC_VERSION), std::to_string(heif_get_version_number())},
#endif
#if HDRVIEW_ENABLE_LIBTIFF
        {"libtiff", TIFFLIB_VERSION_STR, TIFFGetVersion()},
#endif
#if HDRVIEW_ENABLE_LCMS2
        {"lcms2", std::to_string(LCMS_VERSION), std::to_string(cmsGetEncodedCMMversion())},
#endif
#if HDRVIEW_ENABLE_LIBJXL
        // JxlDecoderVersion() encodes 1.2.3 as 1002003
        {"libjxl", std::to_string(JPEGXL_MAJOR_VERSION * 1000000 + JPEGXL_MINOR_VERSION * 1000 + JPEGXL_PATCH_VERSION),
         std::to_string(JxlDecoderVersion())},
#endif
#if HDRVIEW_ENABLE_J2K
        {"OpenJPEG", fmt::format("{}.{}.{}", OPJ_VERSION_MAJOR, OPJ_VERSION_MINOR, OPJ_VERSION_BUILD), opj_version()},
#endif
    };

    for (const auto &lib : libraries)
    {
        INFO("library = ", std::string(lib.name));
        CHECK(lib.compiled == lib.linked);
    }
}
