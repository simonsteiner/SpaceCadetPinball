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
	// How long a release keeps its push: PullbackDelay in 3D Pinball, as Pinball.exe; 0.2 s on Full Tilt's data,
	// as CADET.EXE and DRAGON.EXE (0x418aa6)
	float ReleaseDelay() const;
	// A released pull keeps its push past a shorter ReleaseDelay until the ball touches the plunger, for at most this
	// long
	static constexpr float ReleasePushLimit = 0.1f;
	static constexpr float FullTiltReleaseDelay = 0.2f;
	bool ReleasePushPending{};
	bool ReleaseTouched{};
	bool ReleaseWaitsForTouch{};
	// The latest release's timer: its PullbackDelay one, then its ReleasePushLimit one
	int ReleaseTimerId{};
};
