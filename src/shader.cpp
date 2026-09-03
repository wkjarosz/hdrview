#include "shader.h"

#include <filesystem>       // for current_path, path
#include <spdlog/fmt/fmt.h> // for format, make_format_args, vformat_to
#include <spdlog/spdlog.h>  // for error, info
#include <sstream>          // for basic_ostringstream, bas...

#include <hello_imgui/hello_imgui_assets.h> // for AssetFileData, AssetExists

namespace fs = std::filesystem;

using std::string;
using std::string_view;

#if defined(HELLOIMGUI_HAS_METAL)
static const string shader_extensions[] = {".metallib", ".metal"};
#elif defined(HELLOIMGUI_HAS_OPENGL)
static const string shader_extensions[] = {".glsl"};
#endif
static const size_t num_extensions = sizeof(shader_extensions) / sizeof(shader_extensions[0]);

/// List the folders HelloImGui::AssetFileFullPath() consults, in the order it tries them.
static string describe_search_locations()
{
    std::vector<string> folders;
#ifdef ASSETS_LOCATION
    folders.emplace_back(ASSETS_LOCATION);
#endif
    for (const auto &path : HelloImGui::GetAssetsSearchPaths()) folders.push_back(path);
    std::error_code ec;
    if (auto cwd = fs::current_path(ec); !ec)
        folders.push_back((cwd / "assets").string());

    std::ostringstream oss;
    oss << "\n    Tried these asset folders:";
    for (const auto &folder : folders) oss << "\n        " << folder;
    // HelloImGui's exe-relative fallback is not reachable through the public API
    oss << "\n        plus exe_folder/assets, which HelloImGui resolves internally";
    return oss.str();
}

string Shader::from_asset(string_view basename)
{
    using namespace HelloImGui;
    for (size_t i = 0; i < num_extensions; ++i)
    {
        string filename = string{basename} + shader_extensions[i];

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
    throw std::runtime_error(
        fmt::format("Could not find a shader with base filename \"{}\" with any known shader file extensions.{}",
                    basename, describe_search_locations()));
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
