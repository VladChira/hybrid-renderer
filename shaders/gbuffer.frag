#version 330 core

in vec3 v_world_normal;
in vec2 v_uv0;
in vec2 v_uv1;

uniform vec3 u_base_color;
uniform float u_base_alpha;
uniform sampler2D u_base_color_texture;
uniform int u_has_base_color_texture;
uniform int u_base_color_texcoord;
uniform int u_alpha_masked;
uniform float u_alpha_cutoff;
uniform float u_metallic;
uniform float u_roughness;
uniform uint u_instance_id;

layout (location = 0) out vec4 o_rt0;
layout (location = 1) out vec4 o_rt1;
layout (location = 2) out uint o_entity_id;

void main()
{
    vec2 base_uv = (u_base_color_texcoord == 1) ? v_uv1 : v_uv0;
    vec4 base_sample = (u_has_base_color_texture != 0) ? texture(u_base_color_texture, base_uv) : vec4(1.0);
    vec3 base_color = clamp(u_base_color * base_sample.rgb, 0.0, 1.0);
    float alpha = clamp(u_base_alpha * base_sample.a, 0.0, 1.0);
    if (u_alpha_masked != 0 && alpha < u_alpha_cutoff)
    {
        discard;
    }

    vec3 normal = normalize(v_world_normal);
    if (dot(normal, normal) < 0.00001)
    {
        normal = vec3(0.0, 1.0, 0.0);
    }

    o_rt0 = vec4(base_color, clamp(u_metallic, 0.0, 1.0));
    o_rt1 = vec4(normal, clamp(u_roughness, 0.0, 1.0));
    o_entity_id = u_instance_id;
}
