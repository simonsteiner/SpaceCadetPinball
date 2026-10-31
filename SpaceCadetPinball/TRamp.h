#pragma once

#include "TCollisionComponent.h"
#include "maths.h"
#include "TEdgeManager.h"

class TRamp :
	public TCollisionComponent
{
public:
	TRamp(TPinballTable* table, int groupIndex);
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
	               TEdgeSegment* edge) override;
	int FieldEffect(TBall* ball, vector2* vecDst) override;
	void port_draw() override;

	field_effect_type Field{};
	int CollisionGroup;
	bool BallZOffsetFlag;
	int RampPlaneCount;
	float BallFieldMult;
	ramp_plane_type* RampPlane;
	TEdgeSegment* Line2 = nullptr;
	TEdgeSegment* Line3 = nullptr;
	TEdgeSegment* Line1 = nullptr;
	int Wall1CollisionGroup = 0;
	int Wall2CollisionGroup = 0;
	float Wall1BallOffset = 0;
	float Wall2BallOffset = 0;

private:
	// The ramp's planes, from the table's attribute 1300
	std::vector<ramp_plane_type> Planes;

	// Copies the planes from their attribute, a count and the planes, at most as many as its length holds
	void SetPlanes(const float* attribute, int length);
};
