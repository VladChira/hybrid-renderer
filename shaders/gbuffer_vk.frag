#version 460 core

// Phase 3-stage-C1: per-primitive material lookup via push constant +
// material SSBO. Texture sampling lands in stage C3 (descriptor indexing);
// for now the shader pulls only scalar / vec factors.

layout(location = 0) in vec3 v_world_normal;

layout(location = 0) out vec4 o_rt0;     // rgb = albedo, a = metallic
layout(location = 1) out vec4 o_rt1;     // rgb = encoded normal, a = roughness

// std430 layout — must match renderer/stores/MaterialStore.h::GpuMaterial.
// The texture-handle fields are present so the byte layout matches the GL
// path's struct; the shader doesn't read them in stage C1.
struct GpuMaterial
{
    vec4  base_color_factor;
    vec4  emissive_and_cutoff;     // xyz = emissive, w = alpha_cutoff
    vec4  scalar_factors;          // x = metallic, y = roughness, z = normal_scale, w = occlusion_strength
    uvec4 flags_texcoords;         // x = alpha_mode, y = texcoord_bits, z/w = pad

    uvec2 base_color_handle;
    uvec2 metallic_roughness_handle;
    uvec2 normal_handle;
    uvec2 occlusion_handle;
    uvec2 emissive_handle;
    uvec2 _pad_handle;
    vec4  _pad_tail;
};

layout(std430, set = 0, binding = 1) readonly buffer MaterialBuffer
{
    GpuMaterial materials[];
};

layout(push_constant) uniform PushConstants
{
    mat4 model;
    uint material_index;
} pc;

void main()
{
    GpuMaterial m = materials[pc.material_index];

    vec3 n = normalize(v_world_normal);
    o_rt0 = vec4(m.base_color_factor.rgb, m.scalar_factors.x);
    o_rt1 = vec4(n * 0.5 + 0.5, m.scalar_factors.y);
}
