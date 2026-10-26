#include "pch.h"
#include "control_dragon.h"

const TableRules control_dragon::Rules{
	[](TPinballTable*) {}, [] {}, [](MessageCode) {}, [](TBall&, int) {}, [](char) {}, [] {},
};
