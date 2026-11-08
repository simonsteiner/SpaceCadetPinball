#include "pch.h"
#include "TouchControls.h"

#include "fullscrn.h"
#include "options.h"
#include "pb.h"
#include "winmain.h"

std::map<std::pair<SDL_TouchID, SDL_FingerID>, TouchControls::FingerState> TouchControls::Fingers{};
TouchControls::Layout TouchControls::Rects{};
bool TouchControls::IsActive = false, TouchControls::HasNudged = false;
Uint64 TouchControls::PlungerTicks = 0;

namespace
{
	// Shares of the safe area. Upright: the deck takes the bottom, the score panel at least a band over it. On its
	// side: the table takes the full height, the left pad the margin before it, and the score panel the top of the
	// margin after it, over the right pad and the plunger.
	constexpr float DeckShare = 0.24f, BandShare = 0.12f;
	constexpr float LeftShare = 0.38f, MinMargins = 0.3f, SidePanelShare = 0.42f;
	// The plunger lane's width and the gaps, as shares of the deck's width (upright) or the column's (on its side)
	constexpr float PlungerShare = 0.17f, GapShare = 0.025f;
	// The strip at the top of the deck for the pause button and the nudge hint, as a share of the deck's height
	constexpr float HintShare = 0.2f;
	// A flick: up by this share of the pad's height within this time
	constexpr float FlickShare = 0.25f;
	constexpr Uint64 FlickMs = 300;

	SDL_Rect Rect(float x, float y, float w, float h)
	{
		return SDL_Rect{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)),
						static_cast<int>(std::lround(w)), static_cast<int>(std::lround(h))};
	}

	// The window's safe area in pixels, clear of the camera cutout and the gesture bar; the whole window without one
	SDL_FRect SafeArea(int width, int height)
	{
		SDL_Rect safe{};
		if (!SDL_GetWindowSafeArea(winmain::MainWindow, &safe) || safe.w <= 0 || safe.h <= 0)
		{
			SDL_ClearError();
			return SDL_FRect{0, 0, static_cast<float>(width), static_cast<float>(height)};
		}
		auto density = fullscrn::PixelDensity();
		return SDL_FRect{safe.x * density, safe.y * density, safe.w * density, safe.h * density};
	}

	GameBindings BumpFor(GameBindings flipper)
	{
		return flipper == GameBindings::LeftFlipper ? GameBindings::LeftTableBump : GameBindings::RightTableBump;
	}

	// Escape, as the keyboard and Android's back gesture send it: the pause screen in a game
	void SendEscape()
	{
		for (auto type : {SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP})
		{
			SDL_Event event{};
			event.type = type;
			event.key.windowID = SDL_GetWindowID(winmain::MainWindow);
			event.key.scancode = SDL_SCANCODE_ESCAPE;
			event.key.key = SDLK_ESCAPE;
			event.key.down = type == SDL_EVENT_KEY_DOWN;
			SDL_PushEvent(&event);
		}
	}
} // namespace

bool TouchControls::Arrange(int width, int height, int boardWidth, int tableHeight, float& scale)
{
	IsActive = false;
	if (options::GetEdition() != Editions::Definitive || options::Options.TouchControls != 1 ||
		!options::Options.UniformScaling || fullscrn::Rotation() != 0 || boardWidth <= 0 || tableHeight <= 0)
		return false;

	auto safe = SafeArea(width, height);
	auto boardW = static_cast<float>(boardWidth), tableH = static_cast<float>(tableHeight);
	Layout layout;
	if (safe.h > safe.w)
	{
		// Upright: the table as wide as it fits over the band and the deck
		auto deckH = safe.h * DeckShare;
		scale = std::min(safe.w / boardW, (safe.h - deckH - safe.h * BandShare) / tableH);
		auto w = boardW * scale, h = tableH * scale;
		layout.Board = Rect(safe.x + (safe.w - w) / 2, safe.y, w, h);
		auto deckY = safe.y + safe.h - deckH;
		layout.Panel = Rect(safe.x, safe.y + h, safe.w, deckY - safe.y - h);

		auto gap = safe.w * GapShare;
		auto hintH = deckH * HintShare;
		auto padsY = deckY + hintH, padsH = deckH - hintH - gap;
		auto plungerW = safe.w * PlungerShare;
		auto padW = (safe.w - plungerW - gap * 4) / 2;
		layout.LeftPad = Rect(safe.x + gap, padsY, padW, padsH);
		layout.RightPad = Rect(safe.x + gap * 2 + padW, padsY, padW, padsH);
		layout.Plunger = Rect(safe.x + safe.w - gap - plungerW, padsY, plungerW, padsH);
		layout.Pause = Rect(safe.x + safe.w - gap - hintH, deckY, hintH, hintH);
		layout.Hint = Rect(safe.x + gap, deckY, safe.w - gap * 3 - hintH, hintH);
	}
	else
	{
		// On its side: the table as tall as it fits with room for the pads beside it
		scale = std::min(safe.h / tableH, safe.w * (1 - MinMargins) / boardW);
		auto w = boardW * scale, h = tableH * scale;
		auto margins = safe.w - w;
		if (margins < safe.w * MinMargins * 0.99f)
			return false;
		auto leftW = margins * LeftShare, rightW = margins - leftW;
		layout.Board = Rect(safe.x + leftW, safe.y + (safe.h - h) / 2, w, h);

		// Left: the pause button and the hint over the left pad
		auto gap = leftW * GapShare * 2;
		auto padsH = safe.h * (1 - SidePanelShare) - gap;
		auto padsY = safe.y + safe.h - gap - padsH;
		auto hintH = std::min(leftW * 0.25f, padsY - safe.y - gap);
		layout.Pause = Rect(safe.x + gap, safe.y + gap, hintH, hintH);
		layout.Hint = Rect(safe.x + gap, padsY - hintH - gap, leftW - gap * 2, hintH);
		layout.LeftPad = Rect(safe.x + gap, padsY, leftW - gap * 2, padsH);

		// Right: the score panel over the right pad and the plunger, which keeps to the outer edge
		auto rightX = safe.x + leftW + w;
		layout.Panel = Rect(rightX, safe.y, rightW, padsY - safe.y - gap);
		auto plungerW = rightW * PlungerShare * 1.4f;
		layout.RightPad = Rect(rightX + gap, padsY, rightW - plungerW - gap * 3, padsH);
		layout.Plunger = Rect(rightX + rightW - gap - plungerW, padsY, plungerW, padsH);
	}
	Rects = layout;
	IsActive = true;
	return true;
}

