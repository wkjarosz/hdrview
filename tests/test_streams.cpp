//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "imageio/streams.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

TEST_CASE("StreamBytes yields a stream's exact bytes, viewing a SpanIStream's own rather than copying them")
{
    const auto path = std::filesystem::temp_directory_path() / "hdrview_stream_bytes.bin";

    // sizes around the buffer sizes a std::filebuf or fread() might use
    for (size_t size : {size_t(0), size_t(1), size_t(4095), size_t(4096), size_t(65537), size_t(1) << 20})
    {
        INFO("size = ", size);
        std::string bytes(size, '\0');
        uint32_t    state = uint32_t(size) + 1;
        for (auto &b : bytes)
        {
            state = state * 1664525u + 1013904223u;
            b     = char(state >> 24);
        }
        std::ofstream{path, std::ios::binary}.write(bytes.data(), bytes.size());

        auto check = [&](std::istream &is)
        {
            // from the start, wherever the stream was left
            is.seekg(0, std::ios::end);
            const StreamBytes got{is};
            CHECK(got.size() == size);
            CHECK(got.view() == bytes);
        };

        SpanIStream span{bytes};
        check(span);
        CHECK(StreamBytes{span}.view().data() == bytes.data());

        FileIStream file{path};
        REQUIRE(file.good());
        check(file);

        std::istringstream plain{bytes, std::ios::binary};
        check(plain);
    }
    std::filesystem::remove(path);
}
