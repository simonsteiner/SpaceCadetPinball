#include "pch.h"
#include "PostEffect.h"

#if SDL_VERSION_ATLEAST(3, 4, 0)
#include "shaders/PostEffectShaders.h"
#endif

bool PostEffect::Supported = false;
SDL_Renderer* PostEffect::Renderer = nullptr;
SDL_GPUShader* PostEffect::Shader = nullptr;
SDL_GPURenderState* PostEffect::State = nullptr;

namespace
{
	struct ShaderCode
	{
		const unsigned char* Code;
		size_t Size;
	};

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
		printf("Could not create the GPU renderer for post-processing.\nSDL Error: %s\n", SDL_GetError());
		SDL_ClearError();
	}
	return renderer;
#else
	return nullptr;
#endif
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

void PostEffect::Init(SDL_Renderer* renderer, PostEffects effect)
{
	Uninit();
	Renderer = renderer;
	Supported = IsGpuRenderer(renderer);
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (!Supported || effect == PostEffects::None)
		return;

	auto device = SDL_GetGPURendererDevice(renderer);
	auto formats = device ? SDL_GetGPUShaderFormats(device) : SDL_GPU_SHADERFORMAT_INVALID;
	auto crt = effect == PostEffects::Crt;
	SDL_GPUShaderCreateInfo info{};
	if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
	{
		info.format = SDL_GPU_SHADERFORMAT_SPIRV;
		info.code = crt ? crt_frag_spv : smooth_frag_spv;
		info.code_size = crt ? sizeof crt_frag_spv : sizeof smooth_frag_spv;
	}
	else if (formats & SDL_GPU_SHADERFORMAT_DXIL)
	{
		info.format = SDL_GPU_SHADERFORMAT_DXIL;
		info.code = crt ? crt_frag_dxil : smooth_frag_dxil;
		info.code_size = crt ? sizeof crt_frag_dxil : sizeof smooth_frag_dxil;
	}
	else if (formats & SDL_GPU_SHADERFORMAT_MSL)
	{
		info.format = SDL_GPU_SHADERFORMAT_MSL;
		info.code = crt ? crt_frag_msl : smooth_frag_msl;
		info.code_size = crt ? sizeof crt_frag_msl : sizeof smooth_frag_msl;
	}
	else
	{
		printf("The GPU renderer takes no shader format the game has; no post-processing.\n");
		return;
	}
	info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	info.num_samplers = 1;
	info.num_uniform_buffers = 1;

	Shader = SDL_CreateGPUShader(device, &info);
	if (Shader)
	{
		SDL_GPURenderStateCreateInfo stateInfo{};
		stateInfo.fragment_shader = Shader;
		State = SDL_CreateGPURenderState(renderer, &stateInfo);
	}
	if (!State)
	{
		printf("Could not create the post-processing shader.\nSDL Error: %s\n", SDL_GetError());
		SDL_ClearError();
		Uninit();
		Renderer = renderer;
		Supported = true;
	}
#endif
}

void PostEffect::Uninit()
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (State)
		SDL_DestroyGPURenderState(State);
	if (Shader)
		SDL_ReleaseGPUShader(SDL_GetGPURendererDevice(Renderer), Shader);
#endif
	State = nullptr;
	Shader = nullptr;
	Renderer = nullptr;
	Supported = false;
}

void PostEffect::Begin(float textureWidth, float textureHeight, float scaleX, float scaleY)
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (!State)
		return;

	EffectUniforms uniforms{{textureWidth, textureHeight}, {scaleX, scaleY}};
	SDL_SetGPURenderStateFragmentUniforms(State, 0, &uniforms, sizeof uniforms);
	SDL_SetGPURenderState(Renderer, State);
#endif
}

void PostEffect::End()
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (State)
		SDL_SetGPURenderState(Renderer, nullptr);
#endif
}
