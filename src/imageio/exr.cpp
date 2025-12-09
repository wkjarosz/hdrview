//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "exr.h"
#include "colorspace.h"
#include "common.h" // for lerp, mod, clamp, getExtension
#include "exr_header.h"
#include "exr_std_streams.h"
#include "hello_imgui/dpi_aware.h"
#include "image.h"
#include "imgui.h"
#include "imgui_ext.h"
#include "timer.h"
#include <ImfChannelList.h>
#include <ImfChannelListAttribute.h>
#include <ImfCompression.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfInputPart.h>
#include <ImfMultiPartInputFile.h>
#include <ImfOutputFile.h>
#include <ImfStandardAttributes.h>
#include <ImfTestFile.h> // for isOpenExrFile
#include <ImfTiledOutputFile.h>
#include <ImfVersion.h>
#include <stdexcept> // for runtime_error, out_of_range

using namespace std;

struct EXRSaveOptions
{
    std::vector<bool> group_enabled;                      // size = img.groups.size()
    int               pixel_type  = 1;                    // 0 = Imf::FLOAT, 1 = Imf::HALF
    Imf::Compression  compression = Imf::PIZ_COMPRESSION; // Default compression
    bool              tiled       = false;
    int               tile_width  = 64;
    int               tile_height = 64;
    float             dwa_quality = 45.0f; // Only for DWAA/DWAB
};

static EXRSaveOptions s_opts{};

bool is_exr_image(istream &is_, string_view filename) noexcept
{
    auto is = StdIStream{is_, string(filename).c_str()};
    return Imf::isOpenExrFile(is);
}

vector<ImagePtr> load_exr_image(istream &is_, string_view filename, const ImageLoadOptions &opts)
{
    ScopedMDC mdc{"IO", "EXR"};
    auto      is = StdIStream{is_, string(filename).c_str()};

    Imf::MultiPartInputFile infile{is};

    if (infile.parts() <= 0)
        throw invalid_argument{"EXR file contains no parts!"};

    ImGuiTextFilter filter{opts.channel_selector.c_str()};
    filter.Build();
    spdlog::info("Building filter for selector '{}'", opts.channel_selector);

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
            throw invalid_argument{fmt::format("Image has invalid data window: [{},{}] - [{},{}]",
                                               img->data_window.min.x, img->data_window.min.y, img->data_window.max.x,
                                               img->data_window.max.y)};

        if (img->display_window.is_empty())
            throw invalid_argument{fmt::format("Image has invalid display window: [{},{}] - [{},{}]",
                                               img->display_window.min.x, img->display_window.min.y,
                                               img->display_window.max.x, img->display_window.max.y)};

        Imf::FrameBuffer framebuffer;
        bool             has_channels = false;
        for (auto c = channels.begin(); c != channels.end(); ++c)
        {
            auto name = channel_name(c);
            if (!filter.PassFilter(&name[0], &name[0] + name.size()))
            {
                spdlog::debug("Skipping channel '{}' in part {}: '{}'", name, p, c.name());
                continue;
            }
            else
            {
                has_channels = true;
                spdlog::debug("Loading channel '{}' in part {}: '{}'", name, p, c.name());
            }

            name = c.name();

            img->channels.emplace_back(name, size);
            framebuffer.insert(c.name(), Imf::Slice::Make(Imf::FLOAT, img->channels.back().data(), dataWindow, 0, 0,
                                                          c.channel().xSampling, c.channel().ySampling));
        }

        if (!has_channels)
        {
            spdlog::debug("Part {}: '{}' has no channels matching the filter '{}', skipping...", p,
                          part.header().hasName() ? part.header().name() : "unnamed", opts.channel_selector);
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

            spdlog::warn("Channel '{}' is subsampled ({},{}). Only rudimentary subsampling is supported.", c.name(), xs,
                         ys);
            Array2Df tmp = img->channels[i];

            int subsampled_width = size.x / xs;
            for (int y = 0; y < size.y; ++y)
                for (int x = 0; x < size.x; ++x) img->channels[i]({x, y}) = tmp(x / xs + (y / ys) * subsampled_width);
        }

        images.emplace_back(img);
    }
    return images;
}

