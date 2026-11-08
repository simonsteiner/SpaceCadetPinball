#pragma once

enum class GameBindings;

// Space Cadet 2026's control deck for touch screens (the Touch Controls option): a pad for each flipper and a plunger
// lane where the thumbs are, laid out here for fullscrn, drawn by DefinitiveTouchDeck, and the fingers on them. A flick
// up on a pad nudges the table from that side. The table's own halves still work its flippers.
class TouchControls
{
public:
	// In pixels, like render::BoardRect
	struct Layout
	{
		SDL_Rect Board{}, Panel{}, LeftPad{}, RightPad{}, Plunger{}, Pause{}, Hint{};
	};

	// Lays the window out around the deck, with the table at the largest scale that leaves the deck and the score panel
	// their room, all inside the window's safe area. False where the deck doesn't apply (the option is off, the window
	// is too square for it, or the picture is turned), and fullscrn lays out as before.
	static bool Arrange(int width, int height, int boardWidth, int tableHeight, float& scale);
	// Each frame: folds the plunger lane away while no ball waits at the plunger and no finger holds it, and gives
	// its room to the pads
	static void Animate();
	static bool Active();
	static const Layout& Current();

	// A finger going down, moving, lifting or cancelled; only called while the deck is active
	static void Finger(const SDL_TouchFingerEvent& event);
	// Whether a finger holds the binding, and how long the plunger has been held, for drawing them
	static bool Held(GameBindings binding);
	static float PlungerSeconds();
	// The plunger lane's width unfolded, in pixels
	static float PlungerWidth();
	// Whether the player has nudged by touch yet, after which the deck stops explaining how
	static bool Nudged();

private:
	struct FingerState
	{
		GameBindings Binding;
		float StartY;
		Uint64 StartTicks;
		bool Nudged;
	};
	static std::map<std::pair<SDL_TouchID, SDL_FingerID>, FingerState> Fingers;
	// The row the pads and the plunger share (both pads upright, the right one on its side), and the lane's width
	struct Row
	{
		SDL_FRect Area{};
		float PlungerWidth = 0, Gap = 0;
		bool BothPads = false;
	};
	// Arrange's layout, false where the deck doesn't apply
	static bool Place(int width, int height, int boardWidth, int tableHeight, float& scale);
	// Lets go of every binding and nudge the fingers on the deck hold, when it stops taking them
	static void ReleaseAll();
	static void PlaceRow(float shown);
	static Layout Rects;
	static Row PadRow;
	static bool IsActive, HasNudged;
	static Uint64 PlungerTicks, AnimateTicks;
	// 1 while the lane shows, 0 once folded away
	static float LaneShown;
};
