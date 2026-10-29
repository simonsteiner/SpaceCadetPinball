#include "pch.h"
#include "control_skulduggery.h"

// Skulduggery has no rules until F6 ports them from PIRATES.EXE, so every entry point does nothing
const TableRules control_skulduggery::Rules{
	[](TPinballTable*) { /* no rules to link */ },
	[] { /* no links to clear */ },
	[](MessageCode) { /* no table messages to take */ },
	[](TBall&, int) { /* no stuck ball to free */ },
	[](int) { /* no cheats */ },
	[] { /* no rank to bump */ },
	[] { /* no credits to end */ },
	[](TableView&) { /* nothing of its own to show */ },
	[](bool) { return false; },
};
