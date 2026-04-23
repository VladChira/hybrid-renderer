#version 460 core

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

uniform uint u_directional_light_count;
uniform uint u_point_light_count;
uniform uint u_area_light_count;

struct GpuDirectionalLight
{
    vec4 direction_cast_shadows;  // xyz = direction, w = cast_shadows (0/1)
    vec4 color_intensity;         // xyz = color, w = intensity
};

struct GpuPointLight
{
    vec4 position_intensity;   // xyz = position, w = intensity
    vec4 color_range;          // xyz = color,    w = range
    vec4 attenuation_cast;     // x/y/z = c/l/q,  w = cast_shadows (0/1)
};

struct GpuAreaLight
{
    vec4 position_intensity;   // xyz = position, w = intensity
    vec4 direction_size_x;     // xyz = direction (unit), w = size.x
    vec4 color_size_y;         // xyz = color,    w = size.y
    vec4 two_sided_cast_pad;   // x = two_sided, y = cast_shadows
};

layout(std430, binding = 4) readonly buffer DirectionalLightBuffer
{
    GpuDirectionalLight directional_lights[];
};

layout(std430, binding = 5) readonly buffer PointLightBuffer
{
    GpuPointLight point_lights[];
};

layout(std430, binding = 6) readonly buffer AreaLightBuffer
{
    GpuAreaLight area_lights[];
};

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
    return GeometrySchlickGGX(ndotl, roughness) * GeometrySchlickGGX(ndotv, roughness);
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

void BuildOrthonormalBasis(vec3 n, out vec3 tangent, out vec3 bitangent)
{
    vec3 helper = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    tangent = normalize(cross(helper, n));
    bitangent = normalize(cross(n, tangent));
}

vec3 EvaluateCookTorranceBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    float ndotv = max(dot(N, V), 0.0);
    float ndotl = max(dot(N, L), 0.0);
    if (ndotl <= 0.0 || ndotv <= 0.0)
    {
        return vec3(0.0);
    }

    vec3 half_vector = V + L;
    float half_vector_length_sq = dot(half_vector, half_vector);
    if (half_vector_length_sq <= 1e-8)
    {
        return vec3(0.0);
    }
    vec3 H = half_vector * inversesqrt(half_vector_length_sq);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 numerator = D * G * F;
    float denominator = max(4.0 * ndotv * ndotl, 1e-5);
    vec3 specular = numerator / denominator;
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;
    return diffuse + specular;
}

vec3 ShadeCookTorrance(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    float ndotl = max(dot(N, L), 0.0);
    if (ndotl <= 0.0)
    {
        return vec3(0.0);
    }
    vec3 brdf = EvaluateCookTorranceBRDF(N, V, L, albedo, metallic, roughness, F0);
    return brdf * radiance * ndotl;
}

float Hash1(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

vec2 Hash2(vec2 p)
{
    return vec2(Hash1(p), Hash1(p + vec2(269.5, 183.3)));
}

vec2 QuasiRandom2D(uint sample_index, vec2 seed)
{
    return fract(seed + vec2(0.7548776662466927, 0.5698402909980532) * float(sample_index + 1u));
}

vec3 SampleCosineHemisphere(vec2 xi, vec3 N)
{
    float phi = 2.0 * PI * xi.x;
    float radius = sqrt(xi.y);
    float x = radius * cos(phi);
    float y = radius * sin(phi);
    float z = sqrt(max(1.0 - xi.y, 0.0));

    vec3 tangent = vec3(0.0);
    vec3 bitangent = vec3(0.0);
    BuildOrthonormalBasis(N, tangent, bitangent);
    return normalize(tangent * x + bitangent * y + N * z);
}

vec3 SampleGGXSpecular(vec2 xi, vec3 N, vec3 V, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float phi = 2.0 * PI * xi.x;
    float cos_theta = sqrt(max((1.0 - xi.y) / max(1.0 + (a2 - 1.0) * xi.y, 1e-5), 0.0));
    float sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));

    vec3 tangent = vec3(0.0);
    vec3 bitangent = vec3(0.0);
    BuildOrthonormalBasis(N, tangent, bitangent);
    vec3 H = normalize(tangent * (cos(phi) * sin_theta) +
                       bitangent * (sin(phi) * sin_theta) +
                       N * cos_theta);
    return normalize(reflect(-V, H));
}

