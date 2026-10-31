#include "pch.h"
#include "THole.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "TBall.h"
#include "timer.h"
#include "TPinballTable.h"
#include "TTableLayer.h"

THole::THole(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	visualStruct visual{};
	circle_type circle{};

	Unknown4 = 0.050000001f;
	MessageField = 0;
	Timer = 0;
	BallCapturedFlag = false;
	Unknown3 = loader::query_float_attribute(groupIndex, 0, 407, 0.25f);
	GravityMult = loader::query_float_attribute(groupIndex, 0, 701, 0.2f);
	if (pb::BonusTable())
	{
		// CADET.EXE reads the gravity factor from attribute 703 and the throw from 1306
		GravityMult = loader::query_float_attribute(groupIndex, 0, 703, 0.5f);
		EjectSpeed = loader::query_float_attribute(groupIndex, 0, 1306, 0.0f);
	}
	GravityPull = *loader::required_float_attribute(groupIndex, 0, 305);

	loader::query_visual(groupIndex, 0, &visual);
	Circle.Center.X = visual.FloatArr[0];
	Circle.Center.Y = visual.FloatArr[1];
	Circle.RadiusSq = *loader::required_float_attribute(groupIndex, 0, 306) * visual.FloatArr[2];
	if (Circle.RadiusSq == 0.0f)
		Circle.RadiusSq = 0.001f;

	auto tCircle = new TCircle(this, &ActiveFlag, visual.CollisionGroup,
	                           reinterpret_cast<vector3*>(visual.FloatArr),
	                           Circle.RadiusSq);
	tCircle->place_in_grid(&AABB);
	EdgeList.push_back(tCircle);

	ZSetValue = loader::required_float_attribute(groupIndex, 0, 408)[2];
	CollisionMask = static_cast<int>(floor(*loader::required_float_attribute(groupIndex, 0, 1304)));

	// CollisionMask difference: 3DPB - value given as is, FT: mask = 1 << value
	if (pb::FullTiltMode)
		CollisionMask = 1 << CollisionMask;

	Circle.RadiusSq = visual.FloatArr[2] * visual.FloatArr[2];
	circle.RadiusSq = Circle.RadiusSq;
	circle.Center.X = Circle.Center.X;
	circle.Center.Y = Circle.Center.Y;

	Field.ActiveFlag = &ActiveFlag;
	Field.CollisionComp = this;
	Field.CollisionGroup = visual.CollisionGroup;
	TTableLayer::edges_insert_circle(&circle, nullptr, &Field);
}

int THole::Message(MessageCode code, float value)
{
	if (pb::BonusTable())
	{
		BonusTableMessage(code, value);
		return 0;
	}
	if (code == MessageCode::Reset && BallCapturedFlag)
	{
		if (Timer)
			timer::kill(Timer);
		Timer = 0;
		BallCapturedSecondStage = true;
	}
	return 0;
}

void THole::BonusTableMessage(MessageCode code, float value)
{
	using enum MessageCode;
	KeepMessageFieldPerPlayer(code, value);
	switch (code)
	{
	case THoleRelease:
		ReleaseAfter(value);
		break;
	case SetTiltLock:
		ReleaseAfter(-1.0f);
		break;
	case PlayerChanged:
	case Reset:
		if (RetryTimer)
			timer::kill(RetryTimer);
		RetryTimer = 0;
		if (BallCapturedFlag)
			ReleaseAfter(-1.0f);
		break;
	default:
		break;
	}
}

void THole::ReleaseAfter(float time)
{
	if (Timer)
		timer::kill(Timer);
	Timer = 0;
	if (time <= 0.0f)
		BallCapturedSecondStage = true;
	else
		Timer = timer::set(time, this, TimerExpired);
}

void THole::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	if (!BallCapturedFlag)
	{
		BallCapturedSecondStage = false;
		Threshold = 1e9f;
		BallCapturedFlag = true;
		ball->CollisionComp = this;
		ball->Position.X = Circle.Center.X;
		ball->Position.Y = Circle.Center.Y;
		ball->Direction.Z = 0.0;
		ball->CollisionDisabledFlag = true;

		if (pb::BonusTable())
		{
			// CADET.EXE takes the time from attribute 407; a negative time keeps the ball until the rules let it go
			CapturedBall = ball;
			ball->Direction.Z = EjectSpeed;
			Timer = Unknown3 >= 0.0f ? timer::set(Unknown3, this, TimerExpired) : 0;
		}
		else
		{
			// Ramp hole has no delay in FT.
			auto captureTime = pb::FullTiltMode ? 0 : 0.5f;
			Timer = timer::set(captureTime, this, TimerExpired);
		}

		if (!PinballTable->TiltLockFlag)
		{
			loader::play_sound(HardHitSoundId, ball, "THole1");
			control::handler(MessageCode::ControlBallCaptured, this);
		}
	}
	else
	{
		DefaultCollision(ball, nextPosition, direction);
	}
}

