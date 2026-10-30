#include "pch.h"
#include "TKickback.h"


#include "control.h"
#include "loader.h"
#include "maths.h"
#include "pb.h"
#include "render.h"
#include "timer.h"
#include "TPinballTable.h"

TKickback::TKickback(TPinballTable* table, int groupIndex): TCollisionComponent(table, groupIndex, true)
{
	MessageField = 0;
	Timer = 0;
	KickActiveFlag = 0;
	TimerTime = 0.7f;
	TimerTime2 = 0.1f;
	Threshold = 1000000000.0f;
}

int TKickback::Message(MessageCode code, float value)
{
	if (pb::BonusTable())
	{
		KeepMessageFieldPerPlayer(code, value);
		// CADET.EXE (0x415d70): off stops arming; on, a tilt and Reset turn it back on and let go of a kick
		if (code == MessageCode::TKickbackDisable)
		{
			KickDisabled = true;
			return 0;
		}
		if (code == MessageCode::TKickbackEnable || code == MessageCode::SetTiltLock || code == MessageCode::Reset)
			KickDisabled = false;
		if (code == MessageCode::TKickbackEnable)
		{
			if (Timer)
				timer::kill(Timer);
			SpriteSet(0);
			Timer = 0;
			KickActiveFlag = 0;
			Threshold = 1000000000.0;
			return 0;
		}
	}
	if ((code == MessageCode::SetTiltLock || code == MessageCode::Reset) && Timer)
	{
		timer::kill(Timer);
		SpriteSet(-1);
		Timer = 0;
		KickActiveFlag = 0;
		Threshold = 1000000000.0;
	}
	return 0;
}

void TKickback::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                          TEdgeSegment* edge)
{
	if (PinballTable->TiltLockFlag)
	{
		maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, 0.0,
		                       Boost);
	}
	else
	{
		if (!KickActiveFlag && !KickDisabled)
		{
			Threshold = 1000000000.0;
			KickActiveFlag = 1;
			Timer = timer::set(TimerTime, this, TimerExpired);
		}
		if (DefaultCollision(ball, nextPosition, direction))
			KickActiveFlag = 0;
	}
}

void TKickback::TimerExpired(int timerId, void* caller)
{
	auto kick = static_cast<TKickback*>(caller);

	if (kick->KickActiveFlag)
	{
		kick->Threshold = 0.0;
		kick->Timer = timer::set(kick->TimerTime2, kick, TimerExpired);
		loader::play_sound(kick->HardHitSoundId, kick, "TKickback");
		kick->SpriteSet(1);
	}
	else
	{
		kick->SpriteSet(0);
		kick->Timer = 0;
		control::handler(MessageCode::ControlTimerExpired, kick);
	}
}