float PdfGGXSpecular(vec3 N, vec3 V, vec3 L, float roughness)
{
    float ndotl = max(dot(N, L), 0.0);
    if (ndotl <= 0.0)
    {
        return 0.0;
    }

    vec3 half_vector = V + L;
    float half_vector_length_sq = dot(half_vector, half_vector);
    if (half_vector_length_sq <= 1e-8)
    {
        return 0.0;
    }
    vec3 H = half_vector * inversesqrt(half_vector_length_sq);
    float ndoth = max(dot(N, H), 0.0);
    float vdoth = max(dot(V, H), 0.0);
    if (ndoth <= 0.0 || vdoth <= 0.0)
    {
        return 0.0;
    }

    float D = DistributionGGX(N, H, roughness);
    float pdf_h = D * ndoth;
    return pdf_h / max(4.0 * vdoth, 1e-5);
}

float PdfCosineHemisphere(vec3 N, vec3 L)
{
    float ndotl = max(dot(N, L), 0.0);
    return ndotl / PI;
}

float PdfBrdfMixture(vec3 N, vec3 V, vec3 L, float roughness, float specular_probability)
{
    float diffuse_probability = 1.0 - specular_probability;
    float diffuse_pdf = PdfCosineHemisphere(N, L);
    float specular_pdf = PdfGGXSpecular(N, V, L, roughness);
    return diffuse_probability * diffuse_pdf + specular_probability * specular_pdf;
}

vec3 SampleBrdfMixture(float selector,
                       vec2 sample_xi,
                       vec3 N,
                       vec3 V,
                       float roughness,
                       float specular_probability)
{
    if (selector < specular_probability)
    {
        return SampleGGXSpecular(sample_xi, N, V, roughness);
    }

    return SampleCosineHemisphere(sample_xi, N);
}

float PowerHeuristic(float pdf_a, float pdf_b)
{
    float a2 = pdf_a * pdf_a;
    float b2 = pdf_b * pdf_b;
    return a2 / max(a2 + b2, 1e-5);
}

