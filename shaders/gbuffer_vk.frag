#version 460 core

// Phase 3-stage-A: just visualize world-space normal as color so we have
// something we can recognize (helmet shape, lighting hints). Stage-B will
// expand to MRT (RT0 albedo+metallic, RT1 normal+roughness, EntityID).

layout(location = 0) in vec3 v_world_normal;
layout(location = 0) out vec4 o_rt0;

void main()
{
    vec3 n = normalize(v_world_normal);
    o_rt0 = vec4(n * 0.5 + 0.5, 1.0);
}
