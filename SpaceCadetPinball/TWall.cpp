#include "pch.h"
#include "TWall.h"


#include "control.h"
#include "pb.h"
#include "render.h"
#include "timer.h"

// Builds the wall with its sprite, if it has one, hidden
TWall::TWall(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	if (RenderSprite)
		SpriteSet(-1);
}

// Handles Reset, which ends a running hit flash at once; on the bonus tables also keeps the message field per player
int TWall::Message(MessageCode code, float value)
{
	if (pb::BonusTable())
		KeepMessageFieldPerPlayer(code, value);
	if (code == MessageCode::Reset && Timer)
	{
		timer::kill(Timer);
		TimerExpired(Timer, this);
	}
	return 0;
}

// Bounces the ball; a hit harder than the threshold shows the wall's sprite, if it has one, for 0.1 s and tells
// the rules
void TWall::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	if (DefaultCollision(ball, nextPosition, direction))
	{
		if (ListBitmap)
		{
			SpriteSet(0);
			Timer = timer::set(0.1f, this, TimerExpired);
		}
		control::handler(MessageCode::ControlCollision, this);
	}
}

void TWall::TimerExpired(int timerId, void* caller)
{
	auto wall = static_cast<TWall*>(caller);
	wall->SpriteSet(-1);
	wall->Timer = 0;
	wall->MessageField = 0;
}
