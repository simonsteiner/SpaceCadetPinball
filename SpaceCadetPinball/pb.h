#pragma once
#include "high_score.h"
#include <optional>

class TEdgeSegment;
struct ray_type;
struct GameInput;
enum class GameBindings;
class TPinballTable;
class DatFile;
class TBall;
class TTextBox;
enum class Msg : int;

// The table the DAT file holds. Space Cadet comes as 3D Pinball's, Full Tilt's and Full Tilt's demo;
// Skulduggery and Dragon's Keep are Full Tilt's bonus tables (Phase F); Skulduggery isn't loadable yet.
enum class Tables
{
	SpaceCadet3DPB,
	SpaceCadetFullTilt,
	FullTiltDemo,
	Skulduggery,
	DragonsKeep,
};

// A table whose data was found, and where
struct TableData
{
	Tables Table;
	// Ends in a separator, or is empty for the working directory
	std::string Folder;
	// As found, in upper or lower case
	std::string DatFileName;
};

enum class GameModes
{
	InGame = 1,
	GameOver = 2,
};

class pb
{
public:
	static int time_ticks;
	static float time_now, time_next, time_ticks_remainder;
	static float BallMaxSpeed, BallHalfRadius, BallToBallCollisionDistance;
	static GameModes game_mode;
	static bool cheat_mode, CreditsActive;
	static DatFile* record_table;
	static TPinballTable* MainTable;
	static Tables Table;
	// A table asked for with -table or by the recording -replay plays, which SelectDatFile loads instead of the
	// player's choice; any of Space Cadet's tables stands for Space Cadet
	static std::optional<Tables> RequestedTable;
	// The tables SelectDatFile found, Space Cadet first, which the table menus list
	static std::vector<TableData> FoundTables;
	// Set with Table: Full Tilt's DAT format and behaviour, and its demo's
	static bool FullTiltMode, FullTiltDemoMode;
	// Skulduggery or Dragon's Keep, whose engine is CADET.EXE's where it differs from Pinball.exe's
	static bool BonusTable()
	{
		return Table == Tables::Skulduggery || Table == Tables::DragonsKeep;
	}
	// False from the start of a game until end_game has checked its high scores, which the table does a few seconds
	// after it shows Game Over, or until the demo is stopped
	static bool GameEnded;
	static std::string DatFileName, BasePath;
	static ImU32 TextBoxColor;
	static int quickFlag;
	static TTextBox *InfoTextBox, *MissTextBox;

	static int init();
	static int uninit();
	// The tables whose data is in the folders, each from the first folder that has it, Space Cadet first. A folder
	// ends in a separator; besides it, each table is looked for in its folder of a Full Tilt! installation: in
	// CADET or DRAGON under it, and next to it when the folder is one of Full Tilt!'s CADET, DRAGON or PIRATE.
	static std::vector<TableData> FindTables(const std::vector<const char*>& dataSearchPaths);
	// Loads the table asked for, else the player's choice (options' Table), else the first table found
	static void SelectDatFile(const std::vector<const char*>& dataSearchPaths);
	// The player's choice from the table menus: saves it and restarts the game with that table, which ends a game in
	// progress. Nothing happens for the table loaded.
	static void SelectTable(Tables table);
	// The table a DAT file holds, from its name in any case
	static Tables TableOf(const std::string& datFileName);
	// Whether two tables are the same for the player: Space Cadet's three DAT files are one table
	static bool SameTable(Tables a, Tables b);
	// The saved choice of table: "SpaceCadet" for any of Space Cadet's, or "DragonsKeep"; an unknown key is Space Cadet
	static const char* TableKey(Tables table);
	static Tables TableFromKey(std::string_view key);
	// The window title and About box name of the table loaded, as each original EXE has it
	static Msg Caption();
	// A table's name and the game its data comes from, for the table menus, the same in every language
	static const char* TableName(Tables table);
	static const char* TableSource(Tables table);
	// The file in a folder that ends in a separator, its name in upper case and then lower case: the name as found,
	// or empty
	static std::string FindFile(const std::string& folder, const std::string& name);
	static void reset_table();
	static void firsttime_setup();
	static void mode_change(GameModes mode);
	static void toggle_demo();
	static void replay_level(bool demoMode);
	static void ballset(float dx, float dy);
	static void frame(float dtMilliSec);
	static void timed_frame(float timeDelta);
	static void pause_continue();
	// Cheat-mode F10: freezes the table or lets it run again, without pausing the game
	static void toggle_single_step();
	static void loose_focus();
	static void InputUp(GameInput input);
	static void InputDown(GameInput input);
	// Bindings match the physical key; the table and replays get the keycode
	static void KeyUp(SDL_Scancode scancode, SDL_Keycode key);
	static void KeyDown(SDL_Scancode scancode, SDL_Keycode key);
	// Input that already is a binding, such as a touch on the table
	static void BindingDown(GameBindings binding);
	static void BindingUp(GameBindings binding);
	// The part of InputUp and InputDown that acts on the table, also called by replay playback
	static void GameInputUp(const std::vector<GameBindings>& bindings);
	static void GameInputDown(GameInput input, const std::vector<GameBindings>& bindings);
	static void launch_ball();
	static bool end_game();
	static void high_scores();
	static void tilt_no_more();
	static bool chk_highscore();
	static void PushCheat(const std::string& cheat);
	// Whether a menu or dialog holds the player's input. Full Tilt's modal dialogs stop its game loop, so its idle
	// credits wait for them; recordings keep the changes, so playing them back doesn't depend on the UI
	static void SetUiHoldsInput(bool holds);
	static LPCSTR get_rc_string(Msg uID);
	static int get_rc_int(Msg uID, int* dst);
	static std::string make_path_name(const std::string& fileName);
	static void ShowMessageBox(Uint32 flags, LPCSTR title, LPCSTR message);
private:
	static bool demo_mode;
	static float IdleTimerMs;
	static bool UiHoldsInput;

	// The shared path of InputUp/Down and BindingUp/Down: shortcuts, then, while a game runs, record and act
	static bool TableTakesInput();
	static void BindingsUp(GameInput input, const std::vector<GameBindings>& bindings);
	static void BindingsDown(GameInput input, const std::vector<GameBindings>& bindings);
	// InputDown and KeyDown: a waiting control dialog takes bound, else bound's bindings act and recorded is recorded
	static void InputDown(GameInput recorded, GameInput bound);
	static float BallToBallCollision(const ray_type& ray, const TBall& ball, TEdgeSegment** edge, float collisionDistance);
};
