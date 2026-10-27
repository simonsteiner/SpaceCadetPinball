#include "pch.h"
#include "control_dragon.h"

#include "control.h"
#include "TBall.h"
#include "TDrain.h"
#include "TFlipper.h"
#include "TKickout.h"
#include "TPinballTable.h"
#include "TPlunger.h"
#include "TSink.h"

// A stand-in until Dragon's Keep's rules are recovered from DRAGON.EXE: it keeps the table playable without them.
// A drained ball is followed by the next, endlessly, and kickouts and sinks give back the balls they catch, which
// only the rules would do otherwise. Holes give them back on their own. A stuck ball is freed as Space Cadet's
// rules free one: away from the flippers and the plunger, it's thrown up, and after 20 tries it's served again.
namespace
{
	TPinballTable* Table = nullptr;

	void StandInControl(MessageCode code, TPinballComponent* caller)
	{
		if (dynamic_cast<TDrain*>(caller))
		{
			if (code == MessageCode::ControlTimerExpired && Table->Plunger)
				Table->Plunger->Message(MessageCode::PlungerStartFeedTimer, 0.0f);
		}
		else if (auto kickout = dynamic_cast<TKickout*>(caller))
		{
			if (code == MessageCode::ControlCollision)
				kickout->Message(MessageCode::TKickoutRestartTimer, kickout->TimerTime1);
		}
		else if (auto sink = dynamic_cast<TSink*>(caller))
		{
			if (code == MessageCode::ControlCollision)
				sink->Message(MessageCode::TSinkResetTimer, sink->TimerTime);
		}
	}

	component_control StandInLink{StandInControl, 0, nullptr};

	void MakeLinks(TPinballTable* table)
	{
		Table = table;
		for (auto component : table->ComponentList)
		{
			if (dynamic_cast<TDrain*>(component) || dynamic_cast<TKickout*>(component) ||
				dynamic_cast<TSink*>(component))
				component->Control = &StandInLink;
		}
	}

	bool InBounds(const TBall& ball, const TCollisionComponent& component)
	{
		auto offset = Table->CollisionCompOffset / 2.0f;
		return ball.ActiveFlag && ball.Position.X >= component.AABB.XMin - offset &&
			   ball.Position.X <= component.AABB.XMax + offset && ball.Position.Y >= component.AABB.YMin - offset &&
			   ball.Position.Y <= component.AABB.YMax + offset;
	}

	void UnstuckBall(TBall& ball, int)
	{
		if (Table->Plunger && InBounds(ball, *Table->Plunger))
			return;
		for (auto flippers : {&Table->FlippersL, &Table->FlippersR})
			for (auto flipper : *flippers)
				if (InBounds(ball, *flipper))
					return;

		if (ball.StuckCounter <= 20)
		{
			vector3 throwDir{0.0f, -1.0f, 0.0f};
			ball.throw_ball(&throwDir, 90.0f, 1.0f, 0.0f);
		}
		else if (Table->Plunger)
		{
			ball.Disable();
			Table->MultiballCount--;
			Table->Plunger->Message(MessageCode::PlungerRelaunchBall, 0);
		}
	}

	void ClearLinks()
	{
		Table = nullptr;
	}
} // namespace

const TableRules control_dragon::Rules{
	MakeLinks, ClearLinks, [](MessageCode) {}, UnstuckBall, [](char) {}, [] {},
};
