#pragma once

#include "DefinitiveFrontEnd.h"

// Space Cadet 2026's Recordings, from the title: the sessions recorded on this machine, played back in the window with
// a timeline. The player says "recordings" because a Replay is the free game the table awards (GLOSSARY.md).
class DefinitiveRecordings
{
public:
	static void Open(const DefinitiveHost& host);
	// The list; false once the player goes back
	static bool Render(const DefinitiveHost& host);
	// The bar over the bottom of the window while a recording plays: where it is, the games in it, and the controls
	static void RenderViewer(const DefinitiveHost& host);

private:
	static std::vector<DefinitiveHost::Recording> List;
	// The recording whose actions show, or -1 for the list, and whether its deletion waits for a confirmation
	static int Selected;
	static bool ConfirmDelete, Opened, BackChosen, Refocus;
};
