#ifndef HYBRID_RT_GEOMETRY_FETCH_GLSL
#define HYBRID_RT_GEOMETRY_FETCH_GLSL

#include "bvh_traversal.glsl"

vec3 NormalizeOrFallback(vec3 value, vec3 fallback_value)
{
    float len_sq = dot(value, value);
    if (len_sq <= 1e-10)
    {
        return fallback_value;
    }
    return value * inversesqrt(len_sq);
}

struct HitGeometry
{
    vec3 world_position;
    vec3 world_normal;
    vec3 world_tangent;
    float tangent_handedness;
    vec2 uv0;
    vec2 uv1;
    uint material_index;
    uint entity_id;
};

HitGeometry FetchHitGeometry(RayHit hit)
{
    GpuTlasInstance inst = tlas_instances[hit.tlas_instance_index];
    GpuPrimitive prim = primitives[inst.primitive_id];

    uint base = prim.index_offset + hit.triangle_in_blas * 3u;
    uint i0 = indices[base + 0u];
    uint i1 = indices[base + 1u];
    uint i2 = indices[base + 2u];

    GpuVertex v0 = vertices[prim.vertex_offset + i0];
    GpuVertex v1 = vertices[prim.vertex_offset + i1];
    GpuVertex v2 = vertices[prim.vertex_offset + i2];

    float u = hit.bary.x;
    float v = hit.bary.y;
    float w = 1.0 - u - v;

    vec3 local_position = w * v0.position + u * v1.position + v * v2.position;
    vec3 local_normal = w * v0.normal + u * v1.normal + v * v2.normal;
    vec4 local_tangent = w * v0.tangent + u * v1.tangent + v * v2.tangent;
    vec2 uv0 = w * v0.uv0 + u * v1.uv0 + v * v2.uv0;
    vec2 uv1 = w * v0.uv1 + u * v1.uv1 + v * v2.uv1;

    // Normal matrix is transpose(inverse(world_from_local.3x3)) - using
    // transpose(local_from_world.3x3) is the cheap equivalent. Tangents,
    // on the other hand, transform as velocity vectors along the surface,
    // so they use the forward transform directly.
    mat3 normal_matrix = transpose(mat3(inst.local_from_world));
    mat3 tangent_matrix = mat3(inst.world_from_local);

    vec3 world_normal = NormalizeOrFallback(normal_matrix * local_normal, vec3(0.0, 1.0, 0.0));
    vec3 world_tangent = NormalizeOrFallback(tangent_matrix * local_tangent.xyz, vec3(1.0, 0.0, 0.0));

    HitGeometry geo;
    geo.world_position = (inst.world_from_local * vec4(local_position, 1.0)).xyz;
    geo.world_normal = world_normal;
    geo.world_tangent = world_tangent;
    geo.tangent_handedness = local_tangent.w;
    geo.uv0 = uv0;
    geo.uv1 = uv1;
    geo.material_index = prim.material_index;
    geo.entity_id = inst.entity_id;
    return geo;
}

mat3 BuildHitTbn(HitGeometry geo)
{
    vec3 normal = NormalizeOrFallback(geo.world_normal, vec3(0.0, 1.0, 0.0));
    vec3 tangent = geo.world_tangent - normal * dot(normal, geo.world_tangent);
    if (dot(tangent, tangent) <= 1e-10)
    {
        vec3 helper_axis = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
        tangent = cross(helper_axis, normal);
    }
    tangent = NormalizeOrFallback(tangent, vec3(1.0, 0.0, 0.0));

    float handedness = geo.tangent_handedness < 0.0 ? -1.0 : 1.0;
    vec3 bitangent = NormalizeOrFallback(cross(normal, tangent) * handedness, vec3(0.0, 0.0, 1.0));
    return mat3(tangent, bitangent, normal);
}

#endif
