// Bindings for the compositor's passes (Compositor.cpp), in an SDL_GPURenderState: the texture being drawn at slot 0
// of space 2 and the frame's depth map at slot 1, uniforms at slot 0 of space 3. SPIR-V gets combined image samplers,
// as SDL's Vulkan backend expects. Every pass draws into a target the size of the frame, so a pixel's position is
// its texel in the frame and in the depth map.

cbuffer Context : register(b0, space3)
{
    float2 frame_size;  // Size of the frame and its depth map, in texels
    float frame_scale;  // Texels per table pixel: 1, or the upscaled art's factor
    float lighting;     // How much of the lighting shows, 0 to 1
    float2 proj_center; // The table's projection (proj.cpp): its centre in table pixels, its scale,
    float proj_d;       // and how a depth turns into a distance from the camera
    float glow;         // How bright the lamps' glow is
    float z_min;
    float z_scaler;
    float2 padding;
    float4 up;          // The table's up, and the direction towards the light, as the camera sees them
    float4 light;
};

[[vk::combinedImageSampler]] Texture2D u_texture : register(t0, space2);
[[vk::combinedImageSampler]] SamplerState u_sampler : register(s0, space2);
[[vk::combinedImageSampler]] Texture2D u_depth : register(t1, space2);
[[vk::combinedImageSampler]] SamplerState u_depth_sampler : register(s1, space2);

struct PSInput
{
    float4 v_color : COLOR0;
    float2 v_uv : TEXCOORD0;
    float4 v_position : SV_Position;
};

struct PSOutput
{
    float4 o_color : SV_Target;
};

static const float NoDepth = 65535.0;

// A depth on the depth maps' 0 to 0xFFFF scale; 0xFFFF where nothing was drawn
float DepthAt(int2 texel)
{
    texel = clamp(texel, int2(0, 0), int2(frame_size) - 1);
    float2 bytes = round(u_depth.Load(int3(texel, 0)).rg * 255.0);
    return bytes.x * 256.0 + bytes.y;
}

// Where a texel's depth puts it, as the camera sees it
float3 PositionAt(int2 texel, float depth)
{
    float2 table = (float2(texel) + 0.5) / frame_scale;
    float3 ray = normalize(float3((table - proj_center) / proj_d, 1.0));
    return ray * (depth / z_scaler + z_min);
}
