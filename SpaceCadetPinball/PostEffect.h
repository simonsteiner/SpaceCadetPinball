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
	static SDL_Renderer* CreateGpuRenderer(SDL_Window* window);
	static bool IsGpuRenderer(SDL_Renderer* renderer);
	static void Init(SDL_Renderer* renderer, PostEffects effect);
	static void Uninit();
	static bool Available()
	{
		return Supported;
	}
	// Draws until End through the effect, if there is one; the sizes are the texture's, in texels, and its scale on
	// screen
	static void Begin(float textureWidth, float textureHeight, float scaleX, float scaleY);
	static void End();

private:
	static bool Supported;
	static SDL_Renderer* Renderer;
	static struct SDL_GPUShader* Shader;
	static struct SDL_GPURenderState* State;
};
