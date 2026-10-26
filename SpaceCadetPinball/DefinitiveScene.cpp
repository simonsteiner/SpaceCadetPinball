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
} // namespace

const DefinitiveScene& DefinitiveScene::Build(const TableView& view)
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
	return Scene;
}
