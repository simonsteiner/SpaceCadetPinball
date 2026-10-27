#pragma once
#include "TableRules.h"

// Dragon's Keep's table rules, to be recovered from DRAGON.EXE (Phase F). Until then a stand-in keeps the table
// playable: it serves the next ball after a drain and has kickouts and sinks give back the balls they catch.
class control_dragon
{
public:
	static const TableRules Rules;
};
