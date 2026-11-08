#pragma once

// Space Cadet 2026's control deck for touch screens, as winmain hands it over: where each control is, in window
// coordinates, and which a finger holds. The game side lays it out and takes the fingers (TouchControls).
struct DefinitiveDeck
{
	SDL_FRect LeftPad{}, RightPad{}, Plunger{}, Pause{}, Hint{};
	bool LeftHeld = false, RightHeld = false, PlungerHeld = false;
	// How long the plunger has been held, in seconds
	float PlungerSeconds = 0;
	// Until the player has nudged by touch once, the deck says how
	bool ExplainNudge = true;
};

class DefinitiveTouchDeck
{
public:
	// Draws the deck under the screens, so their veils cover it as they cover the score panel
	static void Render(const DefinitiveDeck& deck);
};
