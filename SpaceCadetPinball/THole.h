#pragma once
#include "TCircle.h"
#include "TCollisionComponent.h"
#include "TEdgeManager.h"

class THole :
	public TCollisionComponent
{
public:
	THole(TPinballTable* table, int groupIndex);
	int Message(MessageCode code, float value) override;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
	               TEdgeSegment* edge) override;
	int FieldEffect(TBall* ball, vector2* vecDst) override;

	static void TimerExpired(int timerId, void* caller);
	// Full Tilt's later THole (CADET.EXE), the bonus tables only: the ball goes out once no other ball is in the way
	void FinishRelease(TBall* ball);
	static void RetryRelease(int timerId, void* caller);

	bool BallCapturedFlag{};
	bool BallCapturedSecondStage{};
	int Timer;
	float Unknown3;
	float Unknown4;
	float GravityMult;
	float ZSetValue;
	int CollisionMask;
	float GravityPull;
	// The bonus tables only: attribute 1306 throws the ball up at this speed instead of letting it drop
	float EjectSpeed{};
	TBall* CapturedBall{};
	int RetryTimer{};
	circle_type Circle{};
	field_effect_type Field{};
};
