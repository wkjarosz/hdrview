#include "imageview.h"
#include <nanogui/renderpass.h>
#include <nanogui/shader.h>
#include <nanogui/texture.h>
#include <nanogui/screen.h>
#include <nanogui/opengl.h>
#include <iostream>
#include "common.h"
#include "dithermatrix256.h"
#include <random>
#include <sstream>
#include <spdlog/spdlog.h>

using namespace nanogui;
using namespace std;

namespace
{
std::mt19937 g_rand(53);
const float MIN_ZOOM = 0.01f;
const float MAX_ZOOM = 512.f;

// Vertex shader
constexpr char const *const vertex_shader =
R"(#version 330

uniform vec2 image_scale;
uniform vec2 image_pos;
uniform vec2 reference_scale;
uniform vec2 reference_pos;

in vec2 position;

out vec2 image_uv;
out vec2 reference_uv;

void main()
{
    image_uv = (position/2.0 - image_pos + 0.5) / image_scale;
    reference_uv = (position/2.0 - reference_pos + 0.5) / reference_scale;
    gl_Position  = vec4(position.x, -position.y, 0.0, 1.0);
}
)";

constexpr char const *const fragment_shader =
R"(#version 330

#ifndef saturate
#define saturate(v) clamp(v, 0, 1)
#endif

in vec2 image_uv;
in vec2 reference_uv;
in vec4 gl_FragCoord;

uniform bool has_reference;

uniform sampler2D image;
uniform sampler2D reference;
uniform sampler2D ditherImg;
uniform bool do_dither;
uniform vec2 randomness;

uniform int blend_mode;
uniform int channel;

uniform float gain;
uniform float gamma;
uniform bool sRGB;

out vec4 frag_color;

const vec3 minLab = vec3(0, -128, -128);
const vec3 maxLab = vec3(100, 128, 128);
const vec3 rangeLab = maxLab-minLab;
const vec3 LabD65Wts = vec3(.95047, 1.000, 1.08883);

// fitting polynomials to matplotlib colormaps
//
// License CC0 (public domain) 
//   https://creativecommons.org/share-your-work/public-domain/cc0/
//
// feel free to use these in your own work!
//
// similar to https://www.shadertoy.com/view/XtGGzG but with a couple small differences:
//
//  - use degree 6 instead of degree 5 polynomials
//  - use nested horner representation for polynomials
//  - polynomials were fitted to minimize maximum error (as opposed to least squares)
//
// data fitted from https://github.com/BIDS/colormap/blob/master/colormaps.py
// (which is licensed CC0)


vec3 viridis(float t) {

    const vec3 c0 = vec3(0.2777273272234177, 0.005407344544966578, 0.3340998053353061);
    const vec3 c1 = vec3(0.1050930431085774, 1.404613529898575, 1.384590162594685);
    const vec3 c2 = vec3(-0.3308618287255563, 0.214847559468213, 0.09509516302823659);
    const vec3 c3 = vec3(-4.634230498983486, -5.799100973351585, -19.33244095627987);
    const vec3 c4 = vec3(6.228269936347081, 14.17993336680509, 56.69055260068105);
    const vec3 c5 = vec3(4.776384997670288, -13.74514537774601, -65.35303263337234);
    const vec3 c6 = vec3(-5.435455855934631, 4.645852612178535, 26.3124352495832);

    return c0+t*(c1+t*(c2+t*(c3+t*(c4+t*(c5+t*c6)))));

}

vec3 plasma(float t) {

    const vec3 c0 = vec3(0.05873234392399702, 0.02333670892565664, 0.5433401826748754);
    const vec3 c1 = vec3(2.176514634195958, 0.2383834171260182, 0.7539604599784036);
    const vec3 c2 = vec3(-2.689460476458034, -7.455851135738909, 3.110799939717086);
    const vec3 c3 = vec3(6.130348345893603, 42.3461881477227, -28.51885465332158);
    const vec3 c4 = vec3(-11.10743619062271, -82.66631109428045, 60.13984767418263);
    const vec3 c5 = vec3(10.02306557647065, 71.41361770095349, -54.07218655560067);
    const vec3 c6 = vec3(-3.658713842777788, -22.93153465461149, 18.19190778539828);

    return c0+t*(c1+t*(c2+t*(c3+t*(c4+t*(c5+t*c6)))));

}

