#pragma once
#include "TCollisionComponent.h"

class TPlunger :
	public TCollisionComponent
{
public:
	TPlunger(TPinballTable* table, int groupIndex);
	~TPlunger() override = default;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
	               TEdgeSegment* edge) override;
	int Message(MessageCode code, float value) override;
	bool CanStartPull() const;
	void CancelReleaseWait();

	static void BallFeedTimer(int timerId, void* caller);
	static void PullbackTimer(int timerId, void* caller);
	static void ReleasedTimer(int timerId, void* caller);

	int PullbackTimer_;
	int BallFeedTimer_;
	float MaxPullback;
	float PullbackIncrement;
	float PullbackDelay;
	int SoundIndexP1;
	int SoundIndexP2;
	bool PullbackStartedFlag{};
	int SomeCounter{};
	// A released pull keeps its push past PullbackDelay until the ball touches the plunger, for at most this long
	static constexpr float ReleasePushLimit = 0.1f;
	bool ReleasePushPending{};
	bool ReleaseTouched{};
	bool ReleaseWaitsForTouch{};
};
