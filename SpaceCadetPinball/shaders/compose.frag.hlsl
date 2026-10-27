// The compositor's first pass: the software frame, lit by one key light. The depth map gives every texel a position
// and from its neighbours a surface normal; the lighting is relative to that of the flat playfield, so the playfield
// keeps its colours and slopes, ramps and rounded parts turn towards or away from the light.
#include "compositor.frag.hlsli"

// The lamps' glow, added up by glow.frag.hlsl
[[vk::combinedImageSampler]] Texture2D u_glow : register(t2, space2);
[[vk::combinedImageSampler]] SamplerState u_glow_sampler : register(s2, space2);

static const float Ambient = 0.5;
static const float Diffuse = 0.5;
static const float Specular = 0.4;
static const float Shininess = 20.0;
static const float SteepShine = 1.5; // How much more the steep parts, such as rails and rims, shine
static const float GlowLimit = 1.5;  // Overlapping glows add up to at most this many lamps' worth

// The lamps' glow: the brightest at this texel and half of the others, which the glow texture's alpha adds up, and no
// more than GlowLimit times one lamp's brightest, so lamps close together don't add up to a haze
float3 LimitGlow(float4 glows)
{
    float brightest = max(glows.r, max(glows.g, glows.b));
    if (brightest <= 0.0)
        return float3(0.0, 0.0, 0.0);
    float others = max(glows.a - brightest, 0.0);
    float peak = brightest + 0.5 * others;
    float knee = glow;
    if (peak > knee)
    {
        float room = knee * (GlowLimit - 1.0);
        peak = knee + room * (1.0 - exp(-(peak - knee) / room));
    }
    return glows.rgb * (peak / brightest);
}

// The slope towards one neighbour pair: of the steps to either side, the one with less change in depth, so an edge
// between an object and what is behind it doesn't count as a slope
float3 Slope(int2 texel, int2 step, float3 center, float depth)
{
    float before = DepthAt(texel - step), after = DepthAt(texel + step);
    bool useAfter = abs(after - depth) <= abs(depth - before);
    if (after >= NoDepth)
        useAfter = false;
    if (before >= NoDepth && !useAfter)
        return float3(0.0, 0.0, 0.0);
    return useAfter ? PositionAt(texel + step, after) - center : center - PositionAt(texel - step, before);
}

float Shade(float3 normal, float3 toLight)
{
    return Ambient + Diffuse * saturate(dot(normal, toLight));
}

float Shine(float3 normal, float3 halfway)
{
    return pow(saturate(dot(normal, halfway)), Shininess);
}

float3 Lit(int2 texel, float3 color)
{
    float depth = DepthAt(texel);
    if (lighting <= 0.0 || depth >= NoDepth)
        return color;

    // One table pixel to each side
    int step = max(int(frame_scale), 1);
    float3 center = PositionAt(texel, depth);
    float3 alongX = Slope(texel, int2(step, 0), center, depth);
    float3 alongY = Slope(texel, int2(0, step), center, depth);
    float3 normal = cross(alongX, alongY);
    if (dot(normal, normal) < 1e-12)
        return color;
    normal = normalize(normal);
    if (dot(normal, center) > 0.0)
        normal = -normal;

    float3 toLight = light.xyz;
    float3 halfway = normalize(toLight - normalize(center));
    float3 flat = up.xyz;
    float3 lit = color * Shade(normal, toLight) / Shade(flat, toLight);
    float steep = 1.0 - saturate(dot(normal, flat));
    lit += Specular * (1.0 + SteepShine * steep) * max(Shine(normal, halfway) - Shine(flat, halfway), 0.0) *
           (0.5 + 0.5 * color);
    return lerp(color, saturate(lit), lighting);
}

PSOutput main(PSInput input)
{
    PSOutput output;
    int2 texel = int2(input.v_position.xy);
    float3 color = Lit(texel, u_texture.Load(int3(texel, 0)).rgb);
    output.o_color = float4(saturate(color + LimitGlow(u_glow.Load(int3(texel, 0)))), 1.0);
    return output;
}
