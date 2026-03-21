#version 330 core

layout (location = 0) in vec3 a_position;
layout (location = 1) in vec3 a_normal;
layout (location = 2) in vec2 a_uv0;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

out vec3 v_world_normal;
out vec2 v_uv0;

void main()
{
    vec4 world_position = u_model * vec4(a_position, 1.0);
    v_world_normal = mat3(transpose(inverse(u_model))) * a_normal;
    v_uv0 = a_uv0;
    gl_Position = u_projection * u_view * world_position;
}
