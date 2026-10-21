#pragma once

struct TableView;

// Space Cadet 2026's presentation. It reads the table only through TableView and never changes it, so replays play
// the same in both editions (docs/replays.md).
class DefinitiveEdition
{
public:
	// Runs after each table update while Space Cadet 2026 is on, also in headless replay playback, where nothing is
	// drawn. The front end, HUD and compositor keep their per-update state here.
	static void Update(const TableView& view);
	// Forgets the state of the game before, when the table goes away or the edition is switched on
	static void Reset();
	// The view of the update before the latest, for telling what changed
	static const TableView& PreviousView();
};
