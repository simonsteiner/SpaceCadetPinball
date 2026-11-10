#include "pch.h"
#include "TPopupTarget.h"


#include "control.h"
#include "loader.h"
#include "render.h"
#include "timer.h"
#include "TPinballTable.h"

TPopupTarget::TPopupTarget(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	Timer = 0;
	TimerTime = *loader::required_float_attribute(groupIndex, 0, 407);
}

// Handles the target's messages: disable drops it, enable raises it again after its delay (attribute 407),
// PlayerChanged restores the next player's state and Reset raises it at once
int TPopupTarget::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::TPopupTargetDisable:
		ActiveFlag = 0;
		SpriteSet(-1);
		break;
	case MessageCode::TPopupTargetEnable:
		Timer = timer::set(TimerTime, this, TimerExpired);
		break;
	case MessageCode::PlayerChanged:
		PlayerMessagefieldBackup[PinballTable->CurrentPlayer] = MessageField;
		MessageField = PlayerMessagefieldBackup[static_cast<int>(floor(value))];
		TPopupTarget::Message(MessageField ? MessageCode::TPopupTargetDisable : MessageCode::TPopupTargetEnable, 0.0);
		break;
	case MessageCode::Reset:
	{
			MessageField = 0;
			int* playerPtr = PlayerMessagefieldBackup;
			for (auto index = 0; index < PinballTable->PlayerCount; ++index)
			{
				*playerPtr = 0;
				++playerPtr;
			}

			if (Timer)
				timer::kill(Timer);
			TimerExpired(0, this);
			break;
		}
	default:
		break;
	}
	return 0;
}

// Bounces the ball; a hit harder than the threshold drops the target, plays its sound and tells the rules, unless
// the table is tilted
void TPopupTarget::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                             TEdgeSegment* edge)
{
	if (PinballTable->TiltLockFlag)
	{
		maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, 1000000000.0, 0.0);
	}
	else if (maths::basic_collision(
		ball,
		nextPosition,
		direction,
		Elasticity,
		Smoothness,
		Threshold,
		Boost) > Threshold)
	{
		if (HardHitSoundId)
			loader::play_sound(HardHitSoundId, this, "TPopupTarget1");
		Message(MessageCode::TPopupTargetDisable, 0.0);
		control::handler(MessageCode::ControlCollision, this);
	}
}

// Raises the target: active and visible again, with a sound when a timer rather than Reset called it
void TPopupTarget::TimerExpired(int timerId, void* caller)
{
	auto target = static_cast<TPopupTarget*>(caller);
	target->Timer = 0;
	target->ActiveFlag = 1;
	target->SpriteSet(0);
	if (timerId)
	{
		if (target->SoftHitSoundId)
			loader::play_sound(target->SoftHitSoundId, target, "TPopupTarget2");
	}
}
