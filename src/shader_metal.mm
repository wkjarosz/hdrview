#if defined(HELLOIMGUI_HAS_METAL)

#include "renderpass.h"
#include "shader.h"
#include "texture.h"
#include <iostream>

#include <hello_imgui/hello_imgui.h>
#include <hello_imgui/internal/backend_impls/rendering_metal.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#define METAL_BUFFER_THRESHOLD 64

#include <spdlog/spdlog.h>

using HelloImGui::GetMetalGlobals;
using std::string;

id<MTLFunction> compile_metal_shader(id<MTLDevice> device, const std::string &name, const std::string &type_str,
                                     const std::string &src)
{
    if (src.empty())
        return nil;

    id<MTLLibrary> library = nil;
    NSError       *error   = nil;
    std::string    activity;
    if (src.size() > 4 && strncmp(src.data(), "MTLB", 4) == 0)
    {
        dispatch_data_t data = dispatch_data_create(src.data(), src.size(), NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        library              = [device newLibraryWithData:data error:&error];
        activity             = "load";
    }
    else
    {
        NSString          *str  = [NSString stringWithUTF8String:src.c_str()];
        MTLCompileOptions *opts = [MTLCompileOptions new];
        library                 = [device newLibraryWithSource:str options:opts error:&error];
        activity                = "compile";
    }
    if (error)
    {
        const char *error_shader = [[error description] UTF8String];
        throw std::runtime_error(std::string("compile_metal_shader(): unable to ") + activity + " " + type_str +
                                 " shader \"" + name + "\":\n\n" + error_shader);
    }

    NSArray<NSString *> *function_names = [library functionNames];
    if ([function_names count] != 1)
        throw std::runtime_error("compile_metal_shader(name=\"" + name + "\", type=\"" + type_str +
                                 "\"): library must contain exactly 1 shader, but it contains " +
                                 std::to_string([function_names count]) + "!");
    NSString *function_name = [function_names objectAtIndex:0];

    id<MTLFunction> function = [library newFunctionWithName:function_name];
    if (!function)
        throw std::invalid_argument("compile_metal_shader(name=\"" + name + "\"): function not found!");

    return function;
}

Shader::Shader(RenderPass *render_pass, const std::string &name, const std::string &vs_source,
               const std::string &fs_source, BlendMode blend_mode) :
    m_render_pass(render_pass), m_name(name), m_blend_mode(blend_mode)
{
    auto           &gMetalGlobals = GetMetalGlobals();
    id<MTLDevice>   device        = gMetalGlobals.caMetalLayer.device;
    id<MTLFunction> fragment_func = compile_metal_shader(device, name, "fragment", fs_source),
                    vertex_func   = compile_metal_shader(device, name, "vertex", vs_source);

    MTLRenderPipelineDescriptor *pipeline_desc = [MTLRenderPipelineDescriptor new];
    pipeline_desc.vertexFunction               = vertex_func;
    pipeline_desc.fragmentFunction             = fragment_func;

    // sokol-shdc's generated Metal vertex shaders take their `in` attributes via a [[stage_in]] struct (the
    // standard SPIRV-Cross-generated style), unlike the old hand-written vertex shader here, which read
    // straight from a `const device float2 *position` buffer indexed by [[vertex_id]] and so never needed a
    // MTLVertexDescriptor. [[stage_in]] requires one to be set explicitly, or pipeline creation fails with
    // "Vertex function has input attributes but no vertex descriptor was set." Reserve a buffer-index range
    // (30 downward) for these attributes, comfortably clear of the low single-digit indices sokol-shdc assigns
    // to uniform blocks/textures/samplers via @vs/@fs `layout(binding=N)`. Each attribute is also registered
    // in m_buffers below (stage_in attributes don't appear in the MTLArgument-based reflection used for
    // ordinary buffer/texture/sampler arguments further down), so e.g. set_buffer("position", ...) still works.
    MTLVertexDescriptor *vertex_desc = [MTLVertexDescriptor vertexDescriptor];
    for (MTLVertexAttribute *attr in vertex_func.vertexAttributes)
    {
        if (!attr.active)
            continue;

        MTLVertexFormat format;
        VariableType    dtype;
        size_t          component_count;
        switch (attr.attributeType)
        {
        case MTLDataTypeFloat: format = MTLVertexFormatFloat; dtype = VariableType::Float32; component_count = 1; break;
        case MTLDataTypeFloat2: format = MTLVertexFormatFloat2; dtype = VariableType::Float32; component_count = 2; break;
        case MTLDataTypeFloat3: format = MTLVertexFormatFloat3; dtype = VariableType::Float32; component_count = 3; break;
        case MTLDataTypeFloat4: format = MTLVertexFormatFloat4; dtype = VariableType::Float32; component_count = 4; break;
        case MTLDataTypeInt: format = MTLVertexFormatInt; dtype = VariableType::Int32; component_count = 1; break;
        case MTLDataTypeInt2: format = MTLVertexFormatInt2; dtype = VariableType::Int32; component_count = 2; break;
        case MTLDataTypeInt3: format = MTLVertexFormatInt3; dtype = VariableType::Int32; component_count = 3; break;
        case MTLDataTypeInt4: format = MTLVertexFormatInt4; dtype = VariableType::Int32; component_count = 4; break;
        default:
            throw std::invalid_argument("Shader::Shader(): vertex attribute \"" + std::string([attr.name UTF8String]) +
                                        "\": unsupported attribute type!");
        }

        NSUInteger buffer_index                          = 30 - attr.attributeIndex;
        vertex_desc.attributes[attr.attributeIndex].format      = format;
        vertex_desc.attributes[attr.attributeIndex].offset      = 0;
        vertex_desc.attributes[attr.attributeIndex].bufferIndex = buffer_index;
        vertex_desc.layouts[buffer_index].stride                = type_size(dtype) * component_count;
        vertex_desc.layouts[buffer_index].stepFunction           = MTLVertexStepFunctionPerVertex;

        std::string name              = [attr.name UTF8String];
        Buffer      &buf              = m_buffers[name];
        buf.index                     = (int)buffer_index;
        buf.type                      = VertexBuffer;
        buf.dtype                     = dtype;
        buf.ndim                      = 1;
        buf.shape[0]                  = 0;
        buf.shape[1]                  = component_count;
        buf.shape[2]                  = 1;
    }
    pipeline_desc.vertexDescriptor = vertex_desc;

    pipeline_desc.colorAttachments[0].pixelFormat = gMetalGlobals.caMetalLayer.pixelFormat;

    if (blend_mode == BlendMode::AlphaBlend)
    {
        MTLRenderPipelineColorAttachmentDescriptor *att = pipeline_desc.colorAttachments[0];

        att.blendingEnabled             = YES;
        att.rgbBlendOperation           = MTLBlendOperationAdd;
        att.alphaBlendOperation         = MTLBlendOperationAdd;
        att.sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
        att.sourceAlphaBlendFactor      = MTLBlendFactorSourceAlpha;
        att.destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
        att.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    }

    pipeline_desc.sampleCount = 1;

    NSError                     *error      = nil;
    MTLRenderPipelineReflection *reflection = nil;
    id<MTLRenderPipelineState>   pipeline_state =
        [device newRenderPipelineStateWithDescriptor:pipeline_desc
                                             options:MTLPipelineOptionArgumentInfo
                                          reflection:&reflection
                                               error:&error];
    if (error)
    {
        const char *error_pipeline = [[error description] UTF8String];
        throw std::runtime_error("compile_metal_pipeline(): unable to create render pipeline state!\n\n" +
                                 std::string(error_pipeline));
    }

    // [error release];

    m_pipeline_state = (__bridge_retained void *)pipeline_state;

    // Pre-size and zero-fill a struct-typed buffer argument (a named uniform block compiled by sokol-shdc,
    // e.g. "constant fs_params& fsp [[buffer(N)]]"), and register each of its members in
    // m_metal_struct_members so set_buffer("fsp.member", ...) can later memcpy into the right byte offset.
    // See the m_metal_struct_members declaration in shader.h for why this exists.
    // Mirrors the size-based small(raw array)-vs-large(id<MTLBuffer>) split that set_buffer()/begin()/~Shader()
    // already use for ordinary buffers (see METAL_BUFFER_THRESHOLD), so those unmodified code paths upload and
    // free a struct block exactly like any other buffer once this pre-allocates buf.buffer/buf.size correctly.
    // Large blocks use MTLResourceStorageModeShared (not the Private+blit path used elsewhere in this file for
    // write-once buffers) because members are written individually via CPU memcpy at arbitrary times, which
    // requires a CPU-visible .contents pointer.
    auto register_struct_members = [this, device](const std::string &block_name, MTLArgument *arg, Buffer &buf)
    {
        if (!arg.bufferStructType)
            return;
        size_t size = (size_t)arg.bufferDataSize;
        buf.size    = size;
        if (size <= METAL_BUFFER_THRESHOLD)
            buf.buffer = new uint8_t[size]();
        else
        {
            id<MTLBuffer> mtl_buffer = [device newBufferWithLength:size options:MTLResourceStorageModeShared];
            memset(mtl_buffer.contents, 0, size);
            buf.buffer = (__bridge_retained void *)mtl_buffer;
        }
        for (MTLStructMember *member in arg.bufferStructType.members)
        {
            std::string member_name = [member.name UTF8String];
            m_metal_struct_members[block_name + "." + member_name] = {block_name, (size_t)member.offset};
        }
    };

    for (MTLArgument *arg in [reflection vertexArguments])
    {
        std::string name = [arg.name UTF8String];

        // Metal's own pipeline reflection additionally reports a synthetic "vertexBuffer.<index>" entry for
        // each [[stage_in]] buffer slot (on top of the real per-attribute names already registered above via
        // vertex_func.vertexAttributes), representing an implementation detail of stage_in binding rather than
        // a parameter the app should address. Left unfiltered, it's a harmless but permanently-unbound extra
        // Buffer entry that spams "unbound argument" warnings every frame in begin() below.
        if (name.rfind("vertexBuffer.", 0) == 0)
            continue;

        if (m_buffers.find(name) != m_buffers.end())
            throw std::invalid_argument("Shader::Shader(): \"" + name + "\": duplicate argument name in shader code!");
        else if (name == "indices")
            throw std::invalid_argument("Shader::Shader(): argument name 'indices' is reserved!");

        Buffer &buf = m_buffers[name];
        buf.index   = arg.index;
        if (arg.type == MTLArgumentTypeBuffer)
        {
            buf.type = VertexBuffer;
            register_struct_members(name, arg, buf);
        }
        else if (arg.type == MTLArgumentTypeTexture)
            buf.type = VertexTexture;
        else if (arg.type == MTLArgumentTypeSampler)
            buf.type = VertexSampler;
        else
            throw std::invalid_argument("Shader::Shader(): \"" + name + "\": unsupported argument type!");

        // fmt::print("vertex argument: {} of type {}\n", name, (int)buf.type);
    }

    for (MTLArgument *arg in [reflection fragmentArguments])
    {
        std::string name = [arg.name UTF8String];
        if (m_buffers.find(name) != m_buffers.end())
            throw std::invalid_argument("Shader::Shader(): \"" + name + "\": duplicate argument name in shader code!");
        else if (name == "indices")
            throw std::invalid_argument("Shader::Shader(): argument name 'indices' is reserved!");

        Buffer &buf = m_buffers[name];
        buf.index   = arg.index;
        if (arg.type == MTLArgumentTypeBuffer)
        {
            buf.type = FragmentBuffer;
            register_struct_members(name, arg, buf);
        }
        else if (arg.type == MTLArgumentTypeTexture)
            buf.type = FragmentTexture;
        else if (arg.type == MTLArgumentTypeSampler)
            buf.type = FragmentSampler;
        else
            throw std::invalid_argument("Shader::Shader(): \"" + name + "\": unsupported argument type!");

        // fmt::print("vertex argument: {} of type {}\n", name, (int)buf.type);
    }

    Buffer &buf = m_buffers["indices"];
    buf.index   = -1;
    buf.type    = IndexBuffer;
}

Shader::~Shader()
{
    for (const auto &[key, buf] : m_buffers)
    {
        if (!buf.buffer)
            continue;
        if (buf.type == VertexBuffer || buf.type == FragmentBuffer || buf.type == IndexBuffer)
        {
            if (buf.size <= METAL_BUFFER_THRESHOLD)
                delete[] (uint8_t *)buf.buffer;
            else
                (void)(__bridge_transfer id<MTLBuffer>)buf.buffer;
        }
        else if (buf.type == VertexTexture || buf.type == FragmentTexture)
            (void)(__bridge_transfer id<MTLTexture>)buf.buffer;
        else if (buf.type == VertexSampler || buf.type == FragmentSampler)
            (void)(__bridge_transfer id<MTLSamplerState>)buf.buffer;
        else
            spdlog::error("Shader::~Shader(): unknown buffer type!");
    }

    (void)(__bridge_transfer id<MTLRenderPipelineState>)m_pipeline_state;
}

void Shader::set_buffer(const std::string &name, VariableType dtype, size_t ndim, const size_t *shape, const void *data)
{
    auto &gMetalGlobals = GetMetalGlobals();

    // A dotted "block.member" name addresses one member of a uniform block that sokol-shdc compiled to a
    // single struct-typed buffer argument (see register_struct_members() in the constructor above and the
    // m_metal_struct_members declaration in shader.h). Write just that member's bytes into the block's
    // already-fully-sized backing buffer instead of replacing the whole buffer.
    auto member_it = m_metal_struct_members.find(name);
    if (member_it != m_metal_struct_members.end())
    {
        const auto &[block_name, offset] = member_it->second;
        Buffer     &block_buf            = m_buffers[block_name];
        size_t      member_size          = type_size(dtype);
        for (size_t i = 0; i < ndim; ++i) member_size *= shape[i];
        if (offset + member_size > block_buf.size)
            throw std::invalid_argument("Shader::set_buffer(): \"" + name + "\": member write would overrun block \"" +
                                        block_name + "\"!");
        uint8_t *dst = block_buf.size <= METAL_BUFFER_THRESHOLD
                           ? (uint8_t *)block_buf.buffer
                           : (uint8_t *)[(__bridge id<MTLBuffer>)block_buf.buffer contents];
        memcpy(dst + offset, data, member_size);
        return;
    }

    auto it = m_buffers.find(name);
    if (it == m_buffers.end())
        throw std::invalid_argument("Shader::set_buffer(): could not find argument named \"" + name + "\"");
    Buffer &buf = m_buffers[name];
    if (!(buf.type == VertexBuffer || buf.type == FragmentBuffer || buf.type == IndexBuffer))
        throw std::invalid_argument("Shader::set_buffer(): argument named \"" + name + "\" is not a buffer!");

    for (size_t i = 0; i < 3; ++i) buf.shape[i] = i < ndim ? shape[i] : 1;

    size_t size = type_size(dtype) * buf.shape[0] * buf.shape[1] * buf.shape[2];
    if (buf.buffer && buf.size != size)
    {
        if (buf.size <= METAL_BUFFER_THRESHOLD)
            delete[] (uint8_t *)buf.buffer;
        else
            (void)(__bridge_transfer id<MTLBuffer>)buf.buffer;
        buf.buffer = nullptr;
    }

    if (size <= METAL_BUFFER_THRESHOLD && name != "indices")
    {
        if (!buf.buffer)
            buf.buffer = new uint8_t[size];
        memcpy(buf.buffer, data, size);
    }
    else
    {
        /* Procedure recommended by Apple: create a temporary shared buffer and blit into a private GPU-only buffer */
        id<MTLDevice> device = gMetalGlobals.caMetalLayer.device;
        id<MTLBuffer> mtl_buffer;

        if (buf.buffer)
            mtl_buffer = (__bridge_transfer id<MTLBuffer>)buf.buffer;
        else
            mtl_buffer = [device newBufferWithLength:size options:MTLResourceStorageModePrivate];

        id<MTLBuffer> temp_buffer = [device newBufferWithBytes:data length:size options:MTLResourceStorageModeShared];

        id<MTLCommandBuffer>      command_buffer = [gMetalGlobals.mtlCommandQueue commandBuffer];
        id<MTLBlitCommandEncoder> blit_encoder   = [command_buffer blitCommandEncoder];

        [blit_encoder copyFromBuffer:temp_buffer sourceOffset:0 toBuffer:mtl_buffer destinationOffset:0 size:size];

        [blit_encoder endEncoding];
        [command_buffer commit];
        [command_buffer waitUntilCompleted];

        buf.buffer = (__bridge_retained void *)mtl_buffer;
    }

    buf.dtype = dtype;
    buf.ndim  = ndim;
    buf.size  = size;
}

void Shader::set_texture(const std::string &name, Texture *texture)
{
    auto it = m_buffers.find(name);
    if (it == m_buffers.end())
        throw std::invalid_argument("Shader::set_texture(): could not find argument named \"" + name + "\"");
    Buffer &buf = m_buffers[name];
    if (!(buf.type == VertexTexture || buf.type == FragmentTexture))
        throw std::invalid_argument("Shader::set_texture(): argument named \"" + name + "\" is not a texture!");

    if (buf.buffer)
    {
        (void)(__bridge_transfer id<MTLTexture>)buf.buffer;
        buf.buffer = nullptr;
    }

    buf.buffer = (__bridge_retained void *)((__bridge id<MTLTexture>)texture->texture_handle());

    std::string sampler_name;
    if (name.length() > 8 && name.compare(name.length() - 8, 8, "_texture") == 0)
        sampler_name = name.substr(0, name.length() - 8) + "_sampler";
    else
        sampler_name = name + "_sampler";

    if (m_buffers.find(sampler_name) != m_buffers.end())
    {
        // Also set the sampler state
        Buffer &buf2 = m_buffers[sampler_name];

        if (buf2.buffer)
        {
            (void)(__bridge_transfer id<MTLTexture>)buf2.buffer;
            buf2.buffer = nullptr;
        }

        buf2.buffer = (__bridge_retained void *)((__bridge id<MTLSamplerState>)texture->sampler_state_handle());
    }
}

void Shader::begin()
{
    id<MTLRenderPipelineState>  pipeline_state = (__bridge id<MTLRenderPipelineState>)m_pipeline_state;
    id<MTLRenderCommandEncoder> command_enc    = (__bridge id<MTLRenderCommandEncoder>)m_render_pass->command_encoder();

    [command_enc setRenderPipelineState:pipeline_state];

    for (const auto &[key, buf] : m_buffers)
    {
        bool indices = buf.type == IndexBuffer;
        if (!buf.buffer)
        {
            if (!indices)
                spdlog::error("Shader::begin(): shader \"{}\" has an unbound argument \"{}\"!", m_name, key);
            continue;
        }

        switch (buf.type)
        {
        case VertexTexture:
        {
            id<MTLTexture> texture = (__bridge id<MTLTexture>)buf.buffer;
            [command_enc setVertexTexture:texture atIndex:buf.index];
        }
        break;

        case FragmentTexture:
        {
            id<MTLTexture> texture = (__bridge id<MTLTexture>)buf.buffer;
            [command_enc setFragmentTexture:texture atIndex:buf.index];
        }
        break;

        case VertexSampler:
        {
            id<MTLSamplerState> state = (__bridge id<MTLSamplerState>)buf.buffer;
            [command_enc setVertexSamplerState:state atIndex:buf.index];
        }
        break;

        case FragmentSampler:
        {
            id<MTLSamplerState> state = (__bridge id<MTLSamplerState>)buf.buffer;
            [command_enc setFragmentSamplerState:state atIndex:buf.index];
        }
        break;

        default:
            if (buf.size <= METAL_BUFFER_THRESHOLD && !indices)
            {
                if (buf.type == VertexBuffer)
                    [command_enc setVertexBytes:buf.buffer length:buf.size atIndex:buf.index];
                else if (buf.type == FragmentBuffer)
                    [command_enc setFragmentBytes:buf.buffer length:buf.size atIndex:buf.index];
                else
                    throw std::invalid_argument("Shader::begin(): unexpected buffer type!");
            }
            else
            {
                id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)buf.buffer;
                if (buf.type == VertexBuffer)
                    [command_enc setVertexBuffer:buffer offset:0 atIndex:buf.index];
                else if (buf.type == FragmentBuffer)
                    [command_enc setFragmentBuffer:buffer offset:0 atIndex:buf.index];
            }
            break;
        }
    }
}

