#ifndef HYBRID_RT_BVH_TRAVERSAL_GLSL
#define HYBRID_RT_BVH_TRAVERSAL_GLSL

#include "rt/common.glsl"

const int kRayStackDepth = 64;

// ---------------------------------------------------------------------------
// Ray/primitive intersection helpers
// ---------------------------------------------------------------------------

bool IntersectAabbRange(vec3 origin,
                        vec3 inv_direction,
                        vec3 bmin,
                        vec3 bmax,
                        float t_min,
                        float t_max,
                        out float t_near)
{
    vec3 t1 = (bmin - origin) * inv_direction;
    vec3 t2 = (bmax - origin) * inv_direction;
    vec3 tmin3 = min(t1, t2);
    vec3 tmax3 = max(t1, t2);
    float near_t = max(max(tmin3.x, tmin3.y), max(tmin3.z, t_min));
    float far_t  = min(min(tmax3.x, tmax3.y), min(tmax3.z, t_max));
    t_near = near_t;
    return near_t <= far_t;
}

// Möller-Trumbore triangle intersection. Returns true and writes distance and
// barycentric (u, v) on hit. Barycentric w = 1 - u - v is implicit.
bool IntersectTriangle(vec3 origin,
                       vec3 direction,
                       vec3 v0,
                       vec3 v1,
                       vec3 v2,
                       float t_min,
                       float t_max,
                       out float t_out,
                       out float u_out,
                       out float v_out)
{
    const float kEpsilon = 1e-8;
    vec3 e1 = v1 - v0;
    vec3 e2 = v2 - v0;
    vec3 p  = cross(direction, e2);
    float det = dot(e1, p);
    if (abs(det) < kEpsilon)
    {
        return false;
    }
    float inv_det = 1.0 / det;
    vec3 s = origin - v0;
    float u = dot(s, p) * inv_det;
    if (u < 0.0 || u > 1.0)
    {
        return false;
    }
    vec3 q = cross(s, e1);
    float v = dot(direction, q) * inv_det;
    if (v < 0.0 || u + v > 1.0)
    {
        return false;
    }
    float t = dot(e2, q) * inv_det;
    if (t < t_min || t > t_max)
    {
        return false;
    }
    t_out = t;
    u_out = u;
    v_out = v;
    return true;
}

// ---------------------------------------------------------------------------
// Hit record used by closest-hit traversal
// ---------------------------------------------------------------------------

struct RayHit
{
    uint  tlas_instance_index;  // address into tlas_instances[]
    uint  triangle_in_blas;     // triangle index within the primitive
    float t;                    // world-space t == local-space t because
                                // local_direction is not normalised
    vec2  bary;                 // Möller-Trumbore (u, v)
};

// ---------------------------------------------------------------------------
// Closest-hit traversal
// ---------------------------------------------------------------------------

// BLAS traversal in local space. Updates `t_max` in place (monotonically
// shrinking), and rewrites `hit` whenever a closer triangle is found.
bool IntersectBlasClosestHit(vec3 local_origin,
                             vec3 local_direction,
                             uint primitive_id,
                             uint tlas_instance_index,
                             inout float t_max,
                             inout RayHit hit)
{
    GpuPrimitive prim = primitives[primitive_id];
    if (prim.index_count == 0u)
    {
        return false;
    }

    vec3 inv_dir = 1.0 / local_direction;

    int stack[kRayStackDepth];
    int sp = 0;
    stack[sp++] = int(prim.blas_root);

    bool found = false;
    while (sp > 0)
    {
        int node_index = stack[--sp];
        BvhNode node = blas_nodes[node_index];
        float t_near;
        if (!IntersectAabbRange(local_origin, inv_dir, node.bmin, node.bmax, 0.0, t_max, t_near))
        {
            continue;
        }

        if (node.right_or_count < 0)
        {
            int first = node.left_or_first;
            int count = -node.right_or_count;
            for (int i = 0; i < count; ++i)
            {
                uint tri = blas_triangles[prim.blas_triangle_offset + uint(first + i)];
                uint i0 = indices[prim.index_offset + tri * 3u + 0u];
                uint i1 = indices[prim.index_offset + tri * 3u + 1u];
                uint i2 = indices[prim.index_offset + tri * 3u + 2u];
                vec3 v0 = vertices[prim.vertex_offset + i0].position;
                vec3 v1 = vertices[prim.vertex_offset + i1].position;
                vec3 v2 = vertices[prim.vertex_offset + i2].position;

                float t, u, v;
                if (IntersectTriangle(local_origin, local_direction, v0, v1, v2, 0.0, t_max, t, u, v))
                {
                    t_max = t;
                    hit.tlas_instance_index = tlas_instance_index;
                    hit.triangle_in_blas    = tri;
                    hit.t                   = t;
                    hit.bary                = vec2(u, v);
                    found = true;
                }
            }
        }
        else if (sp + 2 <= kRayStackDepth)
        {
            stack[sp++] = node.left_or_first;
            stack[sp++] = node.right_or_count;
        }
    }
    return found;
}

