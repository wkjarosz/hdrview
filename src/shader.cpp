#include "shader.h"

#include <fmt/core.h>      // for format, make_format_args
#include <fmt/format.h>    // for vformat_to
#include <spdlog/spdlog.h> // for error, info
#include <sstream>         // for basic_ostringstream, bas...
#include <string.h>        // for strncmp

#include "hello_imgui/hello_imgui_assets.h" // for AssetFileData, AssetExists

using std::string;
using std::string_view;

#if defined(HELLOIMGUI_HAS_METAL)
static const string shader_extensions[] = {".metallib", ".metal", ".h"};
#elif defined(HELLOIMGUI_HAS_OPENGL)
static const string shader_extensions[] = {".glsl",    ".vs",      ".fs",      ".gs",    ".vsf",  ".fsh",  ".gsh",
                                           ".vshader", ".fshader", ".gshader", ".comp",  ".vert", ".tesc", ".tese",
                                           ".frag",    ".geom",    ".glslv",   ".glslf", ".glslg"};
#endif
static const size_t num_extensions = sizeof(shader_extensions) / sizeof(shader_extensions[0]);

string Shader::from_asset(string_view basename)
{
    using namespace HelloImGui;
    for (size_t i = 0; i < num_extensions; ++i)
    {
        string filename = basename.data() + shader_extensions[i];

        if (!AssetExists(filename))
            continue;

        string full_path = assetFileFullPath(filename);
        spdlog::info("Loading shader from \"{}\"...", full_path);
        auto shader_txt = LoadAssetFileData(filename.c_str());
        if (shader_txt.data == nullptr)
            throw std::runtime_error(fmt::format("Cannot load shader from file \"{}\"", filename));

        auto source = string((char *)shader_txt.data, shader_txt.dataSize);
        FreeAssetFileData(&shader_txt);
        return source;
    }
    throw std::runtime_error(fmt::format(
        "Could not find a shader with base filename \"{}\" with any known shader file extensions.", basename));
}

string Shader::prepend_includes(string_view shader_string, const std::vector<string_view> &include_files)
{
    // if the shader_string is actually a precompiled binary, we can't prepend
    if (shader_string.size() > 4 && strncmp(shader_string.data(), "MTLB", 4) == 0)
    {
        spdlog::error("Cannot add #includes to precompiled shaders, skipping.");
        return string(shader_string);
    }

    std::string includes;

    for (auto &i : include_files) includes += from_asset(i) + "\n";

    if (includes.empty())
        return string(shader_string);

    std::istringstream iss(shader_string.data());
    std::ostringstream oss;
    std::string        line;

    // first copy over all the #include or #version lines. these should stay at the top of the shader
    while (std::getline(iss, line) && (line.substr(0, 8) == "#include" || line.substr(0, 8) == "#version"))
        oss << line << std::endl;

    // now insert the new #includes
    oss << includes;

    // and copy over the rest of the lines in the shader
    do {
        oss << line << std::endl;
    } while (std::getline(iss, line));

    // spdlog::trace("GLSL #includes: {};\n MERGED: {}", includes, oss.str());

    return oss.str();
}

void Shader::set_buffer_divisor(const string &name, size_t divisor)
{
    auto it = m_buffers.find(name);
    if (it == m_buffers.end())
        throw std::invalid_argument("Shader::set_buffer_divisor(): could not find argument named \"" + name + "\"");

    Buffer &buf          = m_buffers[name];
    buf.instance_divisor = divisor;
    buf.dirty            = true;
}

void Shader::set_buffer_pointer_offset(const string &name, size_t offset)
{
    auto it = m_buffers.find(name);
    if (it == m_buffers.end())
        throw std::invalid_argument("Shader::set_buffer_pointer_offset(): could not find argument named \"" + name +
                                    "\"");

    Buffer &buf        = m_buffers[name];
    buf.pointer_offset = offset;
    buf.dirty          = true;
}

string Shader::Buffer::to_string() const
{
    string result = "Buffer[type=";
    switch (type)
    {
    case BufferType::VertexBuffer: result += "vertex"; break;
    case BufferType::FragmentBuffer: result += "fragment"; break;
    case BufferType::UniformBuffer: result += "uniform"; break;
    case BufferType::IndexBuffer: result += "index"; break;
    default: result += "unknown"; break;
    }
    result += ", dtype=";
    result += type_name(dtype);
    result += ", shape=[";
    for (size_t i = 0; i < ndim; ++i)
    {
        result += std::to_string(shape[i]);
        if (i + 1 < ndim)
            result += ", ";
    }
    result += "]]";
    return result;
}
