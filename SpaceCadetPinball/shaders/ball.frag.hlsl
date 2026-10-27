// A ball: a shaded, reflective steel sphere with a soft contact shadow, drawn over the lit table in a quad around it.
// The texture coordinates are the position in ball radii from its centre; the vertex colour holds the ball's depth
// (red, as a fraction of 0xFFFF) and radius (green, as a fraction of the frame's width). The texture drawn is the
// frame, which the ball reflects. Like the frame's ball sprite, the ball shows only where the depth map is behind it,
// so ramps and walls still cover it.
#include "compositor.frag.hlsli"

static const float3 Steel = float3(0.7, 0.71, 0.74);
static const float Reflection = 0.45; // How much of the table the ball mirrors
static const float ReflectionReach = 3.0; // How far around the ball the mirrored table comes from, in radii
static const float Shininess = 48.0;
static const float ShadowDarkness = 0.65;
static const float ShadowCore = 0.15; // How much darker the shadow is right under the ball
static const float ShadowOffset = 0.4; // How far the shadow falls away from the light, in radii
static const float DepthTolerance = 600.0; // How far behind the ball a surface may be and still catch its shadow

float3 TableAt(float2 position)
{
    int2 texel = clamp(int2(position), int2(0, 0), int2(frame_size) - 1);
    return u_texture.Load(int3(texel, 0)).rgb;
}

PSOutput main(PSInput input)
{
    PSOutput output;
    float2 local = input.v_uv;
    float ballDepth = input.v_color.r * 65535.0;
    float radius = input.v_color.g * frame_size.x;
    float2 position = input.v_position.xy;
    float2 center = position - local * radius;
    float depth = DepthAt(int2(position));

    // The light as the screen sees it: x to the right, y down and z away from the viewer. The projection's scale is
    // negative, which turns the camera's x and y round.
    float flip = sign(proj_d);
    float3 toLight = normalize(float3(light.x * flip, light.y * flip, light.z));

    // The shadow, on what lies behind the ball only
    float2 shadowCenter = -toLight.xy * ShadowOffset;
    float shadowDistance = length((local - shadowCenter) / float2(1.0, 0.85));
    float shadow = ShadowDarkness * (1.0 - smoothstep(0.63, 1.5, shadowDistance)) +
                   ShadowCore * (1.0 - smoothstep(0.0, 0.6, shadowDistance));
    shadow *= saturate((depth - ballDepth + DepthTolerance) / DepthTolerance);

    float r2 = dot(local, local);
    float coverage = 0.0;
    float3 color = float3(0.0, 0.0, 0.0);
    if (r2 < 1.0 && depth > ballDepth)
    {
        float3 normal = float3(local, -sqrt(1.0 - r2));
        float3 reflected = reflect(float3(0.0, 0.0, 1.0), normal);

        // The table around the ball, a little blurred, mirrored in it
        float2 mirrored = center + reflected.xy * radius * ReflectionReach;
        float2 spread = float2(radius, radius) * 0.5;
        float3 table = (TableAt(mirrored) + TableAt(mirrored + float2(spread.x, 0.0)) +
                        TableAt(mirrored - float2(spread.x, 0.0)) + TableAt(mirrored + float2(0.0, spread.y)) +
                        TableAt(mirrored - float2(0.0, spread.y))) / 5.0;

        float diffuse = saturate(dot(normal, toLight));
        float alignment = saturate(dot(reflected, toLight));
        float highlight = 1.3 * pow(alignment, Shininess) + 0.2 * pow(alignment, 6.0);
        color = Steel * (0.3 + 0.6 * diffuse) + Reflection * table + highlight;
        // Steel darkens towards the rim, where it mirrors what is beside it rather than the light
        color *= lerp(1.0, 0.65, r2 * r2);
        // One texel of soft edge
        coverage = saturate((1.0 - sqrt(r2)) * radius);
    }

    // The ball over its shadow, for alpha blending
    float alpha = coverage + shadow * (1.0 - coverage);
    output.o_color = float4(alpha > 0.0 ? saturate(color) * coverage / alpha : color, alpha);
    return output;
}