vec3 magma(float t) {

    const vec3 c0 = vec3(-0.002136485053939582, -0.000749655052795221, -0.005386127855323933);
    const vec3 c1 = vec3(0.2516605407371642, 0.6775232436837668, 2.494026599312351);
    const vec3 c2 = vec3(8.353717279216625, -3.577719514958484, 0.3144679030132573);
    const vec3 c3 = vec3(-27.66873308576866, 14.26473078096533, -13.64921318813922);
    const vec3 c4 = vec3(52.17613981234068, -27.94360607168351, 12.94416944238394);
    const vec3 c5 = vec3(-50.76852536473588, 29.04658282127291, 4.23415299384598);
    const vec3 c6 = vec3(18.65570506591883, -11.48977351997711, -5.601961508734096);

    return c0+t*(c1+t*(c2+t*(c3+t*(c4+t*(c5+t*c6)))));

}

vec3 inferno(float t) {

    const vec3 c0 = vec3(0.0002189403691192265, 0.001651004631001012, -0.01948089843709184);
    const vec3 c1 = vec3(0.1065134194856116, 0.5639564367884091, 3.932712388889277);
    const vec3 c2 = vec3(11.60249308247187, -3.972853965665698, -15.9423941062914);
    const vec3 c3 = vec3(-41.70399613139459, 17.43639888205313, 44.35414519872813);
    const vec3 c4 = vec3(77.162935699427, -33.40235894210092, -81.80730925738993);
    const vec3 c5 = vec3(-71.31942824499214, 32.62606426397723, 73.20951985803202);
    const vec3 c6 = vec3(25.13112622477341, -12.24266895238567, -23.07032500287172);

    return c0+t*(c1+t*(c2+t*(c3+t*(c4+t*(c5+t*c6)))));

}

float linearToS(float a)
{
    return a < 0.0031308 ? 12.92 * a : 1.055 * pow(a, 1.0/2.4) - 0.055;
}

vec3 linearToSRGB(vec3 color)
{
    return vec3(linearToS(color.r), linearToS(color.g), linearToS(color.b));
}

float sToLinear(float a)
{
    return a < 0.04045 ? (1.0 / 12.92) * a : pow((a + 0.055) * (1.0 / 1.055), 2.4);
}

vec3 sRGBToLinear(vec3 color)
{
    return vec3(sToLinear(color.r), sToLinear(color.g), sToLinear(color.b));
}

vec3 tonemap(vec3 color)
{
    return sRGB ? linearToSRGB(color) : pow(color, vec3(1.0/gamma));
}


// returns the luminance of a linear rgb color
vec3 RGBToLuminance(vec3 rgb)
{
    const vec3 RGB2Y = vec3(0.212671, 0.715160, 0.072169);
    return vec3(dot(RGB2Y, rgb));
}

// Converts a color from linear RGB to XYZ space
vec3 RGBToXYZ(vec3 rgb)
{
    const mat3 RGB2XYZ = mat3(
        0.412453, 0.212671, 0.019334,
        0.357580, 0.715160, 0.119193,
        0.180423, 0.072169, 0.950227);
    return RGB2XYZ * rgb;
}

// Converts a color from XYZ to linear RGB space
vec3 XYZToRGB(vec3 xyz)
{
    const mat3 XYZ2RGB = mat3(
            3.240479, -0.969256,  0.055648,
        -1.537150,  1.875992, -0.204043,
        -0.498535,  0.041556,  1.057311);
    return XYZ2RGB * xyz;
}

float labf(float t)
{
    const float c1 = 0.008856451679;    // pow(6.0/29.0, 3.0);
    const float c2 = 7.787037037;       // pow(29.0/6.0, 2.0)/3;
    const float c3 = 0.1379310345;      // 16.0/116.0
    return (t > c1) ? pow(t, 1.0/3.0) : (c2*t) + c3;
}

vec3 XYZToLab(vec3 xyz)
{
    // normalize for D65 white point
    xyz /= LabD65Wts;

    vec3 v = vec3(labf(xyz.x), labf(xyz.y), labf(xyz.z));
    return vec3((116.0 * v.y) - 16.0,
                500.0 * (v.x - v.y),
                200.0 * (v.y - v.z));
}

