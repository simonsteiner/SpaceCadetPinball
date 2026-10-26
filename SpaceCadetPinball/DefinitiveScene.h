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

	// Light the table from its depth map
	bool Lighting;
	std::vector<Glow> Glows;

	// The scene for the view of the last update
	static const DefinitiveScene& Build(const TableView& view);
};
