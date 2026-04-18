#ifndef HYBRID_RT_RANDOM_GLSL
#define HYBRID_RT_RANDOM_GLSL

// Lightweight per-pixel hash RNG for stochastic ray passes. Not
// cryptographic — just enough decorrelation for area-light sampling and
// importance sampling. Swap for a blue-noise texture if we ever want better
// low-discrepancy sequences.

uint PcgHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint Hash3(uvec3 v)
{
    return PcgHash(v.x + PcgHash(v.y + PcgHash(v.z)));
}

// Returns a float in [0, 1) from a 32-bit hashed seed.
float Unorm(uint seed)
{
    // Keep the top 24 bits — enough resolution for any sampling we care about.
    return float(seed & 0x00FFFFFFu) / float(0x01000000u);
}

// Per-pixel, per-frame 2D sample in [0, 1)^2. Deterministic across replays.
vec2 Hash2PerPixel(ivec2 pixel, uint frame_index, uint stream)
{
    uvec3 k = uvec3(uint(pixel.x), uint(pixel.y), frame_index ^ (stream * 0xA341316Cu));
    uint h = Hash3(k);
    return vec2(Unorm(h), Unorm(PcgHash(h)));
}

const float kPi = 3.14159265358979323846;

// Cosine-weighted hemisphere direction around `normal`, given two uniform
// samples in [0, 1). For cosine-weighted sampling, the weight cos(θ)/pdf
// collapses to 1, so callers can just multiply the sampled radiance by π⁻¹·π
// = 1 (or by albedo/π for a diffuse BRDF — see SSGI composite).
vec3 CosineWeightedHemisphere(vec3 normal, vec2 u)
{
    float cos_theta = sqrt(max(1.0 - u.y, 0.0));
    float sin_theta = sqrt(u.y);
    float phi = 2.0 * kPi * u.x;

    vec3 local = vec3(cos(phi) * sin_theta,
                      sin(phi) * sin_theta,
                      cos_theta);

    vec3 helper  = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, normal));
    vec3 bitan   = cross(normal, tangent);
    return tangent * local.x + bitan * local.y + normal * local.z;
}

#endif // HYBRID_RT_RANDOM_GLSL
