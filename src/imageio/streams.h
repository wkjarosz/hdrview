//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <filesystem>
#include <fstream>
#include <istream>
#include <streambuf>
#include <string_view>

/// A binary std::ifstream that remembers its path, so a loader can let a library open the file itself.
class FileIStream : public std::ifstream
{
public:
    explicit FileIStream(const std::filesystem::path &path) : std::ifstream{path, std::ios::binary}, m_path{path} {}

    const std::filesystem::path &path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

/// A read-only std::istream over bytes it does not own, so a loader can reach them without a copy.
class SpanIStream : public std::istream
{
public:
    explicit SpanIStream(std::string_view bytes) : std::istream{nullptr}, m_buf{bytes} { rdbuf(&m_buf); }

    std::string_view view() const { return m_buf.view; }

private:
    struct Buf : std::streambuf
    {
        std::string_view view;

        explicit Buf(std::string_view bytes) : view{bytes}
        {
            // the get area wants char *, but with no put area and no putback nothing writes through it
            auto p = const_cast<char *>(view.data());
            setg(p, p, p + view.size());
        }

        pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which) override
        {
            if (!(which & std::ios_base::in))
                return pos_type(off_type(-1));

            off_type base = dir == std::ios_base::beg   ? 0
                            : dir == std::ios_base::cur ? gptr() - eback()
                                                        : egptr() - eback();
            off_type pos  = base + off;
            if (pos < 0 || pos > egptr() - eback())
                return pos_type(off_type(-1));

            setg(eback(), eback() + pos, egptr());
            return pos_type(pos);
        }

        pos_type seekpos(pos_type pos, std::ios_base::openmode which) override
        {
            return seekoff(off_type(pos), std::ios_base::beg, which);
        }
    };

    Buf m_buf;
};