void save_exr_image(const Image &img, ostream &os_, string_view filename, const EXRSaveOptions *params)
{
    try
    {
        if (!params)
            params = &s_opts;
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

        // Compression
        header.compression() = params->compression;

        // Tiled
        if (params->tiled)
            header.setTileDescription(Imf::TileDescription(params->tile_width, params->tile_height, Imf::ONE_LEVEL));

        // DWA quality
        if (params->compression == Imf::DWAA_COMPRESSION || params->compression == Imf::DWAB_COMPRESSION)
            header.insert("dwaCompressionLevel", Imf::FloatAttribute(params->dwa_quality));

        Imf::FrameBuffer frameBuffer;

        std::map<std::string, std::vector<half>> halfBuffers; // Temporary storage for half buffers
        for (int g = 0; g < (int)img.groups.size(); ++g)
        {
            if (g >= (int)params->group_enabled.size() || !params->group_enabled[g])
                continue;
            auto &group = img.groups[g];
            if (!group.visible)
                continue;

            for (int c = 0; c < group.num_channels; ++c)
            {
                auto &channel    = img.channels[group.channels[c]];
                auto  pixel_type = (params->pixel_type == 1) ? Imf::HALF : Imf::FLOAT;

                // Specify desired file type in header
                header.channels().insert(channel.name, Imf::Channel(pixel_type));

                if (pixel_type == Imf::HALF)
                {
                    // Convert float buffer to half buffer
                    std::vector<half> &hbuf = halfBuffers[channel.name];
                    hbuf.resize(channel.num_elements());
                    const float *fbuf = channel.data();
                    for (int i = 0; i < channel.num_elements(); ++i) hbuf[i] = half(fbuf[i]);
                    frameBuffer.insert(channel.name, Imf::Slice::Make(Imf::HALF, hbuf.data(), dataWindow));
                }
                else
                {
                    // Use float buffer directly
                    frameBuffer.insert(channel.name, Imf::Slice::Make(Imf::FLOAT, channel.data(), dataWindow));
                }
            }
        }

        auto os = StdOStream{os_, string(filename).c_str()};
        if (params->tiled)
        {
            Imf::TiledOutputFile file{os, header};
            file.setFrameBuffer(frameBuffer);
            file.writeTiles(0, file.numXTiles() - 1, 0, file.numYTiles() - 1);
        }
        else
        {
            Imf::OutputFile file{os, header};
            file.setFrameBuffer(frameBuffer);
            file.writePixels(img.data_window.size().y);
        }
        spdlog::info("Saved EXR image to \"{}\" in {} seconds.", filename, (timer.elapsed() / 1000.f));
    }
    catch (const exception &e)
    {
        throw runtime_error{fmt::format("Failed to write EXR image \"{}\" failed: {}", filename, e.what())};
    }
}