vec3 LabToXYZ(vec3 lab)
{
    const float eps = 216.0 / 24389.0;
    const float kappa = 24389.0 / 27.0;
    float yr = (lab.x > kappa*eps) ? pow((lab.x + 16.0) / 116.0, 3.) : lab.x / kappa;
    float fy = (yr > eps) ? (lab.x + 16.0) / 116.0 : (kappa*yr + 16.0) / 116.0;
    float fx = lab.y / 500.0 + fy;
    float fz = fy - lab.z / 200.0;

    float fx3 = pow(fx, 3.);
    float fz3 = pow(fz, 3.);

    vec3 xyz = vec3((fx3 > eps) ? fx3 : (116.0 * fx - 16.0) / kappa,
                    yr,
                    (fz3 > eps) ? fz3 : (116.0 * fz - 16.0) / kappa);

    // unnormalize for D65 white point
    xyz *= LabD65Wts;
    return xyz;
}

vec3 RGBToLab(vec3 rgb)
{
    vec3 lab = XYZToLab(RGBToXYZ(rgb));

    // renormalize
    return (lab-minLab)/rangeLab;
}

vec3 LabToRGB(vec3 lab)
{
    // unnormalize
    lab = lab*rangeLab + minLab;

    return XYZToRGB(LabToXYZ(lab));
}

vec3 jetFalseColor(float x)
{
    float r = saturate((x < 0.7) ? 4.0 * x - 1.5 : -4.0 * x + 4.5);
    float g = saturate((x < 0.5) ? 4.0 * x - 0.5 : -4.0 * x + 3.5);
    float b = saturate((x < 0.3) ? 4.0 * x + 0.5 : -4.0 * x + 2.5);
    return vec3(r, g, b);
}

vec3 positiveNegative(vec3 col)
{
    float x = dot(col, vec3(1.0)/3.0);
    float r = saturate(mix(0.0, 1.0, max(x, 0.0)));
    float g = 0.0;
    float b = saturate(mix(0.0, 1.0, -min(x, 0.0)));
    return vec3(r, g, b);
}

// note: uniformly distributed, normalized rand, [0;1[
float nrand(vec2 n)
{
    return fract(sin(dot(n.xy, vec2(12.9898, 78.233)))* 43758.5453);
}

float randZeroMeanUniform(vec2 xy)
{
    // Result is in range [-0.5, 0.5]
    return texture(ditherImg, xy/vec2(256,256)).r/65536 - 0.5;
}

float randZeroMeanTriangle(vec2 xy)
{
    float r = randZeroMeanUniform(xy);

    // Convert uniform distribution into triangle-shaped distribution
    // Result is in range [-1.0,1.0]
    float rp = sqrt(2*r);       // positive triangle
    float rn = sqrt(2*r+1)-1;   // negative triangle
    return (r < 0) ? rn : rp;
}

vec3 chooseChannel(vec3 col)
{
    switch (channel)
    {
        case CHANNEL_RED:               return col.rrr;
        case CHANNEL_GREEN:             return col.ggg;
        case CHANNEL_BLUE:              return col.bbb;
        case CHANNEL_LUMINANCE:         return RGBToLuminance(col);
        case CHANNEL_CIE_L:             return RGBToLab(col).xxx;
        case CHANNEL_CIE_a:             return RGBToLab(col).yyy;
        case CHANNEL_CIE_b:             return RGBToLab(col).zzz;
        case CHANNEL_CIE_CHROMATICITY:  return LabToRGB(vec3(0.5, RGBToLab(col).yz));
        // case CHANNEL_FALSE_COLOR:       return jetFalseColor(saturate(RGBToLuminance(col).r));
        case CHANNEL_FALSE_COLOR:       return inferno(saturate(RGBToLuminance(col).r));
        case CHANNEL_POSITIVE_NEGATIVE: return positiveNegative(col);
    }
    return col;
}

vec4 blend(vec4 imageVal, vec4 referenceVal)
{
    vec3 diff = imageVal.rgb - referenceVal.rgb;
    float alpha = imageVal.a + referenceVal.a*(1-imageVal.a);
    switch (blend_mode)
    {
        case NORMAL_BLEND:              return vec4(imageVal.rgb*imageVal.a + referenceVal.rgb*referenceVal.a*(1-imageVal.a), alpha);
        case MULTIPLY_BLEND:            return vec4(imageVal.rgb * referenceVal.rgb, alpha);
        case DIVIDE_BLEND:              return vec4(imageVal.rgb / referenceVal.rgb, alpha);
        case ADD_BLEND:                 return vec4(imageVal.rgb + referenceVal.rgb, alpha);
        case AVERAGE_BLEND:             return 0.5*(imageVal + referenceVal);
        case SUBTRACT_BLEND:            return vec4(diff, alpha);
        case DIFFERENCE_BLEND:          return vec4(abs(diff), alpha);
        case RELATIVE_DIFFERENCE_BLEND: return vec4(abs(diff) / (referenceVal.rgb + vec3(0.01)), alpha);
    }
    return vec4(0.0);
}

