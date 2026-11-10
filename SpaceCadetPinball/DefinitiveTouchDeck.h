#pragma once

// Space Cadet 2026's control deck for touch screens, as winmain hands it over: where each control is, in window
// coordinates, and which a finger holds. The game side lays it out and takes the fingers (TouchControls).
struct DefinitiveDeck
{
	SDL_FRect LeftPad{};
	SDL_FRect RightPad{};
	SDL_FRect Plunger{};
	SDL_FRect Pause{};
	SDL_FRect Hint{};
	bool LeftHeld = false;
	bool RightHeld = false;
	bool PlungerHeld = false;
	// Whether a ball waits on the table's plunger, which the lane then shows on its own
	bool BallWaiting = false;
	// How long the plunger has been held, in seconds, and the lane's width unfolded; Plunger narrows as it folds
	float PlungerSeconds = 0;
	float PlungerWidth = 0;
	// Until the player has nudged by touch once, the deck says how
	bool ExplainNudge = true;
};

class DefinitiveTouchDeck
{
public:
	// Draws the deck under the screens, so their veils cover it as they cover the score panel
	static void Render(const DefinitiveDeck& deck);
};
