// Bindings for the compositor's passes (Compositor.cpp), in an SDL_GPURenderState: the texture being drawn at slot 0
// of space 2 and the frame's depth map at slot 1, uniforms at slot 0 of space 3. SPIR-V gets combined image samplers,
// as SDL's Vulkan backend expects. Every pass draws into a target the size of the frame, so a pixel's position is
// its texel in the frame and in the depth map.

cbuffer Context : register(b0, space3)
{
    float2 frame_size; // Size of the frame and its depth map, in texels
    float frame_scale; // Texels per table pixel: 1, or the upscaled art's factor
    float padding;
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
