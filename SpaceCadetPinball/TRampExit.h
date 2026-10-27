#pragma once

#include "TCollisionComponent.h"

// Object type 1035, Full Tilt's ramp exits (s_rexit*), recovered from CADET.EXE. A line the ball can cross from
// either side: crossing it moves the ball to the collision layer on the other side and tells the rules which way.
class TRampExit : public TCollisionComponent
{
public:
	TRampExit(TPinballTable* table, int groupIndex);
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge) override;

	// Line1 collides with layer 1 and leads into layer 2; Line2 is the same line seen from layer 2
	TEdgeSegment* Line1{};
	TEdgeSegment* Line2{};
	int Layer1CollisionGroup{};
	int Layer2CollisionGroup{};
};
