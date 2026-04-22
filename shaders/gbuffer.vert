#version 460 core

layout (location = 0) in vec3 a_position;
layout (location = 1) in vec3 a_normal;
layout (location = 2) in vec2 a_uv0;
layout (location = 3) in vec2 a_uv1;
layout (location = 4) in vec4 a_tangent;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

out vec3 v_world_normal;
out vec3 v_world_tangent;
out vec3 v_world_bitangent;
out vec2 v_uv0;
out vec2 v_uv1;

void main()
{
    vec4 world_position = u_model * vec4(a_position, 1.0);
    mat3 normal_matrix = mat3(transpose(inverse(u_model)));
    vec3 world_normal = normalize(normal_matrix * a_normal);
    vec3 world_tangent = normalize(normal_matrix * a_tangent.xyz);
    world_tangent = normalize(world_tangent - world_normal * dot(world_normal, world_tangent));
    vec3 world_bitangent = normalize(cross(world_normal, world_tangent)) * a_tangent.w;

    v_world_normal = world_normal;
    v_world_tangent = world_tangent;
    v_world_bitangent = world_bitangent;
    v_uv0 = a_uv0;
    v_uv1 = a_uv1;
    gl_Position = u_projection * u_view * world_position;
}
