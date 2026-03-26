#version 330 core

in vec3 v_world_normal;
in vec3 v_world_tangent;
in vec3 v_world_bitangent;
in vec2 v_uv0;
in vec2 v_uv1;

uniform vec3 u_base_color;
uniform float u_base_alpha;
uniform sampler2D u_base_color_texture;
uniform int u_has_base_color_texture;
uniform int u_base_color_texcoord;
uniform int u_alpha_masked;
uniform float u_alpha_cutoff;
uniform float u_metallic;
uniform float u_roughness;
uniform sampler2D u_metallic_roughness_texture;
uniform int u_has_metallic_roughness_texture;
uniform int u_metallic_roughness_texcoord;
uniform sampler2D u_normal_texture;
uniform int u_has_normal_texture;
uniform int u_normal_texcoord;
uniform float u_normal_scale;
uniform uint u_instance_id;

layout (location = 0) out vec4 o_rt0;
layout (location = 1) out vec4 o_rt1;
layout (location = 2) out uint o_entity_id;

void main()
{
    vec2 base_uv = (u_base_color_texcoord == 1) ? v_uv1 : v_uv0;
    vec4 base_sample = (u_has_base_color_texture != 0) ? texture(u_base_color_texture, base_uv) : vec4(1.0);
    vec3 base_color = clamp(u_base_color * base_sample.rgb, 0.0, 1.0);
    float alpha = clamp(u_base_alpha * base_sample.a, 0.0, 1.0);
    if (u_alpha_masked != 0 && alpha < u_alpha_cutoff)
    {
        discard;
    }

    vec2 mr_uv = (u_metallic_roughness_texcoord == 1) ? v_uv1 : v_uv0;
    vec4 mr_sample = (u_has_metallic_roughness_texture != 0) ? texture(u_metallic_roughness_texture, mr_uv) : vec4(1.0);
    float metallic = clamp(u_metallic * mr_sample.b, 0.0, 1.0);
    float roughness = clamp(u_roughness * mr_sample.g, 0.0, 1.0);

    vec3 normal = normalize(v_world_normal);
    if (dot(normal, normal) < 0.00001)
    {
        normal = vec3(0.0, 1.0, 0.0);
    }

    if (u_has_normal_texture != 0)
    {
        vec2 normal_uv = (u_normal_texcoord == 1) ? v_uv1 : v_uv0;
        vec3 tangent_normal = texture(u_normal_texture, normal_uv).xyz * 2.0 - 1.0;
        tangent_normal.xy *= u_normal_scale;
        tangent_normal = normalize(tangent_normal);

        vec3 tangent = normalize(v_world_tangent);
        vec3 bitangent = normalize(v_world_bitangent);
        if (dot(tangent, tangent) < 0.00001 || dot(bitangent, bitangent) < 0.00001)
        {
            vec3 helper_axis = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
            tangent = normalize(cross(helper_axis, normal));
            bitangent = normalize(cross(normal, tangent));
        }

        mat3 tbn = mat3(tangent, bitangent, normal);
        normal = normalize(tbn * tangent_normal);
    }

    o_rt0 = vec4(base_color, metallic);
    o_rt1 = vec4(normal * 0.5 + 0.5, roughness);
    o_entity_id = u_instance_id;
}
