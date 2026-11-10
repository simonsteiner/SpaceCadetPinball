#include "pch.h"
#include "Compositor.h"

#include "ChangedTiles.h"

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
SDL_GPUShader* Compositor::ComposeShader = nullptr;
SDL_GPUShader* Compositor::GlowShader = nullptr;
SDL_GPUShader* Compositor::BallShader = nullptr;
SDL_GPURenderState* Compositor::ComposeState = nullptr;
SDL_GPURenderState* Compositor::GlowState = nullptr;
SDL_GPURenderState* Compositor::BallState = nullptr;
SDL_Texture* Compositor::BoundDepth = nullptr;
SDL_Texture* Compositor::BoundGlow = nullptr;

namespace
{
	// The uniforms every pass gets, as compositor.hlsli declares them
	struct PassUniforms
	{
		std::array<float, 2> FrameSize;
		float FrameScale;
		float Lighting;
		std::array<float, 2> ProjCenter;
		float ProjD;
		float Glow;
		float ZMin;
		float ZScaler;
		float GlowPulse;
		float Padding;
		std::array<float, 4> Up;
		std::array<float, 4> Light;

		bool operator==(const PassUniforms&) const = default;
	};

	// The key light comes from the upper left and from above; the table's x runs from right to left on screen
	constexpr std::array<float, 3> LightDirection = {0.45f, -0.55f, 0.7f};
	constexpr float GlowBrightness = 0.45f;
	// How far a lamp's glow reaches beyond its sprite, in table pixels: a part of the sprite's size and a few pixels,
	// up to a limit, so the large lit pictures don't flood the table
	constexpr float GlowReachSize = 0.35f;
	constexpr float GlowReachPixels = 3.0f;
	constexpr float GlowReachMax = 9.0f;

	// A ball's quad reaches this far from its centre, in radii, to hold its shadow
	constexpr float BallQuadExtent = 1.9f;

	// The composed picture is kept between frames, and a frame composes only the tiles whose inputs changed: the
	// frame's or the depth map's texels, a lamp's glow, a ball that was drawn there, or the lighting itself. On a
	// phone the passes over the whole frame were more than half of the GPU's work, for a picture of which a few
	// percent change from one frame to the next. A tile composed again gets exactly what composing all of it gives.
	class TileSet
	{
	public:
		void Resize(int width, int height)
		{
			Width = width;
			Height = height;
			Across = (width + ChangedTiles::Size - 1) / ChangedTiles::Size;
			Tiles.assign(static_cast<size_t>(Across) * ((height + ChangedTiles::Size - 1) / ChangedTiles::Size), 0);
			Some = false;
		}

		// Whether the set was sized for a texture of this many texels
		bool SizeIs(int width, int height) const
		{
			return Width == width && Height == height;
		}

		bool Any() const
		{
			return Some;
		}

		void Clear()
		{
			std::fill(Tiles.begin(), Tiles.end(), static_cast<uint8_t>(0));
			Some = false;
		}

		void AddAll()
		{
			Add(SDL_Rect{0, 0, Width, Height});
		}

		// The tiles a rectangle of texels touches
		void Add(const SDL_Rect& rect)
		{
			const SDL_Rect bounds{0, 0, Width, Height};
			SDL_Rect clipped;
			if (!SDL_GetRectIntersection(&rect, &bounds, &clipped))
				return;
			for (int y = clipped.y / ChangedTiles::Size; y <= (clipped.y + clipped.h - 1) / ChangedTiles::Size; y++)
			{
				for (int x = clipped.x / ChangedTiles::Size; x <= (clipped.x + clipped.w - 1) / ChangedTiles::Size; x++)
					Tiles[static_cast<size_t>(y) * Across + x] = 1;
			}
			Some = true;
		}

		// Adds another set's tiles; both sets must have the same size
		void Add(const TileSet& other)
		{
			for (auto i = 0u; i < Tiles.size(); i++)
				Tiles[i] |= other.Tiles[i];
			Some = Some || other.Some;
		}

