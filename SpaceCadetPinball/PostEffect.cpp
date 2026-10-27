#include "pch.h"
#include "PostEffect.h"

#if SDL_VERSION_ATLEAST(3, 4, 0)
#include "shaders/PostEffectShaders.h"
#endif

bool PostEffect::Supported = false;
bool PostEffect::GpuFailed = false;
SDL_Renderer* PostEffect::Renderer = nullptr;
PostEffects PostEffect::Effect = PostEffects::None;
SDL_GPUShader* PostEffect::Shaders[static_cast<int>(PostEffects::Max)]{};
SDL_GPURenderState* PostEffect::States[static_cast<int>(PostEffects::Max)]{};

namespace
{
	struct EffectUniforms
	{
		float TextureSize[2];
		float OutputScale[2];
	};
} // namespace

SDL_Renderer* PostEffect::CreateGpuRenderer(SDL_Window* window)
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	auto props = SDL_CreateProperties();
	SDL_SetStringProperty(props, SDL_PROP_RENDERER_CREATE_NAME_STRING, SDL_GPU_RENDERER);
	SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
	SDL_SetBooleanProperty(props, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_SPIRV_BOOLEAN, true);
	SDL_SetBooleanProperty(props, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_DXIL_BOOLEAN, true);
	SDL_SetBooleanProperty(props, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_MSL_BOOLEAN, true);
	auto renderer = SDL_CreateRendererWithProperties(props);
	SDL_DestroyProperties(props);
	if (!renderer)
	{
		printf("Could not create SDL's GPU renderer, using the default renderer.\nSDL Error: %s\n", SDL_GetError());
		SDL_ClearError();
	}
#else
	SDL_Renderer* renderer = nullptr;
#endif
	GpuFailed = !renderer;
	return renderer;
}

bool PostEffect::IsGpuRenderer(SDL_Renderer* renderer)
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	auto name = renderer ? SDL_GetRendererName(renderer) : nullptr;
	return name && strcmp(name, SDL_GPU_RENDERER) == 0;
#else
	return false;
#endif
}

void PostEffect::Init(SDL_Renderer* renderer)
{
	if (renderer == Renderer)
		return;
	Uninit();
	Renderer = renderer;
	Supported = IsGpuRenderer(renderer);
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (!Supported)
		return;

	// Indexed by PostEffects; None has no shader
	constexpr ShaderCode spirv[] = {{}, {crt_frag_spv, sizeof crt_frag_spv}, {smooth_frag_spv, sizeof smooth_frag_spv}};
	constexpr ShaderCode dxil[] = {
		{}, {crt_frag_dxil, sizeof crt_frag_dxil}, {smooth_frag_dxil, sizeof smooth_frag_dxil}};
	constexpr ShaderCode msl[] = {{}, {crt_frag_msl, sizeof crt_frag_msl}, {smooth_frag_msl, sizeof smooth_frag_msl}};
	static_assert(std::size(spirv) == static_cast<size_t>(PostEffects::Max), "One shader per effect");

	for (int i = static_cast<int>(PostEffects::None) + 1; i < static_cast<int>(PostEffects::Max); i++)
	{
		Shaders[i] = CreateShader(renderer, spirv[i], dxil[i], msl[i], 1);
		if (Shaders[i])
		{
			SDL_GPURenderStateCreateInfo stateInfo{};
			stateInfo.fragment_shader = Shaders[i];
			States[i] = SDL_CreateGPURenderState(renderer, &stateInfo);
		}
		if (!States[i])
		{
			printf("Could not create the post-processing shader.\nSDL Error: %s\n", SDL_GetError());
			SDL_ClearError();
		}
	}
#endif
}

SDL_GPUShader* PostEffect::CreateShader(SDL_Renderer* renderer, ShaderCode spirv, ShaderCode dxil, ShaderCode msl,
										int samplers)
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	auto device = IsGpuRenderer(renderer) ? SDL_GetGPURendererDevice(renderer) : nullptr;
	auto formats = device ? SDL_GetGPUShaderFormats(device) : SDL_GPU_SHADERFORMAT_INVALID;
	SDL_GPUShaderCreateInfo info{};
	ShaderCode code;
	if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
	{
		info.format = SDL_GPU_SHADERFORMAT_SPIRV;
		code = spirv;
	}
	else if (formats & SDL_GPU_SHADERFORMAT_DXIL)
	{
		info.format = SDL_GPU_SHADERFORMAT_DXIL;
		code = dxil;
	}
	else if (formats & SDL_GPU_SHADERFORMAT_MSL)
	{
		info.format = SDL_GPU_SHADERFORMAT_MSL;
		code = msl;
	}
	else
	{
		if (device)
			printf("The GPU renderer takes no shader format the game has.\n");
		return nullptr;
	}
	info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	info.num_samplers = samplers;
	info.num_uniform_buffers = 1;
	info.code = code.Code;
	info.code_size = code.Size;
	return SDL_CreateGPUShader(device, &info);
#else
	return nullptr;
#endif
}

void PostEffect::Uninit()
{
	for (int i = 0; i < static_cast<int>(PostEffects::Max); i++)
	{
#if SDL_VERSION_ATLEAST(3, 4, 0)
		if (States[i])
			SDL_DestroyGPURenderState(States[i]);
		if (Shaders[i])
			SDL_ReleaseGPUShader(SDL_GetGPURendererDevice(Renderer), Shaders[i]);
#endif
		States[i] = nullptr;
		Shaders[i] = nullptr;
	}
	Renderer = nullptr;
	Supported = false;
}

void PostEffect::SetEffect(PostEffects effect)
{
	Effect = effect;
}

void PostEffect::Begin(float textureWidth, float textureHeight, float scaleX, float scaleY)
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	auto state = States[static_cast<int>(Effect)];
	if (!state)
		return;

	EffectUniforms uniforms{{textureWidth, textureHeight}, {scaleX, scaleY}};
	SDL_SetGPURenderStateFragmentUniforms(state, 0, &uniforms, sizeof uniforms);
	SDL_SetGPURenderState(Renderer, state);
#endif
}

void PostEffect::End()
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (States[static_cast<int>(Effect)])
		SDL_SetGPURenderState(Renderer, nullptr);
#endif
}
