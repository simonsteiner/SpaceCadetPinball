#include "pch.h"
#include "TLightBargraph.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "timer.h"
#include "TPinballTable.h"

// Builds the bargraph and reads its countdown times (attribute 904), two for each light, when the group has them
TLightBargraph::TLightBargraph(TPinballTable* table, int groupIndex) : TLightGroup(table, groupIndex)
{
	TimerTimeArray = nullptr;
	TLightBargraph::Reset();
	if (groupIndex > 0)
	{
		float* floatArr = loader::query_float_attribute(groupIndex, 0, 904);
		if (floatArr)
		{
			auto count = 2 * List.size();
			TimerTimeArray = new float[count];
			if (TimerTimeArray)
			{
				for (auto i = 0u; i < count; ++floatArr)
					TimerTimeArray[i++] = *floatArr;
			}
		}
	}
}

TLightBargraph::~TLightBargraph()
{
	delete[] TimerTimeArray;
}

// Handles the bargraph's messages: TLightGroupToggleSplitIndex sets the level to <value>, two steps per light with
// the top lit light flashing on even ones, and starts that step's timer; TLightGroupGetOnCount returns the level. A
// tilt resets it, Reset and PlayerChanged keep the level per player, and the rest go to TLightGroup
int TLightBargraph::Message(MessageCode code, float value)
{
	// TLightGroup keeps its message field per player itself, but this class handles the player change on its own
	if (pb::BonusTable())
		KeepMessageFieldPerPlayer(code, value);
	switch (code)
	{
	case MessageCode::TLightGroupGetOnCount:
		return TimeIndex;
	case MessageCode::TLightGroupToggleSplitIndex:
		{
			if (TimerBargraph)
			{
				timer::kill(TimerBargraph);
				TimerBargraph = 0;
			}
			auto timeIndex = static_cast<int>(floor(value));
			auto maxCount = static_cast<int>(List.size()) * 2;
			if (timeIndex >= maxCount)
				timeIndex = maxCount - 1;
			if (timeIndex >= 0)
			{
				TLightGroup::Message(MessageCode::TLightGroupToggleSplitIndex, static_cast<float>(timeIndex / 2));
				if (!(timeIndex & 1))
					TLightGroup::Message(MessageCode::TLightGroupStartFlasher, 0.0);
				if (TimerTimeArray)
					TimerBargraph = timer::set(TimerTimeArray[timeIndex], this, BargraphTimerExpired);
				TimeIndex = timeIndex;
			}
			else
			{
				TLightGroup::Message(MessageCode::TLightResetAndTurnOff, 0.0);
				TimeIndex = 0;
			}
			break;
		}
	case MessageCode::SetTiltLock:
		Reset();
		break;
	case MessageCode::PlayerChanged:
		if (TimerBargraph)
		{
			timer::kill(TimerBargraph);
			TimerBargraph = 0;
		}
		PlayerTimerIndexBackup[PinballTable->CurrentPlayer] = TimeIndex;
		Reset();
		TimeIndex = PlayerTimerIndexBackup[static_cast<int>(floor(value))];
		if (TimeIndex)
		{
			TLightBargraph::Message(MessageCode::TLightGroupToggleSplitIndex, static_cast<float>(TimeIndex));
		}
		break;
	case MessageCode::Reset:
		{
			Reset();
			int* playerPtr = PlayerTimerIndexBackup;
			for (auto index = 0; index < PinballTable->PlayerCount; ++index)
			{
				*playerPtr = TimeIndex;

				++playerPtr;
			}
			TLightGroup::Message(MessageCode::Reset, value);
			break;
		}
	default:
		TLightGroup::Message(code, value);
		break;
	}
	return 0;
}

// Stops the countdown timer and sets the level to 0, then resets the light group
void TLightBargraph::Reset()
{
	if (TimerBargraph)
	{
		timer::kill(TimerBargraph);
		TimerBargraph = 0;
	}
	TimeIndex = 0;
	TLightGroup::Reset();
}

// Timer callback that counts the level down one step and tells the rules; at level 0 it turns the lights off and
// tells them the countdown ended
void TLightBargraph::BargraphTimerExpired(int timerId, void* caller)
{
	auto bar = static_cast<TLightBargraph*>(caller);
	bar->TimerBargraph = 0;
	if (bar->TimeIndex)
	{
		bar->Message(MessageCode::TLightGroupToggleSplitIndex, static_cast<float>(bar->TimeIndex - 1));
		control::handler(MessageCode::ControlTimerExpired, bar);
	}
	else
	{
		bar->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		control::handler(MessageCode::TLightGroupCountdownEnded, bar);
	}
}
