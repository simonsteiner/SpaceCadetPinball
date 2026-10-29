#pragma once

struct TableView;

// Space Cadet 2026's Mission Guide, from the pause screen: how missions and ranks work, and for each rank what each
// mission target chooses, what the mission asks for and what it awards. A panel on the left, like Settings, with the
// basics and the ranks' missions as tabs. On Dragon's Keep its tabs are the basics, the quests, the multiball modes,
// the shop and Slay Dragon, each with what starts it and what it needs.
class DefinitiveGuide
{
public:
	// Opens on the missions the player's rank offers, with the mission that runs in focus, if one does
	static void Open(const TableView& view);
	// Draws the screen; false once the player goes back
	static bool Render(const TableView& view);

private:
	// The tab shown, and the row to give the focus to, with the frames left to try
	static int Current;
	static int Focus;
	static int FocusFrames;
	static bool Opened;
	static bool BackChosen;

	// A row of text that takes the focus when it is row's turn; row counts the rows drawn
	static void Paragraph(int& row, const char* id, const char* title, const char* aside, const char* text,
						  const char* tag, float footer = 0);
	// The rows of the basics' tab, and of the missions of a rank's tab
	static void RenderBasics(const TableView& view, int& row);
	static void RenderMissions(const TableView& view, int& row);
	// The rows of Dragon's Keep's tab
	static void RenderDragon(const TableView& view, int& row);
};
