#include "pch.h"
#include "DefinitiveTouchDeck.h"
#include "DefinitiveTheme.h"
#include "DefinitiveUi.h"
#include "translations.h"

namespace
{
	using Theme = DefinitiveTheme;
	using Ui = DefinitiveUi;

	// A flipper at rest points down towards the drain and swings up while held, as the table's do, in radians from
	// pointing straight at the other pad
	constexpr float RestAngle = 0.5f, HeldAngle = -0.38f;
	// Radians per second it swings, so a tap still shows
	constexpr float SwingSpeed = 14;
	// The knob reaches the bottom of the lane after this long; the table's plunger pulls back at its own pace
	constexpr float PullSeconds = 1.0f;
	constexpr int MeterLamps = 6;

	float LeftAngle = RestAngle, RightAngle = RestAngle;

	ImVec2 Min(const SDL_FRect& rect)
	{
		return ImVec2{rect.x, rect.y};
	}

	ImVec2 Max(const SDL_FRect& rect)
	{
		return ImVec2{rect.x + rect.w, rect.y + rect.h};
	}

	// Moves value towards target by step at most, without passing it
	float Approach(float value, float target, float step)
	{
		return value < target ? std::min(value + step, target) : std::max(value - step, target);
	}

	// A pad in the panel's blue, lit in the lamps' orange around its rim while a finger holds it
	void DrawControl(ImDrawList* drawList, const SDL_FRect& rect, bool held)
	{
		auto min = Min(rect), max = Max(rect);
		Theme::DrawPanel(drawList, min, max);
		if (held)
		{
			drawList->AddRectFilled(min, max, Theme::WithAlpha(Theme::LedOrange, 0.12f), Theme::Rounding);
			drawList->AddRect(min, max, Theme::WithAlpha(Theme::LedOrange, 0.9f), Theme::Rounding, 2.0f);
		}
	}

	// The flipper the pad works, drawn in it: a tapered bar from a round pivot, swung to angle
	void DrawFlipper(ImDrawList* drawList, const SDL_FRect& pad, bool left, float angle, bool held)
	{
		auto side = left ? 1.0f : -1.0f;
		auto length = pad.w * 0.5f;
		auto pivotRadius = std::min(pad.h * 0.11f, length * 0.2f);
		auto tipRadius = pivotRadius * 0.45f;
		ImVec2 pivot{left ? pad.x + pad.w * 0.25f : pad.x + pad.w * 0.75f, pad.y + pad.h * 0.45f};
		ImVec2 direction{std::cos(angle) * side, std::sin(angle)};
		ImVec2 tip{pivot.x + direction.x * length, pivot.y + direction.y * length};
		ImVec2 normal{-direction.y, direction.x};
		auto color = held ? Theme::Chrome50 : Theme::Chrome200;
		if (held)
			drawList->AddCircleFilled(pivot, pivotRadius * 2.4f, Theme::WithAlpha(Theme::LedOrange, 0.18f));
		ImVec2 body[] = {
			{pivot.x + normal.x * pivotRadius, pivot.y + normal.y * pivotRadius},
			{tip.x + normal.x * tipRadius, tip.y + normal.y * tipRadius},
			{tip.x - normal.x * tipRadius, tip.y - normal.y * tipRadius},
			{pivot.x - normal.x * pivotRadius, pivot.y - normal.y * pivotRadius},
		};
		drawList->AddConvexPolyFilled(body, 4, color);
		drawList->AddCircleFilled(pivot, pivotRadius, color);
		drawList->AddCircleFilled(tip, tipRadius, color);
		// The pivot's bolt, in the lamps' colour while held
		drawList->AddCircleFilled(pivot, pivotRadius * 0.35f, held ? Theme::LedOrange : Theme::Space700);
	}

