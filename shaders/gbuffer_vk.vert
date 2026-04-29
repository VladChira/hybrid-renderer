#version 460 core

// Vulkan-only simplified GBuffer vertex shader. Phase 3-stage-A: no
// materials, single color attachment, position/normal pass-through. Sibling
// of shaders/gbuffer.vert (which is GL-only and uses different binding /
// uniform model). Stage-B/C will add the MRT layout and bindless materials.

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv0;
layout(location = 3) in vec2 a_uv1;
layout(location = 4) in vec4 a_tangent;

layout(set = 0, binding = 0) uniform GBufferFrame
{
    mat4 view;
    mat4 projection;
} u_frame;

layout(push_constant) uniform PushConstants
{
    mat4 model;
    uint material_index;
} pc;

layout(location = 0) out vec3 v_world_normal;

void main()
{
    vec4 world_position = pc.model * vec4(a_position, 1.0);
    mat3 normal_matrix  = mat3(transpose(inverse(pc.model)));
    v_world_normal      = normalize(normal_matrix * a_normal);
    gl_Position         = u_frame.projection * u_frame.view * world_position;
}