		// Each row's runs of tiles, as rectangles of texels
		template <typename Draw> void ForEachRun(Draw&& draw) const
		{
			for (int y = 0; Some && static_cast<size_t>(y) * Across < Tiles.size(); y++)
			{
				for (int x = 0; x < Across; x++)
				{
					if (!Tiles[static_cast<size_t>(y) * Across + x])
						continue;
					auto first = x;
					while (x + 1 < Across && Tiles[static_cast<size_t>(y) * Across + x + 1])
						x++;
					SDL_Rect run{first * ChangedTiles::Size, y * ChangedTiles::Size,
								 (x + 1 - first) * ChangedTiles::Size, ChangedTiles::Size};
					run.w = std::min(run.w, Width - run.x);
					run.h = std::min(run.h, Height - run.y);
					draw(run);
				}
			}
		}

	private:
		int Width = 0;
		int Height = 0;
		int Across = 0;
		bool Some = false;
		std::vector<uint8_t> Tiles;
	};

	// What the kept textures hold: the tiles to draw again this frame, and the scene they were drawn from
	TileSet StaleTiles, StaleGlowTiles;
	bool AllStale = true;
	std::vector<DefinitiveScene::Glow> DrawnGlows;
	std::vector<SDL_Rect> DrawnBalls;
	PassUniforms DrawnUniforms{};

	bool Touches(const std::vector<SDL_Rect>& changes, const SDL_Rect& rect)
	{
		return std::any_of(changes.begin(), changes.end(),
						   [&rect](const SDL_Rect& change) { return SDL_HasRectIntersection(&change, &rect); });
	}

	// Whether two glows have the same sprite rectangle and strength, so the one drawn still stands for the other
	bool SameGlow(const DefinitiveScene::Glow& a, const DefinitiveScene::Glow& b)
	{
		return a.Sprite.x == b.Sprite.x && a.Sprite.y == b.Sprite.y && a.Sprite.w == b.Sprite.w &&
			   a.Sprite.h == b.Sprite.h && a.Strength == b.Strength;
	}

	// The texels a rectangle given in fractions of texels covers
	SDL_Rect Texels(const SDL_FRect& rect)
	{
		auto left = static_cast<int>(std::floor(rect.x));
		auto top = static_cast<int>(std::floor(rect.y));
		return {left, top, static_cast<int>(std::ceil(rect.x + rect.w)) - left,
				static_cast<int>(std::ceil(rect.y + rect.h)) - top};
	}

	// A lamp's sprite in the frame's texels, how far its glow reaches, and the quad the glow is drawn in, which is
	// far enough out for the glow to have faded
	struct GlowQuad
	{
		SDL_FRect Sprite;
		float Reach;
		SDL_FRect Quad;
	};

	GlowQuad QuadOf(const DefinitiveScene::Glow& glow, float scale)
	{
		GlowQuad quad{};
		quad.Sprite = {glow.Sprite.x * scale, glow.Sprite.y * scale, glow.Sprite.w * scale, glow.Sprite.h * scale};
		quad.Reach =
			std::min(GlowReachSize * std::max(glow.Sprite.w, glow.Sprite.h) + GlowReachPixels, GlowReachMax) * scale;
		auto margin = quad.Reach * 2.5f;
		quad.Quad = {quad.Sprite.x - margin, quad.Sprite.y - margin, quad.Sprite.w + 2 * margin,
					 quad.Sprite.h + 2 * margin};
		return quad;
	}

	// A direction in the table's coordinates as the camera sees it
	std::array<float, 4> ToCamera(const mat4_row_major& matrix, const std::array<float, 3>& direction)
	{
		auto x = direction[0];
		auto y = direction[1];
		auto z = direction[2];
		std::array<float, 3> v = {matrix.Row0.X * x + matrix.Row0.Y * y + matrix.Row0.Z * z,
								  matrix.Row1.X * x + matrix.Row1.Y * y + matrix.Row1.Z * z,
								  matrix.Row2.X * x + matrix.Row2.Y * y + matrix.Row2.Z * z};
		auto length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		std::array<float, 4> result{};
		for (int i = 0; i < 3; i++)
			result[i] = length > 0 ? v[i] / length : 0;
		return result;
	}
} // namespace

