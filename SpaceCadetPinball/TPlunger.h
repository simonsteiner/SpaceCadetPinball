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
	// How long a release keeps its push: PullbackDelay in 3D Pinball, as Pinball.exe; FullTiltReleaseDelay on Full
	// Tilt's data, as CADET.EXE and DRAGON.EXE (0x418aa6)
	float ReleaseDelay() const;

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
	static constexpr float FullTiltReleaseDelay = 0.2f;
	// A released pull keeps its push past a shorter ReleaseDelay, 3D Pinball's, until the ball touches the plunger,
	// for at most this long
	static constexpr float ReleasePushLimit = 0.1f;
	bool ReleasePushPending{};
	bool ReleaseTouched{};
	bool ReleaseWaitsForTouch{};
	// The latest release's timer: its ReleaseDelay one, then in 3D Pinball its ReleasePushLimit one
	int ReleaseTimerId{};
};
