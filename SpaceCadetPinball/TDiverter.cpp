#include "pch.h"
#include "TDiverter.h"

#include "control.h"
#include "loader.h"
#include "TBall.h"
#include "timer.h"
#include "TLine.h"
#include "TPinballTable.h"

#include <utility>

TDiverter::TDiverter(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	visualStruct visual{};
	loader::query_visual(groupIndex, 0, &visual);
	FlashTime = loader::query_float_attribute(groupIndex, 0, 407, 0.1f);

	// The visual's wall when it's a line, else every line (2) listed in attribute 604, 5 floats each
	if (visual.FloatArrCount == 2)
	{
		AddLine(visual.FloatArr, visual.CollisionGroup);
		return;
	}
	auto lines = loader::query_float_attribute(groupIndex, 0, 604);
	for (; lines && lines[0] == 2.0f; lines += 6)
		AddLine(lines + 1, visual.CollisionGroup);
}

void TDiverter::AddLine(const float* points, int collisionGroup)
{
	// Each line is two, one for each side
	vector2 start{points[0], points[1]};
	vector2 end{points[2], points[3]};

	auto line = new TLine(this, &ActiveFlag, collisionGroup, start, end);
	line->Offset(PinballTable->CollisionCompOffset);
	line->place_in_grid(&AABB);
	EdgeList.push_back(line);

	line = new TLine(this, &ActiveFlag, collisionGroup, end, start);
	line->Offset(-(PinballTable->CollisionCompOffset * 0.8f));
	line->place_in_grid(&AABB);
	EdgeList.push_back(line);
}

int TDiverter::Message(MessageCode code, float value)
{
	switch (std::to_underlying(code))
	{
	case std::to_underlying(MessageCode::TDiverterEnable):
	case 63:
	case 75:
		SetEnabled(true);
		break;
	case std::to_underlying(MessageCode::TDiverterDisable):
	case 62:
	case 76:
		SetEnabled(false);
		break;
	case std::to_underlying(MessageCode::PlayerChanged):
		{
			KeepMessageFieldPerPlayer(MessageCode::PlayerChanged, value);
			PlayerActiveFlags[PinballTable->CurrentPlayer] = ActiveFlag;
			SetEnabled(PlayerActiveFlags[static_cast<int>(value)] != 0);
			break;
		}
	case std::to_underlying(MessageCode::Reset):
		KeepMessageFieldPerPlayer(MessageCode::Reset, value);
		std::ranges::fill(PlayerActiveFlags, 0);
		SetEnabled(false);
		break;
	default:
		break;
	}
	return 0;
}

void TDiverter::SetEnabled(bool enabled)
{
	// CADET.EXE also plays two sounds here, which its constructor never sets, so they play nothing
	ActiveFlag = enabled;
	SpriteSet(enabled ? 0 : -1);
}

void TDiverter::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	ball->not_again(edge);
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance -= distance;
	if (!PinballTable->TiltLockFlag)
	{
		loader::play_sound(HardHitSoundId, this, "TDiverter");
		control::handler(MessageCode::ControlCollision, this);
	}
	if (ListBitmap && ListBitmap->size() > 1)
	{
		SpriteSet(1);
		timer::set(FlashTime, this, FlashEnd);
	}
}

void TDiverter::FlashEnd(int, void* caller)
{
	auto diverter = static_cast<TDiverter*>(caller);
	diverter->SpriteSet(diverter->ActiveFlag ? 0 : -1);
}
