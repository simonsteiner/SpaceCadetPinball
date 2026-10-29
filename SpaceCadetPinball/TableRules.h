#pragma once

class TPinballTable;
class TBall;
enum class MessageCode;

// The entry points of a table's rules that the engine calls. Components reach the rules through
// control::handler and the component_control their table's rules link to them; these are the rest.
// Space Cadet's rules are in control.cpp, each bonus table's in control_<table>.cpp.
struct TableRules
{
	void (*MakeLinks)(TPinballTable* table);
	void (*ClearLinks)();
	void (*TableControl)(MessageCode code);
	void (*UnstuckBall)(TBall& ball, int dt);
	// A key pressed: an SDL keycode, or a character of a cheat the menu, the idle credits or a recording types
	void (*Cheat)(int key);
	void (*CheatBumpRank)();
	// The idle credits stop, on a key or a change of game mode
	void (*EndCredits)();

	// The rules of the table pb::SelectDatFile picked
	static const TableRules& Current();
};