EXRSaveOptions *exr_parameters_gui(const ImagePtr &img)
{
    static ImGuiSelectionBasicStorage group_selection;

    if (s_opts.group_enabled.size() != img->groups.size())
    {
        s_opts.group_enabled.assign(img->groups.size(), true);
        group_selection.Clear();
        for (int i = 0; i < (int)img->groups.size(); ++i) group_selection.SetItemSelected(i, true);
    }

    if (ImGui::PE::Begin("OpenEXR Save Options",
                         ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize))
    {
        ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_None);
        ImGui::TableSetupColumn("two", ImGuiTableColumnFlags_WidthStretch);

        // Channels (custom multi-select widget)
        ImGui::PE::Entry(
            fmt::format("Channels ({}/{})", group_selection.Size, (int)img->groups.size()),
            [&]
            {
                if (ImGui::BeginChild("##Groups", ImVec2(-FLT_MIN, ImGui::GetFontSize() * 10),
                                      ImGuiChildFlags_FrameStyle | ImGuiChildFlags_ResizeY))
                {
                    ImGuiMultiSelectFlags flags =
                        ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_BoxSelect1d;
                    ImGuiMultiSelectIO *ms_io =
                        ImGui::BeginMultiSelect(flags, group_selection.Size, (int)img->groups.size());
                    group_selection.ApplyRequests(ms_io);

                    int width = std::to_string(img->groups.size()).size();
                    for (int i = 0; i < (int)img->groups.size(); ++i)
                    {
                        auto &group            = img->groups[i];
                        bool  item_is_selected = group_selection.Contains((ImGuiID)i);
                        ImGui::SetNextItemSelectionUserData(i);

                        auto       &channel    = img->channels[group.channels[0]];
                        string      group_name = group.num_channels == 1 ? group.name : "(" + group.name + ")";
                        string      layer_path = Channel::head(channel.name) + group_name;
                        std::string label      = fmt::format("{:>{}d} {}", i + 1, width, layer_path);

                        ImGui::Selectable(label.c_str(), item_is_selected);
                    }

                    ms_io = ImGui::EndMultiSelect();
                    group_selection.ApplyRequests(ms_io);

                    // Update s_opts.group_enabled based on selection
                    if (s_opts.group_enabled.size() != img->groups.size())
                        s_opts.group_enabled.assign(img->groups.size(), true);
                    for (int i = 0; i < (int)img->groups.size(); ++i)
                        s_opts.group_enabled[i] = group_selection.Contains((ImGuiID)i);
                }
                ImGui::EndChild();
                return true;
            },
            "Select which channel groups to write to the EXR file.");

        // Pixel format
        ImGui::PE::Combo("Pixel format", &s_opts.pixel_type, "Float (32-bit)\0Half (16-bit)\0", -1,
                         "Choose whether to store channels as 32-bit float or 16-bit half in the EXR file.");

        // Compression (custom enumerated combo with tooltips)
        ImGui::PE::Entry(
            "Compression",
            [&]
            {
                static const Imf::Compression compression_values[] = {
                    Imf::NO_COMPRESSION,   Imf::RLE_COMPRESSION,   Imf::ZIPS_COMPRESSION,    Imf::ZIP_COMPRESSION,
                    Imf::PIZ_COMPRESSION,  Imf::PXR24_COMPRESSION, Imf::B44_COMPRESSION,     Imf::B44A_COMPRESSION,
                    Imf::DWAA_COMPRESSION, Imf::DWAB_COMPRESSION,  Imf::HTJ2K32_COMPRESSION, Imf::HTJ2K256_COMPRESSION};
                static const int num_compressions = IM_ARRAYSIZE(compression_values);

                string name;
                Imf::getCompressionNameFromId(compression_values[s_opts.compression], name);

                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##Compression", name.c_str()))
                {
                    for (int i = 0; i < num_compressions; ++i)
                    {
                        bool is_selected = (s_opts.compression == compression_values[i]);
                        Imf::getCompressionNameFromId(compression_values[i], name);
                        if (ImGui::Selectable(name.c_str(), is_selected))
                            s_opts.compression = compression_values[i];

                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                        Imf::getCompressionDescriptionFromId(compression_values[i], name);
                        ImGui::Tooltip(name.c_str());
                    }
                    ImGui::EndCombo();
                }

                return true;
            },
            "Select the compression method for the EXR file.");

        // DWA compression quality
        if (s_opts.compression == Imf::DWAA_COMPRESSION || s_opts.compression == Imf::DWAB_COMPRESSION)
            ImGui::PE::SliderFloat("DWA compression quality", &s_opts.dwa_quality, 0.0f, 100.0f, "%.3f", 0,
                                   "Set the lossy quality for DWA compression (higher is better, 45 is default).");

        // Tiled vs scanline
        ImGui::PE::Entry(
            "Tiled",
            [&]
            {
                ImGui::Checkbox("##Tiled", &s_opts.tiled);
                if (s_opts.tiled)
                {
                    ImGui::SameLine();
                    ImGui::BeginGroup();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x / 2);
                    ImGui::SliderInt("##Tile width", &s_opts.tile_width, 16, 512, "Width: %d");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    ImGui::SliderInt("##Tile height", &s_opts.tile_height, 16, 512, "Height: %d");
                    ImGui::EndGroup();
                    ImGui::Tooltip("Set the tile size for tiled EXR output.");
                }
                return false;
            },
            "Enable to save as a tiled EXR file (recommended for large images).");

        ImGui::PE::End();
    }

    if (ImGui::Button("Reset options to defaults"))
        s_opts = EXRSaveOptions{};

    return &s_opts;
}
