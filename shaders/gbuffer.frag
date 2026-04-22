#version 460 core
#extension GL_ARB_bindless_texture : require

in vec3 v_world_normal;
in vec3 v_world_tangent;
in vec3 v_world_bitangent;
in vec2 v_uv0;
in vec2 v_uv1;

uniform uint u_material_index;
uniform uint u_instance_id;

struct GpuMaterial
{
    vec4 base_color_factor;
    vec4 emissive_and_cutoff;    // xyz = emissive, w = alpha_cutoff
    vec4 scalar_factors;         // x = metallic, y = roughness, z = normal_scale, w = occlusion_strength
    uvec4 flags_texcoords;       // x = alpha_mode (0/1/2), y = texcoord_bits, z/w = pad

    uvec2 base_color_handle;
    uvec2 metallic_roughness_handle;
    uvec2 normal_handle;
    uvec2 occlusion_handle;
    uvec2 emissive_handle;
    uvec2 _pad_handle;
    vec4 _pad_tail;
};

layout(std430, binding = 3) readonly buffer MaterialBuffer
{
    GpuMaterial materials[];
};

layout (location = 0) out vec4 o_rt0;
layout (location = 1) out vec4 o_rt1;
layout (location = 2) out uint o_entity_id;


// Bitfield that tells the shader which UV set each texture uses
// If base color, go fetch UV0 from the vertex attribute
const uint kTexcoordBitBaseColor          = 0u;
const uint kTexcoordBitMetallicRoughness  = 1u;
const uint kTexcoordBitNormal             = 2u;
const uint kTexcoordBitOcclusion          = 3u;
const uint kTexcoordBitEmissive           = 4u;

vec2 SelectUv(uint texcoord_bits, uint bit)
{
    return ((texcoord_bits >> bit) & 1u) == 1u ? v_uv1 : v_uv0;
}

void main()
{
    GpuMaterial material = materials[u_material_index];
    uint texcoord_bits = material.flags_texcoords.y;
    uint alpha_mode = material.flags_texcoords.x;

    vec2 base_uv = SelectUv(texcoord_bits, kTexcoordBitBaseColor);
    vec4 base_sample = texture(sampler2D(material.base_color_handle), base_uv);
    vec3 base_color = clamp(material.base_color_factor.rgb * base_sample.rgb, 0.0, 1.0);
    float alpha = clamp(material.base_color_factor.a * base_sample.a, 0.0, 1.0);

    if (alpha_mode == 1u && alpha < material.emissive_and_cutoff.w)
    {
        discard;
    }

    vec2 mr_uv = SelectUv(texcoord_bits, kTexcoordBitMetallicRoughness);
    vec4 mr_sample = texture(sampler2D(material.metallic_roughness_handle), mr_uv);
    float metallic  = clamp(material.scalar_factors.x * mr_sample.b, 0.0, 1.0);
    float roughness = clamp(material.scalar_factors.y * mr_sample.g, 0.0, 1.0);

    vec3 normal = normalize(v_world_normal);
    if (dot(normal, normal) < 0.00001)
    {
        normal = vec3(0.0, 1.0, 0.0);
    }

    vec2 normal_uv = SelectUv(texcoord_bits, kTexcoordBitNormal);
    vec3 tangent_normal = texture(sampler2D(material.normal_handle), normal_uv).xyz * 2.0 - 1.0;
    tangent_normal.xy *= material.scalar_factors.z;
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

    o_rt0 = vec4(base_color, metallic);
    o_rt1 = vec4(normal * 0.5 + 0.5, roughness);
    o_entity_id = u_instance_id;
}
