#include "pch.h"
#include "TLightSweeps.h"

#include "control.h"
#include "loader.h"
#include "timer.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TTableLayer.h"

TLightSweeps::TLightSweeps(TPinballTable* table, const TTableLayer& tableLayer, TLightGroup& lights, int groupIndex) :
	TPinballComponent(table, groupIndex, false), XMin(tableLayer.XMin), YMin(tableLayer.YMin),
	ColumnsPerUnit(static_cast<float>(1.0 / ((tableLayer.XMax - tableLayer.XMin) / Columns))),
	RowsPerUnit(static_cast<float>(1.0 / ((tableLayer.YMax - tableLayer.YMin) / Rows)))
{

	for (auto index = static_cast<int>(lights.List.size()) - 1; index >= 0; index--)
	{
		auto light = lights.List[index];
		auto position = loader::query_float_attribute(light->GroupIndex, 0, 501);
		if (position)
			Cells[CellRow(position[1]) * Columns + CellColumn(position[0])].push_back(light);
	}
}

int TLightSweeps::CellColumn(float x) const
{
	return static_cast<int>(std::clamp(floor((x - XMin) * ColumnsPerUnit), 0.0f, Columns - 1.0f));
}

int TLightSweeps::CellRow(float y) const
{
	return static_cast<int>(std::clamp(floor((y - YMin) * RowsPerUnit), 0.0f, Rows - 1.0f));
}

int TLightSweeps::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::TLightSweepsStart:
		{
			// TODO: a start during a running sweep leaves its timer running, so the sweep steps
			// twice as fast. When a rule first sends this, check the exe's type 1038 start case
			// and add KillTimer() here, or note that the double speed is faithful.
			if (GroupIndex <= 0)
				break;
			auto sweep = static_cast<int>(value);
			FrameTime = loader::query_float_attribute(GroupIndex, sweep, 1602, 0.1f);
			LightTime = loader::query_float_attribute(GroupIndex, sweep, 1604, FrameTime);
			auto center = loader::query_float_attribute(GroupIndex, sweep, 1600);
			CenterColumn = center ? CellColumn(center[0]) : 0;
			CenterRow = center ? CellRow(center[1]) : 0;
			Outward = static_cast<int>(loader::query_float_attribute(GroupIndex, sweep, 1601, 0.0f)) != 0;
			RepeatCount = static_cast<int>(loader::query_float_attribute(GroupIndex, sweep, 1603, 1.0f));

			// The ring has to reach the grid's farthest corner from the center
			auto left = CenterColumn;
			auto top = CenterRow;
			auto right = Columns - CenterColumn - 1;
			auto bottom = Rows - CenterRow - 1;
			auto farthest = std::max({left * left + top * top, right * right + bottom * bottom,
									  right * right + top * top, left * left + bottom * bottom});
			MaxRadius = static_cast<int>(sqrt(static_cast<double>(farthest)));
			Radius = Outward ? 0 : MaxRadius;
			Step();
			break;
		}
	case MessageCode::PlayerChanged:
		KillTimer();
		KeepMessageFieldPerPlayer(code, value);
		break;
	case MessageCode::Reset:
		KeepMessageFieldPerPlayer(code, value);
		KillTimer();
		break;
	case MessageCode::SetTiltLock:
		KillTimer();
		break;
	default:
		break;
	}
	return 0;
}

void TLightSweeps::Step()
{
	Radius += Outward ? 1 : -1;
	if (Outward ? Radius > MaxRadius : Radius < 0)
	{
		if (--RepeatCount <= 0)
		{
			control::handler(MessageCode::ControlTimerExpired, this);
			return;
		}
		Radius = Outward ? 0 : MaxRadius;
	}
	LightRing(CenterColumn, CenterRow, Radius);
	Timer = timer::set(FrameTime, this, StepTimer);
}

void TLightSweeps::LightRing(int column, int row, int radius)
{
	// The midpoint circle algorithm, eight cells at a time
	int x = 0;
	int error = radius * radius;
	int limit = (radius - 1) * radius;
	do
	{
		LightCell(column + x, row - radius);
		LightCell(column - x, row - radius);
		LightCell(column + x, row + radius);
		LightCell(column - x, row + radius);
		LightCell(column + radius, row - x);
		LightCell(column - radius, row - x);
		auto rowBelow = row + x;
		x++;
		LightCell(column + radius, rowBelow);
		LightCell(column - radius, rowBelow);
		error += 1 - 2 * x;
		if (error <= limit)
		{
			limit -= 2 * radius - 2;
			radius--;
		}
	} while (x <= radius);
}

void TLightSweeps::LightCell(int column, int row)
{
	if (column < 0 || column >= Columns || row < 0 || row >= Rows)
		return;
	auto& cell = Cells[row * Columns + column];
	for (auto index = static_cast<int>(cell.size()) - 1; index >= 0; index--)
		cell[index]->Message(MessageCode::TLightFtTmpOverrideOn, LightTime);
}

void TLightSweeps::KillTimer()
{
	if (Timer)
		timer::kill(Timer);
	Timer = 0;
}

void TLightSweeps::StepTimer(int, void* caller)
{
	auto sweeps = static_cast<TLightSweeps*>(caller);
	sweeps->Timer = 0;
	sweeps->Step();
}
