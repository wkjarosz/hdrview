precision mediump float;

in vec2 uv;

uniform sampler2D color_texture;
uniform bool      linear_output;
uniform float     sdr_white_level;

out vec4 frag_color;

// scRGB's fixed convention: 1.0 (linear) == 80 nits. The one constant a future generalization of this pass
// (e.g. a real per-transfer-function table, needed for eventual Linux/Wayland PQ/HLG support, mirroring
// nanogui-1's transferWhiteLevel()) would replace.
const float SCRGB_REFERENCE_WHITE_NITS = 80.0;

void main()
{
    vec4 color = texture(color_texture, uv);
    // The offscreen texture always holds ordinary sRGB-encoded colors (Dear ImGui and HDRView's own image
    // shader are both unaware of the real display's transfer function, and always emit sRGB). Windows' scRGB
    // HDR framebuffer instead expects genuinely linear values, so decode here; everywhere else, pass through.
    //
    // The decoded value is then scaled by the display's actual configured SDR white level (in nits) relative
    // to scRGB's fixed 80-nit reference -- otherwise our output would be dimmer than plain SDR mode whenever
    // the user has that Windows setting above the 80-nit default (very common). Applied uniformly to the
    // whole buffer (image content and UI alike), matching tev/nanogui's default behavior.
    frag_color =
        linear_output ? vec4(sRGBToLinear(color.rgb) * (sdr_white_level / SCRGB_REFERENCE_WHITE_NITS), color.a)
                       : color;
}
