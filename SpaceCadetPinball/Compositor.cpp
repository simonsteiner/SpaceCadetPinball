#include "pch.h"
#include "Compositor.h"

#include "PostEffect.h"

#if SDL_VERSION_ATLEAST(3, 4, 0)
#include "shaders/PostEffectShaders.h"
#endif

SDL_Renderer* Compositor::Renderer = nullptr;
SDL_Texture* Compositor::Target = nullptr;
SDL_GPUSampler* Compositor::DepthSampler = nullptr;
SDL_GPUShader* Compositor::ComposeShader = nullptr;
SDL_GPURenderState* Compositor::ComposeState = nullptr;
SDL_Texture* Compositor::BoundDepth = nullptr;

namespace
{
	// The uniforms every pass gets, as compositor.hlsli declares them
	struct PassUniforms
	{
		float FrameSize[2];
		float FrameScale;
		float Padding;
	};
} // namespace

void Compositor::Init(SDL_Renderer* renderer)
{
	if (renderer == Renderer)
		return;
	Uninit();
	Renderer = renderer;
#if SDL_VERSION_ATLEAST(3, 4, 0)
	auto device = PostEffect::IsGpuRenderer(renderer) ? SDL_GetGPURendererDevice(renderer) : nullptr;
	if (!device)
		return;

	// The texture drawn is at slot 0 and the depth map at 1
	ComposeShader = PostEffect::CreateShader(renderer, {compose_frag_spv, sizeof compose_frag_spv},
											 {compose_frag_dxil, sizeof compose_frag_dxil},
											 {compose_frag_msl, sizeof compose_frag_msl}, 2);
	SDL_GPUSamplerCreateInfo samplerInfo{};
	samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
	samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
	samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	DepthSampler = SDL_CreateGPUSampler(device, &samplerInfo);
	if (!ComposeShader || !DepthSampler)
	{
		printf("Could not create Space Cadet 2026's compositor; it draws the table as Classic does.\nSDL Error: %s\n",
			   SDL_GetError());
		SDL_ClearError();
		Uninit();
		Renderer = renderer;
	}
#endif
}

void Compositor::Uninit()
{
	DestroyStates();
	if (Target)
		SDL_DestroyTexture(Target);
	Target = nullptr;
#if SDL_VERSION_ATLEAST(3, 4, 0)
	auto device = Renderer && PostEffect::IsGpuRenderer(Renderer) ? SDL_GetGPURendererDevice(Renderer) : nullptr;
	if (device)
	{
		if (ComposeShader)
			SDL_ReleaseGPUShader(device, ComposeShader);
		if (DepthSampler)
			SDL_ReleaseGPUSampler(device, DepthSampler);
	}
#endif
	ComposeShader = nullptr;
	DepthSampler = nullptr;
	Renderer = nullptr;
}

bool Compositor::Available()
{
	return ComposeShader && DepthSampler;
}

SDL_Texture* Compositor::Compose(SDL_Texture* frame, SDL_Texture* depth, int frameScale)
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (!Available() || !frame || !depth || !PrepareTarget(frame) || !PrepareStates(depth))
		return nullptr;

	float width, height;
	SDL_GetTextureSize(frame, &width, &height);
	PassUniforms uniforms{{width, height}, static_cast<float>(frameScale), 0};
	SDL_SetGPURenderStateFragmentUniforms(ComposeState, 0, &uniforms, sizeof uniforms);

	// Every pass draws in the frame's own texels, so a pass's pixel is the frame's and the depth map's texel
	SDL_SetRenderTarget(Renderer, Target);
	SDL_SetGPURenderState(Renderer, ComposeState);
	SDL_RenderTexture(Renderer, frame, nullptr, nullptr);
	SDL_SetGPURenderState(Renderer, nullptr);
	SDL_SetRenderTarget(Renderer, nullptr);
	return Target;
#else
	(void)frame;
	(void)depth;
	(void)frameScale;
	return nullptr;
#endif
}

bool Compositor::PrepareTarget(SDL_Texture* frame)
{
	// The same size and scale mode as the frame, so it is drawn exactly as the frame would be
	float width, height, targetWidth = 0, targetHeight = 0;
	SDL_GetTextureSize(frame, &width, &height);
	if (Target)
		SDL_GetTextureSize(Target, &targetWidth, &targetHeight);
	if (!Target || targetWidth != width || targetHeight != height)
	{
		if (Target)
			SDL_DestroyTexture(Target);
		Target = SDL_CreateTexture(Renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_TARGET, static_cast<int>(width),
								   static_cast<int>(height));
		if (!Target)
			return false;
		SDL_SetTextureBlendMode(Target, SDL_BLENDMODE_NONE);
	}

	SDL_ScaleMode scaleMode;
	if (SDL_GetTextureScaleMode(frame, &scaleMode))
		SDL_SetTextureScaleMode(Target, scaleMode);
	return true;
}

bool Compositor::PrepareStates(SDL_Texture* depth)
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (depth == BoundDepth && ComposeState)
		return true;

	DestroyStates();
	SDL_GPUTextureSamplerBinding depthBinding{};
	depthBinding.texture = static_cast<SDL_GPUTexture*>(
		SDL_GetPointerProperty(SDL_GetTextureProperties(depth), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
	depthBinding.sampler = DepthSampler;
	if (!depthBinding.texture)
		return false;

	SDL_GPURenderStateCreateInfo stateInfo{};
	stateInfo.fragment_shader = ComposeShader;
	stateInfo.num_sampler_bindings = 1;
	stateInfo.sampler_bindings = &depthBinding;
	ComposeState = SDL_CreateGPURenderState(Renderer, &stateInfo);
	if (!ComposeState)
		return false;
	BoundDepth = depth;
	return true;
#else
	(void)depth;
	return false;
#endif
}

void Compositor::DestroyStates()
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (ComposeState)
		SDL_DestroyGPURenderState(ComposeState);
#endif
	ComposeState = nullptr;
	BoundDepth = nullptr;
}
