#ifndef HYBRID_BVH_TRAVERSAL_GLSL
#define HYBRID_BVH_TRAVERSAL_GLSL

#include "common.glsl"

layout(std430, binding = 2)  readonly buffer PrimitiveBuffer  { GpuPrimitive primitives[]; };
layout(std430, binding = 7)  readonly buffer BlasNodes        { BvhNode blas_nodes[]; };
layout(std430, binding = 9)  readonly buffer TlasNodes        { BvhNode tlas_nodes[]; };
layout(std430, binding = 10) readonly buffer TlasInstances    { GpuTlasInstance tlas_instances[]; };

uniform uint u_tlas_node_count;

const int kMaxStackDepth = 64;

uint TraverseBlas(vec3 origin, vec3 direction, uint primitive_id)
{
    GpuPrimitive prim = primitives[primitive_id];
    if (prim.index_count == 0u)
    {
        return 0u;
    }

    vec3 inv_dir = 1.0 / direction;

    int stack[kMaxStackDepth];
    int stack_size = 0;
    stack[stack_size++] = int(prim.blas_root);

    uint visits = 0u;
    while (stack_size > 0)
    {
        int node_index = stack[--stack_size];
        BvhNode node = blas_nodes[node_index];
        visits += 1u;

        if (!IntersectAabb(origin, inv_dir, node.bmin, node.bmax, 0.0, 1e30))
        {
            continue;
        }

        if (node.right_or_count < 0)
        {
            // This node is a leaf, count how many triangles we *would have* to intersect
            // without actually intersecting
            visits += uint(-node.right_or_count);
        }
        else if (stack_size + 2 <= kMaxStackDepth)
        {
            // Otherwise, it's an internal node, test children recursively
            stack[stack_size++] = node.left_or_first;
            stack[stack_size++] = node.right_or_count;
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

    int stack[kMaxStackDepth];
    int stack_size = 0;
    stack[stack_size++] = 0;

    uint visits = 0u;
    while (stack_size > 0)
    {
        int node_index = stack[--stack_size];
        BvhNode node = tlas_nodes[node_index];
        visits += 1u;

        if (!IntersectAabb(origin, inv_dir, node.bmin, node.bmax, 0.0, 1e30))
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
        }
        else if (stack_size + 2 <= kMaxStackDepth)
        {
            stack[stack_size++] = node.left_or_first;
            stack[stack_size++] = node.right_or_count;
        }
    }
    return visits;
}

#endif
