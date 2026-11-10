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
	constexpr float RestAngle = 0.5f;
	constexpr float HeldAngle = -0.38f;
	// Radians per second it swings, so a tap still shows
	constexpr float SwingSpeed = 14;
	// The lane's tip is pulled all the way after this long; the table's plunger pulls back at its own pace
	constexpr float PullSeconds = 1.0f;
	constexpr int MeterLamps = 6;

	float LeftAngle = RestAngle;
	float RightAngle = RestAngle;

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
		auto min = Min(rect);
		auto max = Max(rect);
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
		auto body = std::to_array<ImVec2>({
			{pivot.x + normal.x * pivotRadius, pivot.y + normal.y * pivotRadius},
			{tip.x + normal.x * tipRadius, tip.y + normal.y * tipRadius},
			{tip.x - normal.x * tipRadius, tip.y - normal.y * tipRadius},
			{pivot.x - normal.x * pivotRadius, pivot.y - normal.y * pivotRadius},
		});
		drawList->AddConvexPolyFilled(body.data(), static_cast<int>(body.size()), color);
		drawList->AddCircleFilled(pivot, pivotRadius, color);
		drawList->AddCircleFilled(tip, tipRadius, color);
		// The pivot's bolt, in the lamps' colour while held
		drawList->AddCircleFilled(pivot, pivotRadius * 0.35f, held ? Theme::LedOrange : Theme::Space700);
	}

	// The plunger's tip follows the pull at this many lane lengths a second on the way down, and springs back faster
	constexpr float PullSpeed = 6;
	constexpr float SnapSpeed = 14;
	// A released ball leaves the lane at this many lane lengths a second, without a pull and at the full one
	constexpr float ShotSlow = 2.5f;
	constexpr float ShotFast = 9;
	// After a shot the lane shows no ball for this long, until the table's ball has left its plunger or come back
	constexpr float ShotSeconds = 0.6f;

	// The lane's own little machine, which only draws: where the tip is, and the ball a release has sent off
	struct PlungerState
	{
		// 0 at rest, 1 pulled all the way
		float Tip = 0;
		bool WasHeld = false;
		bool HadBall = false;
		// The shot ball: where the tip was when it left, its way up the lane since in lane lengths, its speed in
		// lane lengths a second, and the seconds left of ShotSeconds
		float ShotFrom = 0;
		float ShotTravel = 0;
		float ShotSpeed = 0;
		float ShotLeft = 0;
	};
	PlungerState Lane{};
	// The ImGui frame the lane was last drawn in
	int LaneFrame = -1;

	// A steel ball: a light sphere with a shine up and to the left and a dark rim
	void DrawBall(ImDrawList* drawList, ImVec2 centre, float radius)
	{
		drawList->AddCircleFilled(centre, radius, Theme::Chrome200);
		drawList->AddCircleFilled(ImVec2{centre.x - radius * 0.28f, centre.y - radius * 0.3f}, radius * 0.42f,
								  Theme::Chrome50);
		drawList->AddCircle(centre, radius, Theme::WithAlpha(Theme::Space950, 0.8f), 0, std::max(1.5f, radius * 0.12f));
	}

	// The plunger lane: a shooter lane with the ball resting on the plunger's tip and a spring under it. A hold pulls
	// the tip down, the ball with it, and coils the spring; letting go springs the tip back and sends the ball up and
	// out of the lane. A column of lamps beside it fills as the pull goes on.
	void DrawPlunger(ImDrawList* drawList, const SDL_FRect& lane, const DefinitiveDeck& deck)
	{
		auto held = deck.PlungerHeld;
		auto pull = held ? std::clamp(deck.PlungerSeconds / PullSeconds, 0.0f, 1.0f) : 0.0f;
		auto delta = ImGui::GetIO().DeltaTime;

		// The lane only moves while it is drawn. Behind a screen or in Classic it isn't, and what it remembers of a
		// hold is old by then: a finger lifted meanwhile is no shot.
		auto frame = ImGui::GetFrameCount();
		if (frame != LaneFrame + 1)
			Lane = PlungerState{};
		LaneFrame = frame;

		// Letting go with a ball on the tip shoots it from where the tip was, the faster the further it had come.
		// A new hold ends the wait for the last shot.
		if (Lane.WasHeld && !held && Lane.HadBall)
			Lane = PlungerState{.Tip = Lane.Tip,
								.ShotFrom = Lane.Tip,
								.ShotSpeed = ShotSlow + (ShotFast - ShotSlow) * Lane.Tip,
								.ShotLeft = ShotSeconds};
		else if (held)
			Lane.ShotLeft = 0;
		else if (Lane.ShotLeft > 0)
		{
			Lane.ShotTravel += Lane.ShotSpeed * delta;
			Lane.ShotLeft = std::max(0.0f, Lane.ShotLeft - delta);
		}
		Lane.WasHeld = held;
		Lane.Tip = Approach(Lane.Tip, pull, delta * (held ? PullSpeed : SnapSpeed));
		auto shooting = Lane.ShotLeft > 0;
		auto resting = deck.BallWaiting && !shooting;
		Lane.HadBall = resting;

		auto caption = translations::GetTranslation(Msg::Definitive_TouchLaunch);
		auto textSize = Ui::TextSize(Theme::TextFont(), Theme::LabelSize, caption);
		auto top = lane.y + lane.h * 0.1f + textSize.y;
		auto bottom = lane.y + lane.h * 0.93f;
		auto length = bottom - top;
		auto centreX = lane.x + lane.w * 0.4f;
		auto ballRadius = std::min(lane.w * 0.19f, length * 0.09f);
		auto half = ballRadius * 1.25f;
		auto line = std::max(1.5f, ballRadius * 0.14f);

		// The lane: a dark channel, open at the top where the ball leaves
		drawList->AddRectFilled(ImVec2{centreX - half, top}, ImVec2{centreX + half, bottom},
								Theme::WithAlpha(Theme::Space950, 0.85f), ballRadius * 0.4f);
		auto wall = Theme::WithAlpha(Theme::Chrome600, 0.9f);
		drawList->AddLine(ImVec2{centreX - half, top}, ImVec2{centreX - half, bottom}, wall, line);
		drawList->AddLine(ImVec2{centreX + half, top}, ImVec2{centreX + half, bottom}, wall, line);
		drawList->AddLine(ImVec2{centreX - half, bottom}, ImVec2{centreX + half, bottom}, wall, line);

		// The tip: a flat cap as wide as the ball, in the lamps' colour while held, a third of the way down at rest
		auto travel = length * 0.34f;
		auto tipHeight = ballRadius * 0.55f;
		auto tipY = top + length * 0.36f + travel * Lane.Tip;
		auto metal = held ? Theme::LedOrange : Theme::Chrome200;

		// The spring between the tip and the lane's end, closer coiled the further the tip has come, around its rod
		constexpr int Coils = 6;
		auto springTop = tipY + tipHeight;
		auto springBottom = bottom - line;
		auto coilWidth = ballRadius * 0.7f;
		auto rod = Theme::WithAlpha(Theme::Chrome600, 0.9f);
		drawList->AddLine(ImVec2{centreX, springTop}, ImVec2{centreX, springBottom}, rod, line * 1.6f);
		auto step = (springBottom - springTop) / (Coils * 2);
		auto coil = Theme::WithAlpha(Theme::Chrome400, 0.95f);
		for (auto i = 0; i < Coils * 2; i++)
		{
			auto x0 = centreX + (i % 2 ? coilWidth : -coilWidth);
			drawList->AddLine(ImVec2{x0, springTop + step * static_cast<float>(i)},
							  ImVec2{centreX * 2 - x0, springTop + step * static_cast<float>(i + 1)}, coil, line);
		}
		drawList->AddRectFilled(ImVec2{centreX - ballRadius, tipY}, ImVec2{centreX + ballRadius, tipY + tipHeight},
								metal, tipHeight * 0.35f);

		// The ball: on the tip while one waits on the table's plunger, or on its way out after a release
		if (resting)
			DrawBall(drawList, ImVec2{centreX, tipY - ballRadius}, ballRadius);
		else if (shooting)
		{
			auto y = top + length * 0.36f + travel * Lane.ShotFrom - ballRadius - Lane.ShotTravel * length;
			if (y > lane.y - ballRadius)
				DrawBall(drawList, ImVec2{centreX, y}, ballRadius);
		}

		// The lamps, bottom up, lit as far as the pull has come
		auto lampX = lane.x + lane.w * 0.82f;
		auto lampRadius = std::min(lane.w * 0.07f, length / (MeterLamps * 3.0f));
		for (auto i = 0; i < MeterLamps; i++)
		{
			auto y = bottom - lampRadius - (length - lampRadius * 2) * static_cast<float>(i) / (MeterLamps - 1);
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
		auto barW = chip.w * 0.14f;
		auto barH = chip.h * 0.42f;
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
		DrawPlunger(drawList, full, deck);
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
