// The glow around a lit lamp, one quad per lamp around its sprite, into the glow texture the lighting pass adds. The
// vertex colour holds the sprite's rectangle and the texture coordinates its strength and the glow's reach, all as
// fractions of the frame; the texture drawn is the frame, from which the lamp's colour is taken.
#include "compositor.frag.hlsli"

static const float GlowInside = 0.2; // How much of the glow falls on the middle of the lamp itself
static const float DepthTolerance = 600.0; // How far in front of the lamp something may be and still catch its glow

PSOutput main(PSInput input)
{
    PSOutput output;
    float2 position = input.v_position.xy;
    float4 sprite = input.v_color * float4(frame_size, frame_size);
    float strength = input.v_uv.x;
    float reach = input.v_uv.y * frame_size.x;

    // The lamp's colour: its texels, the brighter ones counting most, so the dark parts of the sprite don't dull it
    float3 sum = float3(0.0, 0.0, 0.0);
    float weights = 0.0;
    for (int j = 0; j < 4; j++)
    {
        for (int i = 0; i < 4; i++)
        {
            int2 texel = int2(sprite.xy + (float2(i, j) + 0.5) / 4.0 * sprite.zw);
            texel = clamp(texel, int2(0, 0), int2(frame_size) - 1);
            float3 color = u_texture.Load(int3(texel, 0)).rgb;
            float luma = dot(color, float3(0.299, 0.587, 0.114));
            float weight = luma * luma * luma + 1e-4;
            sum += color * weight;
            weights += weight;
        }
    }
    float3 lamp = sum / weights;

    // Lamps are round or rounded, so the glow spreads from the ellipse the sprite's rectangle holds
    float2 center = sprite.xy + sprite.zw * 0.5;
    float2 halfSize = max(sprite.zw * 0.5, 0.5);
    float radius = length((position - center) / halfSize);
    // In pixels past the ellipse's edge, along the line from the lamp's centre
    float distance = length(position - center) * (1.0 - 1.0 / max(radius, 1.0));
    float falloff = exp(-distance * distance / (reach * reach));
    if (radius < 1.0)
        falloff *= lerp(GlowInside, 1.0, radius * radius);

    // Nothing that stands in front of the lamp, such as a ramp, catches its glow
    float lampDepth = DepthAt(int2(center));
    falloff *= smoothstep(lampDepth - 2.0 * DepthTolerance, lampDepth - DepthTolerance, DepthAt(int2(position)));

    // Drawn with the brightest glow kept in colour and the peaks added up in alpha (Compositor::DrawGlows)
    float3 color = lamp * falloff * strength * glow;
    output.o_color = float4(color, max(color.r, max(color.g, color.b)));
    return output;
}
