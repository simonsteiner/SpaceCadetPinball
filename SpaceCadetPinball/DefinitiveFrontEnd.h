#pragma once

#include <string>
#include <vector>

struct TableView;

// What Space Cadet 2026's screens ask the game to do. Presentation code can't reach the table or winmain, so winmain
// fills this in with the same entry points the Classic menus use (pb and the options), and replays record them alike.
struct DefinitiveHost
{
	struct Record
	{
		std::string Name;
		int Score;
	};

	// A new game with the number of players the options hold, as New Game starts it
	void (*Play)();
	// The table playing itself, behind the title
	void (*StartDemo)();
	// Null where the game can't end itself, as in a browser tab
	void (*Quit)();
	std::vector<Record> (*Records)();
	void (*ClearRecords)();
	// Whether the screens should wait: while the table is still ending a game, which is when it checks the high
	// scores, and while a dialog such as the high score name entry is open
	bool (*Busy)();
	const char* Version;

	// Settings that need more than an option set, as the Classic menus do after changing them
	void (*SetPostProcessing)(int effect);
	// Why post-processing can't show here, or null
	const char* (*PostProcessingUnavailable)();
	void (*SetUpscaleFactor)(int factor);
	void (*SetSoundVolume)(int volume);
	void (*SetSoundChannels)(int channels);
	void (*SetMusicVolume)(int volume);
	void (*FrameRateChanged)();
	void (*PreciseSleepChanged)();
	// Restarts the game in another language, as the Classic menu does
	void (*SetLanguage)(const char* shortName);
	// The largest table resolution the data has, 0 for 640 by 480 only
	int (*MaxResolution)();
	// The key or button that switches editions, empty when none is bound
	std::string (*EditionKey)();
};

// Space Cadet 2026's front end: the title with the main menu and the screens it leads to. It takes the place of
// Classic's menu bar while Space Cadet 2026 is on.
class DefinitiveFrontEnd
{
public:
	static void SetHost(const DefinitiveHost& host);
	// Each frame while Space Cadet 2026 is on
	static void Render(const TableView& view);
	// The title, over the table's demo
	static void ShowTitle();
	// Settings, opened on the controls when controls is true; back returns to the screen before
	static void ShowSettings(bool controls = false);
	// Whether a screen is open, rather than the game in play
	static bool ScreenOpen();

private:
	enum class Screen
	{
		None,
		Title,
		Records,
		Credits,
		Settings,
	};

	static DefinitiveHost Host;
	static Screen Current, SettingsFrom;
	static bool Opened, ConfirmClear;
	static float CreditsScroll;

	static void Open(Screen screen);
	static void RenderTitle(const TableView& view);
	static void RenderRecords();
	static void RenderCredits();
	// A window filling the column on the left, over a fade that keeps the table in view; returns its width
	static float BeginColumn(const char* name, float width);
	static void EndColumn();
	static bool BackPressed();
};
