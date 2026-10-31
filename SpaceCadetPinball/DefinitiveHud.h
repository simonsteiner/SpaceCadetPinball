#pragma once

struct TableView;

// Space Cadet 2026's score panel, drawn in place of the table's own: the score and ball, every player's score, the
// mission and what it needs next, the rank, and the messages of the table's two text boxes, under a header with the
// table's own panel art. Everything on it comes from the view, and the art from the host as a texture.
class DefinitiveHud
{
public:
	// Draws the panel under every window, where the table's panel is and in the window's margin beyond it: beside the
	// table as tall as it is, or under it in the stacked layout. The rectangles are in window coordinates.
	// In a band, the panel is the band under the table in a window taller than wide. The art, the table's own panel
	// art or null, heads the panel where there is room, but not in a band or in high contrast.
	static void Render(const TableView& view, SDL_FRect board, SDL_FRect panel, bool band, SDL_Texture* art);
	// The mission a state of the view's Mission runs, from 2 for Target Practice to 18 for Maelstrom, or 0 while none
	// runs
	static int MissionOf(int state);
	// The name of a mission from MissionOf, or null for another number
	static const char* MissionName(int mission);
	// The name of a rank from 1 for Cadet to 9 for Fleet Admiral, or null for another number
	static const char* RankName(int rank);
	// The name of a Dragon's Keep quest by its state, 4 to 8, or of Slay Dragon, 9; null for another state
	static const char* QuestName(int state);
	// The name of a Skulduggery mode by its state, 4 to 11, or of its multiballs, 12 Lava-Ball and 13 Jailbreak; null
	// for another state
	static const char* SkulduggeryModeName(int state);

private:
	static float DrawContents(ImDrawList* drawList, const TableView& view, ImVec2 min, ImVec2 max);
	// The same in a band under the table, wider than high, for a window taller than wide
	static float DrawBand(ImDrawList* drawList, const TableView& view, ImVec2 min, ImVec2 max);
	static void RenderBand(const TableView& view, SDL_FRect panel);
};
