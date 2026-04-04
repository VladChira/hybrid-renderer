#version 330 core

in vec2 v_uv;

uniform sampler2D u_gbuffer_rt0;
uniform sampler2D u_gbuffer_rt1;
uniform sampler2D u_gbuffer_depth;
uniform samplerCube u_skybox_cubemap;
uniform samplerCube u_irradiance_cubemap;
uniform samplerCube u_prefiltered_env_cubemap;
uniform sampler2D u_brdf_lut;
uniform mat4 u_inv_view;
uniform mat4 u_inv_projection;
uniform vec3 u_camera_position;
uniform float u_exposure;
uniform int u_tonemapper;
uniform float u_legacy_curve_strength;
uniform float u_legacy_gamma;
uniform float u_aces_input_scale;
uniform float u_aces_saturation;

uniform int u_has_skybox;
uniform int u_has_irradiance;
uniform int u_has_specular_ibl;
uniform float u_skybox_intensity;
uniform float u_skybox_yaw_radians;

const int MAX_POINT_LIGHTS = 64;
const int MAX_DIRECTIONAL_LIGHTS = 16;
struct PointLight
{
    vec3 position;
    vec3 color;
    float intensity;
    float range;
    float attenuation_constant;
    float attenuation_linear;
    float attenuation_quadratic;
};
uniform int u_point_light_count;
uniform PointLight u_point_lights[MAX_POINT_LIGHTS];

struct DirectionalLight
{
    vec3 direction;
    vec3 color;
    float intensity;
};
uniform int u_directional_light_count;
uniform DirectionalLight u_directional_lights[MAX_DIRECTIONAL_LIGHTS];

out vec4 o_color;

const float PI = 3.14159265359;

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 ToneMapACES(vec3 color)
{
    // Common ACES fitted curve (Narkowicz approximation).
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 LinearToSrgb(vec3 color)
{
    color = max(color, 0.0);
    vec3 low = 12.92 * color;
    vec3 high = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), color));
}

vec3 ApplySaturation(vec3 color, float saturation)
{
    const vec3 luma_weights = vec3(0.2126, 0.7152, 0.0722);
    float luma = dot(color, luma_weights);
    return mix(vec3(luma), color, max(saturation, 0.0));
}

vec3 ToneMapAndEncode(vec3 linear_color)
{
    vec3 exposed = linear_color * max(u_exposure, 0.0001);

    if (u_tonemapper == 0)
    {
        vec3 mapped = vec3(1.0) - exp(-exposed * max(u_legacy_curve_strength, 0.0001));
        return pow(max(mapped, 0.0), vec3(1.0 / max(u_legacy_gamma, 0.0001)));
    }

    vec3 mapped = ToneMapACES(exposed * max(u_aces_input_scale, 0.0001));
    mapped = ApplySaturation(mapped, u_aces_saturation);
    return LinearToSrgb(mapped);
}

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

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 ReconstructWorldPosition(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = u_inv_projection * ndc;
    view /= max(view.w, 1e-5);
    vec4 world = u_inv_view * vec4(view.xyz, 1.0);
    return world.xyz;
}

vec3 ReconstructWorldDirection(vec2 uv)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 view = u_inv_projection * ndc;
    vec3 view_direction = normalize(view.xyz / max(view.w, 1e-5));
    vec3 world_direction = normalize((u_inv_view * vec4(view_direction, 0.0)).xyz);
    return world_direction;
}

vec3 RotateAroundY(vec3 direction, float angle_radians)
{
    float c = cos(angle_radians);
    float s = sin(angle_radians);
    return vec3(c * direction.x + s * direction.z,
                direction.y,
                -s * direction.x + c * direction.z);
}

