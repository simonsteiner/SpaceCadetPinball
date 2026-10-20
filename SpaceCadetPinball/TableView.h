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
	std::string InfoText, MissionText;
	int PlayerCount, CurrentPlayer;
	std::array<int, 4> Scores;
	// The current player's balls left
	int BallCount;
	bool GameOver, Demo, Tilted;
	// The table rules' mission state: 0 while waiting for deployment, 1 while choosing a mission, else the mission
	// that runs. -1 when the table has no mission light (lite198).
	int Mission;
	// The lit lights of the rank circle (middle_circle), 1 for Cadet up to 9; the rank names are Msg::STRING185 to
	// Msg::STRING193
	int Rank;

	// The view of the table in play
	static const TableView& Get();
	// Called only by pb: Fill after each update, Clear when the table goes away
	static void Fill();
	static void Clear();
};