bool TraceClosestHit(vec3 origin, vec3 direction, uint tlas_node_count, float t_max, out RayHit hit)
{
    hit.tlas_instance_index = 0u;
    hit.triangle_in_blas    = 0u;
    hit.t                   = t_max;
    hit.bary                = vec2(0.0);

    if (tlas_node_count == 0u)
    {
        return false;
    }

    vec3 inv_dir = 1.0 / direction;

    int stack[kRayStackDepth];
    int sp = 0;
    stack[sp++] = 0;

    bool found = false;
    while (sp > 0)
    {
        int node_index = stack[--sp];
        BvhNode node = tlas_nodes[node_index];
        float t_near;
        if (!IntersectAabbRange(origin, inv_dir, node.bmin, node.bmax, 0.0, t_max, t_near))
        {
            continue;
        }

        if (node.right_or_count < 0)
        {
            int first = node.left_or_first;
            int count = -node.right_or_count;
            for (int i = 0; i < count; ++i)
            {
                uint tlas_idx = uint(first + i);
                GpuTlasInstance inst = tlas_instances[tlas_idx];
                vec3 local_o = (inst.local_from_world * vec4(origin, 1.0)).xyz;
                vec3 local_d = (inst.local_from_world * vec4(direction, 0.0)).xyz;
                if (IntersectBlasClosestHit(local_o, local_d, inst.primitive_id, tlas_idx, t_max, hit))
                {
                    found = true;
                }
            }
        }
        else if (sp + 2 <= kRayStackDepth)
        {
            stack[sp++] = node.left_or_first;
            stack[sp++] = node.right_or_count;
        }
    }
    return found;
}

// ---------------------------------------------------------------------------
// Any-hit traversal (shadow rays)
// ---------------------------------------------------------------------------

bool IntersectBlasAnyHit(vec3 local_origin, vec3 local_direction, uint primitive_id, float t_max)
{
    GpuPrimitive prim = primitives[primitive_id];
    if (prim.index_count == 0u)
    {
        return false;
    }

    vec3 inv_dir = 1.0 / local_direction;

    int stack[kRayStackDepth];
    int sp = 0;
    stack[sp++] = int(prim.blas_root);

    while (sp > 0)
    {
        int node_index = stack[--sp];
        BvhNode node = blas_nodes[node_index];
        float t_near;
        if (!IntersectAabbRange(local_origin, inv_dir, node.bmin, node.bmax, 0.0, t_max, t_near))
        {
            continue;
        }

        if (node.right_or_count < 0)
        {
            int first = node.left_or_first;
            int count = -node.right_or_count;
            for (int i = 0; i < count; ++i)
            {
                uint tri = blas_triangles[prim.blas_triangle_offset + uint(first + i)];
                uint i0 = indices[prim.index_offset + tri * 3u + 0u];
                uint i1 = indices[prim.index_offset + tri * 3u + 1u];
                uint i2 = indices[prim.index_offset + tri * 3u + 2u];
                vec3 v0 = vertices[prim.vertex_offset + i0].position;
                vec3 v1 = vertices[prim.vertex_offset + i1].position;
                vec3 v2 = vertices[prim.vertex_offset + i2].position;
                float t, u, v;
                if (IntersectTriangle(local_origin, local_direction, v0, v1, v2, 0.0, t_max, t, u, v))
                {
                    return true;
                }
            }
        }
        else if (sp + 2 <= kRayStackDepth)
        {
            stack[sp++] = node.left_or_first;
            stack[sp++] = node.right_or_count;
        }
    }
    return false;
}

