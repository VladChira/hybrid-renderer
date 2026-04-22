#version 460 core

layout(location = 0) in vec2 a_local_xy;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

void main()
{
    vec4 world_position = u_model * vec4(a_local_xy, 0.0, 1.0);
    gl_Position = u_projection * u_view * world_position;
}
