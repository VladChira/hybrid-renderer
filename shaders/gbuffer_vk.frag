#version 460 core

// Phase 3-stage-B: 2 color attachments. Layout matches the GL gbuffer's
// RT0/RT1 split — RT0 = albedo+metallic, RT1 = normal+roughness — so the
// downstream lighting / picking infra can land later without shader churn.
// Stage B has no materials yet: RT0 is a flat gray placeholder, RT1
// surfaces the actual world normal + a constant roughness.

layout(location = 0) in vec3 v_world_normal;

layout(location = 0) out vec4 o_rt0;     // .rgb = albedo, .a = metallic
layout(location = 1) out vec4 o_rt1;     // .rgb = encoded normal, .a = roughness

void main()
{
    vec3 n = normalize(v_world_normal);
    o_rt0 = vec4(0.7, 0.7, 0.7, 0.0);
    o_rt1 = vec4(n * 0.5 + 0.5, 0.5);
}
