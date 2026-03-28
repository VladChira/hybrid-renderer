#version 330 core
out vec4 FragColor;
in vec3 v_local_position;

uniform samplerCube u_env_map;

const float PI = 3.14159265359;
const float MAX_SAMPLE_RADIANCE = 5000.0;

void main()
{		
    // the sample direction equals the hemisphere's orientation 
    vec3 normal = normalize(v_local_position);
  
    vec3 irradiance = vec3(0.0);
  
    // Avoid degenerate tangent basis near poles.
    vec3 up    = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 right = normalize(cross(up, normal));
    up         = normalize(cross(normal, right));

    float sampleDelta = 0.01;
    float nrSamples = 0.0; 
    for(float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for(float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            // spherical to cartesian (in tangent space)
            vec3 tangentSample = vec3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
            // tangent space to world
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal; 

            vec3 sampleColor = texture(u_env_map, sampleVec).rgb;
            sampleColor = min(sampleColor, vec3(MAX_SAMPLE_RADIANCE));
            irradiance += sampleColor * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = PI * irradiance * (1.0 / float(nrSamples));
  
    FragColor = vec4(irradiance, 1.0);
}