vec3 dither(vec3 color)
{
    if (!do_dither)
		return color;

    return color + vec3(randZeroMeanTriangle(gl_FragCoord.xy + randomness)/255.0);
}

void main() {
    float darkGray = 0.1;
    float lightGray = 0.2;

    float checkerboard = mod(int(floor(gl_FragCoord.x / 8) + floor(gl_FragCoord.y / 8)), 2) == 0 ? darkGray : lightGray;

    vec4 background = vec4(vec3(checkerboard), 1.0);

    vec4 value = texture(image, image_uv);
    if (image_uv.x > 1.0 || image_uv.y > 1.0 || image_uv.x < 0.0 || image_uv.y < 0.0)
        value.a = 0.0;
    
    if (has_reference)
    {
        vec4 reference_val = texture(reference, reference_uv);
        if (reference_uv.x > 1.0 || reference_uv.y > 1.0 || reference_uv.x < 0.0 || reference_uv.y < 0.0)
            reference_val.a = 0.0;

        value = blend(value, reference_val);
    }

	frag_color.a = 1.0;
    frag_color.rgb = mix(background.rgb, dither(tonemap(chooseChannel(gain * value.rgb))), value.a);
}
)";



#define DEFINE_PARAMS(parent,name) defines += std::string("#define ") + #name + std::string(" ") + to_string(parent::name) + "\n";
#define DEFINE_PARAMS2(parent,name,prefix) defines += std::string("#define ") + #prefix#name + std::string(" ") + to_string(parent::name) + "\n";

std::string add_defines(std::string shader_string)
{
    std::string defines;

    // add #defines to the shader
    DEFINE_PARAMS2(EChannel, RED, CHANNEL_);
    DEFINE_PARAMS2(EChannel, GREEN, CHANNEL_);
    DEFINE_PARAMS2(EChannel, BLUE, CHANNEL_);
    DEFINE_PARAMS2(EChannel, RGB, CHANNEL_);
    DEFINE_PARAMS2(EChannel, LUMINANCE, CHANNEL_);
    DEFINE_PARAMS2(EChannel, CIE_L, CHANNEL_);
    DEFINE_PARAMS2(EChannel, CIE_a, CHANNEL_);
    DEFINE_PARAMS2(EChannel, CIE_b, CHANNEL_);
    DEFINE_PARAMS2(EChannel, CIE_CHROMATICITY, CHANNEL_);
    DEFINE_PARAMS2(EChannel, FALSE_COLOR, CHANNEL_);
    DEFINE_PARAMS2(EChannel, POSITIVE_NEGATIVE, CHANNEL_);

    DEFINE_PARAMS(EBlendMode, NORMAL_BLEND);
    DEFINE_PARAMS(EBlendMode, MULTIPLY_BLEND);
    DEFINE_PARAMS(EBlendMode, DIVIDE_BLEND);
    DEFINE_PARAMS(EBlendMode, ADD_BLEND);
    DEFINE_PARAMS(EBlendMode, AVERAGE_BLEND);
    DEFINE_PARAMS(EBlendMode, SUBTRACT_BLEND);
    DEFINE_PARAMS(EBlendMode, DIFFERENCE_BLEND);
    DEFINE_PARAMS(EBlendMode, RELATIVE_DIFFERENCE_BLEND);

    if (!defines.empty())
    {
        if (shader_string.length() > 8 && shader_string.substr(0, 8) == "#version")
        {
            std::istringstream iss(shader_string);
            std::ostringstream oss;
            std::string line;
            std::getline(iss, line);
            oss << line << std::endl;
            oss << defines;
            while (std::getline(iss, line))
                oss << line << std::endl;
            shader_string = oss.str();
        }
        else
        {
            shader_string = defines + shader_string;
        }
    }

    return shader_string;
}

}