	// The plunger lane: a rod with a spring under its knob, which sinks while held, and a column of lamps beside it
	// that fills as the pull goes on
	void DrawPlunger(ImDrawList* drawList, const SDL_FRect& lane, bool held, float seconds)
	{
		auto pull = held ? std::clamp(seconds / PullSeconds, 0.0f, 1.0f) : 0.0f;
		auto caption = translations::GetTranslation(Msg::Definitive_TouchLaunch);
		auto textSize = Ui::TextSize(Theme::TextFont(), Theme::LabelSize, caption);
		auto top = lane.y + lane.h * 0.08f + textSize.y;
		auto bottom = lane.y + lane.h * 0.92f;
		auto centreX = lane.x + lane.w * 0.42f;
		auto knobRadius = std::min(lane.w * 0.22f, lane.h * 0.08f);
		auto travel = (bottom - top) * 0.5f;
		auto knobY = top + knobRadius + travel * pull;

		auto rod = Theme::WithAlpha(Theme::Chrome400, 0.8f);
		drawList->AddLine(ImVec2{centreX, top}, ImVec2{centreX, knobY}, rod, std::max(2.0f, knobRadius * 0.25f));
		// The spring, closer coiled the further it is pulled
		constexpr int Coils = 7;
		auto coilWidth = knobRadius * 0.9f;
		auto springTop = knobY + knobRadius;
		auto step = (bottom - springTop) / (Coils * 2);
		for (auto i = 0; i < Coils * 2; i++)
		{
			auto x0 = centreX + (i % 2 ? coilWidth : -coilWidth);
			auto x1 = centreX + (i % 2 ? -coilWidth : coilWidth);
			drawList->AddLine(ImVec2{x0, springTop + step * static_cast<float>(i)},
							  ImVec2{x1, springTop + step * static_cast<float>(i + 1)}, rod, 2);
		}
		drawList->AddCircleFilled(ImVec2{centreX, knobY}, knobRadius, held ? Theme::LedOrange : Theme::Chrome200);
		drawList->AddCircle(ImVec2{centreX, knobY}, knobRadius, Theme::WithAlpha(Theme::Space950, 0.8f), 0, 2.0f);

		// The lamps, bottom up, lit as far as the pull has come
		auto lampX = lane.x + lane.w * 0.82f;
		auto lampRadius = std::min(lane.w * 0.07f, (bottom - top) / (MeterLamps * 3.0f));
		for (auto i = 0; i < MeterLamps; i++)
		{
			auto y = bottom - lampRadius - (bottom - top - lampRadius * 2) * static_cast<float>(i) / (MeterLamps - 1);
			auto lit = held && pull * MeterLamps > static_cast<float>(i);
			Theme::DrawLamp(drawList, ImVec2{lampX, y}, lampRadius, lit ? Theme::Lamp::Lit : Theme::Lamp::Dark);
		}

		Ui::DrawText(drawList, Theme::TextFont(), Theme::LabelSize,
					 ImVec2{lane.x + (lane.w - textSize.x) / 2, lane.y + lane.h * 0.05f},
					 held ? Theme::LedOrange : Theme::Chrome400, caption);
	}

	// Two bars, as a pause button has everywhere
	void DrawPause(ImDrawList* drawList, const SDL_FRect& rect)
	{
		auto inset = rect.w * 0.12f;
		SDL_FRect chip{rect.x + inset, rect.y + inset, rect.w - inset * 2, rect.h - inset * 2};
		Theme::DrawPanel(drawList, Min(chip), Max(chip));
		auto barW = chip.w * 0.14f, barH = chip.h * 0.42f;
		auto y = chip.y + (chip.h - barH) / 2;
		for (auto x : {chip.x + chip.w * 0.5f - barW * 1.5f, chip.x + chip.w * 0.5f + barW * 0.5f})
			drawList->AddRectFilled(ImVec2{x, y}, ImVec2{x + barW, y + barH}, Theme::Chrome200, barW * 0.3f);
	}
} // namespace

// Swings the pads' flippers, then draws the pads, the lane cut to what shows, the pause button and the hint
void DefinitiveTouchDeck::Render(const DefinitiveDeck& deck)
{
	auto drawList = ImGui::GetBackgroundDrawList();
	auto step = ImGui::GetIO().DeltaTime * SwingSpeed;
	LeftAngle = Approach(LeftAngle, deck.LeftHeld ? HeldAngle : RestAngle, step);
	RightAngle = Approach(RightAngle, deck.RightHeld ? HeldAngle : RestAngle, step);

	DrawControl(drawList, deck.LeftPad, deck.LeftHeld);
	DrawFlipper(drawList, deck.LeftPad, true, LeftAngle, deck.LeftHeld);
	DrawControl(drawList, deck.RightPad, deck.RightHeld);
	DrawFlipper(drawList, deck.RightPad, false, RightAngle, deck.RightHeld);
	// The lane folds away while no ball waits for it; drawn at its full width and cut to what shows of it
	if (deck.Plunger.w >= 2)
	{
		auto full = deck.Plunger;
		full.w = std::max(full.w, deck.PlungerWidth);
		drawList->PushClipRect(Min(deck.Plunger), Max(deck.Plunger), true);
		DrawControl(drawList, full, deck.PlungerHeld);
		DrawPlunger(drawList, full, deck.PlungerHeld, deck.PlungerSeconds);
		drawList->PopClipRect();
	}
	if (deck.Pause.w > 0)
		DrawPause(drawList, deck.Pause);

	if (deck.ExplainNudge && deck.Hint.w > 0)
	{
		auto text = translations::GetTranslation(Msg::Definitive_TouchNudge);
		auto size = Ui::TextSize(Theme::TextFont(), Theme::LabelSize, text);
		Ui::DrawText(drawList, Theme::TextFont(), Theme::LabelSize,
					 ImVec2{deck.Hint.x, deck.Hint.y + (deck.Hint.h - size.y) / 2}, Theme::Chrome400, text,
					 deck.Hint.w);
	}
}
