#pragma once

#include "TCollisionComponent.h"

// Object type 1011 on Full Tilt's bonus tables, recovered from CADET.EXE, whose TBlocker is a later revision of
// Pinball.exe's: animated props with a wall for each of up to 8 states, any of them missing. State -1 shows nothing
// and blocks nothing. Space Cadet keeps TBlocker, which this matches only for a single state.
class TAnimatedBlocker final : public TCollisionComponent
{
public:
	static constexpr int MaxStates = 8;

	TAnimatedBlocker(TPinballTable* table, int groupIndex);
	int Message(MessageCode code, float value) override;

	int StateCount() const;
	void SetState(int state);
	void KillTimers();

	char StateActive[MaxStates]{};
	int State = -1;
	int InitialState;
	int TimeoutTimer = 0;
	int TargetState = -1;
	float FrameTime;
	int AnimationTimer = 0;
	int DeferredTimer = 0;
	MessageCode DeferredCode{};
	float DeferredValue = 0;
	int OnSoundIndex, OffSoundIndex;
	int PlayerMessageFields[4]{};

	static void TimeoutExpired(int timerId, void* caller);
	static void AnimationStep(int timerId, void* caller);
	static void DeferredMessage(int timerId, void* caller);
};
