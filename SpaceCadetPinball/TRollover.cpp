#include "pch.h"
#include "TRollover.h"


#include "control.h"
#include "gdrv.h"
#include "loader.h"
#include "pb.h"
#include "render.h"
#include "TBall.h"
#include "TEdgeSegment.h"
#include "timer.h"
#include "TPinballTable.h"

TRollover::TRollover(TPinballTable* table, int groupIndex, bool createWall) : TCollisionComponent(
	table, groupIndex, createWall)
{
}


TRollover::TRollover(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	SpriteSet(0);
	build_walls(groupIndex);
}


int TRollover::Message(MessageCode code, float value)
{
	if (pb::BonusTable())
		KeepMessageFieldPerPlayer(code, value);
	if (code == MessageCode::Reset)
	{
		ActiveFlag = 1;
		RolloverFlag = 0;
		SpriteSet(0);
	}
	return 0;
}

void TRollover::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                          TEdgeSegment* edge)
{
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance -= distance;
	ball->not_again(edge);
	
	if (!PinballTable->TiltLockFlag)
	{
		if (RolloverFlag)
		{
			timer::set(0.1f, this, TimerExpired);
			ActiveFlag = 0;
		}
		else
		{
			loader::play_sound(SoftHitSoundId, ball, "TRollover");
			control::handler(MessageCode::ControlCollision, this);
		}
		RolloverFlag = RolloverFlag == 0;
		SpriteSet(RolloverFlag ? -1 : 0);
	}
}

void TRollover::build_walls(int groupIndex)
{
	visualStruct visual{};

	loader::query_visual(groupIndex, 0, &visual);
	int length;
	const float* arr1 = loader::query_float_array(groupIndex, 0, 600, &length);
	TEdgeSegment::install_wall(arr1, length, this, &ActiveFlag, visual.CollisionGroup, 0.0, 600);
	const float* arr2 = loader::query_float_array(groupIndex, 0, 603, &length);
	TEdgeSegment::install_wall(arr2, length, this, &RolloverFlag, visual.CollisionGroup, 0.0, 603);
}

void TRollover::TimerExpired(int timerId, void* caller)
{
	auto roll = static_cast<TRollover*>(caller);
	roll->ActiveFlag = 1;
}
