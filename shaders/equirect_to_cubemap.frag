#version 330 core

in vec3 v_local_position;

uniform sampler2D u_equirectangular_map;

out vec4 o_color;

const vec2 INV_ATAN = vec2(0.15915494309, 0.31830988618);

vec2 SampleSphericalMap(vec3 direction)
{
    vec2 uv = vec2(atan(direction.z, direction.x), asin(clamp(direction.y, -1.0, 1.0)));
    uv *= INV_ATAN;
    uv += 0.5;
    uv.y = 1.0 - uv.y;
    return uv;
}

void main()
{
    vec3 direction = normalize(v_local_position);
    vec2 uv = SampleSphericalMap(direction);
    vec3 color = texture(u_equirectangular_map, uv).rgb;
    o_color = vec4(color, 1.0);
}
