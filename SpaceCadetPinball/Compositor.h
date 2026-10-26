#pragma once

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
	static SDL_Texture* Compose(SDL_Texture* frame, SDL_Texture* depth, int frameScale);

private:
	static SDL_Renderer* Renderer;
	static SDL_Texture* Target;
	static struct SDL_GPUSampler* DepthSampler;
	static struct SDL_GPUShader* ComposeShader;
	static struct SDL_GPURenderState* ComposeState;
	// The depth texture the render state binds; a new depth texture needs a new state
	static SDL_Texture* BoundDepth;

	static bool PrepareTarget(SDL_Texture* frame);
	static bool PrepareStates(SDL_Texture* depth);
	static void DestroyStates();
};