HDRImageView::HDRImageView(Widget *parent)
    : Canvas(parent, 1, false, false, true),
    m_exposure_callback(std::function<void(float)>()), m_gamma_callback(std::function<void(float)>()),
	m_sRGB_callback(std::function<void(bool)>()), m_zoom_callback(std::function<void(float)>())
{
    m_zoom = 1.f / screen()->pixel_ratio();
    m_offset = Vector2f(0.0f);

    set_background_color(Color(0.15f, 0.15f, 0.15f, 1.f));
    
    try
    {
        m_image_shader = new Shader(
            render_pass(),
            /* An identifying name */
            "ImageView",
            vertex_shader, add_defines(fragment_shader),
            Shader::BlendMode::AlphaBlend
        );

        const float positions[] = {
            -1.f, -1.f, 1.f, -1.f, -1.f, 1.f,
            1.f, -1.f, 1.f,  1.f, -1.f, 1.f
        };

        set_draw_border(false);

        m_image_shader->set_buffer("position", VariableType::Float32, { 6, 2 }, positions);
        m_render_pass->set_cull_mode(RenderPass::CullMode::Disabled);

        m_dither_tex = new Texture(
                Texture::PixelFormat::R,
                Texture::ComponentFormat::Float32,
                Vector2i(256, 256),
                Texture::InterpolationMode::Nearest,
                Texture::InterpolationMode::Nearest,
                Texture::WrapMode::Repeat);
        m_dither_tex->upload((const uint8_t *)dither_matrix256);
        m_image_shader->set_texture("ditherImg", m_dither_tex);

        // create an empty texture so that nanogui's shader doesn't print errors
        // before we've selected a reference image
        // FIXME: at some point, find a more elegant solution for this.
        m_reference_image = new Texture(
                Texture::PixelFormat::R,
                Texture::ComponentFormat::Float32,
                Vector2i(0, 0),
                Texture::InterpolationMode::Nearest,
                Texture::InterpolationMode::Nearest,
                Texture::WrapMode::Repeat);
        m_image_shader->set_texture("reference", m_reference_image);
    }
    catch(const std::exception& e)
    {
        spdlog::get("console")->trace("{}", e.what());
    }
}

void HDRImageView::set_current_image(TextureRef cur)
{
    spdlog::get("console")->debug("setting current image: {}", cur);
    m_current_image = std::move(cur);
    if (m_current_image)
        m_image_shader->set_texture("image", m_current_image);
}

void HDRImageView::set_reference_image(TextureRef ref)
{
    spdlog::get("console")->debug("setting reference image: {}", ref);
    m_reference_image = std::move(ref);
    if (m_reference_image)
        m_image_shader->set_texture("reference", m_reference_image);
}


Vector2f HDRImageView::center_offset(TextureRef img) const
{
	return (size_f() - scaled_image_size_f(img)) / 2;
}

Vector2f HDRImageView::image_coordinate_at(const Vector2f& position) const
{
	auto image_pos = position - (m_offset + center_offset(m_current_image));
	return image_pos / m_zoom;
}

Vector2f HDRImageView::position_for_coordinate(const Vector2f& imageCoordinate) const
{
	return m_zoom * imageCoordinate + (m_offset + center_offset(m_current_image));
}

Vector2f HDRImageView::screen_position_for_coordinate(const Vector2f& imageCoordinate) const
{
	return position_for_coordinate(imageCoordinate) + position_f();
}

void HDRImageView::set_image_coordinate_at(const Vector2f& position, const Vector2f& imageCoordinate)
{
	// Calculate where the new offset must be in order to satisfy the image position equation.
	// Round the floating point values to balance out the floating point to integer conversions.
	m_offset = position - (imageCoordinate * m_zoom);

	// Clamp offset so that the image remains near the screen.
	m_offset = max(min(m_offset, size_f()), -scaled_image_size_f(m_current_image));

	m_offset -= center_offset(m_current_image);
}

void HDRImageView::image_position_and_scale(Vector2f& position, Vector2f& scale, TextureRef image)
{
	scale = scaled_image_size_f(image) / Vector2f(size());
	position = (m_offset + center_offset(image)) / Vector2f(size());
}

void HDRImageView::center()
{
	m_offset = Vector2f(0.f, 0.f);
}

void HDRImageView::fit()
{
	// Calculate the appropriate scaling factor.
	Vector2f factor(size_f() / image_size_f(m_current_image));
	m_zoom = std::min(factor[0], factor[1]);
	center();

	m_zoom_callback(m_zoom);
}

void HDRImageView::set_zoom_level(float level)
{
	m_zoom = ::clamp(std::pow(m_zoom_sensitivity, level), MIN_ZOOM, MAX_ZOOM);
	m_zoom_level = log(m_zoom) / log(m_zoom_sensitivity);

	m_zoom_callback(m_zoom);
}