bool TraceAnyHit(vec3 origin, vec3 direction, uint tlas_node_count, float t_max)
{
    if (tlas_node_count == 0u)
    {
        return false;
    }

    vec3 inv_dir = 1.0 / direction;

    int stack[kRayStackDepth];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0)
    {
        int node_index = stack[--sp];
        BvhNode node = tlas_nodes[node_index];
        float t_near;
        if (!IntersectAabbRange(origin, inv_dir, node.bmin, node.bmax, 0.0, t_max, t_near))
        {
            continue;
        }

        if (node.right_or_count < 0)
        {
            int first = node.left_or_first;
            int count = -node.right_or_count;
            for (int i = 0; i < count; ++i)
            {
                GpuTlasInstance inst = tlas_instances[uint(first + i)];
                vec3 local_o = (inst.local_from_world * vec4(origin, 1.0)).xyz;
                vec3 local_d = (inst.local_from_world * vec4(direction, 0.0)).xyz;
                if (IntersectBlasAnyHit(local_o, local_d, inst.primitive_id, t_max))
                {
                    return true;
                }
            }
        }
        else if (sp + 2 <= kRayStackDepth)
        {
            stack[sp++] = node.left_or_first;
            stack[sp++] = node.right_or_count;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Traversal-cost traversal (heatmap)
// ---------------------------------------------------------------------------

uint TraceTraversalCost(vec3 origin, vec3 direction, uint tlas_node_count)
{
    if (tlas_node_count == 0u)
    {
        return 0u;
    }

    vec3 inv_dir = 1.0 / direction;

    int stack[kRayStackDepth];
    int sp = 0;
    stack[sp++] = 0;

    uint visits = 0u;
    while (sp > 0)
    {
        int node_index = stack[--sp];
        BvhNode node = tlas_nodes[node_index];
        visits += 1u;
        float t_near;
        if (!IntersectAabbRange(origin, inv_dir, node.bmin, node.bmax, 0.0, 1e30, t_near))
        {
            continue;
        }

        if (node.right_or_count < 0)
        {
            int first = node.left_or_first;
            int count = -node.right_or_count;
            for (int i = 0; i < count; ++i)
            {
                GpuTlasInstance inst = tlas_instances[uint(first + i)];
                vec3 local_o = (inst.local_from_world * vec4(origin, 1.0)).xyz;
                vec3 local_d = (inst.local_from_world * vec4(direction, 0.0)).xyz;

                GpuPrimitive prim = primitives[inst.primitive_id];
                if (prim.index_count == 0u) { continue; }

                vec3 inv_local = 1.0 / local_d;
                int blas_stack[kRayStackDepth];
                int blas_sp = 0;
                blas_stack[blas_sp++] = int(prim.blas_root);
                while (blas_sp > 0)
                {
                    int bn = blas_stack[--blas_sp];
                    BvhNode bnode = blas_nodes[bn];
                    visits += 1u;
                    float dummy;
                    if (!IntersectAabbRange(local_o, inv_local, bnode.bmin, bnode.bmax, 0.0, 1e30, dummy))
                    {
                        continue;
                    }
                    if (bnode.right_or_count < 0)
                    {
                        visits += uint(-bnode.right_or_count);
                    }
                    else if (blas_sp + 2 <= kRayStackDepth)
                    {
                        blas_stack[blas_sp++] = bnode.left_or_first;
                        blas_stack[blas_sp++] = bnode.right_or_count;
                    }
                }
            }
        }
        else if (sp + 2 <= kRayStackDepth)
        {
            stack[sp++] = node.left_or_first;
            stack[sp++] = node.right_or_count;
        }
    }
    return visits;
}

#endif // HYBRID_RT_BVH_TRAVERSAL_GLSL
