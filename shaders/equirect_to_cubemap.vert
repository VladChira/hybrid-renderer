#version 330 core

out vec3 v_local_position;

uniform mat4 u_projection;
uniform mat4 u_view;

vec3 CubePosition(int index)
{
    const vec3 positions[36] = vec3[](
        vec3(-1.0, 1.0, -1.0),
        vec3(-1.0, -1.0, -1.0),
        vec3(1.0, -1.0, -1.0),
        vec3(1.0, -1.0, -1.0),
        vec3(1.0, 1.0, -1.0),
        vec3(-1.0, 1.0, -1.0),

        vec3(-1.0, -1.0, 1.0),
        vec3(-1.0, -1.0, -1.0),
        vec3(-1.0, 1.0, -1.0),
        vec3(-1.0, 1.0, -1.0),
        vec3(-1.0, 1.0, 1.0),
        vec3(-1.0, -1.0, 1.0),

        vec3(1.0, -1.0, -1.0),
        vec3(1.0, -1.0, 1.0),
        vec3(1.0, 1.0, 1.0),
        vec3(1.0, 1.0, 1.0),
        vec3(1.0, 1.0, -1.0),
        vec3(1.0, -1.0, -1.0),

        vec3(-1.0, -1.0, 1.0),
        vec3(-1.0, 1.0, 1.0),
        vec3(1.0, 1.0, 1.0),
        vec3(1.0, 1.0, 1.0),
        vec3(1.0, -1.0, 1.0),
        vec3(-1.0, -1.0, 1.0),

        vec3(-1.0, 1.0, -1.0),
        vec3(1.0, 1.0, -1.0),
        vec3(1.0, 1.0, 1.0),
        vec3(1.0, 1.0, 1.0),
        vec3(-1.0, 1.0, 1.0),
        vec3(-1.0, 1.0, -1.0),

        vec3(-1.0, -1.0, -1.0),
        vec3(-1.0, -1.0, 1.0),
        vec3(1.0, -1.0, -1.0),
        vec3(1.0, -1.0, -1.0),
        vec3(-1.0, -1.0, 1.0),
        vec3(1.0, -1.0, 1.0)
    );
    return positions[index];
}

void main()
{
    vec3 local_position = CubePosition(gl_VertexID);
    v_local_position = local_position;
    gl_Position = u_projection * u_view * vec4(local_position, 1.0);
}