void HDRImageView::zoom_by(float amount, const Vector2f& focusPosition)
{
	auto focusedCoordinate = image_coordinate_at(focusPosition);
	float scaleFactor = std::pow(m_zoom_sensitivity, amount);
	m_zoom = ::clamp(scaleFactor * m_zoom, MIN_ZOOM, MAX_ZOOM);
	m_zoom_level = log(m_zoom) / log(m_zoom_sensitivity);
	set_image_coordinate_at(focusPosition, focusedCoordinate);

	m_zoom_callback(m_zoom);
}

void HDRImageView::zoom_in()
{
	// keep position at center of window fixed while zooming
	auto centerPosition = Vector2f(size_f() / 2.f);
	auto centerCoordinate = image_coordinate_at(centerPosition);

	// determine next higher power of 2 zoom level
	float levelForPow2Sensitivity = ceil(log(m_zoom) / log(2.f) + 0.5f);
	float newScale = std::pow(2.f, levelForPow2Sensitivity);
	m_zoom = ::clamp(newScale, MIN_ZOOM, MAX_ZOOM);
	m_zoom_level = log(m_zoom) / log(m_zoom_sensitivity);
	set_image_coordinate_at(centerPosition, centerCoordinate);

	m_zoom_callback(m_zoom);
}

void HDRImageView::zoom_out()
{
	// keep position at center of window fixed while zooming
	auto centerPosition = Vector2f(size_f() / 2.f);
	auto centerCoordinate = image_coordinate_at(centerPosition);

	// determine next lower power of 2 zoom level
	float levelForPow2Sensitivity = floor(log(m_zoom) / log(2.f) - 0.5f);
	float newScale = std::pow(2.f, levelForPow2Sensitivity);
	m_zoom = ::clamp(newScale, MIN_ZOOM, MAX_ZOOM);
	m_zoom_level = log(m_zoom) / log(m_zoom_sensitivity);
	set_image_coordinate_at(centerPosition, centerCoordinate);

	m_zoom_callback(m_zoom);
}

bool HDRImageView::mouse_drag_event(const Vector2i& p, const Vector2i& rel, int /* button */, int /*modifiers*/)
{
    if (!m_enabled || !m_current_image)
        return false;
        
    set_image_coordinate_at(p + rel, image_coordinate_at(p));
    
    return true;
}

bool HDRImageView::scroll_event(const Vector2i& p, const Vector2f& rel)
{
	if (Canvas::scroll_event(p, rel))
		return true;

	// query glfw directly to check if a modifier key is pressed
	int lState = glfwGetKey(screen()->glfw_window(), GLFW_KEY_LEFT_SHIFT);
	int rState = glfwGetKey(screen()->glfw_window(), GLFW_KEY_RIGHT_SHIFT);

	if (lState == GLFW_PRESS || rState == GLFW_PRESS)
	{
		// panning
		set_image_coordinate_at(Vector2f(p) + rel * 4.f, image_coordinate_at(p));
		return true;
	}
	else //if (screen()->modifiers() == 0)
	{
		// zooming
		float v = rel.y();
		if (std::abs(v) < 1)
			v = std::copysign(1.f, v);
		zoom_by(v / 4.f, p - position());

		return true;
	}

	return false;
}

bool HDRImageView::keyboard_event(int key, int /* scancode */, int action, int /* modifiers */) {
    if (!m_enabled || !m_current_image)
        return false;

    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_R) {
            center();
            return true;
        }
    }
    return false;
}

void HDRImageView::draw(NVGcontext *ctx)
{
    if (size().x() <= 1 || size().y() <= 1)
        return;

    Canvas::draw(ctx);      // calls HDRImageView draw_contents

    if (m_current_image)
    {
        draw_image_border(ctx);
        draw_pixel_grid(ctx);
        draw_pixel_info(ctx);
    }
    
    draw_widget_border(ctx);
}