// Creates the three passes' shaders and the depth sampler on the renderer's GPU device; on another renderer, or if one
// of them can't be created, the compositor stays unavailable
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
	GlowShader =
		PostEffect::CreateShader(renderer, {glow_frag_spv, sizeof glow_frag_spv},
								 {glow_frag_dxil, sizeof glow_frag_dxil}, {glow_frag_msl, sizeof glow_frag_msl}, 2);
	BallShader =
		PostEffect::CreateShader(renderer, {ball_frag_spv, sizeof ball_frag_spv},
								 {ball_frag_dxil, sizeof ball_frag_dxil}, {ball_frag_msl, sizeof ball_frag_msl}, 2);
	if (!ComposeShader || !GlowShader || !BallShader || !DepthSampler)
	{
		printf("Could not create Space Cadet 2026's compositor; it draws the table as Classic does.\nSDL Error: %s\n",
			   SDL_GetError());
		SDL_ClearError();
		Uninit();
		Renderer = renderer;
	}
#endif
}

// Destroys the render states, the kept textures, the shaders and the sampler, and forgets the renderer
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
	if (auto device = Renderer && PostEffect::IsGpuRenderer(Renderer) ? SDL_GetGPURendererDevice(Renderer) : nullptr)
	{
		if (ComposeShader)
			SDL_ReleaseGPUShader(device, ComposeShader);
		if (GlowShader)
			SDL_ReleaseGPUShader(device, GlowShader);
		if (BallShader)
			SDL_ReleaseGPUShader(device, BallShader);
		if (DepthSampler)
			SDL_ReleaseGPUSampler(device, DepthSampler);
	}
#endif
	ComposeShader = nullptr;
	GlowShader = nullptr;
	BallShader = nullptr;
	DepthSampler = nullptr;
	Renderer = nullptr;
}

// Whether Init made everything the passes need
bool Compositor::Available()
{
	return ComposeShader && GlowShader && BallShader && DepthSampler;
}

