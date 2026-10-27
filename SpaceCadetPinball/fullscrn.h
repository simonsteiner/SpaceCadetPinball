#pragma once

struct resolution_info
{
	int16_t ScreenWidth;
	int16_t ScreenHeight;
	int16_t TableWidth;
	int16_t TableHeight;
	int16_t ResolutionMenuId;
};

class fullscrn
{
public:
	static int screen_mode;
	static int display_changed;
	static const resolution_info resolution_array[3];
	static float ScaleX;
	static float ScaleY;
	static int OffsetX;
	static int OffsetY;
	// Whether Space Cadet 2026 lays the table out for a window taller than wide, with the HUD in the band under it
	static bool Portrait;

	static void init();
	static void shutdown();
	static int set_screen_mode(int isFullscreen);
	static void activate(int flag);
	static int GetResolution();
	static void SetResolution(int value);
	static int GetMaxResolution();
	static void window_size_changed();
	static void ResetWindowSize(bool center);
	static float PixelDensity();
	static float ContentScale();
	static SDL_Rect GetScreenRectFromPinballRect(SDL_Rect rect);
	static float GetScreenToPinballRatio();

	// Space Cadet 2026 turns the whole picture a quarter turn at a time for a monitor on its side, as in a pinball
	// cabinet; only the drawing turns, not the table or the input. Classic is never turned.
	// Quarter turns clockwise, 0 to 3
	static int Rotation();
	// The size of the picture in pixels: the renderer's output, with width and height swapped for a quarter turn
	static void OutputSize(int& width, int& height);
	// A point from 0 to 1 across the window, where it lies in the picture
	static SDL_FPoint ToPicture(SDL_FPoint point);
	// A mouse event's position, from the window's coordinates into the picture's, for ImGui
	static void TurnPointerEvent(SDL_Event& event);
	// Around drawing a frame: while the picture is turned it is drawn into a texture, which EndFrame turns into the
	// window
	static void BeginFrame();
	static void EndFrame();
	// Before the renderer goes away
	static void ReleaseFrame();

private :
	static int resolution;
	static SDL_Texture* TurnedFrame;

	static void FitScale(int width, int height, int contentWidth, int contentHeight, float& scaleX, float& scaleY);
	static int enableFullscreen();
	static int disableFullscreen();
};
