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

const int kRayStackDepth = 32;

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

bool TraceAnyHit(vec3 origin, vec3 direction, uint tlas_node_count, float t_max)
{
    if (tlas_node_count == 0u)
    {
        return false;
    }

    vec3 tlas_inv_dir = 1.0 / direction;

    int stack[kRayStackDepth];
    int stack_size = 0;

    int root_index = 0;
    BvhNode root_node = tlas_nodes[root_index];
    float root_near_t = IntersectAabbNearT(origin, tlas_inv_dir, root_node.bmin, root_node.bmax, 0.0, t_max);
    if (root_near_t >= t_max)
    {
        return false;
    }

    stack[stack_size] = root_index;
    stack_size += 1;

    int pending_first = 0;
    int pending_count = 0;
    int pending_next = 0;
    bool has_pending_instances = false;

    vec3 blas_origin = vec3(0.0);
    vec3 blas_direction = vec3(0.0);
    vec3 blas_inv_dir = vec3(0.0);
    GpuPrimitive blas_primitive;

    while (true)
    {
        if (has_pending_instances)
        {
            bool top_is_blas = (stack_size > 0) && (stack[stack_size - 1] < 0);
            if (!top_is_blas)
            {
                bool pushed_blas = false;

                while (pending_next < pending_count)
                {
                    GpuTlasInstance inst = tlas_instances[uint(pending_first + pending_next)];
                    GpuPrimitive prim = primitives[inst.primitive_id];
                    if (prim.index_count == 0u)
                    {
                        pending_next += 1;
                        continue;
                    }

                    vec3 local_origin = (inst.local_from_world * vec4(origin, 1.0)).xyz;
                    vec3 local_direction = (inst.local_from_world * vec4(direction, 0.0)).xyz;
                    vec3 local_inv_dir = 1.0 / local_direction;

                    int blas_root_index = int(prim.blas_root);
                    BvhNode blas_root = blas_nodes[blas_root_index];
                    float blas_root_near_t = IntersectAabbNearT(local_origin, local_inv_dir,
                                                                blas_root.bmin, blas_root.bmax,
                                                                0.0, t_max);
                    if (blas_root_near_t >= t_max)
                    {
                        pending_next += 1;
                        continue;
                    }

                    if (stack_size >= kRayStackDepth)
                    {
                        break;
                    }

                    blas_origin = local_origin;
                    blas_direction = local_direction;
                    blas_inv_dir = local_inv_dir;
                    blas_primitive = prim;

                    stack[stack_size] = -(blas_root_index + 1);
                    stack_size += 1;
                    pending_next += 1;
                    pushed_blas = true;
                    break;
                }

                if (pending_next >= pending_count)
                {
                    has_pending_instances = false;
                }

                if (pushed_blas)
                {
                    continue;
                }
            }
        }

        if (stack_size == 0)
        {
            break;
        }

        stack_size -= 1;
        int encoded_node = stack[stack_size];
        bool in_blas = encoded_node < 0;
        int node_index = in_blas ? (-encoded_node - 1) : encoded_node;

        BvhNode node = in_blas ? blas_nodes[node_index] : tlas_nodes[node_index];

        while (true)
        {
            if (node.right_or_count < 0)
            {
                int first = node.left_or_first;
                int count = -node.right_or_count;

                if (in_blas)
                {
                    for (int i = 0; i < count; ++i)
                    {
                        uint tri = blas_triangles[blas_primitive.blas_triangle_offset + uint(first + i)];
                        uint i0 = indices[blas_primitive.index_offset + tri * 3u + 0u];
                        uint i1 = indices[blas_primitive.index_offset + tri * 3u + 1u];
                        uint i2 = indices[blas_primitive.index_offset + tri * 3u + 2u];
                        vec3 v0 = vertices[blas_primitive.vertex_offset + i0].position;
                        vec3 v1 = vertices[blas_primitive.vertex_offset + i1].position;
                        vec3 v2 = vertices[blas_primitive.vertex_offset + i2].position;
                        float t, u, v;
                        if (IntersectTriangle(blas_origin, blas_direction, v0, v1, v2, 0.0, t_max, t, u, v))
                        {
                            return true;
                        }
                    }
                }
                else
                {
                    pending_first = first;
                    pending_count = count;
                    pending_next = 0;
                    has_pending_instances = count > 0;
                }

                break;
            }

            int left_index = node.left_or_first;
            int right_index = node.right_or_count;

            BvhNode left_node = in_blas ? blas_nodes[left_index] : tlas_nodes[left_index];
            BvhNode right_node = in_blas ? blas_nodes[right_index] : tlas_nodes[right_index];

            vec3 ray_origin = in_blas ? blas_origin : origin;
            vec3 ray_inv_dir = in_blas ? blas_inv_dir : tlas_inv_dir;

            float left_near_t = IntersectAabbNearT(ray_origin, ray_inv_dir, left_node.bmin, left_node.bmax, 0.0, t_max);
            float right_near_t = IntersectAabbNearT(ray_origin, ray_inv_dir, right_node.bmin, right_node.bmax, 0.0, t_max);
            bool left_hit = left_near_t < t_max;
            bool right_hit = right_near_t < t_max;

            if (!left_hit && !right_hit)
            {
                break;
            }

            if (left_hit && right_hit)
            {
                bool left_is_near = left_near_t <= right_near_t;
                int near_index = left_is_near ? left_index : right_index;
                BvhNode near_node = left_is_near ? left_node : right_node;
                int far_index = left_is_near ? right_index : left_index;

                if (stack_size < kRayStackDepth)
                {
                    stack[stack_size] = in_blas ? -(far_index + 1) : far_index;
                    stack_size += 1;
                }

                node_index = near_index;
                node = near_node;
                continue;
            }

            if (left_hit)
            {
                node_index = left_index;
                node = left_node;
                continue;
            }

            node_index = right_index;
            node = right_node;
        }
    }

    return false;
}

#endif
