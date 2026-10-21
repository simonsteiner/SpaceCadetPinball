#pragma once

struct DefinitiveHost;
struct GameInput;

// Space Cadet 2026's Settings: a panel on the left with the groups as tabs, so the table shows each change behind it.
// Every setting writes the option the Classic menus write, through the same setters.
class DefinitiveSettings
{
public:
	enum class Group
	{
		Edition,
		Display,
		Graphics,
		Audio,
		Controls,
		Accessibility,
		Count,
	};

	static void Open(Group group);
	// Draws the screen; false once the player goes back. With a game running, settings that restart the game say so.
	static bool Render(const DefinitiveHost& host, bool gameRunning);
	// Whether one of the Controls slots waits for the key or button to bind
	static bool WaitsForBinding();

private:
	static Group Current;
	static bool Opened, Languages, ShownLanguages, BackChosen, FocusLanguage;
	static int Slot;
	static GameInput* Waited;

	static void RenderEdition(const DefinitiveHost& host, bool gameRunning);
	static void RenderLanguages(const DefinitiveHost& host, bool opened);
	static void RenderDisplay(const DefinitiveHost& host, bool gameRunning);
	static void RenderGraphics(const DefinitiveHost& host);
	static void RenderAudio(const DefinitiveHost& host);
	static void RenderControls();
	static void RenderAccessibility();
};