void Shader::end() { /* No-op */ }

void Shader::draw_array(PrimitiveType primitive_type, size_t offset, size_t count, bool indexed, size_t instances)
{
    MTLPrimitiveType primitive_type_mtl;
    switch (primitive_type)
    {
    case PrimitiveType::Point: primitive_type_mtl = MTLPrimitiveTypePoint; break;
    case PrimitiveType::Line: primitive_type_mtl = MTLPrimitiveTypeLine; break;
    case PrimitiveType::LineStrip: primitive_type_mtl = MTLPrimitiveTypeLineStrip; break;
    case PrimitiveType::Triangle: primitive_type_mtl = MTLPrimitiveTypeTriangle; break;
    case PrimitiveType::TriangleStrip: primitive_type_mtl = MTLPrimitiveTypeTriangleStrip; break;
    default: throw std::invalid_argument("Shader::draw_array(): invalid primitive type!");
    }

    id<MTLRenderCommandEncoder> command_enc = (__bridge id<MTLRenderCommandEncoder>)m_render_pass->command_encoder();

    if (!indexed)
    {
        [command_enc drawPrimitives:primitive_type_mtl vertexStart:offset vertexCount:count];
    }
    else
    {
        id<MTLBuffer> index_buffer = (__bridge id<MTLBuffer>)m_buffers["indices"].buffer;
        [command_enc drawIndexedPrimitives:primitive_type_mtl
                                indexCount:count
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:index_buffer
                         indexBufferOffset:offset * 4];
    }
}

#endif // defined(HELLOIMGUI_HAS_METAL)
