#pragma once
#include "TCollisionComponent.h"

class TPlunger :
	public TCollisionComponent
{
public:
	// launcher: Skulduggery's s_launcher1 (type 1034), CADET.EXE's plunger with its flag at 0x96 set
	TPlunger(TPinballTable* table, int groupIndex, bool launcher = false);
	~TPlunger() override = default;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
	               TEdgeSegment* edge) override;
	int Message(MessageCode code, float value) override;
	bool CanStartPull() const;
	void CancelReleaseWait();

	static void BallFeedTimer(int timerId, void* caller);
	static void PullbackTimer(int timerId, void* caller);
	static void ReleasedTimer(int timerId, void* caller);

	// A launcher has no sprites and no pull: a press launches the ball with MaxPullback at once
	bool Launcher;
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
	// The latest release's timer: its PullbackDelay one, then its ReleasePushLimit one
	int ReleaseTimerId{};
};
