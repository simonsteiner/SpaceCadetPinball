#include "pch.h"
#include "TLightRollover.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "render.h"
#include "TBall.h"
#include "timer.h"
#include "TPinballTable.h"

TLightRollover::TLightRollover(TPinballTable* table, int groupIndex) : TRollover(table, groupIndex, false)
{
	RolloverFlag = 0;
	Timer = 0;
	SpriteSet(-1);
	build_walls(groupIndex);
	FloatArr = *loader::required_float_attribute(groupIndex, 0, 407);
}

// Handles Reset, which rearms the rollover, stops the timer that hides its sprite and hides it; on the bonus tables
// also keeps the message field per player
int TLightRollover::Message(MessageCode code, float value)
{
	if (pb::BonusTable())
		KeepMessageFieldPerPlayer(code, value);
	if (code == MessageCode::Reset)
	{
		ActiveFlag = 1;
		RolloverFlag = 0;
		if (Timer)
			timer::kill(Timer);
		Timer = 0;
		SpriteSet(-1);
	}
	return 0;
}

// Lets the ball pass. With RolloverFlag clear it shows the sprite, plays the sound and tells the rules; with it set
// it disarms the rollover for 0.1 s and starts the delay that hides the sprite. A tilted table does neither
void TLightRollover::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                               TEdgeSegment* edge)
{
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance -= distance;
	ball->not_again(edge);
	if (!PinballTable->TiltLockFlag)
	{
		if (RolloverFlag)
		{
			timer::set(0.1f, this, TimerExpired);
			ActiveFlag = 0;
			RolloverFlag = RolloverFlag == 0;
			if (Timer == 0)
				Timer = timer::set(FloatArr, this, delay_expired);
		}
		else
		{
			loader::play_sound(SoftHitSoundId, this, "TLightRollover");
			control::handler(MessageCode::ControlCollision, this);
			RolloverFlag = RolloverFlag == 0;
			SpriteSet(0);
		}
	}
}

void TLightRollover::delay_expired(int timerId, void* caller)
{
	auto roll = static_cast<TLightRollover*>(caller);
	roll->SpriteSet(-1);
	roll->Timer = 0;
}
