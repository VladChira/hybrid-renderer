#ifndef HYBRID_COMMON_GLSL
#define HYBRID_COMMON_GLSL

struct BvhNode
{
    vec3  bmin;
    int   left_or_first;
    vec3  bmax;
    int   right_or_count;
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

struct GpuTlasInstance
{
    mat4 world_from_local;
    mat4 local_from_world;
    uint primitive_id;
    uint entity_id;
    uint _pad0;
    uint _pad1;
};

float IntersectAabbNearT(vec3 origin, vec3 inv_direction, vec3 bmin, vec3 bmax, float t_min, float t_max)
{
    vec3 t1 = (bmin - origin) * inv_direction;
    vec3 t2 = (bmax - origin) * inv_direction;
    vec3 tmin3 = min(t1, t2);
    vec3 tmax3 = max(t1, t2);
    float near_t = max(max(tmin3.x, tmin3.y), max(tmin3.z, t_min));
    float far_t  = min(min(tmax3.x, tmax3.y), min(tmax3.z, t_max));
    return (near_t <= far_t) ? near_t : 1e30;
}

#endif
