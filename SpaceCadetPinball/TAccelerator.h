#pragma once

#include "TCollisionComponent.h"
#include "TEdgeManager.h"

// Object type 1036, Full Tilt's accelerators (a_accl*), recovered from CADET.EXE. A wall that catches the ball and,
// in the same frame's field effects, throws it on in a set direction, as a kickout throws without holding it.
class TAccelerator : public TCollisionComponent
{
public:
	TAccelerator(TPinballTable* table, int groupIndex);
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge) override;
	int FieldEffect(TBall* ball, vector2* vecDst) override;

	TBall* Ball{};
	vector3 ThrowDirection{};
	float ThrowAngleMult{};
	float ThrowSpeedMult1{};
	float ThrowSpeedMult2{};
	int HitSoundIndex{};
	field_effect_type Field{};
};
