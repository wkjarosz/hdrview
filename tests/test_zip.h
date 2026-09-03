/** \file test_zip.h
    \author Wojciech Jarosz

    Zip archives built in memory, for the loader's archive paths.
*/

/**
    Separate from test_support.h because miniz declares zlib's names itself, so a translation unit that
    reaches zlib.h (anything including libpng's headers, for instance) cannot see both.
*/

#pragma once

#include "test_support.h"

#include <miniz.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace hdrview_test
{

/// A deflated zip archive holding each `{name, contents}` entry, built in memory.
inline std::string zip_bytes(const std::vector<std::pair<std::string, std::string>> &entries)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    mz_zip_writer_init_heap(&zip, 0, 0);
    for (const auto &[name, contents] : entries)
        mz_zip_writer_add_mem(&zip, name.c_str(), contents.data(), contents.size(), MZ_BEST_COMPRESSION);

    void       *buf  = nullptr;
    size_t      size = 0;
    std::string out;
    if (mz_zip_writer_finalize_heap_archive(&zip, &buf, &size))
        out.assign(reinterpret_cast<char *>(buf), size);
    mz_zip_writer_end(&zip);
    return out;
}

/// Rewrites the first entry's uncompressed size in both of its headers, leaving the stored bytes alone.
/**
    The result is an archive claiming more than it holds, as a truncated, hand-edited or bomb archive does.
    The field sits at offset 22 of a local file header (PK\3\4) and offset 24 of a central directory
    header (PK\1\2).
*/
inline void declare_uncompressed_size(std::string &zip, uint32_t declared)
{
    auto patch_at = [&](const char *sig, size_t field_offset)
    {
        if (size_t pos = zip.find(sig, 0, 4); pos != std::string::npos)
            patch(zip, pos + field_offset, declared);
    };
    patch_at("PK\x03\x04", 22);
    patch_at("PK\x01\x02", 24);
}

} // namespace hdrview_test
