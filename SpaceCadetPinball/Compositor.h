#pragma once

struct DefinitiveScene;

// Space Cadet 2026's drawing of the table on SDL's GPU renderer (SDL 3.4 and newer). The software frame and its depth
// map go up as textures, and passes on the GPU compose them into a texture the size of the frame, which the post
// effect then draws as it would draw the frame. Classic draws the frame itself, and so does Space Cadet 2026 where
// the compositor isn't available (another renderer, such as in a browser, or shaders it couldn't create).
class Compositor
{
public:
	// Creates the passes' shaders for the renderer, once per renderer, as PostEffect::Init does
	static void Init(SDL_Renderer* renderer);
	// Releases them; call before the renderer is destroyed
	static void Uninit();
	static bool Available();
	// Composes the frame with its depth map, both frameScale texels per table pixel; the depth map holds each pixel's
	// depth, 0 to 0xFFFF, in red (the high byte) and green. Returns the composed texture, or null if it couldn't
	// compose, in which case the frame should be drawn as it is.
	static SDL_Texture* Compose(SDL_Texture* frame, SDL_Texture* depth, int frameScale, const DefinitiveScene& scene);
	// Lets go of the depth texture Compose was given; call before destroying it, since a new texture can have the same
	// address
	static void ForgetDepth();

private:
	static SDL_Renderer* Renderer;
	static SDL_Texture* Target;
	// The lamps' glow, added up before the lighting pass adds it to the table with a limit
	static SDL_Texture* GlowTarget;
	static struct SDL_GPUSampler* DepthSampler;
	static struct SDL_GPUShader *ComposeShader, *GlowShader;
	static struct SDL_GPURenderState *ComposeState, *GlowState;
	// The textures the render states bind; new textures need new states (ForgetDepth, PrepareTarget)
	static SDL_Texture *BoundDepth, *BoundGlow;

	static bool PrepareTarget(SDL_Texture* frame);
	static bool PrepareStates(SDL_Texture* depth);
	static void DestroyStates();
	static void DrawGlows(SDL_Texture* frame, int frameScale, const DefinitiveScene& scene);
};
