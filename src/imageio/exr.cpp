//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "colorspace.h"
#include "common.h" // for lerp, mod, clamp, getExtension
#include "exr_std_streams.h"
#include "image.h"
#include "texture.h"
#include "timer.h"
#include <ImfChannelList.h>
#include <ImfChromaticities.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfInputPart.h>
#include <ImfMultiPartInputFile.h>
#include <ImfRgbaYca.h>
#include <ImfStandardAttributes.h>
#include <ImfTestFile.h> // for isOpenExrFile
#include <fstream>
#include <stdexcept> // for runtime_error, out_of_range

#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>

#include "imageio/pfm.h"
#include "imageio/stb.h"
#include "imageio/uhdr.h"

using namespace std;

// static methods and member definitions
//

const float3 Image::Rec709_luminance_weights = float3{&Imf::RgbaYca::computeYw(Imf::Chromaticities{})[0]};

//
// end static methods and member definitions

bool is_exr_image(istream &is_, const string &filename) noexcept
{
    auto is = StdIStream{is_, filename.c_str()};
    return Imf::isOpenExrFile(is);
}

vector<ImagePtr> load_exr_image(istream &is_, const string &filename)
{
    auto is = StdIStream{is_, filename.c_str()};

    Imf::MultiPartInputFile infile{is};

    if (infile.parts() <= 0)
        throw invalid_argument{"EXR file contains no parts!"};

    vector<ImagePtr> images;
    for (int p = 0; p < infile.parts(); ++p)
    {
        Imf::InputPart part{infile, p};

        Imath::Box2i dataWindow    = part.header().dataWindow();
        Imath::Box2i displayWindow = part.header().displayWindow();
        int2         size          = {dataWindow.max.x - dataWindow.min.x + 1, dataWindow.max.y - dataWindow.min.y + 1};

        if (size.x <= 0 || size.y <= 0)
        {
            spdlog::warn("EXR part {}: '{}' has zero pixels, skipping...", p,
                         part.header().hasName() ? part.header().name() : "unnamed");
            continue;
        }

        images.emplace_back(make_shared<Image>());
        auto &data = *images.back();

        if (part.header().hasName())
            data.partname = part.header().name();
        if (auto owner = part.header().findTypedAttribute<Imf::StringAttribute>("owner"))
            data.owner = owner->value();
        if (auto comments = part.header().findTypedAttribute<Imf::StringAttribute>("comments"))
            data.comments = comments->value();
        if (auto capture_date = part.header().findTypedAttribute<Imf::StringAttribute>("capDate"))
            data.capture_date = capture_date->value();

        // OpenEXR library's boxes include the max element, our boxes don't, so we increment by 1
        data.data_window    = {{dataWindow.min.x, dataWindow.min.y}, {dataWindow.max.x + 1, dataWindow.max.y + 1}};
        data.display_window = {{displayWindow.min.x, displayWindow.min.y},
                               {displayWindow.max.x + 1, displayWindow.max.y + 1}};

        if (data.data_window.is_empty())
            throw invalid_argument{fmt::format("EXR image has invalid data window: [{},{}] - [{},{}]",
                                               data.data_window.min.x, data.data_window.min.y, data.data_window.max.x,
                                               data.data_window.max.y)};

        if (data.display_window.is_empty())
            throw invalid_argument{fmt::format("EXR image has invalid display window: [{},{}] - [{},{}]",
                                               data.display_window.min.x, data.display_window.min.y,
                                               data.display_window.max.x, data.display_window.max.y)};

        const auto &channels = part.header().channels();

        Imf::FrameBuffer framebuffer;
        for (auto c = channels.begin(); c != channels.end(); ++c)
        {
            string name = c.name();

            data.channels.emplace_back(name, size);
            framebuffer.insert(c.name(), Imf::Slice::Make(Imf::FLOAT, data.channels.back().data(), dataWindow, 0, 0,
                                                          c.channel().xSampling, c.channel().ySampling));
        }

        part.setFrameBuffer(framebuffer);
        part.readPixels(dataWindow.min.y, dataWindow.max.y);

        // now up-res any subsampled channels
        // FIXME: OpenEXR v3.3.0 and above seems to break this subsample channel loading
        // see https://github.com/AcademySoftwareFoundation/openexr/issues/1949
        // Until that is fixed in the next release, we are sticking with v3.2.4
        int i = 0;
        for (auto c = part.header().channels().begin(); c != part.header().channels().end(); ++c, ++i)
        {
            int xs = c.channel().xSampling;
            int ys = c.channel().ySampling;
            if (xs == 1 && ys == 1)
                continue;

            spdlog::warn("EXR channel '{}' is subsampled ({},{}). Only rudimentary subsampling is supported.", c.name(),
                         xs, ys);
            Array2Df tmp = data.channels[i];

            int subsampled_width = size.x / xs;
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x) data.channels[i]({x, y}) = tmp(x / xs + (y / ys) * subsampled_width);
        }

        if (Imf::hasWhiteLuminance(part.header()))
            spdlog::debug("File has white luminance info.");
        else
            spdlog::debug("File does NOT have white luminance info.");

        // If the file specifies a chromaticity attribute, we'll need to convert to sRGB/Rec709.
        if (Imf::hasChromaticities(part.header()))
        {
            Imf::Chromaticities rec709_cr; // default rec709 (sRGB) primaries
            Imf::Chromaticities file_cr = Imf::chromaticities(part.header());
            if (file_cr != rec709_cr)
            {
                // Imath matrices multiply row vectors to their left, so are read from left-to-right.
                // This transforms from the file's RGB to Rec.709 RGB (via XYZ)
                Imath::M44f M = Imf::RGBtoXYZ(file_cr, 1) * Imf::XYZtoRGB(rec709_cr, 1);

                for (int m = 0; m < 4; ++m)
                    for (int n = 0; n < 4; ++n) data.M_to_Rec709[m][n] = M.x[m][n];

                spdlog::info("Converting pixel values to Rec. 709/sRGB primaries and whitepoint.");
            }

            data.luminance_weights = float3{&Imf::RgbaYca::computeYw(file_cr)[0]};

            spdlog::debug("M_to_Rec709 = {}", data.M_to_Rec709);
            spdlog::debug("Yw = {}", data.luminance_weights);
        }
    }
    return images;
}
