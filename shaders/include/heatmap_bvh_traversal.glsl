#ifndef HYBRID_BVH_TRAVERSAL_GLSL
#define HYBRID_BVH_TRAVERSAL_GLSL

#include "common.glsl"

layout(std430, binding = 2)  readonly buffer PrimitiveBuffer  { GpuPrimitive primitives[]; };
layout(std430, binding = 7)  readonly buffer BlasNodes        { BvhNode blas_nodes[]; };
layout(std430, binding = 9)  readonly buffer TlasNodes        { BvhNode tlas_nodes[]; };
layout(std430, binding = 10) readonly buffer TlasInstances    { GpuTlasInstance tlas_instances[]; };

uniform uint u_tlas_node_count;

const int kMaxStackDepth = 64;
const float kNoHitDistance = 1e30;

uint TraverseBlas(vec3 origin, vec3 direction, uint primitive_id)
{
    GpuPrimitive prim = primitives[primitive_id];
    if (prim.index_count == 0u)
    {
        return 0u;
    }

    vec3 inv_dir = 1.0 / direction;
    float closest_t = kNoHitDistance;

    int stack[kMaxStackDepth];
    float near_stack[kMaxStackDepth];
    int stack_size = 0;
    int root_index = int(prim.blas_root);
    BvhNode root_node = blas_nodes[root_index];
    float root_near_t = IntersectAabbNearT(origin, inv_dir, root_node.bmin, root_node.bmax, 0.0, closest_t);
    if (root_near_t >= closest_t)
    {
        return 0u;
    }
    stack[stack_size] = root_index;
    near_stack[stack_size] = root_near_t;
    stack_size += 1;

    uint visits = 0u;
    while (stack_size > 0)
    {
        stack_size -= 1;
        int node_index = stack[stack_size];
        float node_near_t = near_stack[stack_size];
        BvhNode node = blas_nodes[node_index];
        visits += 1u;

        if (node_near_t >= closest_t)
        {
            continue;
        }

        if (node.right_or_count < 0)
        {
            // This node is a leaf, count how many triangles we *would have* to intersect
            // without actually intersecting
            visits += uint(-node.right_or_count);
            closest_t = min(closest_t, node_near_t);
        }
        else
        {
            int left_index = node.left_or_first;
            int right_index = node.right_or_count;
            BvhNode left_node = blas_nodes[left_index];
            BvhNode right_node = blas_nodes[right_index];
            float left_near_t = IntersectAabbNearT(origin, inv_dir, left_node.bmin, left_node.bmax, 0.0, closest_t);
            float right_near_t = IntersectAabbNearT(origin, inv_dir, right_node.bmin, right_node.bmax, 0.0, closest_t);
            bool left_hit = left_near_t < closest_t;
            bool right_hit = right_near_t < closest_t;

            if (left_hit && right_hit)
            {
                bool left_is_near = left_near_t <= right_near_t;
                int near_index = left_is_near ? left_index : right_index;
                float near_t = left_is_near ? left_near_t : right_near_t;
                int far_index = left_is_near ? right_index : left_index;
                float far_t = left_is_near ? right_near_t : left_near_t;

                if (stack_size + 2 <= kMaxStackDepth)
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
                if (stack_size < kMaxStackDepth)
                {
                    stack[stack_size] = left_index;
                    near_stack[stack_size] = left_near_t;
                    stack_size += 1;
                }
            }
            else if (right_hit)
            {
                if (stack_size < kMaxStackDepth)
                {
                    stack[stack_size] = right_index;
                    near_stack[stack_size] = right_near_t;
                    stack_size += 1;
                }
            }
        }
    }
    return visits;
}

uint TraverseTlas(vec3 origin, vec3 direction)
{
    if (u_tlas_node_count == 0u)
    {
        return 0u;
    }
    vec3 inv_dir = 1.0 / direction;
    float closest_t = kNoHitDistance;

    int stack[kMaxStackDepth];
    float near_stack[kMaxStackDepth];
    int stack_size = 0;
    int root_index = 0;
    BvhNode root_node = tlas_nodes[root_index];
    float root_near_t = IntersectAabbNearT(origin, inv_dir, root_node.bmin, root_node.bmax, 0.0, closest_t);
    if (root_near_t >= closest_t)
    {
        return 0u;
    }
    stack[stack_size] = root_index;
    near_stack[stack_size] = root_near_t;
    stack_size += 1;

    uint visits = 0u;
    while (stack_size > 0)
    {
        stack_size -= 1;
        int node_index = stack[stack_size];
        float node_near_t = near_stack[stack_size];
        BvhNode node = tlas_nodes[node_index];
        visits += 1u;

        if (node_near_t >= closest_t)
        {
            continue;
        }

        if (node.right_or_count < 0)
        {
            int count = -node.right_or_count;
            int first = node.left_or_first;
            for (int i = 0; i < count; ++i)
            {
                GpuTlasInstance inst = tlas_instances[first + i];
                vec3 local_origin    = (inst.local_from_world * vec4(origin, 1.0)).xyz;
                vec3 local_direction = (inst.local_from_world * vec4(direction, 0.0)).xyz;
                visits += TraverseBlas(local_origin, local_direction, inst.primitive_id);
            }
            closest_t = min(closest_t, node_near_t);
        }
        else
        {
            int left_index = node.left_or_first;
            int right_index = node.right_or_count;
            BvhNode left_node = tlas_nodes[left_index];
            BvhNode right_node = tlas_nodes[right_index];
            float left_near_t = IntersectAabbNearT(origin, inv_dir, left_node.bmin, left_node.bmax, 0.0, closest_t);
            float right_near_t = IntersectAabbNearT(origin, inv_dir, right_node.bmin, right_node.bmax, 0.0, closest_t);
            bool left_hit = left_near_t < closest_t;
            bool right_hit = right_near_t < closest_t;

            if (left_hit && right_hit)
            {
                bool left_is_near = left_near_t <= right_near_t;
                int near_index = left_is_near ? left_index : right_index;
                float near_t = left_is_near ? left_near_t : right_near_t;
                int far_index = left_is_near ? right_index : left_index;
                float far_t = left_is_near ? right_near_t : left_near_t;

                if (stack_size + 2 <= kMaxStackDepth)
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
                if (stack_size < kMaxStackDepth)
                {
                    stack[stack_size] = left_index;
                    near_stack[stack_size] = left_near_t;
                    stack_size += 1;
                }
            }
            else if (right_hit)
            {
                if (stack_size < kMaxStackDepth)
                {
                    stack[stack_size] = right_index;
                    near_stack[stack_size] = right_near_t;
                    stack_size += 1;
                }
            }
        }
    }
    return visits;
}

#endif
