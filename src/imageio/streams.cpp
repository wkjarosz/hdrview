//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "streams.h"

#include <cstdio>
#include <stdexcept>
#include <string>

StreamBytes::StreamBytes(std::istream &is)
{
    if (auto span = dynamic_cast<SpanIStream *>(&is))
    {
        m_view = span->view();
        return;
    }

    is.clear();
    is.seekg(0, std::ios::end);
    auto end = is.tellg();
    if (end < 0)
        throw std::invalid_argument{"Cannot determine the size of the stream."};
    is.seekg(0, std::ios::beg);

    const auto size = static_cast<size_t>(end);
    m_copy.reset(new char[size]);

    // a file that cannot be reopened, or has shrunk since, is read through the stream instead
    bool read = false;
    if (auto file = dynamic_cast<FileIStream *>(&is))
    {
#ifdef _WIN32
        std::FILE *f = _wfopen(file->path().c_str(), L"rb");
#else
        std::FILE *f = std::fopen(file->path().c_str(), "rb");
#endif
        if (f)
        {
            read = std::fread(m_copy.get(), 1, size, f) == size;
            std::fclose(f);
        }
    }
    if (!read && !is.read(m_copy.get(), static_cast<std::streamsize>(size)))
        throw std::invalid_argument{"Failed to read " + std::to_string(size) + " bytes, got " +
                                    std::to_string(is.gcount()) + "."};

    m_view = std::string_view{m_copy.get(), size};
}
