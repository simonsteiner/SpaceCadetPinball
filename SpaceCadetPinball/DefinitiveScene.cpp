#include "pch.h"
#include "DefinitiveScene.h"

#include "TableView.h"

#include <numbers>

namespace
{
	DefinitiveScene Scene{};

	// The glow breathes gently, so the lit lamps look alive without blinking
	constexpr float PulsePeriod = 2.4f; // Seconds
	constexpr float PulseDepth = 0.15f;

	// A ball that moved further than this between two updates, such as into a hole or back to the plunger, isn't drawn
	// in between, as render::InterpolateBalls does for the frame's ball sprites
	constexpr int MaxStep = 24;

	SDL_FPoint Center(const SDL_Rect& sprite)
	{
		return {static_cast<float>(sprite.x) + static_cast<float>(sprite.w) / 2,
				static_cast<float>(sprite.y) + static_cast<float>(sprite.h) / 2};
	}
} // namespace

const DefinitiveScene& DefinitiveScene::Build(const TableView& view, const TableView& previous, float alpha)
{
	Scene.Lighting = true;

	auto seconds = static_cast<float>(SDL_GetTicks() % 60000) / 1000.0f;
	auto pulse = 1.0f - PulseDepth * 0.5f * (1.0f + std::sin(seconds * 2.0f * std::numbers::pi_v<float> / PulsePeriod));
	Scene.Glows.clear();
	for (const auto& light : view.Lights)
	{
		// A light shows a sprite only while it shows lit, which also follows flashing and timed overrides
		if (light.Sprite.w <= 0 || light.Sprite.h <= 0)
			continue;
		SDL_FRect sprite;
		SDL_RectToFRect(&light.Sprite, &sprite);
		Scene.Glows.push_back({sprite, pulse});
	}

	// Where the frame would have drawn each ball's sprite
	Scene.ShadedBalls = true;
	Scene.Balls.clear();
	for (auto index = 0u; index < view.Balls.size(); index++)
	{
		const auto& sprite = view.Balls[index].Sprite;
		if (sprite.w <= 0 || sprite.h <= 0)
			continue;
		auto center = Center(sprite);
		if (alpha < 1.0f && index < previous.Balls.size())
		{
			const auto& from = previous.Balls[index].Sprite;
			if (from.w > 0 && std::abs(sprite.x - from.x) <= MaxStep && std::abs(sprite.y - from.y) <= MaxStep)
			{
				auto start = Center(from);
				center = {start.x + (center.x - start.x) * alpha, start.y + (center.y - start.y) * alpha};
			}
		}
		Scene.Balls.push_back(
			{center, static_cast<float>(std::min(sprite.w, sprite.h)) / 2, view.Balls[index].Depth});
	}
	return Scene;
}
