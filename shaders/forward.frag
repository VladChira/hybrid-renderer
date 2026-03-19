#version 330 core

in vec3 v_world_position;
in vec3 v_world_normal;
in vec2 v_uv0;

uniform vec3 u_camera_position;
uniform vec3 u_base_color;
uniform int u_render_mode;

out vec4 o_color;

void main()
{
    vec3 normal = normalize(v_world_normal);
    if (dot(normal, normal) < 0.00001)
    {
        normal = vec3(0.0, 1.0, 0.0);
    }

    if (u_render_mode == 1)
    {
        o_color = vec4(u_base_color, 1.0);
        return;
    }

    vec3 light_direction = normalize(vec3(-0.35, -0.43, 0.25));
    float ndotl = max(dot(normal, light_direction), 0.0);

    vec3 view_direction = normalize(u_camera_position - v_world_position);
    vec3 half_vector = normalize(light_direction + view_direction);
    float specular = pow(max(dot(normal, half_vector), 0.0), 32.0);

    vec3 ambient = 0.08 * u_base_color;
    vec3 diffuse = ndotl * u_base_color;
    vec3 spec = 0.05 * specular * vec3(1.0);

    o_color = vec4(ambient + diffuse + spec, 1.0);
}
