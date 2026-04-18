#version 460 core

in vec3 v_color;

uniform sampler2D u_gbuffer_depth;
uniform vec2 u_inv_render_extent;

out vec4 o_color;

vec3 LinearToSrgb(vec3 color)
{
    color = max(color, 0.0);
    vec3 low = 12.92 * color;
    vec3 high = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), color));
}

void main()
{
    // Manual depth test against the G-buffer depth — the scene framebuffer
    // has its own (unpopulated) depth attachment, so we can't rely on the
    // hardware depth test here.
    vec2 screen_uv = gl_FragCoord.xy * u_inv_render_extent;
    float scene_depth = texture(u_gbuffer_depth, screen_uv).r;
    if (gl_FragCoord.z > scene_depth)
    {
        discard;
    }

    o_color = vec4(LinearToSrgb(clamp(v_color, 0.0, 1.0)), 1.0);
}
