/**
    \file texture.h
*/

#pragma once

#include "fwd.h"
#include "traits.h"
#include <string>

/**
    Defines an abstraction for textures that works with OpenGL, OpenGL ES, and Metal.

    This is adapted from NanoGUI's Texture class. Copyright follows.
    ----------
    NanoGUI was developed by Wenzel Jakob <wenzel.jakob@epfl.ch>.
    The widget drawing code is based on the NanoVG demo application
    by Mikko Mononen.

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/
class Texture
{
public:
    /// Overall format of the texture (e.g. luminance-only or RGBA)
    enum class PixelFormat : uint8_t
    {
        R,           ///< Single-channel bitmap
        RA,          ///< Two-channel bitmap
        RGB,         ///< RGB bitmap
        RGBA,        ///< RGB bitmap + alpha channel
        BGR,         ///< BGR bitmap
        BGRA,        ///< BGR bitmap + alpha channel
        Depth,       ///< Depth map
        DepthStencil ///< Combined depth + stencil map
    };

    /// Number format of pixel components
    enum class ComponentFormat : uint8_t
    {
        // Signed and unsigned integer formats
        UInt8  = (uint8_t)VariableType::UInt8,
        Int8   = (uint8_t)VariableType::Int8,
        UInt16 = (uint16_t)VariableType::UInt16,
        Int16  = (uint16_t)VariableType::Int16,
        UInt32 = (uint32_t)VariableType::UInt32,
        Int32  = (uint32_t)VariableType::Int32,

        // Floating point formats
        Float16 = (uint16_t)VariableType::Float16,
        Float32 = (uint32_t)VariableType::Float32
    };

    /// Texture interpolation mode
    enum class InterpolationMode : uint8_t
    {
        Nearest,  ///< Nearest neighbor interpolation
        Bilinear, ///< Bilinear interpolation
        Trilinear ///< Trilinear interpolation (using MIP mapping)
    };

    /// How should out-of-bounds texture evaluations be handled?
    enum class WrapMode : uint8_t
    {
        ClampToEdge,  ///< Clamp evaluations to the edge of the texture
        Repeat,       ///< Repeat the texture
        MirrorRepeat, ///< Repeat, but flip the texture after crossing the boundary
    };

    /// How will the texture be used? (Must specify at least one)
    enum TextureFlags : uint8_t
    {
        ShaderRead   = 0x01, ///< Texture to be read in shaders
        RenderTarget = 0x02  ///< Target framebuffer for rendering
    };

    /**
        Allocate memory for a texture with the given configuration

        \note
            Certain combinations of pixel and component formats may not be natively supported by the hardware. In this
            case, \ref init() chooses a similar supported configuration that can subsequently be queried using \ref
            pixel_format() and \ref component_format(). Some caution must be exercised in this case, since \ref upload()
            will need to provide the data in a different storage format.
    */
    Texture(PixelFormat pixel_format, ComponentFormat component_format, int2 size,
            InterpolationMode min_interpolation_mode = InterpolationMode::Bilinear,
            InterpolationMode mag_interpolation_mode = InterpolationMode::Bilinear,
            WrapMode wrap_mode = WrapMode::ClampToEdge, uint8_t samples = 1, uint8_t flags = TextureFlags::ShaderRead,
            bool manual_mipmapping = false);

    /// Release all resources
    virtual ~Texture();

    /// Return the pixel format
    PixelFormat pixel_format() const { return m_pixel_format; }

    /// Return the component format
    ComponentFormat component_format() const { return m_component_format; }

    /// Return the interpolation mode for minification
    InterpolationMode min_interpolation_mode() const { return m_min_interpolation_mode; }

    /// Return the interpolation mode for magnification
    InterpolationMode mag_interpolation_mode() const { return m_mag_interpolation_mode; }

    /// Return the wrap mode
    WrapMode wrap_mode() const { return m_wrap_mode; }

    /// Return the number of samples (MSAA)
    uint8_t samples() const { return m_samples; }

    /// Return a combination of flags (from \ref Texture::TextureFlags)
    uint8_t flags() const { return m_flags; }

    /// Return the size of this texture
    const int2 &size() const { return m_size; }

    /// Return the number of bytes consumed per pixel of this texture
    size_t bytes_per_pixel() const;

    /// Return the number of channels of this texture
    size_t channels() const;

    /// Upload packed pixel data from the CPU to the GPU
    void upload(const uint8_t *data);

    /// Upload packed pixel data to a rectangular sub-region of the texture from the CPU to the GPU
    void upload_sub_region(const uint8_t *data, const int2 &origin, const int2 &size);

    /// Download packed pixel data from the GPU to the CPU
    void download(uint8_t *data);

    /// Resize the texture (discards the current contents)
    void resize(const int2 &size);

    /// Generates the mipmap. Done automatically upon upload if manual mipmapping is disabled.
    void generate_mipmap();

#if defined(HELLOIMGUI_HAS_OPENGL)
    uint32_t texture_handle() const { return m_texture_handle; }
    uint32_t renderbuffer_handle() const { return m_renderbuffer_handle; }
#elif defined(HELLOIMGUI_HAS_METAL)
    void *texture_handle() const { return m_texture_handle; }
    void *sampler_state_handle() const { return m_sampler_state_handle; }
#endif

protected:
    /// Initialize the texture handle
    void init();

protected:
    PixelFormat       m_pixel_format;
    ComponentFormat   m_component_format;
    InterpolationMode m_min_interpolation_mode;
    InterpolationMode m_mag_interpolation_mode;
    WrapMode          m_wrap_mode;
    uint8_t           m_samples;
    uint8_t           m_flags;
    int2              m_size;
    bool              m_manual_mipmapping;

#if defined(HELLOIMGUI_HAS_OPENGL)
    uint32_t m_texture_handle      = 0;
    uint32_t m_renderbuffer_handle = 0;
#elif defined(HELLOIMGUI_HAS_METAL)
    void *m_texture_handle       = nullptr;
    void *m_sampler_state_handle = nullptr;
#endif
};
