#ifndef HYBRID_GEOMETRY_FETCH_GLSL
#define HYBRID_GEOMETRY_FETCH_GLSL

#include "bvh_traversal.glsl"

struct HitGeometry
{
    vec3  world_position;
    vec3  world_normal;
    vec4  world_tangent;   // xyz = tangent direction, w = handedness
    vec2  uv0;
    vec2  uv1;
    uint  material_index;
};

vec3 SafeNormalizeHit(vec3 value, vec3 fallback)
{
    float len2 = dot(value, value);
    if (len2 <= 1e-10)
    {
        return fallback;
    }
    return value * inversesqrt(len2);
}

// Reconstruct interpolated world-space geometry from a closest-hit record.
// ray_origin and ray_direction are in world space; world_position = origin + t * direction.
HitGeometry FetchHitGeometry(RayHit hit, vec3 ray_origin, vec3 ray_direction)
{
    GpuTlasInstance inst = tlas_instances[hit.tlas_instance_index];
    GpuPrimitive    prim = primitives[inst.primitive_id];

    uint blas_tri = blas_triangles[prim.blas_triangle_offset + hit.triangle_in_blas];
    uint i0 = indices[prim.index_offset + blas_tri * 3u + 0u];
    uint i1 = indices[prim.index_offset + blas_tri * 3u + 1u];
    uint i2 = indices[prim.index_offset + blas_tri * 3u + 2u];

    GpuVertex v0 = vertices[prim.vertex_offset + i0];
    GpuVertex v1 = vertices[prim.vertex_offset + i1];
    GpuVertex v2 = vertices[prim.vertex_offset + i2];

    // Moller-Trumbore barycentrics: (1-u-v, u, v)
    float w = 1.0 - hit.bary.x - hit.bary.y;
    float u = hit.bary.x;
    float v = hit.bary.y;

    vec3 local_normal  = v0.normal  * w + v1.normal  * u + v2.normal  * v;
    vec4 local_tangent = v0.tangent * w + v1.tangent * u + v2.tangent * v;
    vec2 interp_uv0    = v0.uv0     * w + v1.uv0     * u + v2.uv0     * v;
    vec2 interp_uv1    = v0.uv1     * w + v1.uv1     * u + v2.uv1     * v;

    // Normal matrix: transpose(inverse(world_from_local)) = transpose(local_from_world)
    mat3 normal_mat      = transpose(mat3(inst.local_from_world));
    vec3 world_normal      = SafeNormalizeHit(normal_mat * local_normal, vec3(0.0, 1.0, 0.0));
    vec3 world_tangent_xyz = SafeNormalizeHit(mat3(inst.world_from_local) * local_tangent.xyz,
                                              vec3(1.0, 0.0, 0.0));

    HitGeometry geo;
    geo.world_position = ray_origin + hit.t * ray_direction;
    geo.world_normal   = world_normal;
    geo.world_tangent  = vec4(world_tangent_xyz, local_tangent.w);
    geo.uv0            = interp_uv0;
    geo.uv1            = interp_uv1;
    geo.material_index = prim.material_index;
    return geo;
}

mat3 BuildHitTbn(HitGeometry geo)
{
    vec3 N = SafeNormalizeHit(geo.world_normal, vec3(0.0, 1.0, 0.0));
    vec3 T = SafeNormalizeHit(geo.world_tangent.xyz, vec3(1.0, 0.0, 0.0));
    float handedness = abs(geo.world_tangent.w) > 0.5 ? geo.world_tangent.w : 1.0;
    T = SafeNormalizeHit(T - dot(T, N) * N,
                         SafeNormalizeHit(cross(abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0)
                                                                 : vec3(0.0, 1.0, 0.0),
                                                N),
                                          vec3(1.0, 0.0, 0.0)));
    vec3 B = SafeNormalizeHit(cross(N, T), vec3(0.0, 0.0, 1.0)) * handedness;
    return mat3(T, B, N);
}

#endif
