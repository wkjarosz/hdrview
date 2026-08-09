/**
    \file renderpass.h
*/
#pragma once

#include "fwd.h"
#include <unordered_map>

class Shader;

/**
    An abstraction for rendering passes that work with OpenGL, OpenGL ES, and Metal.

    This is a greatly simplified version of NanoGUI's RenderPass class. Original copyright follows.
    ----------
    NanoGUI was developed by Wenzel Jakob <wenzel.jakob@epfl.ch>.
    The widget drawing code is based on the NanoVG demo application
    by Mikko Mononen.

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/
class RenderPass
{
public:
    /// Depth test
    enum class DepthTest
    {
        Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always
    };

    /// Culling mode
    enum class CullMode
    {
        Disabled,
        Front,
        Back
    };

    /**
     * Create a new render pass for rendering to the main color and (optionally) depth buffer, or to an
     * offscreen texture.
     *
     * \param write_depth
     *     Should we write to the depth buffer?
     *
     * \param clear
     *     Should \ref begin() begin by clearing all buffers?
     *
     * \param color_target
     *     If non-null, render into this texture instead of the window's framebuffer. The texture must have
     *     been created with \ref Texture::TextureFlags::RenderTarget. The render pass does not take
     *     ownership of it, but does resize it from \ref resize(). Only supported on the OpenGL backend --
     *     the Metal backend always renders to the swapchain drawable.
     */
    RenderPass(bool write_depth = true, bool clear = true, Texture *color_target = nullptr);

    ~RenderPass();

    /**
     * Begin the render pass
     *
     * The specified drawing state (e.g. depth tests, culling mode, blending mode) are automatically set up at this
     * point. Later changes between \ref begin() and \ref end() are possible but cause additional OpenGL/GLES/Metal API
     * calls.
     */
    void begin();

    /// Finish the render pass
    void end();

    /// Return the clear color for a given color attachment
    const float4 &clear_color() const { return m_clear_color; }

    /// Set the clear color for a given color attachment
    void set_clear_color(const float4 &color);

    /// Return the clear depth for the depth attachment
    float clear_depth() const { return m_clear_depth; }

    /// Set the clear depth for the depth attachment
    void set_clear_depth(float depth);

    /// Specify the depth test and depth write mask of this render pass
    void set_depth_test(DepthTest depth_test, bool depth_write);

    /// Return the depth test and depth write mask of this render pass
    std::pair<DepthTest, bool> depth_test() const { return {m_depth_test, m_depth_write}; }

    /// Set the pixel offset and size of the viewport region
    void set_viewport(const int2 &offset, const int2 &size);

    /// Return the pixel offset and size of the viewport region
    std::pair<int2, int2> viewport() { return {m_viewport_offset, m_viewport_size}; }

    /// Specify the culling mode associated with the render pass
    void set_cull_mode(CullMode mode);

    /// Return the culling mode associated with the render pass
    CullMode cull_mode() const { return m_cull_mode; }

    /// Resize all texture targets attached to the render pass
    void resize(const int2 &size);

    /// The offscreen color target, or nullptr when rendering to the window's framebuffer
    Texture *color_target() const { return m_color_target; }

#if defined(HELLOIMGUI_HAS_METAL)
    void *command_encoder() const { return m_command_encoder; }
    void *command_buffer() const { return m_command_buffer; }
#endif

protected:
#if defined(HELLOIMGUI_HAS_OPENGL)
    /// (Re)attach m_color_target to m_framebuffer_handle; throws if the result is not a complete framebuffer
    void attach_color_target();
#endif

    /// Offscreen render target, or nullptr to render to the window's framebuffer. Not owned.
    Texture  *m_color_target = nullptr;
    bool      m_clear;
    float4    m_clear_color      = float4{0, 0, 0, 0};
    float     m_clear_depth      = 1.f;
    int2      m_viewport_offset  = int2{0};
    int2      m_viewport_size    = int2{0};
    int2      m_framebuffer_size = int2{0};
    DepthTest m_depth_test;
    bool      m_depth_write;
    CullMode  m_cull_mode;
    bool      m_active = false;

#if defined(HELLOIMGUI_HAS_OPENGL)
    /// FBO wrapping m_color_target; 0 when rendering to the window's framebuffer
    uint32_t m_framebuffer_handle = 0;
    /// Whatever framebuffer was bound when begin() ran, restored by end() so passes can nest
    int32_t m_framebuffer_backup = 0;
    int4    m_viewport_backup, m_scissor_backup;
    bool    m_depth_test_backup;
    bool    m_depth_write_backup;
    bool    m_scissor_test_backup;
    bool    m_cull_face_backup;
    bool    m_blend_backup;
#elif defined(HELLOIMGUI_HAS_METAL)
    void                   *m_command_buffer;
    void                   *m_command_encoder;
    void                   *m_pass_descriptor;
    std::unique_ptr<Shader> m_clear_shader;
#endif
};
