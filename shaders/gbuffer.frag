#version 330 core

in vec3 v_world_normal;
in vec2 v_uv0;

uniform vec3 u_base_color;
uniform float u_metallic;
uniform float u_roughness;
uniform uint u_instance_id;

layout (location = 0) out vec4 o_rt0;
layout (location = 1) out vec4 o_rt1;
layout (location = 2) out uint o_entity_id;

void main()
{
    vec3 normal = normalize(v_world_normal);
    if (dot(normal, normal) < 0.00001)
    {
        normal = vec3(0.0, 1.0, 0.0);
    }

    o_rt0 = vec4(clamp(u_base_color, 0.0, 1.0), clamp(u_metallic, 0.0, 1.0));
    o_rt1 = vec4(normal, clamp(u_roughness, 0.0, 1.0));
    o_entity_id = u_instance_id;
}
