#include "pch.h"
#include "TRamp.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "TBall.h"
#include "TEdgeSegment.h"
#include "TLine.h"
#include "TPinballTable.h"
#include "TTableLayer.h"


void TRamp::SetPlanes(const float* attribute, int length)
{
	// The planes are copied out of the attribute's floats, as the table lays them out
	constexpr int planeFloats = sizeof(ramp_plane_type) / sizeof(float);
	static_assert(std::is_trivially_copyable_v<ramp_plane_type> && sizeof(ramp_plane_type) % sizeof(float) == 0);
	auto count = attribute ? loader::whole_number(*attribute) : 0;
	count = std::clamp(count, 0, std::max(0, (length - 1) / planeFloats));
	Planes.resize(count);
	if (count > 0)
		std::memcpy(Planes.data(), attribute + 1, Planes.size() * sizeof(ramp_plane_type));
	RampPlaneCount = count;
	RampPlane = count > 0 ? Planes.data() : nullptr;
}

TRamp::TRamp(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	visualStruct visual{};
	vector2 wall1End{}, wall1Start{}, wall2Start{}, wall2End{};

	MessageField = 0;
	UnusedBaseFlag = 1;
	loader::query_visual(groupIndex, 0, &visual);
	CollisionGroup = visual.CollisionGroup;

	BallFieldMult = loader::query_float_attribute(groupIndex, 0, 701, 0.2f);
	BallZOffsetFlag = static_cast<int>(loader::query_float_attribute(groupIndex, 0, 1305, 0));

	// The planes' count, then the planes; a damaged table's count can't run past the attribute
	int planesLength;
	auto floatArr3Plane = loader::query_float_array(groupIndex, 0, 1300, &planesLength);
	SetPlanes(floatArr3Plane, planesLength);

	// Full Tilt's bonus tables leave out some of the three walls, which CADET.EXE then doesn't build; their members
	// keep the defaults from TRamp.h
	int wall1Enabled = 0;
	int wall2Enabled = 0;

	if (auto wall0Arr = loader::query_float_attribute(groupIndex, 0, 1303))
	{
		auto wall0CollisionGroup = loader::collision_group_bit(wall0Arr[0]);
		auto wall0Pts = reinterpret_cast<wall_point_type*>(wall0Arr + 2);
		Line1 = new TLine(this, &ActiveFlag, wall0CollisionGroup, wall0Pts->Pt1, wall0Pts->Pt0);
		Line1->WallValue = nullptr;
		Line1->place_in_grid(&AABB);
		EdgeList.push_back(Line1);
	}

	if (auto wall1Arr = loader::query_float_attribute(groupIndex, 0, 1301))
	{
		Wall1CollisionGroup = loader::collision_group_bit(wall1Arr[0]);
		wall1Enabled = loader::whole_number(wall1Arr[1]);
		Wall1BallOffset = wall1Arr[7];
		maths::find_closest_edge(RampPlane, RampPlaneCount, reinterpret_cast<wall_point_type*>(wall1Arr + 3), wall1End,
								 wall1Start);

		Line2 = new TLine(this, &ActiveFlag, CollisionGroup, wall1Start, wall1End);
		Line2->WallValue = nullptr;
		Line2->place_in_grid(&AABB);
		EdgeList.push_back(Line2);
	}

	if (auto wall2Arr = loader::query_float_attribute(groupIndex, 0, 1302))
	{
		Wall2CollisionGroup = loader::collision_group_bit(wall2Arr[0]);
		wall2Enabled = loader::whole_number(wall2Arr[1]);
		Wall2BallOffset = wall2Arr[7];
		maths::find_closest_edge(RampPlane, RampPlaneCount, reinterpret_cast<wall_point_type*>(wall2Arr + 3), wall2End,
								 wall2Start);

		Line3 = new TLine(this, &ActiveFlag, CollisionGroup, wall2Start, wall2End);
		Line3->WallValue = nullptr;
		Line3->place_in_grid(&AABB);
		EdgeList.push_back(Line3);
	}


	auto xMin = 1000000000.0f;
	auto yMin = 1000000000.0f;
	auto yMax = -1000000000.0f;
	auto xMax = -1000000000.0f;
	for (auto index = 0; index < RampPlaneCount; index++)
	{
		auto& plane = RampPlane[index];
		vector2* pointOrder[4] = { &plane.V1, &plane.V2, &plane.V3, &plane.V1 };

		xMin = std::min(std::min(std::min(plane.V3.X, plane.V1.X), plane.V2.X), xMin);
		yMin = std::min(std::min(std::min(plane.V3.Y, plane.V1.Y), plane.V2.Y), xMin); // Sic
		xMax = std::max(std::max(std::max(plane.V3.X, plane.V1.X), plane.V2.X), xMin);
		yMax = std::max(std::max(std::max(plane.V3.Y, plane.V1.Y), plane.V2.Y), xMin);
		
		for (auto pt = 0; pt < 3; pt++)
		{
			auto& point1 = *pointOrder[pt], point2 = *pointOrder[pt + 1];
			auto collisionGroup = 0;

			if (point1 == wall1End && point2 == wall1Start)
			{
				if (wall1Enabled)
					collisionGroup = Wall1CollisionGroup;
			}
			else if (point1 == wall2End && point2 == wall2Start)
			{
				if (wall2Enabled)
					collisionGroup = Wall2CollisionGroup;
			}
			else
				collisionGroup = visual.CollisionGroup;

			if (collisionGroup)
			{
				auto line = new TLine(this, &ActiveFlag, collisionGroup, point1, point2);
				line->WallValue = &plane;
				line->place_in_grid(&AABB);
				EdgeList.push_back(line);
			}
		}

		plane.FieldForce.X = cos(plane.GravityAngle2) * sin(plane.GravityAngle1) *
			PinballTable->GravityDirVectMult;
		plane.FieldForce.Y = sin(plane.GravityAngle2) * sin(plane.GravityAngle1) *
			PinballTable->GravityDirVectMult;
	}

	Field.ActiveFlag = &ActiveFlag;
	Field.CollisionComp = this;
	Field.CollisionGroup = visual.CollisionGroup;

	auto x1 = xMax;
	auto y1 = yMax;
	auto x0 = xMin;
	auto y0 = yMin;
	TTableLayer::edges_insert_square(y0, x0, y1, x1, nullptr, &Field);
}