// Marks the tiles whose inputs changed stale, draws the glows and then the lit table into them, and the balls on top;
// returns the kept texture, or null when the compositor can't run
SDL_Texture* Compositor::Compose(SDL_Texture* frame, SDL_Texture* depth, int frameScale, const DefinitiveScene& scene,
								 const std::vector<SDL_Rect>& frameChanges, const std::vector<SDL_Rect>& depthChanges)
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (!Available() || !frame || !depth || !PrepareTarget(frame) || !PrepareStates(depth))
		return nullptr;

	float width;
	float height;
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
						  scene.GlowPulse,
						  0.0f,
						  {},
						  {}};
	uniforms.Up = ToCamera(camera.Matrix, {0, 0, 1});
	uniforms.Light = ToCamera(camera.Matrix, LightDirection);
	SDL_SetGPURenderStateFragmentUniforms(ComposeState, 0, &uniforms, sizeof uniforms);
	SDL_SetGPURenderStateFragmentUniforms(GlowState, 0, &uniforms, sizeof uniforms);
	SDL_SetGPURenderStateFragmentUniforms(BallState, 0, &uniforms, sizeof uniforms);

	// What has to be drawn again. Everything, when the textures are new or the lighting changed
	auto texelsAcross = static_cast<int>(width);
	auto texelsDown = static_cast<int>(height);
	if (!StaleTiles.SizeIs(texelsAcross, texelsDown))
	{
		StaleTiles.Resize(texelsAcross, texelsDown);
		StaleGlowTiles.Resize(texelsAcross, texelsDown);
		AllStale = true;
	}
	auto pulsed = uniforms.GlowPulse != DrawnUniforms.GlowPulse;
	DrawnUniforms.GlowPulse = uniforms.GlowPulse;
	if (uniforms != DrawnUniforms)
		AllStale = true;
	DrawnUniforms = uniforms;

	auto scale = static_cast<float>(frameScale);
	if (AllStale)
	{
		StaleGlowTiles.AddAll();
		StaleTiles.AddAll();
	}
	else
	{
		for (const auto& change : frameChanges)
			StaleTiles.Add(change);
		// The lighting takes a texel's slope from its neighbours, one table pixel to each side
		auto reach = std::max(frameScale, 1);
		for (const auto& change : depthChanges)
			StaleTiles.Add(SDL_Rect{change.x - reach, change.y - reach, change.w + 2 * reach, change.h + 2 * reach});

		// A lamp's glow takes its colour from the lamp's texels and is hidden by what stands in front of it, so it is
		// drawn again when those changed, and when the lamp is new, gone or lit differently
		for (const auto& glow : DrawnGlows)
		{
			if (std::none_of(scene.Glows.begin(), scene.Glows.end(),
							 [&glow](const DefinitiveScene::Glow& other) { return SameGlow(glow, other); }))
				StaleGlowTiles.Add(Texels(QuadOf(glow, scale).Quad));
		}
		for (const auto& glow : scene.Glows)
		{
			auto quad = QuadOf(glow, scale);
			auto texels = Texels(quad.Quad);
			if (std::none_of(DrawnGlows.begin(), DrawnGlows.end(),
							 [&glow](const DefinitiveScene::Glow& other) { return SameGlow(glow, other); }) ||
				Touches(frameChanges, Texels(quad.Sprite)) || Touches(depthChanges, texels))
				StaleGlowTiles.Add(texels);
			// The pulse changes the table wherever a glow falls, but not the glow texture
			if (pulsed)
				StaleTiles.Add(texels);
		}
		StaleTiles.Add(StaleGlowTiles);
		// The balls are drawn over the composed table, so the table under the last frame's balls is composed again
		for (const auto& ball : DrawnBalls)
			StaleTiles.Add(ball);
	}

	// Every pass draws in the frame's own texels, so a pass's pixel is the frame's and the depth map's texel. The
	// glows go first, into their own texture, which the lighting pass adds to the table.
	Uint8 red;
	Uint8 green;
	Uint8 blue;
	Uint8 alpha;
	SDL_BlendMode blendMode;
	SDL_GetRenderDrawColor(Renderer, &red, &green, &blue, &alpha);
	SDL_GetRenderDrawBlendMode(Renderer, &blendMode);
	// The window, or the picture Space Cadet 2026 turns for a monitor on its side
	auto output = SDL_GetRenderTarget(Renderer);
	if (StaleGlowTiles.Any())
	{
		SDL_SetRenderTarget(Renderer, GlowTarget);
		SDL_SetRenderDrawColor(Renderer, 0, 0, 0, 0);
		SDL_SetRenderDrawBlendMode(Renderer, SDL_BLENDMODE_NONE);
		DrawGlows(frame, frameScale, scene);
		SDL_SetRenderClipRect(Renderer, nullptr);
		SDL_SetRenderDrawColor(Renderer, red, green, blue, alpha);
		SDL_SetRenderDrawBlendMode(Renderer, blendMode);
	}
	auto balls = scene.ShadedBalls && !scene.Balls.empty();
	if (StaleTiles.Any() || balls)
	{
		SDL_SetRenderTarget(Renderer, Target);
		SDL_SetGPURenderState(Renderer, ComposeState);
		StaleTiles.ForEachRun(
			[frame](const SDL_Rect& run)
			{
				SDL_FRect part;
				SDL_RectToFRect(&run, &part);
				SDL_RenderTexture(Renderer, frame, &part, &part);
			});
		DrawBalls(frame, frameScale, scene);
		SDL_SetGPURenderState(Renderer, nullptr);
	}
	if (!balls)
		DrawnBalls.clear();
	SDL_SetRenderTarget(Renderer, output);

	DrawnGlows = scene.Glows;
	StaleTiles.Clear();
	StaleGlowTiles.Clear();
	AllStale = false;
	return Target;
