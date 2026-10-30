#pragma once

class TPinballTable;
class TBall;
class TCollisionComponent;
struct TableView;
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
	// Copies the table's own state into the view after each update, for the fields only these rules know
	void (*FillView)(TableView& view);
	// Whether the ball being made takes its next frame's second bitmap, as Dragon's Keep's PB cheat asks;
	// available is whether the frame has one. A frame without one may change the rules' state: Dragon's Keep turns
	// PB off, so that frame and the ones after it take their first bitmap, as DRAGON.EXE does.
	bool (*AltBallFrame)(bool available);

	// The rules of the table pb::SelectDatFile picked
	static const TableRules& Current();
	// A stand-in for the bonus tables: a stuck ball is freed as Space Cadet's rules free one. Away from the flippers,
	// the plunger and the components in resting it's thrown up, and after 20 tries it's served again.
	static void UnstuckBonusTableBall(TPinballTable& table, TBall& ball,
									  std::initializer_list<const TCollisionComponent*> resting = {});
};
