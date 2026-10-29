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

	std::array<char, MaxStates> StateActive{};
	int State = -1;
	int InitialState;
	// Attribute 407, as CADET.EXE reads it: how long the rules keep the blocker up, then how long it warns
	int InitialDuration = 55;
	int ExtendedDuration = 5;
	int TimeoutTimer = 0;
	int TargetState = -1;
	float FrameTime;
	int AnimationTimer = 0;
	int DeferredTimer = 0;
	MessageCode DeferredCode{};
	float DeferredValue = 0;
	int OnSoundIndex;
	int OffSoundIndex;

	static void TimeoutExpired(int timerId, void* caller);
	static void AnimationStep(int timerId, void* caller);
	static void DeferredMessage(int timerId, void* caller);
};
