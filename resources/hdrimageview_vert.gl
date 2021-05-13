#version 330

layout(location = 0) in vec2 position;

out vec2 image_uv;
out vec2 reference_uv;

uniform vec2 image_scale;
uniform vec2 image_pos;
uniform vec2 reference_scale;
uniform vec2 reference_pos;

void main()
{
    image_uv = (position/2.0 - image_pos + 0.5) / image_scale;
    reference_uv = (position/2.0 - reference_pos + 0.5) / reference_scale;
    gl_Position  = vec4(position.x, -position.y, 0.0, 1.0);
}