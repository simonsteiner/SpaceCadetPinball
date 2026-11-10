#include "pch.h"
#include "TCollisionComponent.h"
#include "loader.h"
#include "maths.h"
#include "TEdgeSegment.h"
#include "TPinballTable.h"
#include "TBall.h"


// Reads the group's collision properties (threshold, elasticity, smoothness, boost, hit sounds) and, with <createWall>,
// installs the wall of attribute 600. A group index of 0 or less takes the loader's defaults
TCollisionComponent::TCollisionComponent(TPinballTable* table, int groupIndex, bool createWall) :
	TPinballComponent(table, groupIndex, true)
{
	visualStruct visual{};

	ActiveFlag = 1;
	AABB = { -10000, -10000, 10000, 10000 };
	if (GroupName != nullptr)
		UnusedBaseFlag = 1;
	if (groupIndex <= 0)
	{
		loader::default_vsi(&visual);
	}
	else
	{
		loader::query_visual(groupIndex, 0, &visual);
		if (createWall)
		{
			float offset = table->CollisionCompOffset;
			int length;
			const float* floatArr = loader::query_float_array(groupIndex, 0, 600, &length);
			TEdgeSegment::install_wall(floatArr, length, this, &ActiveFlag, visual.CollisionGroup, offset, 0);
		}
	}

	Threshold = visual.Kicker.Threshold;
	Elasticity = visual.Elasticity;
	Smoothness = visual.Smoothness;
	Boost = visual.Kicker.Boost;
	HardHitSoundId = visual.Kicker.HardHitSoundId;
	SoftHitSoundId = visual.SoftHitSoundId;
	GroupIndex = groupIndex;
}

// Deletes the component's edges
TCollisionComponent::~TCollisionComponent()
{
	for (auto edge : EdgeList)
		delete edge;
}


// Passes port_draw on to each of the component's edges
void TCollisionComponent::port_draw()
{
	for (auto edge : EdgeList)
		edge->port_draw();
}

// Bounces the ball off the component and plays the hard or the soft hit sound. Returns whether the hit was harder than
// Threshold, never while the table is tilted, which also gives neither sound nor boost
bool TCollisionComponent::DefaultCollision(TBall* ball, vector2* nextPosition, vector2* direction)
{
	if (PinballTable->TiltLockFlag)
	{
		maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, 1000000000.0, 0.0);
		return false;
	}

	bool collision = false;
	auto projSpeed = maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, Threshold, Boost);
	if (projSpeed > Threshold)
	{
		loader::play_sound(HardHitSoundId, ball, "TCollisionComponent1");
		collision = true;
	}
	else if (projSpeed > 0.2f)
		loader::play_sound(SoftHitSoundId, ball, "TCollisionComponent2");

	return collision;
}

// The default collision: bounces the ball and plays the hard or the soft hit sound; while the table is tilted, without
// sound or boost
void TCollisionComponent::Collision(TBall* ball, vector2* nextPosition, vector2* direction,
                                    float distance, TEdgeSegment* edge)
{
	if (PinballTable->TiltLockFlag)
	{
		maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, 1000000000.0, 0.0);
		return;
	}

	auto projSpeed = maths::basic_collision(
		ball,
		nextPosition,
		direction,
		Elasticity,
		Smoothness,
		Threshold,
		Boost);
	if (projSpeed > Threshold)
		loader::play_sound(HardHitSoundId, ball, "TCollisionComponent3");
	else if (projSpeed > 0.2f)
		loader::play_sound(SoftHitSoundId, ball, "TCollisionComponent4");
}

int TCollisionComponent::FieldEffect(TBall* ball, vector2* vecDst)
{
	return 0;
}
