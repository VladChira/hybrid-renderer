#ifndef HYBRID_RT_MATERIAL_FETCH_GLSL
#define HYBRID_RT_MATERIAL_FETCH_GLSL

#include "material_data.glsl"

// Requires the including shader to enable GL_ARB_bindless_texture.

vec3 NormalizeTangentNormal(vec3 value)
{
    float len_sq = dot(value, value);
    if (len_sq <= 1e-10)
    {
        return vec3(0.0, 0.0, 1.0);
    }
    return value * inversesqrt(len_sq);
}

vec2 SelectMaterialUv(uint texcoord_bits, uint bit, vec2 uv0, vec2 uv1)
{
    return TexcoordBit(texcoord_bits, bit) ? uv1 : uv0;
}

struct ShadedSurface
{
    vec3  albedo;
    float alpha;
    float metallic;
    float roughness;
    vec3  emissive;
    vec3  normal_tangent;
};

ShadedSurface FetchSurface(uint material_index, vec2 uv0, vec2 uv1)
{
    GpuMaterial mat = materials[material_index];
    uint texcoord_bits = mat.flags_texcoords.y;

    vec2 bc_uv = SelectMaterialUv(texcoord_bits, kTexcoordBitBaseColor, uv0, uv1);
    vec4 bc_sample = texture(sampler2D(mat.base_color_handle), bc_uv);
    vec3 albedo = clamp(mat.base_color_factor.rgb * bc_sample.rgb, 0.0, 1.0);
    float alpha = clamp(mat.base_color_factor.a * bc_sample.a, 0.0, 1.0);

    vec2 mr_uv = SelectMaterialUv(texcoord_bits, kTexcoordBitMetallicRoughness, uv0, uv1);
    vec4 mr_sample = texture(sampler2D(mat.metallic_roughness_handle), mr_uv);
    float metallic  = clamp(mat.scalar_factors.x * mr_sample.b, 0.0, 1.0);
    float roughness = clamp(mat.scalar_factors.y * mr_sample.g, 0.0, 1.0);

    vec2 n_uv = SelectMaterialUv(texcoord_bits, kTexcoordBitNormal, uv0, uv1);
    vec3 tangent_normal = texture(sampler2D(mat.normal_handle), n_uv).xyz * 2.0 - 1.0;
    tangent_normal.xy *= mat.scalar_factors.z;

    vec2 e_uv = SelectMaterialUv(texcoord_bits, kTexcoordBitEmissive, uv0, uv1);
    vec3 emissive_sample = texture(sampler2D(mat.emissive_handle), e_uv).rgb;
    vec3 emissive = mat.emissive_and_cutoff.rgb * emissive_sample;

    ShadedSurface surface;
    surface.albedo          = albedo;
    surface.alpha           = alpha;
    surface.metallic        = metallic;
    surface.roughness       = roughness;
    surface.emissive        = emissive;
    surface.normal_tangent  = NormalizeTangentNormal(tangent_normal);
    return surface;
}

bool PassesAlphaTest(uint material_index, vec2 uv0, vec2 uv1)
{
    GpuMaterial mat = materials[material_index];
    uint alpha_mode = mat.flags_texcoords.x;
    if (alpha_mode != 1u)
    {
        return true;
    }

    uint texcoord_bits = mat.flags_texcoords.y;
    vec2 bc_uv = SelectMaterialUv(texcoord_bits, kTexcoordBitBaseColor, uv0, uv1);
    vec4 bc = texture(sampler2D(mat.base_color_handle), bc_uv);
    float alpha = mat.base_color_factor.a * bc.a;
    return alpha >= mat.emissive_and_cutoff.w;
}

#endif
