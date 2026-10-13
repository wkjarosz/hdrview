//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <Iex.h>
#include <ImfIO.h>
#include <OpenEXRConfig.h>
#include <algorithm>
#include <cstring>
#include <ostream>
#include <string_view>

/// An Imf::IStream over a whole file held in memory, which OpenEXR may read from many threads at once.
/**
    OpenEXR serializes every chunk read behind one mutex unless the stream supports stateless reads, so with a
    seek-and-read stream its decoding threads queue on that lock. Reporting size() also enables OpenEXR's checks
    of chunk offsets against the file's extent. The bytes must outlive the stream.
*/
class MemoryIStream : public Imf::IStream
{
public:
    MemoryIStream(std::string_view data, const char fileName[]) : Imf::IStream{fileName}, m_data{data} {}

    bool read(char c[/*n*/], int n) override
    {
        if (n < 0 || uint64_t(n) > m_data.size() - m_pos)
            throw IEX_NAMESPACE::InputExc("Unexpected end of file.");

        std::memcpy(c, m_data.data() + m_pos, n);
        m_pos += n;
        return m_pos < m_data.size();
    }

    uint64_t tellg() override { return m_pos; }

    void seekg(uint64_t pos) override
    {
        if (pos > m_data.size())
            throw IEX_NAMESPACE::InputExc("Seek past end of file.");
        m_pos = pos;
    }

    // OpenEXR 3.3 rebuilt its C++ reading on OpenEXRCore, which is what reads chunks concurrently
#if OPENEXR_VERSION_MAJOR > 3 || (OPENEXR_VERSION_MAJOR == 3 && OPENEXR_VERSION_MINOR >= 3)
    int64_t size() override { return int64_t(m_data.size()); }

    bool isStatelessRead() const override { return true; }

    /// Copies up to sz bytes at offset, zero-filling the rest; a read starting past the end yields 0 bytes.
    int64_t read(void *buf, uint64_t sz, uint64_t offset) override
    {
        uint64_t n = offset < m_data.size() ? std::min<uint64_t>(sz, m_data.size() - offset) : 0;
        if (n)
            std::memcpy(buf, m_data.data() + offset, n);
        std::memset(static_cast<char *>(buf) + n, 0, sz - n);
        return int64_t(n);
    }
#endif

private:
    std::string_view m_data;
    uint64_t         m_pos = 0;
};

// Like Imf::StdOFStream, but uses a generic std::ostream
class StdOStream : public Imf::OStream
{
public:
    StdOStream(std::ostream &stream, const char fileName[]) : Imf::OStream{fileName}, _os{stream} {}

    void write(const char c[/*n*/], int n)
    {
        clearError();
        _os.write(c, n);
        checkError(_os);
    }

    uint64_t tellp() { return std::streamoff(_os.tellp()); }

    void seekp(uint64_t pos)
    {
        _os.seekp(pos);
        checkError(_os);
    }

private:
    // The following error-checking functions were copy&pasted from the OpenEXR source code
    static void clearError() { errno = 0; }

    static void checkError(std::ostream &os)
    {
        if (!os)
        {
            if (errno)
            {
                IEX_NAMESPACE::throwErrnoExc();
            }

            throw IEX_NAMESPACE::ErrnoExc("File output failed.");
        }
    }

    std::ostream &_os;
};
