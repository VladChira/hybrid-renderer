#version 460 core
#extension GL_ARB_bindless_texture : require

in vec3 v_world_normal;
in vec3 v_world_tangent;
in vec3 v_world_bitangent;
in vec2 v_uv0;
in vec2 v_uv1;

uniform uint u_material_index;
uniform uint u_instance_id;

#include "include/material_fetch.glsl"

layout (location = 0) out vec4 o_rt0;
layout (location = 1) out vec4 o_rt1;
layout (location = 2) out uint o_entity_id;

void main()
{
    if (!PassesAlphaTest(u_material_index, v_uv0, v_uv1))
    {
        discard;
    }

    ShadedSurface surface = FetchSurface(u_material_index, v_uv0, v_uv1);

    vec3 normal = normalize(v_world_normal);
    if (dot(normal, normal) < 0.00001)
    {
        normal = vec3(0.0, 1.0, 0.0);
    }

    vec3 tangent = normalize(v_world_tangent);
    vec3 bitangent = normalize(v_world_bitangent);
    if (dot(tangent, tangent) < 0.00001 || dot(bitangent, bitangent) < 0.00001)
    {
        vec3 helper_axis = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
        tangent = normalize(cross(helper_axis, normal));
        bitangent = normalize(cross(normal, tangent));
    }

    mat3 tbn = mat3(tangent, bitangent, normal);
    normal = normalize(tbn * surface.normal_tangent);

    o_rt0 = vec4(surface.albedo, surface.metallic);
    o_rt1 = vec4(normal * 0.5 + 0.5, surface.roughness);
    o_entity_id = u_instance_id;
}
