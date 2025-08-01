//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "colorspace.h"
#include "common.h" // for lerp, mod, clamp, getExtension
#include "exr_header.h"
#include "exr_std_streams.h"
#include "image.h"
#include "imgui.h"
#include "texture.h"
#include "timer.h"
#include <ImfChannelList.h>
#include <ImfChannelListAttribute.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfInputPart.h>
#include <ImfMultiPartInputFile.h>
#include <ImfOutputFile.h>
#include <ImfStandardAttributes.h>
#include <ImfTestFile.h> // for isOpenExrFile
#include <ImfVersion.h>
#include <fstream>
#include <stdexcept> // for runtime_error, out_of_range

#include "Imath_to_linalg.h"

using namespace std;

bool is_exr_image(istream &is_, string_view filename) noexcept
{
    auto is = StdIStream{is_, string(filename).c_str()};
    return Imf::isOpenExrFile(is);
}

vector<ImagePtr> load_exr_image(istream &is_, string_view filename, string_view channel_selector)
{
    auto is = StdIStream{is_, string(filename).c_str()};

    Imf::MultiPartInputFile infile{is};

    if (infile.parts() <= 0)
        throw invalid_argument{"EXR file contains no parts!"};

    ImGuiTextFilter filter{string(channel_selector).c_str()};
    filter.Build();

    vector<ImagePtr> images;
    for (int p = 0; p < infile.parts(); ++p)
    {
        Imf::InputPart part{infile, p};

        auto channel_name = [&](Imf::ChannelList::ConstIterator c)
        {
            string name = c.name();
            if (part.header().hasName())
                name = part.header().name() + "."s + name;
            return name;
        };

        const auto &channels = part.header().channels();

        Imath::Box2i dataWindow    = part.header().dataWindow();
        Imath::Box2i displayWindow = part.header().displayWindow();
        int2         size          = {dataWindow.max.x - dataWindow.min.x + 1, dataWindow.max.y - dataWindow.min.y + 1};

        if (size.x <= 0 || size.y <= 0)
        {
            spdlog::warn("EXR part {}: '{}' has zero pixels, skipping...", p,
                         part.header().hasName() ? part.header().name() : "unnamed");
            continue;
        }

        auto img = make_shared<Image>();
        if (auto a = part.header().findTypedAttribute<Imf::ChromaticitiesAttribute>("chromaticities"))
            img->chromaticities = {{a->value().red.x, a->value().red.y},
                                   {a->value().green.x, a->value().green.y},
                                   {a->value().blue.x, a->value().blue.y},
                                   {a->value().white.x, a->value().white.y}};
        img->metadata["loader"] = "OpenEXR";
        img->metadata["header"] = exr_header_to_json(part.header());

        img->metadata["header"]["version"] = {
            {"type", "version"},
            {"string", fmt::format("{}, flags 0x{:x}", Imf::getVersion(part.version()), Imf::getFlags(part.version()))},
            {"version", Imf::getVersion(part.version())},
            {"flags", fmt::format("0x{:x}", Imf::getFlags(part.version()))}};

        // spdlog::debug("exr header: {}", img->metadata["header"].dump(2));

        if (part.header().hasName())
            img->partname = part.header().name();

        // OpenEXR library's boxes include the max element, our boxes don't, so we increment by 1
        img->data_window    = {{dataWindow.min.x, dataWindow.min.y}, {dataWindow.max.x + 1, dataWindow.max.y + 1}};
        img->display_window = {{displayWindow.min.x, displayWindow.min.y},
                               {displayWindow.max.x + 1, displayWindow.max.y + 1}};

        if (img->data_window.is_empty())
            throw invalid_argument{fmt::format("EXR image has invalid data window: [{},{}] - [{},{}]",
                                               img->data_window.min.x, img->data_window.min.y, img->data_window.max.x,
                                               img->data_window.max.y)};

        if (img->display_window.is_empty())
            throw invalid_argument{fmt::format("EXR image has invalid display window: [{},{}] - [{},{}]",
                                               img->display_window.min.x, img->display_window.min.y,
                                               img->display_window.max.x, img->display_window.max.y)};

        Imf::FrameBuffer framebuffer;
        bool             has_channels = false;
        for (auto c = channels.begin(); c != channels.end(); ++c)
        {
            auto name = channel_name(c);
            if (!filter.PassFilter(&name[0], &name[0] + name.size()))
            {
                spdlog::debug("Skipping EXR channel '{}' in part {}: '{}'", name, p);
                continue;
            }
            else
            {
                has_channels = true;
                spdlog::debug("Loading EXR channel '{}' in part {}: '{}'", name, p, c.name());
            }

            name = c.name();

            img->channels.emplace_back(name, size);
            framebuffer.insert(c.name(), Imf::Slice::Make(Imf::FLOAT, img->channels.back().data(), dataWindow, 0, 0,
                                                          c.channel().xSampling, c.channel().ySampling));
        }

        if (!has_channels)
        {
            spdlog::debug("EXR part {}: '{}' has no channels matching the filter '{}', skipping...", p,
                          part.header().hasName() ? part.header().name() : "unnamed", channel_selector);
            continue;
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
            Array2Df tmp = img->channels[i];

            int subsampled_width = size.x / xs;
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x) img->channels[i]({x, y}) = tmp(x / xs + (y / ys) * subsampled_width);
        }

        images.emplace_back(img);
    }
    return images;
}

void save_exr_image(const Image &img, ostream &os_, string_view filename)
{
    try
    {
        Timer timer;
        // OpenEXR expects the display window to be inclusive, while our images are exclusive
        auto displayWindow = Imath::Box2i(Imath::V2i(img.display_window.min.x, img.display_window.min.y),
                                          Imath::V2i(img.display_window.max.x - 1, img.display_window.max.y - 1));
        auto dataWindow    = Imath::Box2i(Imath::V2i(img.data_window.min.x, img.data_window.min.y),
                                          Imath::V2i(img.data_window.max.x - 1, img.data_window.max.y - 1));

        Imf::Header header;
        if (img.chromaticities)
            header.insert(
                "chromaticities",
                Imf::ChromaticitiesAttribute{{Imath::V2f(img.chromaticities->red.x, img.chromaticities->red.y),
                                              Imath::V2f(img.chromaticities->green.x, img.chromaticities->green.y),
                                              Imath::V2f(img.chromaticities->blue.x, img.chromaticities->blue.y),
                                              Imath::V2f(img.chromaticities->white.x, img.chromaticities->white.y)}});
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

        auto            os = StdOStream{os_, string(filename).c_str()};
        Imf::OutputFile file{os, header};
        file.setFrameBuffer(frameBuffer);
        file.writePixels(img.data_window.size().y);
        spdlog::info("Saved EXR image to \"{}\" in {} seconds.", filename, (timer.elapsed() / 1000.f));
    }
    catch (const exception &e)
    {
        throw runtime_error{fmt::format("Failed to write EXR image \"{}\" failed: {}", filename, e.what())};
    }
}
