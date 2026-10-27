#pragma once

#include <vector>

struct TableView;

// What Space Cadet 2026's compositor draws on top of the frame, worked out from the table view each frame. The
// compositor, on the table's side, only draws it.
struct DefinitiveScene
{
	struct Glow
	{
		// The lit lamp's sprite in the 1x frame, in pixels
		SDL_FRect Sprite;
		// 0 to 1
		float Strength;
	};

	struct Ball
	{
		// In the 1x frame, in pixels
		SDL_FPoint Center;
		float Radius;
		// On the depth maps' 0 to 0xFFFF scale, as the frame's ball sprite is tested
		int Depth;
	};

	// Light the table from its depth map
	bool Lighting;
	std::vector<Glow> Glows;
	// Draw the balls as shaded spheres instead of the frame's ball sprites
	bool ShadedBalls;
	std::vector<Ball> Balls;

	// The scene for the view of the last update. alpha, below 1 when frames come faster than updates, is how far the
	// frame is between the update before (previous) and the last, which the balls are moved by.
	static const DefinitiveScene& Build(const TableView& view, const TableView& previous, float alpha);
};
