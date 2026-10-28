#include "pch.h"
#include "TRampExit.h"

#include "control.h"
#include "loader.h"
#include "TBall.h"
#include "TLine.h"
#include "TPinballTable.h"

TRampExit::TRampExit(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	// Wall 1301: the two layers' collision groups, the wall type, which must be a line (2), and its two points
	auto wall = loader::query_float_attribute(groupIndex, 0, 1301);
	if (!wall || wall[2] != 2.0f)
		return;

	Layer1CollisionGroup = static_cast<int>(1u << static_cast<unsigned>(floor(wall[0])));
	Layer2CollisionGroup = static_cast<int>(1u << static_cast<unsigned>(floor(wall[1])));
	vector2 start{wall[3], wall[4]}, end{wall[5], wall[6]};

	auto line1 = new TLine(this, &ActiveFlag, Layer1CollisionGroup, start, end);
	line1->Offset(table->CollisionCompOffset);
	line1->place_in_grid(&AABB);
	EdgeList.push_back(line1);
	Line1 = line1;

	auto line2 = new TLine(this, &ActiveFlag, Layer2CollisionGroup, end, start);
	line2->Offset(-table->CollisionCompOffset);
	line2->place_in_grid(&AABB);
	EdgeList.push_back(line2);
	Line2 = line2;
}

void TRampExit::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	ball->not_again(edge);
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance -= distance;

	// The ball goes on in the other layer from the next frame on
	if (edge == Line1)
	{
		control::handler(MessageCode::ControlRampExitToLayer2, this);
		ball->CollisionDisabledFlag = true;
		ball->CollisionMask = Layer2CollisionGroup;
	}
	else
	{
		control::handler(MessageCode::ControlRampExitToLayer1, this);
		ball->CollisionDisabledFlag = true;
		ball->CollisionMask = Layer1CollisionGroup;
	}
}
