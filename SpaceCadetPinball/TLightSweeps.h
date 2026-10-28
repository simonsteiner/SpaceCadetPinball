#pragma once

#include "TPinballComponent.h"

class TLightGroup;
class TLight;
class TTableLayer;

// Object type 1038, light_sweeps on Full Tilt's bonus tables, recovered from CADET.EXE. A light show: it sorts the
// table's lights into a grid by their position (attribute 501), and a sweep lights a ring of cells around a point
// that grows or shrinks one cell each frame. Each state of the group holds one sweep's settings (1600 to 1604).
class TLightSweeps : public TPinballComponent
{
public:
	static constexpr int Columns = 15;
	static constexpr int Rows = 25;

	TLightSweeps(TPinballTable* table, const TTableLayer& tableLayer, TLightGroup& lights, int groupIndex);
	int Message(MessageCode code, float value) override;

	int CellColumn(float x) const;
	int CellRow(float y) const;
	void Step();
	void LightRing(int column, int row, int radius);
	void LightCell(int column, int row);
	void KillTimer();

	float XMin;
	float YMin;
	float ColumnsPerUnit;
	float RowsPerUnit;
	std::array<std::vector<TLight*>, Columns * Rows> Cells;
	float FrameTime = 0;
	float LightTime = 0;
	int CenterColumn = 0;
	int CenterRow = 0;
	bool Outward = false;
	int RepeatCount = 0;
	int Radius = 0;
	int MaxRadius = 0;
	int Timer = 0;
	std::array<int, 4> PlayerMessageFields{};

	static void StepTimer(int timerId, void* caller);
};