int THole::FieldEffect(TBall* ball, vector2* vecDst)
{
	int result;
	vector2 direction{};

	if (BallCapturedFlag)
	{
		if (BallCapturedSecondStage && pb::BonusTable())
		{
			// CADET.EXE (0x416ab0): thrown up or dropped until the ball reaches the release height
			if (EjectSpeed > 0.0f)
				ball->Direction.Z = EjectSpeed;
			else
				ball->Direction.Z -= PinballTable->GravityDirVectMult * GravityMult * ball->TimeDelta;
			ball->Position.Z += ball->Direction.Z;
			ball->Direction.X = 0.0f;
			ball->Direction.Y = 0.0f;
			if (EjectSpeed > 0.0f ? ball->Position.Z >= ZSetValue : ball->Position.Z <= ZSetValue)
			{
				ball->Direction.Z = 0.0f;
				ball->Speed = 0.0f;
				FinishRelease(ball);
			}
		}
		else if (BallCapturedSecondStage)
		{
			ball->Direction.Z -= PinballTable->GravityDirVectMult * ball->TimeDelta * GravityMult;
			ball->Position.Z += ball->Direction.Z;
			if (ball->Position.Z <= ZSetValue)
			{
				BallCapturedFlag = false;
				BallCapturedSecondStage = false;
				ball->Position.Z = ZSetValue;
				ball->CollisionMask = CollisionMask;
				ball->CollisionComp = nullptr;
				ball->Direction = {};
				ball->Speed = 0.0f;
				loader::play_sound(SoftHitSoundId, ball, "THole2");
				control::handler(MessageCode::ControlBallReleased, this);
			}
		}
		result = 0;
	}
	else
	{
		direction.X = Circle.Center.X - ball->Position.X;
		direction.Y = Circle.Center.Y - ball->Position.Y;
		if (direction.X * direction.X + direction.Y * direction.Y <= Circle.RadiusSq)
		{
			maths::normalize_2d(direction);
			vecDst->X = direction.X * GravityPull - ball->Direction.X * ball->Speed;
			vecDst->Y = direction.Y * GravityPull - ball->Direction.Y * ball->Speed;
			result = 1;
		}
		else
		{
			result = 0;
		}
	}
	return result;
}

void THole::TimerExpired(int timerId, void* caller)
{
	auto hole = static_cast<THole*>(caller);
	hole->Timer = 0;
	hole->BallCapturedSecondStage = true;
}

void THole::FinishRelease(TBall* ball)
{
	if (RetryTimer)
		timer::kill(RetryTimer);
	RetryTimer = 0;
	if (!BallCapturedFlag)
		return;

	// Another ball on the release layer within two ball widths keeps this one in, to be tried again in 0.5 s
	auto margin = PinballTable->CollisionCompOffset * 2.0f;
	for (auto other : PinballTable->BallList)
	{
		if (other->ActiveFlag && (other->CollisionMask & CollisionMask) &&
			other->Position.X >= Circle.Center.X - margin && other->Position.X <= Circle.Center.X + margin &&
			other->Position.Y >= Circle.Center.Y - margin && other->Position.Y <= Circle.Center.Y + margin)
		{
			RetryTimer = timer::set(0.5f, this, RetryRelease);
			return;
		}
	}

	BallCapturedFlag = false;
	BallCapturedSecondStage = false;
	CapturedBall = nullptr;
	ball->Position.Z = ZSetValue;
	ball->CollisionDisabledFlag = true;
	ball->CollisionMask = CollisionMask;
	ball->CollisionComp = nullptr;
	loader::play_sound(SoftHitSoundId, ball, "THole2");
	control::handler(MessageCode::ControlBallReleased, this);
}

void THole::RetryRelease(int, void* caller)
{
	auto hole = static_cast<THole*>(caller);
	hole->RetryTimer = 0;
	if (hole->CapturedBall)
		hole->FinishRelease(hole->CapturedBall);
}
