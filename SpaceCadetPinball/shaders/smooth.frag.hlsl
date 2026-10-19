// Smooth edges: EPX (Scale2x), the edge-directed scaler that xBR grew out of. Each table pixel is drawn as four
// quarters; a quarter takes the colour of its two outer neighbours when they match and the pixel sits on an edge.
#include "common.frag.hlsli"

float4 Texel(int2 position)
{
    int2 size = int2(texture_size);
    return u_texture.Load(int3(clamp(position, int2(0, 0), size - 1), 0));
}

bool Same(float4 a, float4 b)
{
    return all(abs(a - b) < 1.0 / 512.0);
}

PSOutput main(PSInput input)
{
    PSOutput output;
    float2 position = input.v_uv * texture_size;
    int2 cell = int2(floor(position));
    float2 inside = position - floor(position);

    float4 center = Texel(cell);
    float4 up = Texel(cell + int2(0, -1));
    float4 right = Texel(cell + int2(1, 0));
    float4 left = Texel(cell + int2(-1, 0));
    float4 down = Texel(cell + int2(0, 1));

    float4 color = center;
    if (inside.y < 0.5)
    {
        if (inside.x < 0.5)
        {
            if (Same(left, up) && !Same(left, down) && !Same(up, right))
                color = up;
        }
        else if (Same(up, right) && !Same(up, left) && !Same(right, down))
            color = right;
    }
    else
    {
        if (inside.x < 0.5)
        {
            if (Same(down, left) && !Same(down, right) && !Same(left, up))
                color = left;
        }
        else if (Same(right, down) && !Same(right, up) && !Same(down, left))
            color = down;
    }

    output.o_color = color * input.v_color;
    return output;
}
