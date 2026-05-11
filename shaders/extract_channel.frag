#version 460 core

in vec2 v_uv;

layout(location = 0) out vec4 out_color;

uniform sampler2D u_source_texture;
uniform int u_channel_index;
uniform int u_output_mode;
uniform float u_depth_near_plane;
uniform float u_depth_far_plane;

float LinearizeDepth(float depth_sample)
{
    float z_ndc = depth_sample * 2.0 - 1.0;
    return (2.0 * u_depth_near_plane * u_depth_far_plane) /
           (u_depth_far_plane + u_depth_near_plane - z_ndc * (u_depth_far_plane - u_depth_near_plane));
}

void main()
{
    vec4 source = texture(u_source_texture, v_uv);
    if (u_output_mode == 1)
    {
        float linear_depth = LinearizeDepth(source.r);
        float normalized_depth = clamp((linear_depth - u_depth_near_plane) /
                                       max(u_depth_far_plane - u_depth_near_plane, 1e-5),
                                       0.0,
                                       1.0);
        out_color = vec4(vec3(normalized_depth), 1.0);
        return;
    }

    if (u_channel_index < 0)
    {
        out_color = vec4(source.rgb, 1.0);
        return;
    }

    float value = source.r;
    if (u_channel_index == 1)
    {
        value = source.g;
    }
    else if (u_channel_index == 2)
    {
        value = source.b;
    }
    else if (u_channel_index == 3)
    {
        value = source.a;
    }

    out_color = vec4(value, value, value, 1.0);
}
