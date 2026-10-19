// CRT look: dark gaps between the rows of table pixels and a faint aperture grille
#include "common.frag.hlsli"

static const float ScanlineDepth = 0.5; // How dark the gap under each row gets
static const float GrilleDepth = 0.12;  // How much a column dims the two colours it doesn't carry
static const float Brightness = 1.2;    // Makes up for the darkening

PSOutput main(PSInput input)
{
    PSOutput output;
    // Sharp table pixels whatever filter the renderer bound: sample the middle of each texel
    float2 texel = input.v_uv * texture_size;
    float4 color = u_texture.Sample(u_sampler, (floor(texel) + 0.5) / texture_size) * input.v_color;

    // The lower half of each table row darkens towards its bottom edge, so even at 2x every other line is darker.
    // Below 2 screen pixels per row, scanlines would only alias.
    float gap = saturate(frac(texel.y) * 2.0 - 1.0);
    float scanline = 1.0 - ScanlineDepth * saturate(output_scale.y - 1.0) * gap;

    // Each screen column carries one of red, green and blue
    int column = int(input.v_position.x) % 3;
    float3 grille = float3(1.0 - GrilleDepth, 1.0 - GrilleDepth, 1.0 - GrilleDepth);
    grille[column] = 1.0;

    output.o_color = float4(saturate(color.rgb * scanline * grille * Brightness), color.a);
    return output;
}
