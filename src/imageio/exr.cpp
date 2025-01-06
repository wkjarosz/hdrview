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
#include <ImfChannelListAttribute.h>
#include <ImfChromaticities.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfInputPart.h>
#include <ImfMultiPartInputFile.h>
#include <ImfOutputFile.h>
#include <ImfRgbaYca.h>
#include <ImfStandardAttributes.h>
#include <ImfTestFile.h> // for isOpenExrFile
#include <fstream>
#include <stdexcept> // for runtime_error, out_of_range

#include "Imath_to_linalg.h"

using namespace std;

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
        auto &img  = *images.back();
        img.header = part.header();

        for (auto a = begin(img.header); a != end(img.header); ++a) spdlog::debug("Attribute: {}", a.name());

        if (img.header.hasName())
            img.partname = img.header.name();

        // OpenEXR library's boxes include the max element, our boxes don't, so we increment by 1
        img.data_window    = {{dataWindow.min.x, dataWindow.min.y}, {dataWindow.max.x + 1, dataWindow.max.y + 1}};
        img.display_window = {{displayWindow.min.x, displayWindow.min.y},
                              {displayWindow.max.x + 1, displayWindow.max.y + 1}};

        if (img.data_window.is_empty())
            throw invalid_argument{fmt::format("EXR image has invalid data window: [{},{}] - [{},{}]",
                                               img.data_window.min.x, img.data_window.min.y, img.data_window.max.x,
                                               img.data_window.max.y)};

        if (img.display_window.is_empty())
            throw invalid_argument{fmt::format("EXR image has invalid display window: [{},{}] - [{},{}]",
                                               img.display_window.min.x, img.display_window.min.y,
                                               img.display_window.max.x, img.display_window.max.y)};

        const auto &channels = img.header.channels();

        Imf::FrameBuffer framebuffer;
        for (auto c = channels.begin(); c != channels.end(); ++c)
        {
            string name = c.name();

            img.channels.emplace_back(name, size);
            framebuffer.insert(c.name(), Imf::Slice::Make(Imf::FLOAT, img.channels.back().data(), dataWindow, 0, 0,
                                                          c.channel().xSampling, c.channel().ySampling));
        }

        part.setFrameBuffer(framebuffer);
        part.readPixels(dataWindow.min.y, dataWindow.max.y);

        // now up-res any subsampled channels
        // FIXME: OpenEXR v3.3.0 and above seems to break this subsample channel loading
        // see https://github.com/AcademySoftwareFoundation/openexr/issues/1949
        // Until that is fixed in the next release, we are sticking with v3.2.4
        int i = 0;
        for (auto c = img.header.channels().begin(); c != img.header.channels().end(); ++c, ++i)
        {
            int xs = c.channel().xSampling;
            int ys = c.channel().ySampling;
            if (xs == 1 && ys == 1)
                continue;

            spdlog::warn("EXR channel '{}' is subsampled ({},{}). Only rudimentary subsampling is supported.", c.name(),
                         xs, ys);
            Array2Df tmp = img.channels[i];

            int subsampled_width = size.x / xs;
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x) img.channels[i]({x, y}) = tmp(x / xs + (y / ys) * subsampled_width);
        }

        if (Imf::hasChromaticities(img.header))
        {
            img.luminance_weights = to_linalg(Imf::RgbaYca::computeYw(Imf::chromaticities(img.header)));
            spdlog::debug("Yw = {}", img.luminance_weights);
        }

        static const Imf::Chromaticities rec709_cr{}; // default rec709 (sRGB) primaries
        Imath::M44f                      M;
        if (color_conversion_matrix(M, img.header, rec709_cr))
        {
            img.M_to_Rec709 = to_linalg(M);
            // img.luminance_weights = to_linalg(Imf::RgbaYca::computeYw(rec709_cr));
            img.luminance_weights = to_linalg(Imf::RgbaYca::computeYw(Imf::chromaticities(img.header)));
            spdlog::info("Converting pixel values to Rec. 709/sRGB primaries and whitepoint.");
            spdlog::debug("M_to_Rec709 = {}", img.M_to_Rec709);
        }
    }
    return images;
}

bool save_exr_image(const Image &img, ostream &os_, const string &filename)
{
    try
    {
        // OpenEXR expects the display window to be inclusive, while our images are exclusive
        auto displayWindow = Imath::Box2i(Imath::V2i(img.display_window.min.x, img.display_window.min.y),
                                          Imath::V2i(img.display_window.max.x - 1, img.display_window.max.y - 1));
        auto dataWindow    = Imath::Box2i(Imath::V2i(img.data_window.min.x, img.data_window.min.y),
                                          Imath::V2i(img.data_window.max.x - 1, img.data_window.max.y - 1));

        // start with the header we have from reading in the file
        Imf::Header header = img.header;
        header.insert("channels", Imf::ChannelListAttribute());
        header.displayWindow() = displayWindow;
        header.dataWindow()    = dataWindow;

        Imf::FrameBuffer frameBuffer;

        for (int g = 0; g < (int)img.groups.size(); ++g)
        {
            auto &group = img.groups[g];
            if (!group.visible)
                continue;

            for (int c = 0; c < group.num_channels; ++c)
            {
                auto &channel = img.channels[group.channels[c]];
                header.channels().insert(channel.name, Imf::Channel(Imf::FLOAT));

                // OpenEXR expects the base address to point to the display window origin, while our channels only store
                // pixels for the data_window. The Slice::Make function below does the heavy lifting of computing the
                // base pointer for a slice
                frameBuffer.insert(channel.name, Imf::Slice::Make(Imf::FLOAT, channel.data(), dataWindow));
            }
        }

        auto            os = StdOStream{os_, filename.c_str()};
        Imf::OutputFile file{os, header};
        file.setFrameBuffer(frameBuffer);
        file.writePixels(img.data_window.size().y);
        return true;
    }
    catch (const exception &e)
    {
        spdlog::error("Unable to write exr image file \"{}\": {}", filename, e.what());
        return false;
    }
}
