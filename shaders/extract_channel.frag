#version 460 core

in vec2 v_uv;

layout(location = 0) out vec4 out_color;

uniform sampler2D u_source_texture;
uniform int u_channel_index;

void main()
{
    vec4 source = texture(u_source_texture, v_uv);
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
