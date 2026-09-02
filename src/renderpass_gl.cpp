#if defined(HELLOIMGUI_HAS_OPENGL)

#include "opengl_check.h"
#include "renderpass.h"
#include "texture.h"
#include <hello_imgui/hello_imgui_include_opengl.h> // cross-platform way to include OpenGL headers

#include <spdlog/fmt/fmt.h>
#include <stdexcept>

RenderPass::RenderPass(bool write_depth, bool clear, Texture *color_target) :
    m_color_target(color_target), m_clear(clear), m_depth_test(write_depth ? DepthTest::Less : DepthTest::Always),
    m_depth_write(write_depth), m_cull_mode(CullMode::Back)
{
    if (!m_color_target)
        return;

    CHK(glGenFramebuffers(1, &m_framebuffer_handle));
    attach_color_target();
}

RenderPass::~RenderPass()
{
    if (m_framebuffer_handle)
        CHK(glDeleteFramebuffers(1, &m_framebuffer_handle));
}

void RenderPass::attach_color_target()
{
    GLint fbo_backup = 0;
    CHK(glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo_backup));

    CHK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_framebuffer_handle));
    CHK(glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               m_color_target->texture_handle(), 0));

    GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    CHK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)fbo_backup));

    if (status != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error(
            fmt::format("RenderPass: could not attach color target; glCheckFramebufferStatus returned 0x{:04x}. "
                        "The driver most likely cannot render to this texture format.",
                        (unsigned)status));
}

void RenderPass::begin()
{
#if !defined(NDEBUG)
    if (m_active)
        throw std::runtime_error("RenderPass::begin(): render pass is already active!");
#endif
    m_active = true;

    // save whatever was bound so passes can nest: the colorpass target stays bound across the whole frame
    // while the image pass begins and ends inside it
    CHK(glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_framebuffer_backup));
    if (m_color_target)
        CHK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_framebuffer_handle));

    CHK(glGetIntegerv(GL_VIEWPORT, &m_viewport_backup[0]));
    CHK(glGetIntegerv(GL_SCISSOR_BOX, &m_scissor_backup[0]));
    GLboolean depth_write;
    CHK(glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write));
    m_depth_write_backup = depth_write;

    m_depth_test_backup   = glIsEnabled(GL_DEPTH_TEST);
    m_scissor_test_backup = glIsEnabled(GL_SCISSOR_TEST);
    m_cull_face_backup    = glIsEnabled(GL_CULL_FACE);
    m_blend_backup        = glIsEnabled(GL_BLEND);

    set_viewport(m_viewport_offset, m_viewport_size);

    if (m_clear)
    {
        GLenum what = 0;
        if (m_depth_write)
        {
            CHK(glClearDepthf(m_clear_depth));
            what |= GL_DEPTH_BUFFER_BIT;
        }

        CHK(glClearColor(m_clear_color.x, m_clear_color.y, m_clear_color.z, m_clear_color.w));
        what |= GL_COLOR_BUFFER_BIT;

        CHK(glClear(what));
    }

    set_depth_test(m_depth_test, m_depth_write);
    set_cull_mode(m_cull_mode);

    if (m_blend_backup)
        CHK(glDisable(GL_BLEND));
}

void RenderPass::end()
{
#if !defined(NDEBUG)
    if (!m_active)
        throw std::runtime_error("RenderPass::end(): render pass is not active!");
#endif

    CHK(glViewport(m_viewport_backup[0], m_viewport_backup[1], m_viewport_backup[2], m_viewport_backup[3]));
    CHK(glScissor(m_scissor_backup[0], m_scissor_backup[1], m_scissor_backup[2], m_scissor_backup[3]));

    if (m_depth_test_backup)
        CHK(glEnable(GL_DEPTH_TEST));
    else
        CHK(glDisable(GL_DEPTH_TEST));

    CHK(glDepthMask(m_depth_write_backup));

    if (m_scissor_test_backup)
        CHK(glEnable(GL_SCISSOR_TEST));
    else
        CHK(glDisable(GL_SCISSOR_TEST));

    if (m_cull_face_backup)
        CHK(glEnable(GL_CULL_FACE));
    else
        CHK(glDisable(GL_CULL_FACE));

    if (m_blend_backup)
        CHK(glEnable(GL_BLEND));
    else
        CHK(glDisable(GL_BLEND));

    if (m_color_target)
        CHK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)m_framebuffer_backup));

    m_active = false;
}

