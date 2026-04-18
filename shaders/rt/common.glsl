#ifndef HYBRID_RT_COMMON_GLSL
#define HYBRID_RT_COMMON_GLSL

// Shared struct + SSBO declarations for every ray-tracing / compute pass.
// Struct layouts must match the CPU std430-compatible records in
// src/renderer/GeometryStore.h, src/renderer/MaterialStore.h, and
// src/renderer/raytracing/Bvh.h.

struct GpuVertex
{
    vec3  position;
    float _pad_position;
    vec3  normal;
    float _pad_normal;
    vec4  tangent;
    vec2  uv0;
    vec2  uv1;
    vec4  color0;
};

struct GpuPrimitive
{
    uint vertex_offset;
    uint vertex_count;
    uint index_offset;
    uint index_count;
    uint material_index;
    uint blas_root;
    uint blas_triangle_offset;
    uint _pad;
};

struct GpuMaterial
{
    vec4  base_color_factor;
    vec4  emissive_and_cutoff;   // xyz = emissive, w = alpha_cutoff
    vec4  scalar_factors;        // x = metallic, y = roughness, z = normal_scale, w = occlusion_strength
    uvec4 flags_texcoords;       // x = alpha_mode, y = texcoord_bits, z/w = pad

    uvec2 base_color_handle;
    uvec2 metallic_roughness_handle;
    uvec2 normal_handle;
    uvec2 occlusion_handle;
    uvec2 emissive_handle;
    uvec2 _pad_handle;
    vec4  _pad_tail;
};

struct BvhNode
{
    vec3 bmin;
    int  left_or_first;
    vec3 bmax;
    int  right_or_count;
};

struct GpuTlasInstance
{
    mat4 world_from_local;
    mat4 local_from_world;
    uint primitive_id;
    uint entity_id;
    uint _pad0;
    uint _pad1;
};

// Binding slots must match src/renderer/ShaderBindings.h.
layout(std430, binding = 0)  readonly buffer VertexBuffer           { GpuVertex      vertices[];        };
layout(std430, binding = 1)  readonly buffer IndexBuffer            { uint           indices[];         };
layout(std430, binding = 2)  readonly buffer PrimitiveBuffer        { GpuPrimitive   primitives[];      };
layout(std430, binding = 3)  readonly buffer MaterialBuffer         { GpuMaterial    materials[];       };
layout(std430, binding = 7)  readonly buffer BlasNodeBuffer         { BvhNode        blas_nodes[];      };
layout(std430, binding = 8)  readonly buffer BlasTriangleBuffer     { uint           blas_triangles[];  };
layout(std430, binding = 9)  readonly buffer TlasNodeBuffer         { BvhNode        tlas_nodes[];      };
layout(std430, binding = 10) readonly buffer TlasInstanceBuffer     { GpuTlasInstance tlas_instances[]; };

// Texcoord-bit positions in GpuMaterial::flags_texcoords.y.
const uint kTexcoordBitBaseColor         = 0u;
const uint kTexcoordBitMetallicRoughness = 1u;
const uint kTexcoordBitNormal            = 2u;
const uint kTexcoordBitOcclusion         = 3u;
const uint kTexcoordBitEmissive          = 4u;

bool TexcoordBit(uint texcoord_bits, uint bit)
{
    return ((texcoord_bits >> bit) & 1u) != 0u;
}

#endif // HYBRID_RT_COMMON_GLSL
