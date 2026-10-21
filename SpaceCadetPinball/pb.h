#pragma once
#include "high_score.h"

class TEdgeSegment;
struct ray_type;
struct GameInput;
enum class GameBindings;
class TPinballTable;
class DatFile;
class TBall;
class TTextBox;
enum class Msg : int;

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
	static bool FullTiltMode, FullTiltDemoMode;
	// False from the start of a game until end_game has checked its high scores, which the table does a few seconds
	// after it shows Game Over, or until the demo is stopped
	static bool GameEnded;
	static std::string DatFileName, BasePath;
	static ImU32 TextBoxColor;
	static int quickFlag;
	static TTextBox *InfoTextBox, *MissTextBox;

	static int init();
	static int uninit();
	static void SelectDatFile(const std::vector<const char*>& dataSearchPaths);
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
	static void end_game();
	static void high_scores();
	static void tilt_no_more();
	static bool chk_highscore();
	static void PushCheat(const std::string& cheat);
	static LPCSTR get_rc_string(Msg uID);
	static int get_rc_int(Msg uID, int* dst);
	static std::string make_path_name(const std::string& fileName);
	static void ShowMessageBox(Uint32 flags, LPCSTR title, LPCSTR message);
private:
	static bool demo_mode;
	static float IdleTimerMs;

	// The shared path of InputUp/Down and BindingUp/Down: shortcuts, then, while a game runs, record and act
	static bool TableTakesInput();
	static void BindingsUp(GameInput input, const std::vector<GameBindings>& bindings);
	static void BindingsDown(GameInput input, const std::vector<GameBindings>& bindings);
	// InputDown and KeyDown: a waiting control dialog takes bound, else bound's bindings act and recorded is recorded
	static void InputDown(GameInput recorded, GameInput bound);
	static float BallToBallCollision(const ray_type& ray, const TBall& ball, TEdgeSegment** edge, float collisionDistance);
};
