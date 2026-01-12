//
// Minimal XMP parsing (viewer-grade) using TinyXML-2
//
#pragma once

#include "json.h"
#include <cstddef>
#include <string>

class Xmp
{
public:
    Xmp(const char *xml = nullptr, size_t len = 0);
    bool valid() const;
    json to_json() const;

private:
    std::string m_xml;
    bool        m_valid = false;
    json m_json;
    void parse();
};
