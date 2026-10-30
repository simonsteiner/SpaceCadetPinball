#pragma once

class TPinballTable;
class TTextBox;

// What the bonus tables' cheats share, as DRAGON.EXE (0x4282c0) and PIRATES.EXE (0x42a830) have them: the keys,
// taken as Windows' key codes, spell a cheat's word, whose first key picks it and whose last fires it; QUOTE's
// scroller shows the table's quotes over info_text_box, and the idle credits (0x43b6e8, the same in both and in
// control.cpp) the same way; "hidden<Tab>test" (the engine's 0x411f50) turns cheat mode on; and the ball's
// constructor (0x402740) takes the frames' second bitmap while a cheat asks for it. Each table's rules keep one, with
// their own words, quotes and effects.
class BonusTableCheats
{
public:
	// <words> are the table's cheats in capitals, numbered from 1 in that order, QUOTE among them. <fire> does the
	// effect of the cheat with that number, but QUOTE's, and returns whether the cheat ends, as all but FMAX do.
	// With <endQuotesOnce> the first key after QUOTE ends the scroller, as in PIRATES.EXE; without it every key does
	// until the credits next show, as in DRAGON.EXE
	BonusTableCheats(std::initializer_list<std::string_view> words, const char* quotes, bool endQuotesOnce,
					 bool (*fire)(int cheat));

	// A key pressed in a game: an SDL keycode, or a character of a cheat the menu, the idle credits or a recording
	// types
	void Key(int key, TPinballTable& table, TTextBox& textBox);
	// The scroller stops and the text box's messages show again (0x422b50 in DRAGON.EXE, 0x422b60 in PIRATES.EXE,
	// with the text box's 0x41e0e0)
	void EndScroll(TTextBox& textBox);
	// Switches the balls made from now on between their frames' first and second bitmap
	void ToggleAltBall();
	// With the second bitmap asked for, each frame takes it; a frame without one switches it off, and it and the
	// frames after it take their first
	bool AltBallFrame(bool available);
	// The rules' session begins anew, as on a restart into a recording; DRAGON.EXE and PIRATES.EXE keep this state
	// for their whole session
	void Clear();

private:
	std::vector<std::string_view> Words;
	const char* Quotes;
	bool EndQuotesOnce;
	bool (*Fire)(int cheat);

	int CheatKey{};            // the last key of the cheat being typed, 0 for none
	int CheatTyped{};          // which one, numbered as Words
	int HiddenTestStep{};      // 0x439154: how much of "hidden<Tab>test" is typed
	bool AltBall{};            // balls made from now on take their frames' second bitmap
	bool QuotesShown{};        // QUOTE was typed, so a key ends the scroller
	int ScrollTimer{};         // the timer that shows the scroller's next text
	const char* ScrollNext{};  // the scroller's next text, nullptr after the last
	std::string Typed;         // the last keys, for the "credits" pb::frame types after a minute without a key
	TTextBox* ScrollTextBox{}; // where the scroller shows

	bool KeyFits(int key);
	int KeyFires(int key) const;
	void HiddenTest(int key, TPinballTable& table);
	void Scroll(bool restart);
	static void ScrollTimerExpired(int, void* caller);
};