bool TouchControls::Active()
{
	return IsActive;
}

const TouchControls::Layout& TouchControls::Current()
{
	return Rects;
}

void TouchControls::Finger(const SDL_TouchFingerEvent& event)
{
	// Finger IDs are unique only per touch device
	auto key = std::pair{event.touchID, event.fingerID};
	int width, height;
	fullscrn::OutputSize(width, height);
	SDL_FPoint position{event.x * static_cast<float>(width), event.y * static_cast<float>(height)};

	auto held = [](GameBindings binding)
	{
		return std::any_of(Fingers.begin(), Fingers.end(),
						   [binding](const auto& finger) { return finger.second.Binding == binding; });
	};

	if (event.type == SDL_EVENT_FINGER_MOTION)
	{
		// A quick flick up on a pad nudges the table from that side, held until the finger lifts, as a key would be
		auto finger = Fingers.find(key);
		if (finger == Fingers.end() || finger->second.Nudged)
			return;
		auto& state = finger->second;
		if (state.Binding != GameBindings::LeftFlipper && state.Binding != GameBindings::RightFlipper)
			return;
		auto& pad = state.Binding == GameBindings::LeftFlipper ? Rects.LeftPad : Rects.RightPad;
		if (state.StartY - position.y >= pad.h * FlickShare && SDL_GetTicks() - state.StartTicks <= FlickMs)
		{
			state.Nudged = true;
			HasNudged = true;
			pb::BindingDown(BumpFor(state.Binding));
		}
		return;
	}

	if (event.type != SDL_EVENT_FINGER_DOWN)
	{
		auto finger = Fingers.find(key);
		if (finger == Fingers.end())
			return;
		auto state = finger->second;
		Fingers.erase(finger);
		if (state.Nudged)
			pb::BindingUp(BumpFor(state.Binding));
		if (!held(state.Binding))
		{
			pb::BindingUp(state.Binding);
			if (state.Binding == GameBindings::Plunger)
				PlungerTicks = 0;
		}
		return;
	}
	if (Fingers.count(key) || winmain::ImIO->WantCaptureMouse)
		return;

	SDL_Point point{static_cast<int>(position.x), static_cast<int>(position.y)};
	if (SDL_PointInRect(&point, &Rects.Pause))
	{
		SendEscape();
		return;
	}
	GameBindings binding;
	if (SDL_PointInRect(&point, &Rects.LeftPad))
		binding = GameBindings::LeftFlipper;
	else if (SDL_PointInRect(&point, &Rects.RightPad))
		binding = GameBindings::RightFlipper;
	else if (SDL_PointInRect(&point, &Rects.Plunger))
		binding = GameBindings::Plunger;
	else if (SDL_PointInRect(&point, &Rects.Board))
		binding = point.x < Rects.Board.x + Rects.Board.w / 2 ? GameBindings::LeftFlipper : GameBindings::RightFlipper;
	else
		return;

	auto first = !held(binding);
	Fingers[key] = FingerState{binding, position.y, SDL_GetTicks(), false};
	if (first)
	{
		if (binding == GameBindings::Plunger)
			PlungerTicks = SDL_GetTicks();
		pb::BindingDown(binding);
	}
}

bool TouchControls::Held(GameBindings binding)
{
	return std::any_of(Fingers.begin(), Fingers.end(),
					   [binding](const auto& finger) { return finger.second.Binding == binding; });
}

float TouchControls::PlungerSeconds()
{
	return PlungerTicks ? static_cast<float>(SDL_GetTicks() - PlungerTicks) / 1000.0f : 0.0f;
}

bool TouchControls::Nudged()
{
	return HasNudged;
}
