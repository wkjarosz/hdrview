//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <Iex.h>
#include <ImfIO.h>

// Like Imf::StdIFStream, but uses a generic std::std::istream
class StdIStream : public Imf::IStream
{
public:
    StdIStream(std::istream &stream, const char fileName[]) : Imf::IStream{fileName}, _is{stream} {}

    bool read(char c[/*n*/], int n) override
    {
        if (!_is)
            throw IEX_NAMESPACE::InputExc("Unexpected end of file.");

        clearError();
        _is.read(c, n);
        return checkError(_is, n);
    }

    uint64_t tellg() override { return std::streamoff(_is.tellg()); }

    void seekg(uint64_t pos) override
    {
        _is.seekg(pos);
        checkError(_is);
    }

    void clear() override { _is.clear(); }

private:
    static void clearError() { errno = 0; }

    static bool checkError(std::istream &is, std::streamsize expected = 0)
    {
        if (!is)
        {
            if (errno)
            {
                IEX_NAMESPACE::throwErrnoExc();
            }

            if (is.gcount() < expected)
            {
                THROW(IEX_NAMESPACE::InputExc,
                      "Early end of file: read " << is.gcount() << " out of " << expected << " requested bytes.");
            }

            return false;
        }

        return true;
    }

    std::istream &_is;
};

// Like Imf::StdOFStream, but uses a generic std::std::ostream
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