void HDRImageView::draw_image_border(NVGcontext* ctx) const
{
    if (!m_current_image || squared_norm(m_current_image->size()) == 0)
        return;

	int ds = m_theme->m_window_drop_shadow_size, cr = m_theme->m_window_corner_radius;

	Vector2i borderPosition = m_pos + Vector2i(m_offset + center_offset(m_current_image));
	Vector2i borderSize(scaled_image_size_f(m_current_image));

	if (m_reference_image && squared_norm(m_reference_image->size()) > 0)
	{
		borderPosition = min(borderPosition, m_pos + Vector2i(m_offset + center_offset(m_reference_image)));
		borderSize = max(borderSize, Vector2i(scaled_image_size_f(m_reference_image)));
	}

	// Draw a drop shadow
	NVGpaint shadowPaint =
		nvgBoxGradient(ctx, borderPosition.x(), borderPosition.y(), borderSize.x(), borderSize.y(), cr * 2, ds * 2,
					   m_theme->m_drop_shadow, m_theme->m_transparent);

	nvgSave(ctx);
	nvgBeginPath(ctx);
	nvgScissor(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y());
	nvgRect(ctx, borderPosition.x() - ds, borderPosition.y() - ds, borderSize.x() + 2 * ds, borderSize.y() + 2 * ds);
	nvgRoundedRect(ctx, borderPosition.x(), borderPosition.y(), borderSize.x(), borderSize.y(), cr);
	nvgPathWinding(ctx, NVG_HOLE);
	nvgFillPaint(ctx, shadowPaint);
	nvgFill(ctx);
	nvgRestore(ctx);

	// draw a line border
	nvgSave(ctx);
	nvgBeginPath(ctx);
	nvgScissor(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y());
	nvgStrokeWidth(ctx, 2.0f);
	nvgRect(ctx, borderPosition.x() - 0.5f, borderPosition.y() - 0.5f, borderSize.x() + 1, borderSize.y() + 1);
	nvgStrokeColor(ctx, Color(0.5f, 0.5f, 0.5f, 1.0f));
	nvgStroke(ctx);
	nvgResetScissor(ctx);
	nvgRestore(ctx);
}

void HDRImageView::draw_pixel_grid(NVGcontext* ctx) const
{
    if (!m_current_image)
        return;

    if (!m_draw_grid || (m_grid_threshold == -1) || (m_zoom <= m_grid_threshold))
        return;

	float factor = clamp01((m_zoom - m_grid_threshold) / (2 * m_grid_threshold));
	float alpha = lerp(0.0f, 0.2f, smoothStep(0.0f, 1.0f, factor));
    
    if (alpha > 0.0f)
    {
        Vector2f xy0 = screen_position_for_coordinate(Vector2f(0.0f));
        int minJ = max(0, int(-xy0.y() / m_zoom));
        int maxJ = min(m_current_image->size().y(), int(ceil((screen()->size().y() - xy0.y()) / m_zoom)));
        int minI = max(0, int(-xy0.x() / m_zoom));
        int maxI = min(m_current_image->size().x(), int(ceil((screen()->size().x() - xy0.x()) / m_zoom)));

        nvgBeginPath(ctx);

        // draw vertical lines
        for (int i = minI; i <= maxI; ++i)
        {
            Vector2f sxy0 = screen_position_for_coordinate(Vector2f(i, minJ));
            Vector2f sxy1 = screen_position_for_coordinate(Vector2f(i, maxJ));
            nvgMoveTo(ctx, sxy0.x(), sxy0.y());
            nvgLineTo(ctx, sxy1.x(), sxy1.y());
        }

        // draw horizontal lines
        for (int j = minJ; j <= maxJ; ++j)
        {
            Vector2f sxy0 = screen_position_for_coordinate(Vector2f(minI, j));
            Vector2f sxy1 = screen_position_for_coordinate(Vector2f(maxI, j));
            nvgMoveTo(ctx, sxy0.x(), sxy0.y());
            nvgLineTo(ctx, sxy1.x(), sxy1.y());
        }

        nvgStrokeWidth(ctx, 2.0f);
        nvgStrokeColor(ctx, Color(1.0f, 1.0f, 1.0f, alpha));
        nvgStroke(ctx);
    }
}


void HDRImageView::draw_widget_border(NVGcontext* ctx) const
{
	int ds = m_theme->m_window_drop_shadow_size, cr = m_theme->m_window_corner_radius;

    if (m_size.x() <= ds || m_size.y() <= ds)
        return;
    
	// Draw an inner drop shadow. (adapted from Window) and tev
	NVGpaint shadowPaint =
		nvgBoxGradient(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y(), cr, ds, m_theme->m_transparent,
					   m_theme->m_drop_shadow);

	nvgSave(ctx);
	nvgResetScissor(ctx);
	nvgBeginPath(ctx);
	nvgRect(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y());
	nvgRoundedRect(ctx, m_pos.x() + ds, m_pos.y() + ds, m_size.x() - 2 * ds, m_size.y() - 2 * ds, cr);
	nvgPathWinding(ctx, NVG_HOLE);
	nvgFillPaint(ctx, shadowPaint);
	nvgFill(ctx);
	nvgRestore(ctx);
}

