#include "pch.h"
#include "TKickout.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "TBall.h"
#include "TCircle.h"
#include "timer.h"
#include "TPinballTable.h"
#include "TTableLayer.h"

TKickout::TKickout(TPinballTable* table, int groupIndex, bool someFlag): TCollisionComponent(
	table, groupIndex, false)
{
	visualStruct visual{};
	circle_type circle{};

	NotSomeFlag = !someFlag;
	if (!someFlag)
		ActiveFlag = 0;
	TimerTime1 = 1.5;
	TimerTime2 = 0.05f;
	MessageField = 0;
	Timer = 0;
	BallCaputeredFlag = 0;
	FieldMult = *loader::query_float_attribute(groupIndex, 0, 305);
	loader::query_visual(groupIndex, 0, &visual);
	SoftHitSoundId = visual.SoftHitSoundId;
	HardHitSoundId = visual.Kicker.HardHitSoundId;

	Circle.Center.X = visual.FloatArr[0];
	Circle.Center.Y = visual.FloatArr[1];
	// Skulduggery's magnets have neither attribute 306 nor 408; CADET.EXE takes 0.1 and the ball's radius for them
	auto radiusFactor = loader::query_float_attribute(groupIndex, 0, 306);
	Circle.RadiusSq = (radiusFactor ? *radiusFactor : 0.1f) * visual.FloatArr[2];
	if (Circle.RadiusSq == 0.0f)
		Circle.RadiusSq = 0.001f;
	auto tCircle = new TCircle(this, &ActiveFlag, visual.CollisionGroup,
	                           reinterpret_cast<vector3*>(visual.FloatArr), Circle.RadiusSq);
	if (tCircle)
	{
		tCircle->place_in_grid(&AABB);
		EdgeList.push_back(tCircle);
	}

	Circle.RadiusSq = visual.FloatArr[2] * visual.FloatArr[2];
	if (auto zAttr = loader::query_float_attribute(groupIndex, 0, 408); !zAttr)
		CollisionBallSetZ = table->CollisionCompOffset;
	else
		CollisionBallSetZ = pb::FullTiltMode && !pb::FullTiltDemoMode ? zAttr[3] : zAttr[2];
	ThrowSpeedMult2 = visual.Kicker.ThrowBallMult * 0.01f;
	BallThrowDirection = visual.Kicker.ThrowBallDirection;
	ThrowAngleMult = visual.Kicker.ThrowBallAngleMult;
	ThrowSpeedMult1 = visual.Kicker.Boost;

	circle.RadiusSq = Circle.RadiusSq;
	circle.Center.X = Circle.Center.X;
	circle.Center.Y = Circle.Center.Y;
	Field.ActiveFlag = &ActiveFlag;
	Field.CollisionComp = this;
	Field.CollisionGroup = visual.CollisionGroup;
	TTableLayer::edges_insert_circle(&circle, nullptr, &Field);
}

int TKickout::Message(MessageCode code, float value)
{
	if (pb::BonusTable())
		KeepMessageFieldPerPlayer(code, value);
	switch (code)
	{
	case MessageCode::TKickoutEnable:
		// CADET.EXE's (0x4163a0), the bonus tables only
		if (pb::BonusTable())
			ActiveFlag = 1;
		break;
	case MessageCode::TKickoutDisable:
		// CADET.EXE's: off, and a ball it holds goes out at once
		if (!pb::BonusTable())
			break;
		ActiveFlag = 0;
		if (BallCaputeredFlag)
		{
			if (Timer)
				timer::kill(Timer);
			TimerExpired(0, this);
			if (Timer)
				timer::kill(Timer);
			Timer = 0;
		}
		break;
	case MessageCode::TKickoutRestartTimer:
		if (BallCaputeredFlag)
		{
			if (value < 0.0f)
				value = TimerTime1;
			Timer = timer::set(value, this, TimerExpired);
		}
		break;
	case MessageCode::SetTiltLock:
		if (NotSomeFlag)
			ActiveFlag = 0;
		break;
	case MessageCode::Reset:
		if (BallCaputeredFlag)
		{
			if (Timer)
				timer::kill(Timer);
			TimerExpired(0, this);
		}
		if (NotSomeFlag)
			ActiveFlag = 0;
		break;
	default:
		break;
	}

	return 0;
}

void TKickout::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	if (!BallCaputeredFlag)
	{
		Ball = ball;
		Threshold = 1000000000.0;
		BallCaputeredFlag = 1;
		ball->CollisionComp = this;
		ball->Position.X = Circle.Center.X;
		ball->Position.Y = Circle.Center.Y;
		OriginalBallZ = ball->Position.Z;
		ball->Position.Z = CollisionBallSetZ;
		ball->CollisionDisabledFlag = true;
		if (PinballTable->TiltLockFlag)
		{
			Message(MessageCode::TKickoutRestartTimer, 0.1f);
		}
		else
		{
			loader::play_sound(SoftHitSoundId, ball, "TKickout1");
			control::handler(MessageCode::ControlCollision, this);
		}
	}
	else
	{
		ball->Position.X = nextPosition->X;
		ball->Position.Y = nextPosition->Y;
		ball->RayMaxDistance -= distance;
		ball->not_again(edge);
	}
}

int TKickout::FieldEffect(TBall* ball, vector2* dstVec)
{
	vector2 direction{};

	if (BallCaputeredFlag)
		return 0;
	direction.X = Circle.Center.X - ball->Position.X;
	direction.Y = Circle.Center.Y - ball->Position.Y;
	if (direction.Y * direction.Y + direction.X * direction.X > Circle.RadiusSq)
		return 0;
	maths::normalize_2d(direction);
	dstVec->X = direction.X * FieldMult - ball->Direction.X * ball->Speed;
	dstVec->Y = direction.Y * FieldMult - ball->Direction.Y * ball->Speed;
	return 1;
}

void TKickout::TimerExpired(int timerId, void* caller)
{
	auto kick = static_cast<TKickout*>(caller);
	if (kick->BallCaputeredFlag)
	{
		kick->BallCaputeredFlag = 0;
		kick->Timer = timer::set(kick->TimerTime2, kick, ResetTimerExpired);
		if (kick->Ball)
		{		
			loader::play_sound(kick->HardHitSoundId, kick->Ball, "TKickout2");
			kick->Ball->Position.Z = kick->OriginalBallZ;
			kick->Ball->throw_ball(&kick->BallThrowDirection, kick->ThrowAngleMult, kick->ThrowSpeedMult1,
			                  kick->ThrowSpeedMult2);
			kick->ActiveFlag = 0;
			kick->Ball = nullptr;
		}
	}
}

void TKickout::ResetTimerExpired(int timerId, void* caller)
{
	auto kick = static_cast<TKickout*>(caller);
	if (!kick->NotSomeFlag)
		kick->ActiveFlag = 1;
	kick->Timer = 0;
}
