#pragma once

struct TableView;

// Space Cadet 2026's Mission Guide, from the pause screen: how missions and ranks work, and for each rank what each
// mission target chooses, what the mission asks for and what it awards. A panel on the left, like Settings, with the
// basics and the ranks' missions as tabs.
class DefinitiveGuide
{
public:
	// Opens on the missions the player's rank offers, with the mission that runs in focus, if one does
	static void Open(const TableView& view);
	// Draws the screen; false once the player goes back
	static bool Render(const TableView& view);

private:
	// The tab shown, and the row to give the focus to, with the frames left to try
	static int Current, Focus, FocusFrames;
	static bool Opened, BackChosen;
};