#else
	(void)frame;
	(void)depth;
	(void)frameScale;
	(void)scene;
	(void)frameChanges;
	(void)depthChanges;
	return nullptr;
#endif
}

// Makes the table's and the glow's target textures the size of the frame, new ones when the size changed, and gives
// the table's the frame's scale mode; false if a texture couldn't be created
bool Compositor::PrepareTarget(SDL_Texture* frame)
{
	// The same size and scale mode as the frame, so it is drawn exactly as the frame would be
	float width;
	float height;
	float targetWidth = 0;
	float targetHeight = 0;
	SDL_GetTextureSize(frame, &width, &height);
	if (Target)
		SDL_GetTextureSize(Target, &targetWidth, &targetHeight);
	if (!Target || !GlowTarget || targetWidth != width || targetHeight != height)
	{
		// The states bind the glow texture, and a new one can have the old one's address
		DestroyStates();
		AllStale = true;
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

	if (SDL_ScaleMode scaleMode; SDL_GetTextureScaleMode(frame, &scaleMode))
		SDL_SetTextureScaleMode(Target, scaleMode);
	return true;
}

// Makes the passes' render states, which bind the depth texture and the glow target, unless the states in use bind
// these already; false if one couldn't be created
bool Compositor::PrepareStates(SDL_Texture* depth)
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (depth == BoundDepth && GlowTarget == BoundGlow && ComposeState)
		return true;

	DestroyStates();
	// The depth map, then the glow, which only the lighting pass reads
	std::array<SDL_GPUTextureSamplerBinding, 2> bindings{};
	std::array<SDL_Texture*, 2> textures = {depth, GlowTarget};
	for (auto i = 0u; i < bindings.size(); i++)
	{
		bindings[i].texture = static_cast<SDL_GPUTexture*>(SDL_GetPointerProperty(
			SDL_GetTextureProperties(textures[i]), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
		bindings[i].sampler = DepthSampler;
		if (!bindings[i].texture)
			return false;
	}

	SDL_GPURenderStateCreateInfo stateInfo{};
	stateInfo.sampler_bindings = bindings.data();
	stateInfo.num_sampler_bindings = 2;
	stateInfo.fragment_shader = ComposeShader;
	ComposeState = SDL_CreateGPURenderState(Renderer, &stateInfo);
	stateInfo.num_sampler_bindings = 1;
	stateInfo.fragment_shader = GlowShader;
	GlowState = SDL_CreateGPURenderState(Renderer, &stateInfo);
	stateInfo.fragment_shader = BallShader;
	BallState = SDL_CreateGPURenderState(Renderer, &stateInfo);
	if (!ComposeState || !GlowState || !BallState)
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

// Destroys the passes' render states and marks everything kept stale
void Compositor::DestroyStates()
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (ComposeState)
		SDL_DestroyGPURenderState(ComposeState);
	if (GlowState)
		SDL_DestroyGPURenderState(GlowState);
	if (BallState)
		SDL_DestroyGPURenderState(BallState);
#endif
	ComposeState = nullptr;
	GlowState = nullptr;
	BallState = nullptr;
	BoundDepth = nullptr;
	BoundGlow = nullptr;
	// A new depth texture, or new targets: nothing kept counts any more
	AllStale = true;
}

// Empties each stale run of tiles in the glow texture and draws the lamps' glow quads into it, cut to the run
void Compositor::DrawGlows(SDL_Texture* frame, int frameScale, const DefinitiveScene& scene)
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	// One quad per lamp, added up in the glow texture; each vertex carries the lamp's sprite and glow (glow.frag.hlsl)
	float width;
	float height;
	SDL_GetTextureSize(frame, &width, &height);
	auto scale = static_cast<float>(frameScale);
	std::vector<SDL_Vertex> vertices;
	std::vector<int> indices;
	vertices.reserve(scene.Glows.size() * 4);
	indices.reserve(scene.Glows.size() * 6);
	for (const auto& glow : scene.Glows)
	{
		auto quad = QuadOf(glow, scale);
		const auto& sprite = quad.Sprite;
		SDL_FColor lamp{sprite.x / width, sprite.y / height, sprite.w / width, sprite.h / height};
		auto first = static_cast<int>(vertices.size());
		for (auto corner = 0u; corner < 4; corner++)
		{
			auto x = corner & 1u ? quad.Quad.x + quad.Quad.w : quad.Quad.x;
			auto y = corner & 2u ? quad.Quad.y + quad.Quad.h : quad.Quad.y;
			vertices.push_back({{x, y}, lamp, {glow.Strength, quad.Reach / width}});
		}
		for (auto index : {0, 1, 2, 1, 3, 2})
			indices.push_back(first + index);
	}

	// The brightest glow in colour and the sum of the glows' peaks in alpha, for LimitGlow in compose.frag.hlsl
	static const auto blend =
		SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MAXIMUM,
								   SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
	SDL_SetTextureBlendMode(frame, blend);
	// Each stale part is emptied and gets every glow that reaches into it, cut to it
	StaleGlowTiles.ForEachRun(
		[&](const SDL_Rect& run)
		{
			SDL_SetRenderClipRect(Renderer, &run);
			SDL_RenderFillRect(Renderer, nullptr);
			if (vertices.empty())
				return;
			SDL_SetGPURenderState(Renderer, GlowState);
			SDL_RenderGeometry(Renderer, frame, vertices.data(), static_cast<int>(vertices.size()), indices.data(),
							   static_cast<int>(indices.size()));
			SDL_SetGPURenderState(Renderer, nullptr);
		});
	SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE);
#else
	(void)frame;
	(void)frameScale;
	(void)scene;
#endif
}

