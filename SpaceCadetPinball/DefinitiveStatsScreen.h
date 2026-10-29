#pragma once

struct TableView;

// Space Cadet 2026's Stats screen, from the title: the table's stats and the achievements, as two tabs in a panel on
// the left, like Settings. It also draws the notice of an achievement unlocked in play.
class DefinitiveStatsScreen
{
public:
	static void Open();
	// Draws the screen for the table in the view; false once the player goes back
	static bool Render(const TableView& view);
	// The notice of an achievement just unlocked, over the top of the window, while there is one
	static void RenderNotice();

private:
	static int Current;
	static bool Opened;
	static bool ConfirmClear;
	static bool BackChosen;

	static void RenderStats(const TableView& view);
	static void RenderAchievements(const TableView& view);
};