void RenderPass::resize(const int2 &size)
{
    m_framebuffer_size = size;
    m_viewport_offset  = int2(0, 0);
    m_viewport_size    = size;

    if (m_color_target && m_color_target->size() != size)
    {
        // Texture::resize() keeps the same GL name, but reallocates the storage behind it, so the
        // attachment has to be revalidated
        m_color_target->resize(size);
        attach_color_target();
    }
}

void RenderPass::set_clear_color(const float4 &color) { m_clear_color = color; }

void RenderPass::set_clear_depth(float depth) { m_clear_depth = depth; }

void RenderPass::set_viewport(const int2 &offset, const int2 &size)
{
    m_viewport_offset = offset;
    m_viewport_size   = size;

    if (m_active)
    {
        int ypos = m_framebuffer_size.y - m_viewport_size.y - m_viewport_offset.y;
        CHK(glViewport(m_viewport_offset.x, ypos, m_viewport_size.x, m_viewport_size.y));
        // fmt::print("RenderPass::viewport({}, {}, {}, {})\n", m_viewport_offset.x, ypos, m_viewport_size.x,
        // m_viewport_size.y);
        CHK(glScissor(m_viewport_offset.x, ypos, m_viewport_size.x, m_viewport_size.y));

        if (m_viewport_offset == int2(0, 0) && m_viewport_size == m_framebuffer_size)
            CHK(glDisable(GL_SCISSOR_TEST));
        else
            CHK(glEnable(GL_SCISSOR_TEST));
    }
}

void RenderPass::set_depth_test(DepthTest depth_test, bool depth_write)
{
    m_depth_test  = depth_test;
    m_depth_write = depth_write;

    if (m_active)
    {
        if (depth_test != DepthTest::Always)
        {
            GLenum func;
            switch (depth_test)
            {
            case DepthTest::Never: func = GL_NEVER; break;
            case DepthTest::Less: func = GL_LESS; break;
            case DepthTest::Equal: func = GL_EQUAL; break;
            case DepthTest::LessEqual: func = GL_LEQUAL; break;
            case DepthTest::Greater: func = GL_GREATER; break;
            case DepthTest::NotEqual: func = GL_NOTEQUAL; break;
            case DepthTest::GreaterEqual: func = GL_GEQUAL; break;
            default: throw std::invalid_argument("Shader::set_depth_test(): invalid depth test mode!");
            }
            CHK(glEnable(GL_DEPTH_TEST));
            CHK(glDepthFunc(func));
        }
        else
        {
            CHK(glDisable(GL_DEPTH_TEST));
        }
        CHK(glDepthMask(depth_write ? GL_TRUE : GL_FALSE));
        // fmt::print("RenderPass::set_depth_test({}, {})\n", (int)depth_test, depth_write);
    }
}

void RenderPass::set_cull_mode(CullMode cull_mode)
{
    m_cull_mode = cull_mode;

    if (m_active)
    {
        if (cull_mode == CullMode::Disabled)
        {
            CHK(glDisable(GL_CULL_FACE));
        }
        else
        {
            CHK(glEnable(GL_CULL_FACE));
            if (cull_mode == CullMode::Front)
                CHK(glCullFace(GL_FRONT));
            else if (cull_mode == CullMode::Back)
                CHK(glCullFace(GL_BACK));
            else
                throw std::invalid_argument("Shader::set_cull_mode(): invalid cull mode!");
        }
    }
}

#endif // defined(HELLOIMGUI_HAS_OPENGL)
