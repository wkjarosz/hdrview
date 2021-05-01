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
#include <map>
#include <sstream>

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

in vec2 position;

out vec2 uv;

void main()
{
    uv = (position/2.0 - image_pos + 0.5) / image_scale;
    gl_Position  = vec4(position.x, -position.y, 0.0, 1.0);
}
)";

constexpr char const *const fragment_shader =
R"(#version 330

#ifndef saturate
#define saturate(v) clamp(v, 0, 1)
#endif

in vec2 uv;
in vec4 gl_FragCoord;

uniform sampler2D image;
uniform sampler2D ditherImg;
uniform bool hasDither;
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

vec3 jetFalseColor(vec3 col)
{
    float x = saturate(RGBToLuminance(col).r);

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
        case CHANNEL_FALSE_COLOR:       return jetFalseColor(col);
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
    if (!hasDither)
		return color;

    return color + vec3(randZeroMeanTriangle(gl_FragCoord.xy + randomness)/255.0);
}

void main() {
    float darkGray = 0.1;
    float lightGray = 0.2;

    float checkerboard = mod(int(floor(gl_FragCoord.x / 8) + floor(gl_FragCoord.y / 8)), 2) == 0 ? darkGray : lightGray;

    vec4 background = vec4(vec3(checkerboard), 1.0);

    vec4 value = texture(image, uv);
    if (uv.x > 1.0 || uv.y > 1.0 || uv.x < 0.0 || uv.y < 0.0)
        value.a = 0.0;

    if (false)
        vec4 test = blend(value, value);

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

    std::cout << defines << std::endl;

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
    m_exposureCallback(std::function<void(float)>()), m_gammaCallback(std::function<void(float)>()),
	m_sRGBCallback(std::function<void(bool)>()), m_zoomCallback(std::function<void(float)>())
{
    m_zoom = 1.f / screen()->pixel_ratio();
    m_offset = nanogui::Vector2f(0.0f);

    render_pass()->set_clear_color(0, Color(0.3f, 0.3f, 0.32f, 1.f));
    
    try
    {
        m_image_shader = new Shader(
            render_pass(),
            /* An identifying name */
            "a_simple_shader",
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
                nanogui::Vector2i(256, 256),
                Texture::InterpolationMode::Nearest,
                Texture::InterpolationMode::Nearest,
                Texture::WrapMode::Repeat);
        m_dither_tex->upload((const uint8_t *)dither_matrix256);
        m_image_shader->set_texture("ditherImg", m_dither_tex);
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
}

void HDRImageView::set_image(Texture *image) {
    if (image->mag_interpolation_mode() != Texture::InterpolationMode::Nearest)
        throw std::runtime_error(
            "HDRImageView::set_image(): interpolation mode must be set to 'Nearest'!");
    m_image = image;
    m_image_shader->set_texture("image", m_image);
}


nanogui::Vector2f HDRImageView::center_offset(TextureRef img) const
{
	return (size_f() - scaled_image_size_f(img)) / 2;
}

nanogui::Vector2f HDRImageView::image_coordinate_at(const nanogui::Vector2f& position) const
{
	auto image_pos = position - (m_offset + center_offset(m_image));
	return image_pos / m_zoom;
}

nanogui::Vector2f HDRImageView::position_for_coordinate(const nanogui::Vector2f& imageCoordinate) const
{
	return m_zoom * imageCoordinate + (m_offset + center_offset(m_image));
}

nanogui::Vector2f HDRImageView::screen_position_for_coordinate(const nanogui::Vector2f& imageCoordinate) const
{
	return position_for_coordinate(imageCoordinate) + position_f();
}

void HDRImageView::set_image_coordinate_at(const nanogui::Vector2f& position, const nanogui::Vector2f& imageCoordinate)
{
	// Calculate where the new offset must be in order to satisfy the image position equation.
	// Round the floating point values to balance out the floating point to integer conversions.
	m_offset = position - (imageCoordinate * m_zoom);

	// Clamp offset so that the image remains near the screen.
	m_offset = nanogui::max(nanogui::min(m_offset, size_f()), -scaled_image_size_f(m_image));

	m_offset -= center_offset(m_image);
}

void HDRImageView::image_position_and_scale(nanogui::Vector2f& position, nanogui::Vector2f& scale, TextureRef image)
{
	scale = scaled_image_size_f(image) / Vector2f(size());
	position = (m_offset + center_offset(image)) / Vector2f(size());
}

void HDRImageView::center()
{
	m_offset = nanogui::Vector2f(0.f, 0.f);
}

void HDRImageView::fit()
{
	// Calculate the appropriate scaling factor.
	nanogui::Vector2f factor(size_f() / image_size_f(m_image));
	m_zoom = std::min(factor[0], factor[1]);
	center();

	m_zoomCallback(m_zoom);
}

void HDRImageView::set_zoom_level(float level)
{
	m_zoom = ::clamp(std::pow(m_zoomSensitivity, level), MIN_ZOOM, MAX_ZOOM);
	m_zoomLevel = log(m_zoom) / log(m_zoomSensitivity);

	m_zoomCallback(m_zoom);
}

void HDRImageView::zoom_by(float amount, const nanogui::Vector2f& focusPosition)
{
	auto focusedCoordinate = image_coordinate_at(focusPosition);
	float scaleFactor = std::pow(m_zoomSensitivity, amount);
	m_zoom = ::clamp(scaleFactor * m_zoom, MIN_ZOOM, MAX_ZOOM);
	m_zoomLevel = log(m_zoom) / log(m_zoomSensitivity);
	set_image_coordinate_at(focusPosition, focusedCoordinate);

	m_zoomCallback(m_zoom);
}

void HDRImageView::zoom_in()
{
	// keep position at center of window fixed while zooming
	auto centerPosition = nanogui::Vector2f(size_f() / 2.f);
	auto centerCoordinate = image_coordinate_at(centerPosition);

	// determine next higher power of 2 zoom level
	float levelForPow2Sensitivity = ceil(log(m_zoom) / log(2.f) + 0.5f);
	float newScale = std::pow(2.f, levelForPow2Sensitivity);
	m_zoom = ::clamp(newScale, MIN_ZOOM, MAX_ZOOM);
	m_zoomLevel = log(m_zoom) / log(m_zoomSensitivity);
	set_image_coordinate_at(centerPosition, centerCoordinate);

	m_zoomCallback(m_zoom);
}

void HDRImageView::zoom_out()
{
	// keep position at center of window fixed while zooming
	auto centerPosition = nanogui::Vector2f(size_f() / 2.f);
	auto centerCoordinate = image_coordinate_at(centerPosition);

	// determine next lower power of 2 zoom level
	float levelForPow2Sensitivity = floor(log(m_zoom) / log(2.f) - 0.5f);
	float newScale = std::pow(2.f, levelForPow2Sensitivity);
	m_zoom = ::clamp(newScale, MIN_ZOOM, MAX_ZOOM);
	m_zoomLevel = log(m_zoom) / log(m_zoomSensitivity);
	set_image_coordinate_at(centerPosition, centerCoordinate);

    // FIXME after refactor
	m_zoomCallback(m_zoom);
}


bool HDRImageView::mouse_drag_event(const nanogui::Vector2i& p, const nanogui::Vector2i& rel, int /* button */, int /*modifiers*/)
{
    if (!m_enabled || !m_image)
        return false;
        
    set_image_coordinate_at(p + rel, image_coordinate_at(p));
    
    return true;
}

bool HDRImageView::scroll_event(const nanogui::Vector2i& p, const nanogui::Vector2f& rel)
{
	if (Canvas::scroll_event(p, rel))
		return true;

	// query glfw directly to check if a modifier key is pressed
	int lState = glfwGetKey(screen()->glfw_window(), GLFW_KEY_LEFT_SHIFT);
	int rState = glfwGetKey(screen()->glfw_window(), GLFW_KEY_RIGHT_SHIFT);

	if (lState == GLFW_PRESS || rState == GLFW_PRESS)
	{
		// panning
		set_image_coordinate_at(nanogui::Vector2f(p) + rel * 4.f, image_coordinate_at(p));
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
    if (!m_enabled || !m_image)
        return false;

    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_R) {
            center();
            return true;
        }
    }
    return false;
}

void HDRImageView::draw(NVGcontext *ctx) {
    if (!m_enabled || !m_image)
        return;

    Canvas::draw(ctx);      // calls HDRImageView draw_contents

    draw_image_border(ctx);
    draw_pixel_grid(ctx);
    draw_pixel_info(ctx);
    draw_widget_border(ctx);
}



void HDRImageView::draw_image_border(NVGcontext* ctx) const
{
	int ds = m_theme->m_window_drop_shadow_size, cr = m_theme->m_window_corner_radius;

	nanogui::Vector2i borderPosition = m_pos + nanogui::Vector2i(m_offset + center_offset(m_image));
	nanogui::Vector2i borderSize(scaled_image_size_f(m_image));

	// if (m_referenceImage)
	// {
	// 	borderPosition = nanogui::min(borderPosition, m_pos + nanogui::Vector2i(m_offset + center_offset(m_referenceImage)));
	// 	borderSize = nanogui::max(borderSize, nanogui::Vector2i(scaled_image_size_f(m_referenceImage)));
	// }

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
    if (!m_drawGrid || (m_gridThreshold == -1) || (m_zoom <= m_gridThreshold))
        return;

	float factor = clamp01((m_zoom - m_gridThreshold) / (2 * m_gridThreshold));
	float alpha = lerp(0.0f, 0.2f, smoothStep(0.0f, 1.0f, factor));
    
    if (alpha > 0.0f)
    {
        nanogui::Vector2f xy0 = screen_position_for_coordinate(nanogui::Vector2f(0.0f));
        int minJ = max(0, int(-xy0.y() / m_zoom));
        int maxJ = min(m_image->size().y(), int(ceil((screen()->size().y() - xy0.y()) / m_zoom)));
        int minI = max(0, int(-xy0.x() / m_zoom));
        int maxI = min(m_image->size().x(), int(ceil((screen()->size().x() - xy0.x()) / m_zoom)));

        nvgBeginPath(ctx);

        // draw vertical lines
        for (int i = minI; i <= maxI; ++i)
        {
            nanogui::Vector2f sxy0 = screen_position_for_coordinate(nanogui::Vector2f(i, minJ));
            nanogui::Vector2f sxy1 = screen_position_for_coordinate(nanogui::Vector2f(i, maxJ));
            nvgMoveTo(ctx, sxy0.x(), sxy0.y());
            nvgLineTo(ctx, sxy1.x(), sxy1.y());
        }

        // draw horizontal lines
        for (int j = minJ; j <= maxJ; ++j)
        {
            nanogui::Vector2f sxy0 = screen_position_for_coordinate(nanogui::Vector2f(minI, j));
            nanogui::Vector2f sxy1 = screen_position_for_coordinate(nanogui::Vector2f(maxI, j));
            nvgMoveTo(ctx, sxy0.x(), sxy0.y());
            nvgLineTo(ctx, sxy1.x(), sxy1.y());
        }

        nvgStrokeWidth(ctx, 2.0f);
        nvgStrokeColor(ctx, Color(1.0f, 1.0f, 1.0f, alpha));
        nvgStroke(ctx);
    }
}


void HDRImageView::draw_widget_border(NVGcontext* ctx) const {
	// Draw an inner drop shadow. (adapted from nanogui::Window) and tev
	int ds = m_theme->m_window_drop_shadow_size, cr = m_theme->m_window_corner_radius;
	NVGpaint shadowPaint =
		nvgBoxGradient(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y(), cr * 2, ds * 2, m_theme->m_transparent,
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
    if (!m_drawValues || (m_pixelInfoThreshold == -1) || (m_zoom <= m_pixelInfoThreshold))
        return;

    float factor = clamp01((m_zoom - m_pixelInfoThreshold) / (2 * m_pixelInfoThreshold));
    float alpha = lerp(0.0f, 0.5f, smoothStep(0.0f, 1.0f, factor));

    if (alpha > 0.0f && m_pixel_callback)
    {
        nvgSave(ctx);
        nvgIntersectScissor(ctx, m_pos.x(), m_pos.y(), m_size.x(), m_size.y());

        nanogui::Vector2f xy0 = screen_position_for_coordinate(nanogui::Vector2f(0.0f));
        int minJ = max(0, int(-xy0.y() / m_zoom));
        int maxJ = min(m_image->size().y() - 1, int(ceil((screen()->size().y() - xy0.y()) / m_zoom)));
        int minI = max(0, int(-xy0.x() / m_zoom));
        int maxI = min(m_image->size().x() - 1, int(ceil((screen()->size().x() - xy0.x()) / m_zoom)));

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

                auto pos = screen_position_for_coordinate(nanogui::Vector2f(i+0.5f, j+0.5f));

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

void HDRImageView::draw_contents() {
    
    if (!m_image)
        return;

    nanogui::Vector2f pCurrent, sCurrent;
    image_position_and_scale(pCurrent, sCurrent, m_image);

	nanogui::Vector2f randomness(std::generate_canonical<float, 10>(g_rand)*255,
	                             std::generate_canonical<float, 10>(g_rand)*255);
	m_image_shader->set_uniform("randomness", randomness);

    m_image_shader->set_uniform("gain", (float)powf(2.0f, m_exposure));
	m_image_shader->set_uniform("gamma", m_gamma);
	m_image_shader->set_uniform("sRGB", (bool)m_sRGB);
    m_image_shader->set_uniform("hasDither", (bool)m_dither);
	m_image_shader->set_uniform("image_pos", pCurrent);
	m_image_shader->set_uniform("image_scale", sCurrent);
    // std::cout << m_channel << std::endl;
    // m_image_shader->set_uniform("blend_mode", (int)m_blend_mode);
    m_image_shader->set_uniform("channel", (int)m_channel);
    // m_image_shader->set_uniform("hasImage", (int)true);
	// m_image_shader->set_uniform("hasReference", (int)false);
    
    m_image_shader->begin();
    m_image_shader->draw_array(Shader::PrimitiveType::Triangle, 0, 6, false);
    m_image_shader->end();
}
