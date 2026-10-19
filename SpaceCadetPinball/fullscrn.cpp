#include "pch.h"
#include "fullscrn.h"


#include "options.h"
#include "pb.h"
#include "render.h"
#include "winmain.h"


int fullscrn::screen_mode;
int fullscrn::display_changed;

int fullscrn::resolution = 0;
const resolution_info fullscrn::resolution_array[3] =
{
	{640, 480, 600, 416, 501},
	{800, 600, 752, 520, 502},
	{1024, 768, 960, 666, 503},
};
float fullscrn::ScaleX = 1;
float fullscrn::ScaleY = 1;
int fullscrn::OffsetX = 0;
int fullscrn::OffsetY = 0;

void fullscrn::init()
{
	window_size_changed();
}

void fullscrn::shutdown()
{
	if (display_changed)
		set_screen_mode(0);
}

int fullscrn::set_screen_mode(int isFullscreen)
{
	int result = isFullscreen;
	if (isFullscreen == screen_mode)
		return result;
	screen_mode = isFullscreen;
	if (isFullscreen)
	{
		enableFullscreen();
		result = 1;
	}
	else
	{
		disableFullscreen();
		result = 1;
	}
	return result;
}

int fullscrn::enableFullscreen()
{
	if (!display_changed)
	{
		if (SDL_SetWindowFullscreen(winmain::MainWindow, true))
		{
			display_changed = 1;
			return 1;
		}
	}
	return 0;
}

int fullscrn::disableFullscreen()
{
	if (display_changed)
	{
		if (SDL_SetWindowFullscreen(winmain::MainWindow, false))
			display_changed = 0;
	}

	return 0;
}

void fullscrn::activate(int flag)
{
	if (screen_mode)
	{
		if (!flag)
		{
			set_screen_mode(0);
		}
	}
}

int fullscrn::GetResolution()
{
	return resolution;
}

void fullscrn::SetResolution(int value)
{
	if (!pb::FullTiltMode || pb::FullTiltDemoMode)
		value = 0;
	assertm(value >= 0 && value <= 2, "Resolution value out of bounds");
	resolution = value;
}

int fullscrn::GetMaxResolution()
{
	return pb::FullTiltMode && !pb::FullTiltDemoMode ? 2 : 0;
}

void fullscrn::window_size_changed()
{
	// The renderer works in pixels, ImGui and the menu height in window coordinates
	int width, height;
	SDL_GetRenderOutputSize(winmain::Renderer, &width, &height);
	int menuHeight =
		options::Options.ShowMenu ? static_cast<int>(std::ceil(winmain::MainMenuHeight * PixelDensity())) : 0;
	height -= menuHeight;
	auto res = &resolution_array[resolution];
	ScaleX = static_cast<float>(width) / res->TableWidth;
	ScaleY = static_cast<float>(height) / res->TableHeight;
	OffsetX = OffsetY = 0;
	auto offset2X = 0, offset2Y = 0;

	if (options::Options.IntegerScaling)
	{
		ScaleX = ScaleX < 1 ? ScaleX : std::floor(ScaleX);
		ScaleY = ScaleY < 1 ? ScaleY : std::floor(ScaleY);
	}

	if (options::Options.UniformScaling)
	{
		ScaleY = ScaleX = std::min(ScaleX, ScaleY);
	}

	offset2X = static_cast<int>(floor(width - res->TableWidth * ScaleX));
	offset2Y = static_cast<int>(floor(height - res->TableHeight * ScaleY));
	OffsetX = offset2X / 2;
	OffsetY = offset2Y / 2;

	render::DestinationRect = SDL_Rect
	{
		OffsetX, OffsetY + menuHeight,
		width - offset2X, height - offset2Y
	};
}

void fullscrn::ResetWindowSize()
{
	// The largest whole multiple of the table, up to the display's scale, that fits on the display.
	// A window that size shows the table at about the size the original had at 96 DPI, without blurring it.
	auto res = &resolution_array[resolution];
	auto density = PixelDensity();
	auto menuHeight = options::Options.ShowMenu ? winmain::ExpectedMenuHeight() : 0.0f;

	SDL_Rect usable{};
	auto display = SDL_GetDisplayForWindow(winmain::MainWindow);
	if (!display || !SDL_GetDisplayUsableBounds(display, &usable))
	{
		SDL_ClearError();
		usable = {0, 0, res->TableWidth, res->TableHeight};
	}

	auto factor = std::max(1, static_cast<int>(std::lround(SDL_GetWindowDisplayScale(winmain::MainWindow))));
	for (; factor > 1; factor--)
	{
		auto width = res->TableWidth * factor / density;
		auto height = res->TableHeight * factor / density + menuHeight;
		if (width <= usable.w && height <= usable.h)
			break;
	}

	SDL_SetWindowSize(winmain::MainWindow, static_cast<int>(std::ceil(res->TableWidth * factor / density)),
					  static_cast<int>(std::ceil(res->TableHeight * factor / density + menuHeight)));
}

float fullscrn::PixelDensity()
{
	// Pixels per window coordinate: 2 on a Retina display, 1 where windows are sized in pixels
	auto density = SDL_GetWindowPixelDensity(winmain::MainWindow);
	return density > 0 ? density : 1.0f;
}

float fullscrn::ContentScale()
{
	// How much larger than at 96 DPI the user wants things, in window coordinates.
	// On macOS the pixel density already covers it; on Windows and X11 it is the display's scale setting.
	auto scale = SDL_GetWindowDisplayScale(winmain::MainWindow) / PixelDensity();
	return scale > 0 ? scale : 1.0f;
}

SDL_Rect fullscrn::GetScreenRectFromPinballRect(SDL_Rect rect)
{
	// In window coordinates, as ImGui uses them
	auto density = PixelDensity();
	auto& dst = render::DestinationRect;
	SDL_Rect converted_rect;

	converted_rect.x = static_cast<int>((rect.x * dst.w / render::vscreen->Width + dst.x) / density);
	converted_rect.y = static_cast<int>((rect.y * dst.h / render::vscreen->Height + dst.y) / density);

	converted_rect.w = static_cast<int>(rect.w * dst.w / render::vscreen->Width / density);
	converted_rect.h = static_cast<int>(rect.h * dst.h / render::vscreen->Height / density);

	return converted_rect;
}

float fullscrn::GetScreenToPinballRatio()
{
	// Window coordinates per table pixel
	return static_cast<float>(render::DestinationRect.w) / render::vscreen->Width / PixelDensity();
}