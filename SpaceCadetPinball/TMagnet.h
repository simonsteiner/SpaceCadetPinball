#pragma once

#include "TKickout.h"

// Object type 1042, Skulduggery's magnets (s_magnet1 to s_magnet6), recovered from CADET.EXE (0x416560): a TKickout
// whose rules switch it on and off. While on, its field pulls a ball near it to its centre, as a kickout's does; a
// ball that reaches the centre rolls on through it, and the field stays off for TimerTime2 (1 s).
class TMagnet : public TKickout
{
public:
	TMagnet(TPinballTable* table, int groupIndex);
	int Message(MessageCode code, float value) override;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge) override;

	static void FieldOffTimerExpired(int timerId, void* caller);
};