void main()
{
    vec4 rt0 = texture(u_gbuffer_rt0, v_uv);
    vec4 rt1 = texture(u_gbuffer_rt1, v_uv);
    float depth = texture(u_gbuffer_depth, v_uv).r;

    if (depth >= 1.0)
    {
        if (u_has_skybox != 0)
        {
            vec3 world_direction = ReconstructWorldDirection(v_uv);
            world_direction = RotateAroundY(world_direction, u_skybox_yaw_radians);
            vec3 sky_color = texture(u_skybox_cubemap, world_direction).rgb * max(u_skybox_intensity, 0.0);
            o_color = vec4(ToneMapAndEncode(sky_color), 1.0);
        }
        else
        {
            o_color = vec4(0.0, 0.0, 0.0, 1.0);
        }
        return;
    }

    vec3 albedo = clamp(rt0.rgb, 0.0, 1.0);
    float metallic = clamp(rt0.a, 0.0, 1.0);
    vec3 normal = normalize(rt1.xyz * 2.0 - 1.0);
    float roughness = clamp(rt1.a, 0.045, 1.0);

    vec3 world_position = ReconstructWorldPosition(v_uv, depth);
    vec3 V = normalize(u_camera_position - world_position);
    float ndotv = max(dot(normal, V), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 Lo = vec3(0.0);

    for (int light_index = 0; light_index < u_directional_light_count; ++light_index)
    {
        DirectionalLight light = u_directional_lights[light_index];
        vec3 L = normalize(-light.direction);
        float ndotl = max(dot(normal, L), 0.0);
        if (ndotl <= 0.0)
        {
            continue;
        }

        vec3 radiance = light.color * max(light.intensity, 0.0);
        vec3 H = normalize(V + L);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
        float D = DistributionGGX(normal, H, roughness);
        float G = GeometrySmith(normal, V, L, roughness);
        vec3 numerator = D * G * F;
        float denominator = max(4.0 * ndotv * ndotl, 1e-5);
        vec3 specular = numerator / denominator;
        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
        vec3 diffuse = kD * albedo / PI;
        Lo += (diffuse + specular) * radiance * ndotl;
    }

    for (int light_index = 0; light_index < u_point_light_count; ++light_index)
    {
        PointLight light = u_point_lights[light_index];
        vec3 light_vector = light.position - world_position;
        float light_distance = length(light_vector);
        if (light_distance <= 1e-5)
        {
            continue;
        }

        if (light.range > 0.0 && light_distance > light.range)
        {
            // Any range > 0 is not physically based, but it still looks cool, so let's just add a nice falloff
            light.intensity *= smoothstep(1.75 * light.range, light.range, light_distance);
        }

        vec3 L = light_vector / light_distance;
        float ndotl = max(dot(normal, L), 0.0);
        if (ndotl <= 0.0)
        {
            continue;
        }

        float attenuation = light.attenuation_constant +
                            light.attenuation_linear * light_distance +
                            light.attenuation_quadratic * light_distance * light_distance;
        vec3 radiance = light.color * max(light.intensity, 0.0) / max(attenuation, 1e-5);
        vec3 H = normalize(V + L);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
        float D = DistributionGGX(normal, H, roughness);
        float G = GeometrySmith(normal, V, L, roughness);
        vec3 numerator = D * G * F;
        float denominator = max(4.0 * ndotv * ndotl, 1e-5);
        vec3 specular = numerator / denominator;
        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
        vec3 diffuse = kD * albedo / PI;
        Lo += (diffuse + specular) * radiance * ndotl;
    }

    vec3 ambient = vec3(0.0);
    vec3 F_ibl = FresnelSchlickRoughness(ndotv, F0, roughness);
    vec3 kS_ibl = F_ibl;
    vec3 kD_ibl = (vec3(1.0) - kS_ibl) * (1.0 - metallic);

    if (u_has_irradiance != 0)
    {
        vec3 irradiance_direction = RotateAroundY(normal, u_skybox_yaw_radians);
        vec3 irradiance = texture(u_irradiance_cubemap, irradiance_direction).rgb * max(u_skybox_intensity, 0.0);
        vec3 diffuse_ibl = irradiance * albedo;
        ambient += kD_ibl * diffuse_ibl;
    }

    if (u_has_specular_ibl != 0)
    {
        vec3 R = reflect(-V, normal);
        vec3 reflection_direction = RotateAroundY(R, u_skybox_yaw_radians);
        const float MAX_REFLECTION_LOD = 4.0;
        vec3 prefiltered_color =
            textureLod(u_prefiltered_env_cubemap, reflection_direction, roughness * MAX_REFLECTION_LOD).rgb *
            max(u_skybox_intensity, 0.0);
        vec2 env_brdf = texture(u_brdf_lut, vec2(ndotv, roughness)).rg;
        vec3 specular_ibl = prefiltered_color * (F_ibl * env_brdf.x + env_brdf.y);
        ambient += specular_ibl;
    }

    vec3 color = ambient + Lo;
    o_color = vec4(ToneMapAndEncode(color), 1.0);
}
