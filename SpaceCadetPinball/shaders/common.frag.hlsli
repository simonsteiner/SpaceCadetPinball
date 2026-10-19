// Bindings for a fragment shader in an SDL_GPURenderState: the texture being drawn at slot 0 of space 2,
// uniforms at slot 0 of space 3. SPIR-V gets a combined image sampler, as SDL's Vulkan backend expects.

cbuffer Context : register(b0, space3)
{
    float2 texture_size; // Size of the texture being drawn, in texels
    float2 output_scale; // Screen pixels per texel
};

[[vk::combinedImageSampler]] Texture2D u_texture : register(t0, space2);
[[vk::combinedImageSampler]] SamplerState u_sampler : register(s0, space2);

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
