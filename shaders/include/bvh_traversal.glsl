#ifndef HYBRID_BVH_TRAVERSAL_GLSL
#define HYBRID_BVH_TRAVERSAL_GLSL

#include "common.glsl"

struct GpuVertex
{
    vec3 position;
    float _pad_position;
    vec3 normal;
    float _pad_normal;
    vec4 tangent;
    vec2 uv0;
    vec2 uv1;
    vec4 color0;
};

layout(std430, binding = 0)  readonly buffer VertexBuffer    { GpuVertex vertices[]; };
layout(std430, binding = 1)  readonly buffer IndexBuffer     { uint indices[]; };
layout(std430, binding = 2)  readonly buffer PrimitiveBuffer { GpuPrimitive primitives[]; };
layout(std430, binding = 7)  readonly buffer BlasNodes       { BvhNode blas_nodes[]; };
layout(std430, binding = 8)  readonly buffer BlasTriangles   { uint blas_triangles[]; };
layout(std430, binding = 9)  readonly buffer TlasNodes       { BvhNode tlas_nodes[]; };
layout(std430, binding = 10) readonly buffer TlasInstances   { GpuTlasInstance tlas_instances[]; };

const int kRayStackDepth = 64;

// Moller-Trumbore triangle intersection. Returns true and writes distance and
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
    vec2  bary;                 // Moller-Trumbore (u, v)
};

bool IntersectBlasAnyHit(vec3 local_origin, vec3 local_direction, uint primitive_id, float t_max)
{
    GpuPrimitive prim = primitives[primitive_id];
    if (prim.index_count == 0u)
    {
        return false;
    }

    vec3 inv_dir = 1.0 / local_direction;

    int stack[kRayStackDepth];
    float near_stack[kRayStackDepth];
    int stack_size = 0;

    int root_index = int(prim.blas_root);
    BvhNode root_node = blas_nodes[root_index];
    float root_near_t = IntersectAabbNearT(local_origin, inv_dir, root_node.bmin, root_node.bmax, 0.0, t_max);
    if (root_near_t >= t_max)
    {
        return false;
    }

    stack[stack_size] = root_index;
    near_stack[stack_size] = root_near_t;
    stack_size += 1;

    while (stack_size > 0)
    {
        stack_size -= 1;
        int node_index = stack[stack_size];
        float node_near_t = near_stack[stack_size];
        BvhNode node = blas_nodes[node_index];

        if (node_near_t >= t_max)
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
        else
        {
            int left_index = node.left_or_first;
            int right_index = node.right_or_count;
            BvhNode left_node = blas_nodes[left_index];
            BvhNode right_node = blas_nodes[right_index];
            float left_near_t = IntersectAabbNearT(local_origin, inv_dir, left_node.bmin, left_node.bmax, 0.0, t_max);
            float right_near_t = IntersectAabbNearT(local_origin, inv_dir, right_node.bmin, right_node.bmax, 0.0, t_max);
            bool left_hit = left_near_t < t_max;
            bool right_hit = right_near_t < t_max;

            if (left_hit && right_hit)
            {
                bool left_is_near = left_near_t <= right_near_t;
                int near_index = left_is_near ? left_index : right_index;
                float near_t = left_is_near ? left_near_t : right_near_t;
                int far_index = left_is_near ? right_index : left_index;
                float far_t = left_is_near ? right_near_t : left_near_t;

                if (stack_size + 2 <= kRayStackDepth)
                {
                    stack[stack_size] = far_index;
                    near_stack[stack_size] = far_t;
                    stack_size += 1;
                    stack[stack_size] = near_index;
                    near_stack[stack_size] = near_t;
                    stack_size += 1;
                }
            }
            else if (left_hit)
            {
                if (stack_size < kRayStackDepth)
                {
                    stack[stack_size] = left_index;
                    near_stack[stack_size] = left_near_t;
                    stack_size += 1;
                }
            }
            else if (right_hit)
            {
                if (stack_size < kRayStackDepth)
                {
                    stack[stack_size] = right_index;
                    near_stack[stack_size] = right_near_t;
                    stack_size += 1;
                }
            }
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
    float near_stack[kRayStackDepth];
    int stack_size = 0;

    int root_index = 0;
    BvhNode root_node = tlas_nodes[root_index];
    float root_near_t = IntersectAabbNearT(origin, inv_dir, root_node.bmin, root_node.bmax, 0.0, t_max);
    if (root_near_t >= t_max)
    {
        return false;
    }

    stack[stack_size] = root_index;
    near_stack[stack_size] = root_near_t;
    stack_size += 1;

    while (stack_size > 0)
    {
        stack_size -= 1;
        int node_index = stack[stack_size];
        float node_near_t = near_stack[stack_size];
        BvhNode node = tlas_nodes[node_index];

        if (node_near_t >= t_max)
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
                vec3 local_origin = (inst.local_from_world * vec4(origin, 1.0)).xyz;
                vec3 local_direction = (inst.local_from_world * vec4(direction, 0.0)).xyz;
                if (IntersectBlasAnyHit(local_origin, local_direction, inst.primitive_id, t_max))
                {
                    return true;
                }
            }
        }
        else
        {
            int left_index = node.left_or_first;
            int right_index = node.right_or_count;
            BvhNode left_node = tlas_nodes[left_index];
            BvhNode right_node = tlas_nodes[right_index];
            float left_near_t = IntersectAabbNearT(origin, inv_dir, left_node.bmin, left_node.bmax, 0.0, t_max);
            float right_near_t = IntersectAabbNearT(origin, inv_dir, right_node.bmin, right_node.bmax, 0.0, t_max);
            bool left_hit = left_near_t < t_max;
            bool right_hit = right_near_t < t_max;

            if (left_hit && right_hit)
            {
                bool left_is_near = left_near_t <= right_near_t;
                int near_index = left_is_near ? left_index : right_index;
                float near_t = left_is_near ? left_near_t : right_near_t;
                int far_index = left_is_near ? right_index : left_index;
                float far_t = left_is_near ? right_near_t : left_near_t;

                if (stack_size + 2 <= kRayStackDepth)
                {
                    stack[stack_size] = far_index;
                    near_stack[stack_size] = far_t;
                    stack_size += 1;
                    stack[stack_size] = near_index;
                    near_stack[stack_size] = near_t;
                    stack_size += 1;
                }
            }
            else if (left_hit)
            {
                if (stack_size < kRayStackDepth)
                {
                    stack[stack_size] = left_index;
                    near_stack[stack_size] = left_near_t;
                    stack_size += 1;
                }
            }
            else if (right_hit)
            {
                if (stack_size < kRayStackDepth)
                {
                    stack[stack_size] = right_index;
                    near_stack[stack_size] = right_near_t;
                    stack_size += 1;
                }
            }
        }
    }

    return false;
}

#endif
