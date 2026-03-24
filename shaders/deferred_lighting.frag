#version 330 core

in vec2 v_uv;

uniform sampler2D u_gbuffer_rt0;
uniform sampler2D u_gbuffer_rt1;
uniform sampler2D u_gbuffer_depth;
uniform mat4 u_inv_view;
uniform mat4 u_inv_projection;
uniform vec3 u_camera_position;
uniform float u_exposure;

out vec4 o_color;

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float ndoth = max(dot(N, H), 0.0);
    float ndoth2 = ndoth * ndoth;

    float denom = (ndoth2 * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 1e-5);
}

float GeometrySchlickGGX(float ndotv, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotv / max(ndotv * (1.0 - k) + k, 1e-5);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float ndotv = max(dot(N, V), 0.0);
    float ndotl = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(ndotv, roughness);
    float ggx1 = GeometrySchlickGGX(ndotl, roughness);
    return ggx1 * ggx2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 ReconstructWorldPosition(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = u_inv_projection * ndc;
    view /= max(view.w, 1e-5);
    vec4 world = u_inv_view * vec4(view.xyz, 1.0);
    return world.xyz;
}

void main()
{
    vec4 rt0 = texture(u_gbuffer_rt0, v_uv);
    vec4 rt1 = texture(u_gbuffer_rt1, v_uv);
    float depth = texture(u_gbuffer_depth, v_uv).r;

    if (depth >= 1.0)
    {
        o_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 albedo = clamp(rt0.rgb, 0.0, 1.0);
    float metallic = clamp(rt0.a, 0.0, 1.0);
    vec3 normal = normalize(rt1.xyz * 2.0 - 1.0);
    float roughness = clamp(rt1.a, 0.045, 1.0);

    vec3 world_position = ReconstructWorldPosition(v_uv, depth);
    vec3 V = normalize(u_camera_position - world_position);
    vec3 L = normalize(vec3(-0.35, -0.43, 0.25));
    vec3 H = normalize(V + L);

    vec3 radiance = vec3(4.0);
    float ndotl = max(dot(normal, L), 0.0);
    float ndotv = max(dot(normal, V), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float D = DistributionGGX(normal, H, roughness);
    float G = GeometrySmith(normal, V, L, roughness);

    vec3 numerator = D * G * F;
    float denominator = max(4.0 * ndotv * ndotl, 1e-5);
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    vec3 Lo = (diffuse + specular) * radiance * ndotl;
    vec3 ambient = 0.03 * albedo;
    vec3 color = ambient + Lo;

    color = vec3(1.0) - exp(-color * max(u_exposure, 0.0001));
    color = pow(color, vec3(1.0 / 2.2));
    o_color = vec4(color, 1.0);
}
