#include "pch.h"
#include "TTimer.h"

#include "control.h"
#include "pb.h"
#include "timer.h"

TTimer::TTimer(TPinballTable* table, int groupIndex) : TPinballComponent(table, groupIndex, true)
{
	Timer = 0;
}

int TTimer::Message(MessageCode code, float value)
{
	if (pb::BonusTable())
		KeepMessageFieldPerPlayer(code, value);
	switch (code)
	{
	case MessageCode::TTimerResetTimer:
		if (Timer)
			timer::kill(Timer);
		Timer = 0;
		// Full Tilt's TTimer (CADET.EXE) only stops for a negative time; Space Cadet's rules never use this timer
		if (value >= 0.0f)
			Timer = timer::set(value, this, TimerExpired);
		break;
	case MessageCode::SetTiltLock:
	case MessageCode::GameOver:
	case MessageCode::Reset:
	case MessageCode::PlayerChanged: // CADET.EXE stops it for the next player too
		if (Timer)
		{
			timer::kill(Timer);
			Timer = 0;
		}
		break;
	default: break;
	}
	return 0;
}


void TTimer::TimerExpired(int timerId, void* caller)
{
	auto timer = static_cast<TTimer*>(caller);
	timer->Timer = 0;
	control::handler(MessageCode::ControlTimerExpired, timer);
}
