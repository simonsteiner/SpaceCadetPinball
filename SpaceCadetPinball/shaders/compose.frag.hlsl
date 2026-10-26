// The compositor's first pass: the software frame, texel for texel, into the target the other passes draw on
#include "compositor.frag.hlsli"

PSOutput main(PSInput input)
{
    PSOutput output;
    int2 texel = int2(input.v_position.xy);
    output.o_color = float4(u_texture.Load(int3(texel, 0)).rgb, 1.0);
    return output;
}
