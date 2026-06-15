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

vec3 SafeNormalize(vec3 value, vec3 fallback)
{
    float len2 = dot(value, value);
    if (len2 <= 1e-10)
    {
        return fallback;
    }
    return value * inversesqrt(len2);
}

void main()
{
    vec4 world_position = u_model * vec4(a_position, 1.0);
    mat3 normal_matrix = mat3(transpose(inverse(u_model)));
    vec3 world_normal = SafeNormalize(normal_matrix * a_normal, vec3(0.0, 1.0, 0.0));
    vec3 world_tangent = SafeNormalize(normal_matrix * a_tangent.xyz, vec3(1.0, 0.0, 0.0));
    world_tangent = SafeNormalize(world_tangent - world_normal * dot(world_normal, world_tangent),
                                  SafeNormalize(cross(abs(world_normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0),
                                                      world_normal),
                                                vec3(1.0, 0.0, 0.0)));
    vec3 world_bitangent = SafeNormalize(cross(world_normal, world_tangent), vec3(0.0, 0.0, 1.0)) * a_tangent.w;

    v_world_normal = world_normal;
    v_world_tangent = world_tangent;
    v_world_bitangent = world_bitangent;
    v_uv0 = a_uv0;
    v_uv1 = a_uv1;
    gl_Position = u_projection * u_view * world_position;
}