void TRamp::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	ball->not_again(edge);
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance -= distance;

	auto plane = static_cast<ramp_plane_type*>(edge->WallValue);
	if (plane)
	{
		ball->CollisionFlag = 1;
		ball->CollisionOffset.X = plane->BallCollisionOffset.X;
		ball->CollisionOffset.Y = plane->BallCollisionOffset.Y;
		ball->CollisionOffset.Z = plane->BallCollisionOffset.Z;
		ball->RampFieldForce.X = plane->FieldForce.X;
		ball->RampFieldForce.Y = plane->FieldForce.Y;
		ball->Position.Z = ball->Position.X * ball->CollisionOffset.X + ball->Position.Y * ball->CollisionOffset.Y +
			ball->Radius + ball->CollisionOffset.Z;
		// CADET.EXE (0x4177d0), the bonus tables only: a change of layer stops the ball for the rest of the frame and
		// tells the rules
		if (pb::BonusTable() && ball->CollisionMask != CollisionGroup)
		{
			ball->CollisionDisabledFlag = true;
			ball->CollisionMask = CollisionGroup;
			control::handler(MessageCode::ControlRampLayerChanged, this);
			return;
		}
		ball->CollisionMask = CollisionGroup;
		return;
	}

	if (edge == Line1)
	{
		if (!PinballTable->TiltLockFlag)
		{
			loader::play_sound(SoftHitSoundId, ball, "TRamp");
			control::handler(MessageCode::ControlCollision, this);
		}
	}
	else
	{
		ball->CollisionFlag = 0;
		if (edge == Line2)
		{
			ball->CollisionMask = Wall1CollisionGroup;
			if (BallZOffsetFlag)
				ball->Position.Z = ball->Radius + Wall1BallOffset;
		}
		else
		{
			ball->CollisionMask = Wall2CollisionGroup;
			if (BallZOffsetFlag)
				ball->Position.Z = ball->Radius + Wall2BallOffset;
		}
		// CADET.EXE, the bonus tables only: crossing either wall stops the ball for the rest of the frame and tells
		// the rules (0x55 for wall 1301, 0x56 for 1302)
		if (pb::BonusTable())
		{
			ball->CollisionDisabledFlag = true;
			control::handler(
				edge == Line2 ? MessageCode::ControlRampExitToLayer1 : MessageCode::ControlRampExitToLayer2, this);
		}
	}
}

int TRamp::FieldEffect(TBall* ball, vector2* vecDst)
{
	vecDst->X = ball->RampFieldForce.X - ball->Direction.X * ball->Speed * BallFieldMult;
	vecDst->Y = ball->RampFieldForce.Y - ball->Direction.Y * ball->Speed * BallFieldMult;
	return 1;
}

void TRamp::port_draw()
{
	TCollisionComponent::port_draw();
}
