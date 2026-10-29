#include "pch.h"
#include "TMagnet.h"

#include "control.h"
#include "TBall.h"
#include "timer.h"

// TKickout's fields as the magnet uses them: BallCaputeredFlag keeps the field off, Timer turns it back on
TMagnet::TMagnet(TPinballTable* table, int groupIndex) : TKickout(table, groupIndex, true)
{
	TimerTime2 = 1.0f;
	BallCaputeredFlag = 0;
}

int TMagnet::Message(MessageCode code, float value)
{
	using enum MessageCode;
	KeepMessageFieldPerPlayer(code, value);
	switch (code)
	{
	case TMagnetEnable:
		ActiveFlag = 1;
		break;
	case TMagnetDisable:
	case SetTiltLock:
	case PlayerChanged:
	case Reset:
		if (Timer)
			timer::kill(Timer);
		Timer = 0;
		ActiveFlag = 0;
		BallCaputeredFlag = 0;
		break;
	default:
		break;
	}
	return 0;
}

void TMagnet::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance -= distance;
	ball->not_again(edge);

	BallCaputeredFlag = 1;
	Timer = timer::set(TimerTime2, this, FieldOffTimerExpired);
	control::handler(MessageCode::ControlCollision, this);
}

void TMagnet::FieldOffTimerExpired(int, void* caller)
{
	auto magnet = static_cast<TMagnet*>(caller);
	magnet->BallCaputeredFlag = 0;
	magnet->Timer = 0;
	control::handler(MessageCode::ControlTimerExpired, magnet);
}
