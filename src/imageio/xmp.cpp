// Minimal XMP parsing implementation using TinyXML-2
// Produces a JSON structure grouped by schema display name.

#include "xmp.h"
#include "json.h"
#include <cctype>
#include <map>
#include <spdlog/spdlog.h>
#include <string_view>
#include <tinyxml2.h>

using namespace tinyxml2;
using std::string;

static std::string_view extract_XMP_content(const string &xmp_blob)
{
    // Find the opening xpacket processing instruction
    size_t begin_pi = xmp_blob.find("<?xpacket begin");
    if (begin_pi == string::npos)
        return std::string_view();
    // Find the end of the opening PI ("?>")
    size_t begin_pi_end = xmp_blob.find("?>", begin_pi);
    if (begin_pi_end == string::npos)
        return std::string_view();
    begin_pi_end += 2; // position after the PI
    // Find the closing xpacket processing instruction
    size_t end_pi = xmp_blob.find("<?xpacket end", begin_pi_end);
    if (end_pi == string::npos)
        return std::string_view();
    // The XML content is the slice between begin_pi_end and end_pi
    std::string_view sv(xmp_blob.data() + begin_pi_end, end_pi - begin_pi_end);
    // Trim whitespace from both ends
    size_t first = sv.find_first_not_of(" \t\n\r");
    if (first == std::string_view::npos)
        return std::string_view();
    size_t last = sv.find_last_not_of(" \t\n\r");
    return sv.substr(first, last - first + 1);
}

json parse_xml_element(const XMLElement *element)
{
    json result;
    // Parse attributes into the current object
    const XMLAttribute *attr = element->FirstAttribute();
    while (attr)
    {
        std::string attr_name  = attr->Name();
        std::string attr_value = attr->Value();
        // Special-case: preserve the full "xml:lang" key and value instead of
        // splitting into namespace/object. This keeps language tags as plain
        // key/value pairs in the resulting JSON.
        if (attr_name == "xml:lang")
            result[attr_name] = attr_value;
        else if (attr_name.find("stEvt:") == 0 || attr_name.find("stRef:") == 0)
        {
            // Special case: Adobe event structure - keep as flat key/value pairs
            result[attr_name.substr(6)] = attr_value;
        }
        else
        {
            // Extract namespace prefix
            size_t colon_pos = attr_name.find(':');
            if (colon_pos != std::string::npos)
            {
                std::string ns_prefix  = attr_name.substr(0, colon_pos);
                std::string local_name = attr_name.substr(colon_pos + 1);
                if (!result.contains(ns_prefix))
                {
                    result[ns_prefix] = json::object();
                }
                result[ns_prefix][local_name] = attr_value;
            }
            else
            {
                result[attr_name] = attr_value;
            }
        }
        attr = attr->Next();
    }
    // Handle text content
    const char *text = element->GetText();
    if (text && strlen(text) > 0)
    {
        std::string text_str = text;
        // Trim whitespace
        size_t start = text_str.find_first_not_of(" \t\n\r");
        size_t end   = text_str.find_last_not_of(" \t\n\r");
        if (start != std::string::npos && end != std::string::npos)
        {
            text_str = text_str.substr(start, end - start + 1);
            // If we have attributes, add text as "string" field
            if (!result.empty())
            {
                result["string"] = text_str;
                return result;
            }
            // Otherwise just return the text
            return text_str;
        }
    }
    // Parse child elements
    const XMLElement *child = element->FirstChildElement();
    // If no children and no attributes, check for text again
    if (!child && result.empty())
    {
        const char *text2 = element->GetText();
        if (text2 && strlen(text2) > 0)
        {
            return std::string(text2);
        }
    }

    // Check if this element contains only a single rdf:Seq/Alt/Bag child
    // If so, return the array directly instead of wrapping it
    if (child && !child->NextSiblingElement() && result.empty())
    {
        std::string child_name = child->Name();
        if (child_name == "rdf:Seq" || child_name == "rdf:Alt" || child_name == "rdf:Bag")
        {
            json              seq_array = json::array();
            const XMLElement *item      = child->FirstChildElement();
            while (item)
            {
                json item_json = parse_xml_element(item);
                seq_array.push_back(item_json);
                item = item->NextSiblingElement();
            }
            return seq_array;
        }
    }

    while (child)
    {
        std::string child_name = child->Name();
        size_t      colon_pos  = child_name.find(':');
        json        child_json = parse_xml_element(child);

        // Special handling for rdf:Description - skip the wrapper and merge contents directly
        if (child_name == "rdf:Description")
        {
            // Merge the contents directly into result (skip rdf:Description wrapper)
            for (auto &[key, value] : child_json.items())
            {
                if (result.contains(key))
                {
                    // Merge objects
                    if (result[key].is_object() && value.is_object())
                    {
                        result[key].update(value);
                    }
                }
                else
                {
                    result[key] = value;
                }
            }
        }
        // Special-case xml:lang: preserve the full key and use the XML value
        // directly as the JSON value.
        else if (child_name == "xml:lang")
        {
            if (child_json.is_string())
                result["xml:lang"] = child_json.get<std::string>();
            else
                result["xml:lang"] = child_json;
        }
        else if (colon_pos != std::string::npos)
        {
            std::string ns_prefix  = child_name.substr(0, colon_pos);
            std::string local_name = child_name.substr(colon_pos + 1);

            // if (ns_prefix == "crs")
            // {
            //     // if (result.is_null())
            //     //     result = json::object();

            //     spdlog::info("Found crs element: {}:{}", ns_prefix, local_name);
            //     if (child_json.is_string())
            //         result[local_name] = child_json.get<std::string>();
            //     else
            //         result[local_name] = child_json;
            // }
            // else
            {
                if (!result.contains(ns_prefix))
                    result[ns_prefix] = json::object();
                result[ns_prefix][local_name] = child_json;
            }
        }
        else
        {
            result[child_name] = child_json;
        }
        child = child->NextSiblingElement();
    }
    return result;
}

