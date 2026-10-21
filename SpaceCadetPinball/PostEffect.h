#pragma once

// The optional post-processing of the table, set by Options.PostProcessing
enum class PostEffects : int
{
	None = 0,
	Crt = 1,
	SmoothEdges = 2,
	Max
};

// Draws the table through a fragment shader on SDL's GPU renderer (SDL 3.4 and newer).
// Other renderers can't take custom shaders, so an effect needs that renderer; without it, the table is drawn plain.
class PostEffect
{
public:
	// Remembers a failure until the game exits, so restarts don't try again
	static SDL_Renderer* CreateGpuRenderer(SDL_Window* window);
	static bool IsGpuRenderer(SDL_Renderer* renderer);
	// Creates every effect's shader for the renderer, once per renderer, so none is released while SDL's pipeline
	// cache, keyed on the shader's address, can still hold it
	static void Init(SDL_Renderer* renderer);
	// Releases the shaders; call before the renderer is destroyed
	static void Uninit();
	static void SetEffect(PostEffects effect);
	static bool GpuRendererFailed()
	{
		return GpuFailed;
	}
	// Draws until End through the effect, if there is one; the sizes are the texture's, in texels, and its scale on
	// screen
	static void Begin(float textureWidth, float textureHeight, float scaleX, float scaleY);
	static void End();

private:
	static bool Supported;
	static bool GpuFailed;
	static SDL_Renderer* Renderer;
	static PostEffects Effect;
	static struct SDL_GPUShader* Shaders[static_cast<int>(PostEffects::Max)];
	static struct SDL_GPURenderState* States[static_cast<int>(PostEffects::Max)];
};
