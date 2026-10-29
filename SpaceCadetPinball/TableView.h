#pragma once

// What the table shows, copied from it after every update. Presentation code, such as Space Cadet 2026's, reads
// the table only through this view: it never includes the table's or the components' headers, never calls into
// components and never sends messages (tools/check-presentation-includes.sh checks the includes).
struct TableView
{
	struct Ball
	{
		bool Active;
		// Table coordinates; Z is the ball's height
		float X, Y, Z;
		// The direction is a unit vector
		float DirectionX, DirectionY, Speed;
		// The depth the renderer tests the ball's sprite against the depth maps with, on their 0 to 0xFFFF scale
		// (proj::NormalizeDepth of the ball's distance from the camera); 0 while the ball has no sprite
		int Depth;
		// Where the ball's sprite is in the 1x frame, in pixels; empty while it has none
		SDL_Rect Sprite;
	};

	struct Flipper
	{
		// Radians from the rest position; AngleMax when fully extended
		float Angle, AngleMax;
	};

	// The tables, as presentation tells them apart
	enum class TableKind
	{
		SpaceCadet,
		DragonsKeep,
		Skulduggery,
	};

	// Dragon's Keep's own state, which its rules fill (control_dragon.cpp); all zero on the other tables
	struct DragonsKeep
	{
		// The rules' state, as DRAGON.EXE numbers it: 2 play, 3 a quest's restart, 4 to 8 the quests (Dragon Hoard,
		// Fire Lizards, Rescue Maiden, Dragon Pass, Wizard's Fury), 9 Slay Dragon, 12 the dragon egg shop, 13 Tower
		// Barrage, 14 Gargoyle Gaggle-Ball, 15 Dragon Flurry; the others are the serve, the end of a ball and the
		// game's
		int State;
		// In a restart (state 3), the quest it takes up again, by its state; else 0
		int RestartQuest;
		// The quest picked to start next, whose light flashes, by its state; 0 for none
		int PickedQuest;
		// Milliseconds left of the time of a quest, its restart or Slay Dragon; 0 while none runs
		int TimeLeft;
		// The quests done since the round of five began, one bit each from bit 0 for state 4, as their lights show
		unsigned QuestLights;
		// The quests the current player has done in this game
		int QuestsDone;
		int Jackpot;
		// The second jackpot, which Slay Dragon awards as the Super Jackpot
		int SuperJackpot;
		// The dragon eggs lit, 0 to 8
		int Eggs;
		// The weapons lit, 0 to 4; the score multiplier is one more
		int Weapons;
		// The armor lit, one bit each: 1 the chainmail (the kickback), 2 the shield (the center post), 4 the helmet
		// (the right outlane's blocker)
		unsigned Armor;
		// The hits Slay Dragon still needs
		int HitsLeft;
		// How often the ball save has served a drained ball again since the table loaded, which the view can't
		// otherwise tell, as the save shows no text
		int BallSaves;
	};

	struct Light
	{
		// The light's group name in the DAT file, such as "lite198"
		std::string Name;
		bool On, Flashing;
		// Where the light's sprite is in the 1x frame, in pixels; empty while it has none
		SDL_Rect Sprite;
	};

	std::vector<Ball> Balls;
	std::vector<Flipper> Flippers;
	// Every light on the table, in the table's component order
	std::vector<Light> Lights;
	// The messages the two text boxes show now; empty when a box is blank
	std::string InfoText;
	std::string MissionText;
	int PlayerCount;
	int CurrentPlayer;
	std::array<int, 4> Scores;
	// The current player's balls left, and the balls a player starts with; the panel shows ball
	// MaxBallCount - BallCount + 1, and none while BallCount is 0
	int BallCount;
	int MaxBallCount;
	bool GameOver;
	bool Demo;
	bool Tilted;
	// Whether the table is Full Tilt!'s (CADET.DAT or DEMO.DAT), whose rules differ in places from 3D Pinball's
	bool FullTilt;
	// The table rules' mission state: 0 while waiting for deployment, 1 while choosing a mission, else the mission
	// that runs. -1 when the table has no mission light (lite198).
	int Mission;
	// The lit lights of the rank circle (middle_circle), 1 for Cadet up to 9; the rank names are Msg::STRING185 to
	// Msg::STRING193
	int Rank;
	// The lit lamps of the rank progress ring (outer_circle): a finished mission lights some, a new rank clears them
	int RankProgress;
	// How many messages the info text box has started showing, counting one shown again, so presentation can tell a
	// message that repeats from one that stays
	int InfoMessages;
	// The current player's extra balls still to come
	int ExtraBalls;
	// Whether a cheat was used in this game, which keeps it out of the high scores
	bool CheatsUsed;
	// Which table this is
	TableKind Kind;
	DragonsKeep Dragon;

	// 64-bit FNV-1a over every field, so two runs can compare their views frame by frame
	uint64_t Hash() const;

	// The view of the table in play
	static const TableView& Get();
	// Called only by pb: Fill after each update, Clear when the table goes away
	static void Fill();
	static void Clear();
};
