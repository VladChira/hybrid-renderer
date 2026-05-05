#ifndef HYBRID_MATERIAL_DATA_GLSL
#define HYBRID_MATERIAL_DATA_GLSL

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

const uint kTexcoordBitBaseColor         = 0u;
const uint kTexcoordBitMetallicRoughness = 1u;
const uint kTexcoordBitNormal            = 2u;
const uint kTexcoordBitOcclusion         = 3u;
const uint kTexcoordBitEmissive          = 4u;

bool TexcoordBit(uint texcoord_bits, uint bit)
{
    return ((texcoord_bits >> bit) & 1u) == 1u;
}

#endif
