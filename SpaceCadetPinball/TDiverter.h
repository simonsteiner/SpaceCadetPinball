#pragma once

#include "TCollisionComponent.h"

// Object type 1043, Dragon's Keep's a_dvtr* groups, recovered from CADET.EXE. Lines the ball rolls through while
// they're enabled: each pass tells the rules and flashes the second sprite. Enabled shows the first sprite, disabled
// none, and each player keeps their own.
class TDiverter : public TCollisionComponent
{
public:
	TDiverter(TPinballTable* table, int groupIndex);
	int Message(MessageCode code, float value) override;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge) override;

	void AddLine(const float* points, int collisionGroup);
	void SetEnabled(bool enabled);

	float FlashTime;
	std::array<char, 4> PlayerActiveFlags{};

	static void FlashEnd(int timerId, void* caller);
};
