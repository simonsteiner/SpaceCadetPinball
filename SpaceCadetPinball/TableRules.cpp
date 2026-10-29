#include "pch.h"
#include "TableRules.h"

#include "control.h"
#include "control_dragon.h"
#include "control_skulduggery.h"
#include "pb.h"
#include "TBall.h"
#include "TFlipper.h"
#include "TPinballTable.h"
#include "TPlunger.h"
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

	bool InBounds(const TPinballTable& table, const TBall& ball, const TCollisionComponent& component)
	{
		auto offset = table.CollisionCompOffset / 2.0f;
		return ball.ActiveFlag && ball.Position.X >= component.AABB.XMin - offset &&
			   ball.Position.X <= component.AABB.XMax + offset && ball.Position.Y >= component.AABB.YMin - offset &&
			   ball.Position.Y <= component.AABB.YMax + offset;
	}
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

void TableRules::UnstuckBonusTableBall(TPinballTable& table, TBall& ball)
{
	if (table.Plunger && InBounds(table, ball, *table.Plunger))
		return;
	for (auto flippers : {&table.FlippersL, &table.FlippersR})
		for (auto flipper : *flippers)
			if (InBounds(table, ball, *flipper))
				return;

	if (ball.StuckCounter <= 20)
	{
		vector3 throwDir{0.0f, -1.0f, 0.0f};
		ball.throw_ball(&throwDir, 90.0f, 1.0f, 0.0f);
	}
	else if (table.Plunger)
	{
		ball.Disable();
		table.MultiballCount--;
		table.Plunger->Message(MessageCode::PlungerRelaunchBall, 0);
	}
}