void add_xmlns_entries(const XMLElement *e, json &xmlns)
{
    const XMLAttribute *attr = e->FirstAttribute();
    while (attr)
    {
        std::string attr_name = attr->Name();
        if (attr_name.find("xmlns:") == 0)
        {
            static const std::map<std::string, std::string> xmp_namespace_names = {
                // Core XMP namespaces
                {"http://ns.adobe.com/xap/1.0/", "Basic"},
                {"http://purl.org/dc/elements/1.1/", "Dublin Core"},
                {"http://ns.adobe.com/xap/1.0/rights/", "Rights Management"},
                {"http://ns.adobe.com/xap/1.0/mm/", "Media Management"},

                // Media-specific namespaces
                {"http://ns.adobe.com/exif/1.0/", "EXIF"},
                {"http://ns.adobe.com/exif/1.0/aux/", "EXIF Auxiliary"},
                {"http://cipa.jp/exif/1.0/", "EXIF 2.21 or later"},
                {"http://ns.adobe.com/tiff/1.0/", "TIFF Rev. 6.0"},
                {"http://ns.adobe.com/photoshop/1.0/", "Photoshop"},
                {"http://ns.adobe.com/camera-raw-settings/1.0/", "Camera Raw Settings"},

                // Other common namespaces
                {"http://ns.adobe.com/pdf/1.3/", "PDF"},
                {"http://ns.adobe.com/xap/1.0/t/pg/", "Paged-Text"},
                {"http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/", "IPTC Core"},
                {"http://ns.adobe.com/xap/1.0/bj/", "Basic Job Ticket"},
                {"http://ns.adobe.com/xap/1.0/sType/ResourceEvent#", "Resource Event"},
                {"http://ns.adobe.com/xap/1.0/sType/ResourceRef#", "Resource Reference"},
                {"http://ns.adobe.com/hdr-metadata/1.0/", "HDR Metadata"},
                {"http://ns.adobe.com/hdr-gain-map/1.0/", "HDR Gain Map"},
                {"http://ns.adobe.com/xmp/1.0/DynamicMedia/", "Dynamic Media"},

                // RDF namespace (always present in XMP)
                {"http://www.w3.org/1999/02/22-rdf-syntax-ns#", "RDF"},
                {"adobe:ns:meta/", "XMP Meta"}};

            std::string ns_prefix = attr_name.substr(6);
            spdlog::debug("XMP: found namespace '{}', prefix: '{}'", attr->Value(), ns_prefix);
            // map known namespace URIs to friendly names when available
            xmlns[ns_prefix]["prefix"] = ns_prefix;
            std::string uri            = attr->Value();
            xmlns[ns_prefix]["uri"]    = uri;
            auto it                    = xmp_namespace_names.find(uri);
            if (it != xmp_namespace_names.end())
                xmlns[ns_prefix]["name"] = it->second;
            else
                xmlns[ns_prefix]["name"] = ns_prefix;
        }
        attr = attr->Next();
    }
}

json xmp_to_json(const std::string &xmp_packet)
{
    XMLDocument doc;
    XMLError    error = doc.Parse(string(extract_XMP_content(xmp_packet)).c_str());
    if (error != XML_SUCCESS)
    {
        return json::object();
    }
    json result;
    // Parse xmlns declarations from x:xmpmeta or the root element
    const XMLElement *xmpmeta = doc.FirstChildElement("x:xmpmeta");
    if (!xmpmeta)
    {
        xmpmeta = doc.RootElement();
    }
    if (!xmpmeta)
    {
        return json::object();
    }
    // Find rdf:RDF element
    const XMLElement *rdf = xmpmeta->FirstChildElement("rdf:RDF");
    if (!rdf)
    {
        return json::object();
    }
    // Collect all xmlns declarations
    json xmlns = json::object();
    add_xmlns_entries(rdf, xmlns);

    // Parse rdf:Description elements
    const XMLElement *description = rdf->FirstChildElement("rdf:Description");
    while (description)
    {
        add_xmlns_entries(description, xmlns);
        json desc_json = parse_xml_element(description);
        // Merge namespace objects into result
        for (auto &[key, value] : desc_json.items())
        {
            if (result.contains(key))
            {
                // Merge objects
                if (result[key].is_object() && value.is_object())
                {
                    result[key].update(value);
                }
            }
            else
            {
                result[key] = value;
            }
        }
        description = description->NextSiblingElement("rdf:Description");
    }

    if (!xmlns.empty())
        result["xmlns"] = xmlns;

    return result;
}

Xmp::Xmp(const char *xml, size_t len)
{
    if (!xml || len == 0)
        return;
    m_xml.assign(xml, len);
    parse();
}

bool Xmp::valid() const { return m_valid; }

/**
 * @brief Convert an XMP packet (XML) into a nlohmann::json object that follows the
 *        “no‑colon‑in‑key” schema you described.
 *
 * @param xmpXml  The whole XMP packet (including the <?xpacket …?> processing
 *                instructions) as a UTF‑8 string.
 * @return json   The generated JSON tree.
 */
void Xmp::parse()
{
    m_json = xmp_to_json(m_xml);
    spdlog::debug("XMP: produced JSON: {}", m_json.dump(2));
    m_valid = true;
}

json Xmp::to_json() const { return m_valid ? m_json : json::object(); }