// Draws each shaded ball as a quad over the composed table and remembers the texels it covers, so the next frame
// composes the table under it again
void Compositor::DrawBalls(SDL_Texture* frame, int frameScale, const DefinitiveScene& scene)
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (!scene.ShadedBalls || scene.Balls.empty())
		return;

	// One quad per ball, blended over the table; each vertex carries the ball's depth and radius (ball.frag.hlsl)
	float width;
	float height;
	SDL_GetTextureSize(frame, &width, &height);
	auto scale = static_cast<float>(frameScale);
	std::vector<SDL_Vertex> vertices;
	std::vector<int> indices;
	DrawnBalls.clear();
	for (const auto& ball : scene.Balls)
	{
		auto radius = ball.Radius * scale;
		auto reach = radius * BallQuadExtent;
		SDL_FPoint center{ball.Center.x * scale, ball.Center.y * scale};
		DrawnBalls.push_back(Texels(SDL_FRect{center.x - reach, center.y - reach, 2 * reach, 2 * reach}));
		SDL_FColor data{static_cast<float>(ball.Depth) / 0xFFFF, radius / width, 0, 1};
		auto first = static_cast<int>(vertices.size());
		for (auto corner = 0u; corner < 4; corner++)
		{
			auto u = corner & 1u ? BallQuadExtent : -BallQuadExtent;
			auto v = corner & 2u ? BallQuadExtent : -BallQuadExtent;
			vertices.push_back(
				{{center.x + (u > 0 ? reach : -reach), center.y + (v > 0 ? reach : -reach)}, data, {u, v}});
		}
		for (auto index : {0, 1, 2, 1, 3, 2})
			indices.push_back(first + index);
	}

	SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_BLEND);
	SDL_SetGPURenderState(Renderer, BallState);
	SDL_RenderGeometry(Renderer, frame, vertices.data(), static_cast<int>(vertices.size()), indices.data(),
					   static_cast<int>(indices.size()));
	SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE);
#else
	(void)frame;
	(void)frameScale;
	(void)scene;
#endif
}
