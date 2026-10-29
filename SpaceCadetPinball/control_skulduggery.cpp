#include "pch.h"
#include "control_skulduggery.h"

const TableRules control_skulduggery::Rules{
	[](TPinballTable*) {}, [] {}, [](MessageCode) {}, [](TBall&, int) {}, [](int) {}, [] {}, [] {}, [](TableView&) {},
};
