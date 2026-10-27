#include "pch.h"
#include "TableRules.h"

#include "control.h"
#include "control_dragon.h"
#include "control_skulduggery.h"
#include "pb.h"

namespace
{
	const TableRules SpaceCadetRules{
		control::make_links,
		control::ClearLinks,
		control::table_control_handler,
		control::UnstuckBall,
		control::pbctrl_bdoor_controller,
		control::cheat_bump_rank,
	};
}

const TableRules& TableRules::Current()
{
	switch (pb::Table)
	{
	case Tables::Skulduggery:
		return control_skulduggery::Rules;
	case Tables::DragonsKeep:
		return control_dragon::Rules;
	default:
		return SpaceCadetRules;
	}
}
