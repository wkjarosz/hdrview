#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct Uniforms
{
    uint has_reference;
    uint do_dither;
    float2 randomness;
    int blend_mode;
    int channel;
    float gain;
    float gamma;
    uint sRGB;
};

struct VertexOut
{
    float2 image_uv [[user(locn0)]];
    float2 reference_uv [[user(locn1)]];
};

// Implementation of the GLSL mod() function, which is slightly different than Metal fmod()
template<typename Tx, typename Ty>
inline Tx mod(Tx x, Ty y)
{
    return x - y * floor(x / y);
}

static inline __attribute__((always_inline))
float3 tonemap(thread const float3& color, constant Uniforms& ubo)
{
    return pow(color, float3(1.0 / ubo.gamma));
}

fragment float4 fragment_main(VertexOut in [[stage_in]],
                              constant Uniforms& ubo [[buffer(0)]],
                              texture2d<float> image [[texture(0)]],
                              texture2d<float> reference [[texture(1)]],
                              sampler image_sampler [[sampler(0)]],
                              sampler reference_sampler [[sampler(1)]],
                              float4 position [[position]])
{
    float dark_gray = 0.1;
    float light_gray = 0.2;
    float checkerboard = (mod(float(int(floor(position.x / 8.0) + floor(position.y / 8.0))), 2.0) == 0.0) ? dark_gray : light_gray;
    float4 background = float4(float3(checkerboard), 1.0);

    float4 value = image.sample(image_sampler, in.image_uv);
    if (in.image_uv.x > 1.0 || in.image_uv.y > 1.0 || in.image_uv.x < 0.0 || in.image_uv.y < 0.0)
        value = float4(0.0);

    if (ubo.has_reference != 0u)
    {
        float4 reference_val = reference.sample(reference_sampler, in.reference_uv);
        if (in.reference_uv.x > 1.0 || in.reference_uv.y > 1.0 || in.reference_uv.x < 0.0 || in.reference_uv.y < 0.0)
            reference_val = vec4(0.0);
    }

    float3 param = value.xyz * ubo.gain;
    float3 blended = mix(background.xyz, tonemap(param, ubo), float3(value.w));
    return float4(blended.x, blended.y, blended.z, out.frag_color.w);
}

