#include "pch.h"
#include "TableRules.h"

#include "control.h"
#include "control_dragon.h"
#include "control_skulduggery.h"
#include "pb.h"
#include "TTextBox.h"

namespace
{
	const TableRules SpaceCadetRules{
		control::make_links,
		control::ClearLinks,
		control::table_control_handler,
		control::UnstuckBall,
		[](int key) { control::pbctrl_bdoor_controller(static_cast<char>(key)); },
		control::cheat_bump_rank,
		[]
		{
			if (pb::MissTextBox)
				pb::MissTextBox->Clear(true);
		},
		[](TableView&) { /* TableView reads Space Cadet's mission and rank itself */ },
		[](bool) { return false; },
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
