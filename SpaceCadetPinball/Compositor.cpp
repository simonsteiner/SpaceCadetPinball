#include "pch.h"
#include "Compositor.h"

#include "DefinitiveScene.h"
#include "PostEffect.h"
#include "proj.h"

#if SDL_VERSION_ATLEAST(3, 4, 0)
#include "shaders/PostEffectShaders.h"
#endif

SDL_Renderer* Compositor::Renderer = nullptr;
SDL_Texture* Compositor::Target = nullptr;
SDL_Texture* Compositor::GlowTarget = nullptr;
SDL_GPUSampler* Compositor::DepthSampler = nullptr;
SDL_GPUShader *Compositor::ComposeShader = nullptr, *Compositor::GlowShader = nullptr;
SDL_GPURenderState *Compositor::ComposeState = nullptr, *Compositor::GlowState = nullptr;
SDL_Texture *Compositor::BoundDepth = nullptr, *Compositor::BoundGlow = nullptr;

namespace
{
	// The uniforms every pass gets, as compositor.hlsli declares them
	struct PassUniforms
	{
		float FrameSize[2];
		float FrameScale;
		float Lighting;
		float ProjCenter[2];
		float ProjD;
		float Glow;
		float ZMin;
		float ZScaler;
		float Padding[2];
		float Up[4];
		float Light[4];
	};

	// The key light comes from the upper left of the table and from above, in the table's coordinates
	constexpr float LightDirection[3] = {-0.45f, -0.55f, 0.7f};
	constexpr float GlowBrightness = 0.45f;
	// How far a lamp's glow reaches beyond its sprite, in table pixels: a part of the sprite's size and a few pixels,
	// up to a limit, so the large lit pictures don't flood the table
	constexpr float GlowReachSize = 0.35f, GlowReachPixels = 3.0f, GlowReachMax = 9.0f;

	// A direction in the table's coordinates as the camera sees it
	void ToCamera(const mat4_row_major& matrix, const float (&direction)[3], float (&result)[4])
	{
		auto x = direction[0], y = direction[1], z = direction[2];
		float v[3] = {matrix.Row0.X * x + matrix.Row0.Y * y + matrix.Row0.Z * z,
					  matrix.Row1.X * x + matrix.Row1.Y * y + matrix.Row1.Z * z,
					  matrix.Row2.X * x + matrix.Row2.Y * y + matrix.Row2.Z * z};
		auto length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		for (int i = 0; i < 3; i++)
			result[i] = length > 0 ? v[i] / length : 0;
		result[3] = 0;
	}
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

	// The texture drawn is at slot 0, the depth map at 1 and, for the lighting pass, the lamps' glow at 2
	ComposeShader = PostEffect::CreateShader(renderer, {compose_frag_spv, sizeof compose_frag_spv},
											 {compose_frag_dxil, sizeof compose_frag_dxil},
											 {compose_frag_msl, sizeof compose_frag_msl}, 3);
	SDL_GPUSamplerCreateInfo samplerInfo{};
	samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
	samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
	samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	DepthSampler = SDL_CreateGPUSampler(device, &samplerInfo);
	GlowShader = PostEffect::CreateShader(renderer, {glow_frag_spv, sizeof glow_frag_spv},
										  {glow_frag_dxil, sizeof glow_frag_dxil}, {glow_frag_msl, sizeof glow_frag_msl}, 2);
	if (!ComposeShader || !GlowShader || !DepthSampler)
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
	for (auto texture : {Target, GlowTarget})
	{
		if (texture)
			SDL_DestroyTexture(texture);
	}
	Target = nullptr;
	GlowTarget = nullptr;
#if SDL_VERSION_ATLEAST(3, 4, 0)
	auto device = Renderer && PostEffect::IsGpuRenderer(Renderer) ? SDL_GetGPURendererDevice(Renderer) : nullptr;
	if (device)
	{
		if (ComposeShader)
			SDL_ReleaseGPUShader(device, ComposeShader);
		if (GlowShader)
			SDL_ReleaseGPUShader(device, GlowShader);
		if (DepthSampler)
			SDL_ReleaseGPUSampler(device, DepthSampler);
	}
#endif
	ComposeShader = nullptr;
	GlowShader = nullptr;
	DepthSampler = nullptr;
	Renderer = nullptr;
}

bool Compositor::Available()
{
	return ComposeShader && GlowShader && DepthSampler;
}