void HDRImageView::draw_pixel_info(NVGcontext* ctx) const
{
    if (!m_draw_values || (m_pixel_info_threshold == -1) || (m_zoom <= m_pixel_info_threshold))
        return;

    float factor = clamp01((m_zoom - m_pixel_info_threshold) / (2 * m_pixel_info_threshold));
    float alpha = lerp(0.0f, 0.5f, smoothStep(0.0f, 1.0f, factor));

    if (alpha > 0.0f && m_pixel_callback)
    {
        nvgSave(ctx);
        nvgIntersectScissor(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y());

        Vector2f xy0 = screen_position_for_coordinate(Vector2f(0.0f));
        int minJ = max(0, int(-xy0.y() / m_zoom));
        int maxJ = min(m_current_image->size().y() - 1, int(ceil((screen()->size().y() - xy0.y()) / m_zoom)));
        int minI = max(0, int(-xy0.x() / m_zoom));
        int maxI = min(m_current_image->size().x() - 1, int(ceil((screen()->size().x() - xy0.x()) / m_zoom)));

        float font_size = m_zoom / 31.0f * 7;
        nvgFontFace(ctx, "sans");
        nvgFontSize(ctx, font_size);
        nvgTextAlign(ctx, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        size_t bsize = 20;
        char text_buf[4*bsize], *text[4] = { text_buf, text_buf + bsize, text_buf + 2*bsize, text_buf + 3*bsize };

        for (int j = minJ; j <= maxJ; ++j)
        {
            for (int i = minI; i <= maxI; ++i)
            {
                m_pixel_callback(Vector2i(i, j), text, bsize);

                auto pos = screen_position_for_coordinate(Vector2f(i+0.5f, j+0.5f));

                for (int ch = 0; ch < 4; ++ch)
                {
                    Color col(0.f, 0.f, 0.f, alpha);
                    nvgFillColor(ctx, col);
                    nvgFontBlur(ctx, 20);
                    float xpos = pos.x(),
                          ypos = pos.y() + (ch - 1.5f) * font_size;
                    nvgText(ctx, xpos, ypos, text[ch], nullptr);
                    col = Color(0.3f, 0.3f, 0.3f, alpha);
                    if (ch == 3)
                        col[0] = col[1] = col[2] = 1.f;
                    else
                        col[ch] = 1.f;
                    nvgFillColor(ctx, col);
                    nvgFontBlur(ctx, 0);
                    nvgText(ctx, xpos, ypos, text[ch], nullptr);
                }
            }
        }

        nvgRestore(ctx);
    }
}

void HDRImageView::draw_contents()
{
    if (m_current_image && size().x() > 0 && size().y() > 0)
    {
        Vector2f randomness(std::generate_canonical<float, 10>(g_rand)*255,
                                     std::generate_canonical<float, 10>(g_rand)*255);

        m_image_shader->set_uniform("randomness", randomness);
        m_image_shader->set_uniform("gain", (float)powf(2.0f, m_exposure));
        m_image_shader->set_uniform("gamma", m_gamma);
        m_image_shader->set_uniform("sRGB", (bool)m_sRGB);
        m_image_shader->set_uniform("do_dither", (bool)m_dither);

        Vector2f pCurrent, sCurrent;
        image_position_and_scale(pCurrent, sCurrent, m_current_image);
        m_image_shader->set_uniform("image_pos", pCurrent);
        m_image_shader->set_uniform("image_scale", sCurrent);

        m_image_shader->set_uniform("blend_mode", (int)m_blend_mode);
        m_image_shader->set_uniform("channel", (int)m_channel);

        if (m_reference_image)
		{
			Vector2f pReference, sReference;
			image_position_and_scale(pReference, sReference, m_reference_image);
            m_image_shader->set_uniform("has_reference", true);
            m_image_shader->set_uniform("reference_pos", pCurrent);
            m_image_shader->set_uniform("reference_scale", sCurrent);
		}
        else
        {
            m_image_shader->set_uniform("has_reference", false);
            m_image_shader->set_uniform("reference_pos", Vector2f(1.f,1.f));
            m_image_shader->set_uniform("reference_scale", Vector2f(1.f,1.f));
        }
        
        m_image_shader->begin();
        m_image_shader->draw_array(Shader::PrimitiveType::Triangle, 0, 6, false);
        m_image_shader->end();
    }
}