bool IntersectAreaLightRect(vec3 ray_origin,
                            vec3 ray_direction,
                            vec3 light_position,
                            vec3 light_normal,
                            vec3 light_tangent,
                            vec3 light_bitangent,
                            vec2 half_size,
                            bool two_sided,
                            out float distance_squared,
                            out float emitter_cos)
{
    float denominator = dot(ray_direction, light_normal);
    if (abs(denominator) <= 1e-5)
    {
        return false;
    }

    float t = dot(light_position - ray_origin, light_normal) / denominator;
    if (t <= 1e-5)
    {
        return false;
    }

    vec3 hit = ray_origin + ray_direction * t;
    vec3 local = hit - light_position;
    float local_x = dot(local, light_tangent);
    float local_y = dot(local, light_bitangent);
    if (abs(local_x) > half_size.x || abs(local_y) > half_size.y)
    {
        return false;
    }

    emitter_cos = dot(light_normal, -ray_direction);
    if (two_sided)
    {
        emitter_cos = abs(emitter_cos);
    }
    else
    {
        emitter_cos = max(emitter_cos, 0.0);
    }

    if (emitter_cos <= 0.0)
    {
        return false;
    }

    distance_squared = t * t;
    return true;
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

    for (uint i = 0u; i < u_directional_light_count; ++i)
    {
        GpuDirectionalLight light = directional_lights[i];
        vec3 L = normalize(-light.direction_cast_shadows.xyz);
        vec3 radiance = light.color_intensity.rgb * max(light.color_intensity.w, 0.0);
        Lo += ShadeCookTorrance(normal, V, L, radiance, albedo, metallic, roughness, F0);
    }

    for (uint i = 0u; i < u_point_light_count; ++i)
    {
        GpuPointLight light = point_lights[i];
        vec3 light_position = light.position_intensity.xyz;
        vec3 light_vector = light_position - world_position;
        float light_distance = length(light_vector);
        if (light_distance <= 1e-5)
        {
            continue;
        }

        float intensity = light.position_intensity.w;
        float range = light.color_range.w;
        if (range > 0.0 && light_distance > range)
        {
            intensity *= smoothstep(1.75 * range, range, light_distance);
        }

        vec3 L = light_vector / light_distance;
        float ndotl = max(dot(normal, L), 0.0);
        if (ndotl <= 0.0)
        {
            continue;
        }

        float attenuation = light.attenuation_cast.x +
                            light.attenuation_cast.y * light_distance +
                            light.attenuation_cast.z * light_distance * light_distance;
        vec3 radiance = light.color_range.rgb * max(intensity, 0.0) / max(attenuation, 1e-5);
        Lo += ShadeCookTorrance(normal, V, L, radiance, albedo, metallic, roughness, F0);
    }

    const int kAreaLightMisSampleCount = 2;
    const float kAreaLightMisSampleCountF = float(kAreaLightMisSampleCount);
    float specular_probability = clamp(0.25 + 0.5 * metallic + 0.25 * (1.0 - roughness), 0.1, 0.9);

    for (uint area_index = 0u; area_index < u_area_light_count; ++area_index)
    {
        GpuAreaLight light = area_lights[area_index];
        vec3 light_position = light.position_intensity.xyz;
        float intensity = light.position_intensity.w;
        vec3 light_direction = light.direction_size_x.xyz;
        vec2 clamped_size = max(vec2(light.direction_size_x.w, light.color_size_y.w), vec2(1e-3));
        vec3 light_color = light.color_size_y.rgb;
        bool two_sided = light.two_sided_cast_pad.x > 0.5;

        vec3 light_normal = normalize(light_direction);
        vec3 light_tangent = vec3(0.0);
        vec3 light_bitangent = vec3(0.0);
        BuildOrthonormalBasis(light_normal, light_tangent, light_bitangent);

        vec2 half_size = clamped_size * 0.5;
        float light_area = clamped_size.x * clamped_size.y;
        vec3 emitted_radiance = light_color * max(intensity, 0.0);

        vec2 base_seed = Hash2(v_uv * vec2(4096.0, 2048.0) +
                               vec2(float(area_index) * 17.0, float(area_index) * 97.0));

        vec3 accumulated = vec3(0.0);
        for (int sample_index = 0; sample_index < kAreaLightMisSampleCount; ++sample_index)
        {
            uint sample_u = uint(sample_index);

            // Strategy A: sample the light surface, convert to solid-angle pdf, and MIS against BRDF pdf.
            {
                vec2 xi_light = QuasiRandom2D(sample_u * 4u, base_seed + vec2(0.11, 0.37));
                vec2 rect = (xi_light - 0.5) * 2.0;
                vec3 sampled_position = light_position +
                                        light_tangent * (rect.x * half_size.x) +
                                        light_bitangent * (rect.y * half_size.y);

                vec3 light_vector = sampled_position - world_position;
                float distance_squared = dot(light_vector, light_vector);
                if (distance_squared > 1e-8)
                {
                    float light_distance = sqrt(distance_squared);
                    vec3 L = light_vector / light_distance;
                    float ndotl = max(dot(normal, L), 0.0);
                    if (ndotl > 0.0)
                    {
                        float emitter_cos = dot(light_normal, -L);
                        if (two_sided)
                        {
                            emitter_cos = abs(emitter_cos);
                        }
                        else
                        {
                            emitter_cos = max(emitter_cos, 0.0);
                        }

                        if (emitter_cos > 0.0)
                        {
                            float light_pdf = distance_squared / max(emitter_cos * light_area, 1e-5);
                            float brdf_pdf = PdfBrdfMixture(normal, V, L, roughness, specular_probability);
                            float mis_weight = PowerHeuristic(light_pdf, brdf_pdf);
                            vec3 brdf = EvaluateCookTorranceBRDF(normal, V, L, albedo, metallic, roughness, F0);
                            accumulated += brdf * emitted_radiance * ndotl * mis_weight / max(light_pdf, 1e-5);
                        }
                    }
                }
            }

            // Strategy B: sample BRDF, test if the sampled direction hits the area light, then MIS against light pdf.
            {
                vec2 xi_selector = QuasiRandom2D(sample_u * 4u + 1u, base_seed + vec2(0.53, 0.19));
                vec2 xi_brdf = QuasiRandom2D(sample_u * 4u + 2u, base_seed + vec2(0.71, 0.89));
                vec3 L = SampleBrdfMixture(xi_selector.x, xi_brdf, normal, V, roughness, specular_probability);
                float ndotl = max(dot(normal, L), 0.0);
                if (ndotl > 0.0)
                {
                    float brdf_pdf = PdfBrdfMixture(normal, V, L, roughness, specular_probability);
                    if (brdf_pdf > 0.0)
                    {
                        float distance_squared = 0.0;
                        float emitter_cos = 0.0;
                        bool hit = IntersectAreaLightRect(world_position,
                                                          L,
                                                          light_position,
                                                          light_normal,
                                                          light_tangent,
                                                          light_bitangent,
                                                          half_size,
                                                          two_sided,
                                                          distance_squared,
                                                          emitter_cos);
                        if (hit)
                        {
                            float light_pdf = distance_squared / max(emitter_cos * light_area, 1e-5);
                            float mis_weight = PowerHeuristic(brdf_pdf, light_pdf);
                            vec3 brdf = EvaluateCookTorranceBRDF(normal, V, L, albedo, metallic, roughness, F0);
                            accumulated += brdf * emitted_radiance * ndotl * mis_weight / max(brdf_pdf, 1e-5);
                        }
                    }
                }
            }
        }

        Lo += accumulated / kAreaLightMisSampleCountF;
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
