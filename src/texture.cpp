#include "texture.h"
#include <memory>

Texture::Texture(PixelFormat pixel_format, ComponentFormat component_format, int2 size,
                 InterpolationMode min_interpolation_mode, InterpolationMode mag_interpolation_mode, WrapMode wrap_mode,
                 uint8_t samples, uint8_t flags, bool manual_mipmapping) :
    m_pixel_format(pixel_format),
    m_component_format(component_format), m_min_interpolation_mode(min_interpolation_mode),
    m_mag_interpolation_mode(mag_interpolation_mode), m_wrap_mode(wrap_mode), m_samples(samples), m_flags(flags),
    m_size(size), m_manual_mipmapping(manual_mipmapping)
{
    init();
}

size_t Texture::bytes_per_pixel() const
{
    size_t result = 0;
    switch (m_component_format)
    {
    case ComponentFormat::UInt8: result = 1; break;
    case ComponentFormat::Int8: result = 1; break;
    case ComponentFormat::UInt16: result = 2; break;
    case ComponentFormat::Int16: result = 2; break;
    case ComponentFormat::UInt32: result = 4; break;
    case ComponentFormat::Int32: result = 4; break;
    case ComponentFormat::Float16: result = 2; break;
    case ComponentFormat::Float32: result = 4; break;
    default: throw std::invalid_argument("Texture::bytes_per_pixel(): invalid component format!");
    }

    return result * channels();
}

size_t Texture::channels() const
{
    size_t result = 1;
    switch (m_pixel_format)
    {
    case PixelFormat::R: result = 1; break;
    case PixelFormat::RA: result = 2; break;
    case PixelFormat::RGB: result = 3; break;
    case PixelFormat::RGBA: result = 4; break;
    case PixelFormat::BGR: result = 3; break;
    case PixelFormat::BGRA: result = 4; break;
    case PixelFormat::Depth: result = 1; break;
    case PixelFormat::DepthStencil: result = 2; break;
    default: throw std::invalid_argument("Texture::channels(): invalid pixel format!");
    }
    return result;
}
