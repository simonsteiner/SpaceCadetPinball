#include "pch.h"
#include "control_skulduggery.h"

#include "control.h"
#include "TDrain.h"
#include "TKickout.h"
#include "TMagnet.h"
#include "TPinballTable.h"
#include "TPlunger.h"
#include "TSink.h"

// A stand-in until Skulduggery's rules are recovered from PIRATES.EXE (F6): it keeps the table playable without them,
// as Dragon's Keep's did until its rules came. A drained ball is followed by the next, endlessly, and kickouts and
// sinks give back the balls they catch, which only the rules would do otherwise. Holes give them back on their own,
// and the magnets stay off, as only the rules switch them on. A stuck ball is freed as Space Cadet's rules free one:
// away from the flippers and the launcher, it's thrown up, and after 20 tries it's served again.
namespace
{
	TPinballTable* Table = nullptr;

	void StandInControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (dynamic_cast<TDrain*>(caller))
		{
			if (code == ControlTimerExpired && Table->Plunger)
				Table->Plunger->Message(PlungerStartFeedTimer, 0.0f);
		}
		else if (auto kickout = dynamic_cast<TKickout*>(caller))
		{
			if (code == ControlCollision)
				kickout->Message(TKickoutRestartTimer, kickout->TimerTime1);
		}
		else if (auto sink = dynamic_cast<TSink*>(caller))
		{
			if (code == ControlCollision)
				sink->Message(TSinkResetTimer, sink->TimerTime);
		}
	}

	component_control StandInLink{StandInControl, 0, nullptr};

	void MakeLinks(TPinballTable* table)
	{
		Table = table;
		for (auto component : table->ComponentList)
		{
			auto kickout = dynamic_cast<TKickout*>(component) && !dynamic_cast<TMagnet*>(component);
			if (kickout || dynamic_cast<TDrain*>(component) || dynamic_cast<TSink*>(component))
				component->Control = &StandInLink;
		}
	}

	void ClearLinks()
	{
		Table = nullptr;
	}

	void UnstuckBall(TBall& ball, int)
	{
		TableRules::UnstuckBonusTableBall(*Table, ball);
	}
} // namespace

const TableRules control_skulduggery::Rules{
	MakeLinks,
	ClearLinks,
	[](MessageCode) { /* no table messages without the rules */ },
	UnstuckBall,
	[](int) { /* no cheats without the rules */ },
	[] { /* no rank to bump without the rules */ },
	[] { /* no credits to end without the rules */ },
	[](TableView&) { /* nothing of its own to show without the rules */ },
	[](bool) { return false; },
};
