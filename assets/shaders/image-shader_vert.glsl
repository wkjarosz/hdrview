precision mediump float;
in vec2   position;

out vec2 primary_uv;
out vec2 secondary_uv;

uniform vec2 primary_scale;
uniform vec2 primary_pos;
uniform vec2 secondary_scale;
uniform vec2 secondary_pos;

void main()
{
    primary_uv   = (position / 2.0 - primary_pos + 0.5) / primary_scale;
    secondary_uv = (position / 2.0 - secondary_pos + 0.5) / secondary_scale;
    gl_Position  = vec4(position.x, -position.y, 0.0, 1.0);
}