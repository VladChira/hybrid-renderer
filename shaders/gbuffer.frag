#version 460 core

in vec3 v_world_normal;
in vec3 v_world_tangent;
in vec3 v_world_bitangent;
in vec2 v_uv0;
in vec2 v_uv1;
flat in uint v_material_index;
flat in uint v_entity_id;

uniform sampler2D u_base_color_texture;
uniform sampler2D u_metallic_roughness_texture;
uniform sampler2D u_normal_texture;

struct MaterialData
{
    vec4 base_color_alpha;
    vec4 metallic_roughness_normal_scale_alpha_cutoff;
    uvec4 flags;
    uvec4 texcoord_selectors;
};

layout (std430, binding = 2) readonly buffer MaterialSSBO
{
    MaterialData materials[];
};

layout (location = 0) out vec4 o_rt0;
layout (location = 1) out vec4 o_rt1;
layout (location = 2) out uint o_entity_id;

void main()
{
    MaterialData material = materials[v_material_index];

    bool has_base_color_texture = material.flags.x != 0u;
    bool has_metallic_roughness_texture = material.flags.y != 0u;
    bool has_normal_texture = material.flags.z != 0u;
    bool alpha_masked = material.flags.w != 0u;

    vec2 base_uv = (material.texcoord_selectors.x == 1u) ? v_uv1 : v_uv0;
    vec4 base_sample = has_base_color_texture ? texture(u_base_color_texture, base_uv) : vec4(1.0);
    vec3 base_color = clamp(material.base_color_alpha.rgb * base_sample.rgb, 0.0, 1.0);
    float alpha = clamp(material.base_color_alpha.a * base_sample.a, 0.0, 1.0);
    if (alpha_masked && alpha < material.metallic_roughness_normal_scale_alpha_cutoff.w)
    {
        discard;
    }

    vec2 mr_uv = (material.texcoord_selectors.y == 1u) ? v_uv1 : v_uv0;
    vec4 mr_sample = has_metallic_roughness_texture ? texture(u_metallic_roughness_texture, mr_uv) : vec4(1.0);
    float metallic = clamp(material.metallic_roughness_normal_scale_alpha_cutoff.x * mr_sample.b, 0.0, 1.0);
    float roughness = clamp(material.metallic_roughness_normal_scale_alpha_cutoff.y * mr_sample.g, 0.0, 1.0);

    vec3 normal = normalize(v_world_normal);
    if (dot(normal, normal) < 0.00001)
    {
        normal = vec3(0.0, 1.0, 0.0);
    }

    if (has_normal_texture)
    {
        vec2 normal_uv = (material.texcoord_selectors.z == 1u) ? v_uv1 : v_uv0;
        vec3 tangent_normal = texture(u_normal_texture, normal_uv).xyz * 2.0 - 1.0;
        tangent_normal.xy *= material.metallic_roughness_normal_scale_alpha_cutoff.z;
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
    o_entity_id = v_entity_id;
}
