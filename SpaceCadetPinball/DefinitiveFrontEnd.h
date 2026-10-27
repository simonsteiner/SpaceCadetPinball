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
		// The longest name the high score table keeps, in bytes of UTF-8
		static constexpr size_t MaxNameBytes = 31;

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
	// The next new high score that needs a name, with its default name, which is its player's, and its place from 0;
	// false when none is left
	bool (*NextRecord)(Record& record, int& place);
	void (*SaveRecord)(const Record& record, int place);
	// Pausing and resuming the game in play, as the pause key does
	void (*Pause)();
	void (*Resume)();
	bool (*Paused)();
	// Whether the screens should wait: while the table is still ending a game, which is when it checks the high
	// scores, and while a dialog such as the high score name entry is open
	bool (*Busy)();
	const char* Version;

	// Settings that need more than an option set, as the Classic menus do after changing them
	void (*SetPostProcessing)(int effect);
	// Why post-processing can't show here, or null
	const char* (*PostProcessingUnavailable)();
	// Why the lighting, glow and shaded ball can't show here, or null
	const char* (*EffectsUnavailable)();
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
	// Opens the dialog that asks for a font file, as the Classic menu's Change Font does
	void (*ChangeFont)();
	// The key or button that switches editions, empty when none is bound
	std::string (*EditionKey)();
	// Opens the system's folder dialog for the game data; once one is chosen, the game restarts and loads from it.
	// Null where there is no such dialog, as in a browser.
	void (*ChooseDataFolder)();
	// Why the folder dialog couldn't open, or an empty string
	std::string (*FolderDialogError)();
	// Whether the folder chosen last had no game data, so the one in use stays
	bool (*ChosenFolderEmpty)();
	// The folder the game data was loaded from
	std::string (*DataFolder)();
	// Turns the picture by quarter turns clockwise, for a monitor on its side, and lays the table out for it
	void (*SetRotation)(int quarterTurns);
	// Where the table and its score panel are drawn, in window coordinates; the panel is empty when the table has none.
	// band is set in a window taller than wide, where the panel is the band under the table, as wide as the window.
	void (*Layout)(SDL_FRect& board, SDL_FRect& panel, bool& band);
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
	// The pause screen, which pauses the game in play; it comes back on its own while the game is paused
	static void ShowPause();
	// The first-run screen when no game data was found: where it looked and a way to choose the folder; the game
	// runs it in place of the table until a folder is chosen or the player quits
	static void RenderMissingData(const std::vector<std::string>& looked, bool chosenFolderFailed);
	// The pause key: the pause screen, or back to the game from it and from Settings opened there
	static void TogglePause();
	// Whether a screen is open, rather than the game in play
	static bool ScreenOpen();
	// Whether a key the game binds to a shortcut should still reach the game although a screen has the keyboard: while
	// a screen had the focus in the last frame, and for Escape only on the title, the one screen it doesn't take back.
	// The name entry types every key and the summary keeps them, so neither passes any.
	static bool PassesShortcut(bool escape);
	// Whether Settings waits for a key or button to bind to a control, which Escape and a tap cancel instead
	static bool WaitsForBinding();

private:
	enum class Screen
	{
		None,
		Title,
		Records,
		Credits,
		Settings,
		Pause,
		GameOver,
		NameEntry,
		Guide,
		Stats,
	};

	static DefinitiveHost Host;
	static Screen Current, SettingsFrom;
	static bool Opened, ConfirmClear, SummaryShown, Focused;
	static float CreditsScroll;
	// The high score whose name is being entered, its place, the name so far, and the default names, one per player, of
	// the scores that made the table
	static DefinitiveHost::Record Entry;
	static int EntryPlace;
	static std::string EntryName;
	static std::vector<std::string> NewRecords;

	static void Open(Screen screen);
	static void FollowGame(const TableView& view, bool playing);
	static void RenderTitle(const TableView& view);
	static void RenderRecords();
	static void RenderCredits();
	// In DefinitiveInGame.cpp
	static void RenderPause(const TableView& view);
	static void RenderGameOver(const TableView& view);
	static void RenderNameEntry();
	// Asks for the name of the next new high score; false when there is none
	static bool NextName();
	// A panel in the middle of the window over a veil that dims the game; its width and height at scale 1
	static ImVec2 BeginCentred(const char* name, ImVec2 size);
	// A window filling the column on the left, over a fade that keeps the table in view; returns its width
	static float BeginColumn(const char* name, float width);
	static void EndColumn();
	static bool BackPressed();
};