SDL_Texture* Compositor::Compose(SDL_Texture* frame, SDL_Texture* depth, int frameScale, const DefinitiveScene& scene)
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (!Available() || !frame || !depth || !PrepareTarget(frame) || !PrepareStates(depth))
		return nullptr;

	float width, height;
	SDL_GetTextureSize(frame, &width, &height);
	auto camera = proj::GetCamera();
	PassUniforms uniforms{{width, height},
						  static_cast<float>(frameScale),
						  scene.Lighting ? 1.0f : 0.0f,
						  {camera.CenterX, camera.CenterY},
						  camera.D,
						  GlowBrightness,
						  camera.ZMin,
						  camera.ZScaler,
						  {},
						  {},
						  {}};
	ToCamera(camera.Matrix, {0, 0, 1}, uniforms.Up);
	ToCamera(camera.Matrix, LightDirection, uniforms.Light);
	SDL_SetGPURenderStateFragmentUniforms(ComposeState, 0, &uniforms, sizeof uniforms);
	SDL_SetGPURenderStateFragmentUniforms(GlowState, 0, &uniforms, sizeof uniforms);

	// Every pass draws in the frame's own texels, so a pass's pixel is the frame's and the depth map's texel. The
	// glows go first, into their own texture, which the lighting pass adds to the table.
	Uint8 red, green, blue, alpha;
	SDL_GetRenderDrawColor(Renderer, &red, &green, &blue, &alpha);
	SDL_SetRenderTarget(Renderer, GlowTarget);
	SDL_SetRenderDrawColor(Renderer, 0, 0, 0, 0);
	SDL_RenderClear(Renderer);
	SDL_SetRenderDrawColor(Renderer, red, green, blue, alpha);
	DrawGlows(frame, frameScale, scene);
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
	(void)scene;
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
	if (!Target || !GlowTarget || targetWidth != width || targetHeight != height)
	{
		// The states bind the glow texture, and a new one can have the old one's address
		DestroyStates();
		for (auto texture : {&Target, &GlowTarget})
		{
			if (*texture)
				SDL_DestroyTexture(*texture);
			*texture = SDL_CreateTexture(Renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_TARGET,
										 static_cast<int>(width), static_cast<int>(height));
			if (!*texture)
				return false;
			SDL_SetTextureBlendMode(*texture, SDL_BLENDMODE_NONE);
		}
	}

	SDL_ScaleMode scaleMode;
	if (SDL_GetTextureScaleMode(frame, &scaleMode))
		SDL_SetTextureScaleMode(Target, scaleMode);
	return true;
}

bool Compositor::PrepareStates(SDL_Texture* depth)
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (depth == BoundDepth && GlowTarget == BoundGlow && ComposeState)
		return true;

	DestroyStates();
	// The depth map, then the glow, which only the lighting pass reads
	SDL_GPUTextureSamplerBinding bindings[2]{};
	SDL_Texture* textures[] = {depth, GlowTarget};
	for (int i = 0; i < 2; i++)
	{
		bindings[i].texture = static_cast<SDL_GPUTexture*>(SDL_GetPointerProperty(
			SDL_GetTextureProperties(textures[i]), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
		bindings[i].sampler = DepthSampler;
		if (!bindings[i].texture)
			return false;
	}

	SDL_GPURenderStateCreateInfo stateInfo{};
	stateInfo.sampler_bindings = bindings;
	stateInfo.num_sampler_bindings = 2;
	stateInfo.fragment_shader = ComposeShader;
	ComposeState = SDL_CreateGPURenderState(Renderer, &stateInfo);
	stateInfo.num_sampler_bindings = 1;
	stateInfo.fragment_shader = GlowShader;
	GlowState = SDL_CreateGPURenderState(Renderer, &stateInfo);
	if (!ComposeState || !GlowState)
	{
		DestroyStates();
		return false;
	}
	BoundDepth = depth;
	BoundGlow = GlowTarget;
	return true;
#else
	(void)depth;
	return false;
#endif
}

void Compositor::ForgetDepth()
{
	DestroyStates();
}

void Compositor::DestroyStates()
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (ComposeState)
		SDL_DestroyGPURenderState(ComposeState);
	if (GlowState)
		SDL_DestroyGPURenderState(GlowState);
#endif
	ComposeState = nullptr;
	GlowState = nullptr;
	BoundDepth = nullptr;
	BoundGlow = nullptr;
}

void Compositor::DrawGlows(SDL_Texture* frame, int frameScale, const DefinitiveScene& scene)
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (scene.Glows.empty())
		return;

	// One quad per lamp, added up in the glow texture; each vertex carries the lamp's sprite and glow (glow.frag.hlsl)
	float width, height;
	SDL_GetTextureSize(frame, &width, &height);
	auto scale = static_cast<float>(frameScale);
	std::vector<SDL_Vertex> vertices;
	std::vector<int> indices;
	vertices.reserve(scene.Glows.size() * 4);
	indices.reserve(scene.Glows.size() * 6);
	for (const auto& glow : scene.Glows)
	{
		SDL_FRect sprite{glow.Sprite.x * scale, glow.Sprite.y * scale, glow.Sprite.w * scale, glow.Sprite.h * scale};
		auto reach =
			std::min(GlowReachSize * std::max(glow.Sprite.w, glow.Sprite.h) + GlowReachPixels, GlowReachMax) * scale;
		// Far enough out for the glow to have faded
		auto margin = reach * 2.5f;
		SDL_FColor lamp{sprite.x / width, sprite.y / height, sprite.w / width, sprite.h / height};
		auto first = static_cast<int>(vertices.size());
		for (auto corner = 0u; corner < 4; corner++)
		{
			auto x = corner & 1u ? sprite.x + sprite.w + margin : sprite.x - margin;
			auto y = corner & 2u ? sprite.y + sprite.h + margin : sprite.y - margin;
			vertices.push_back({{x, y}, lamp, {glow.Strength, reach / width}});
		}
		for (auto index : {0, 1, 2, 1, 3, 2})
			indices.push_back(first + index);
	}

	// The brightest glow in colour and the sum of the glows' peaks in alpha, for LimitGlow in compose.frag.hlsl
	static const auto blend = SDL_ComposeCustomBlendMode(
		SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MAXIMUM, SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE,
		SDL_BLENDOPERATION_ADD);
	SDL_SetTextureBlendMode(frame, blend);
	SDL_SetGPURenderState(Renderer, GlowState);
	SDL_RenderGeometry(Renderer, frame, vertices.data(), static_cast<int>(vertices.size()), indices.data(),
					   static_cast<int>(indices.size()));
	SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE);
#else
	(void)frame;
	(void)frameScale;
	(void)scene;
#endif
}
