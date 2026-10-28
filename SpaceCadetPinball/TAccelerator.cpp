#include "pch.h"
#include "TAccelerator.h"

#include "control.h"
#include "loader.h"
#include "TBall.h"
#include "TPinballTable.h"
#include "TTableLayer.h"

TAccelerator::TAccelerator(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	visualStruct visual{};
	loader::query_visual(groupIndex, 0, &visual);
	ThrowDirection = visual.Kicker.ThrowBallDirection;
	ThrowAngleMult = visual.Kicker.ThrowBallAngleMult;
	ThrowSpeedMult1 = visual.Kicker.Boost;
	ThrowSpeedMult2 = static_cast<float>(visual.Kicker.ThrowBallMult / 100.0);
	HitSoundIndex = visual.SoundIndex4;

	// The field is a circle around the middle of the wall's bounds; its radius is their larger side divided by 1.8
	auto radius =
		std::max(static_cast<float>((AABB.YMax - AABB.YMin) / 1.8), static_cast<float>((AABB.XMax - AABB.XMin) / 1.8));
	circle_type circle{};
	circle.Center = {(AABB.XMin + AABB.XMax) / 2.0f, (AABB.YMax + AABB.YMin) / 2.0f};
	circle.RadiusSq = radius * radius;
	Field.ActiveFlag = &ActiveFlag;
	Field.CollisionGroup = visual.CollisionGroup;
	Field.CollisionComp = this;
	TTableLayer::edges_insert_circle(&circle, nullptr, &Field);
}

void TAccelerator::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	ball->not_again(edge);
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance -= distance;

	if (!PinballTable->TiltLockFlag)
	{
		Ball = ball;
		ball->CollisionDisabledFlag = true;
		loader::play_sound(HitSoundIndex, this, "TAccelerator");
		control::handler(MessageCode::ControlCollision, this);
	}
}

int TAccelerator::FieldEffect(TBall* ball, vector2* vecDst)
{
	if (Ball != ball)
		return 0;
	Ball = nullptr;
	ball->throw_ball(&ThrowDirection, ThrowAngleMult, ThrowSpeedMult1, ThrowSpeedMult2);
	return 0;
}
