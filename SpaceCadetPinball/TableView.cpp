#include "pch.h"
#include "TableView.h"

#include "pb.h"
#include "render.h"
#include "winmain.h"
#include "TBall.h"
#include "TFlipper.h"
#include "TFlipperEdge.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TTextBox.h"

namespace
{
	TableView View{};

	// Found once per table, so each update only copies state
	TPinballTable* Table = nullptr;
	std::vector<TLight*> TableLights;
	TLight* MissionLight = nullptr;
	TLightGroup* RankCircle = nullptr;

	SDL_Rect SpriteRect(const render_sprite* sprite)
	{
		if (!sprite || !sprite->Bmp)
			return {};
		const auto& rect = sprite->BmpRect;
		return {rect.XPosition, rect.YPosition, rect.Width, rect.Height};
	}

	std::string Text(const TTextBox* box)
	{
		return box && box->CurrentMessage && box->CurrentMessage->Text ? box->CurrentMessage->Text : "";
	}

	// Looks a component up by group name without find_component's warning box for a missing one
	TPinballComponent* FindComponent(const char* name)
	{
		for (auto component : Table->ComponentList)
		{
			if (component->GroupName && strcmp(component->GroupName, name) == 0)
				return component;
		}
		return nullptr;
	}

	void FindComponents()
	{
		Table = pb::MainTable;
		TableLights.clear();
		View.Lights.clear();
		for (auto component : Table->ComponentList)
		{
			if (auto light = dynamic_cast<TLight*>(component))
			{
				TableLights.push_back(light);
				View.Lights.push_back({light->GroupName ? light->GroupName : "", false, false, {}});
			}
		}
		MissionLight = dynamic_cast<TLight*>(FindComponent("lite198"));
		RankCircle = dynamic_cast<TLightGroup*>(FindComponent("middle_circle"));
	}
} // namespace

const TableView& TableView::Get()
{
	return View;
}

void TableView::Fill()
{
	if (!pb::MainTable)
		return;
	if (pb::MainTable != Table)
		FindComponents();

	View.Balls.resize(Table->BallList.size());
	for (auto index = 0u; index < Table->BallList.size(); index++)
	{
		auto ball = Table->BallList[index];
		View.Balls[index] = {
			ball->ActiveFlag != 0, ball->Position.X,  ball->Position.Y, ball->Position.Z,
			ball->Direction.X,     ball->Direction.Y, ball->Speed,      SpriteRect(ball->RenderSprite)};
	}

	View.Flippers.resize(Table->FlipperList.size());
	for (auto index = 0u; index < Table->FlipperList.size(); index++)
	{
		auto edge = Table->FlipperList[index]->FlipperEdge;
		View.Flippers[index] = {edge ? edge->CurrentAngle : 0, edge ? edge->AngleMax : 0};
	}

	for (auto index = 0u; index < TableLights.size(); index++)
	{
		auto& light = View.Lights[index];
		light.On = TableLights[index]->LightOnFlag;
		light.Flashing = TableLights[index]->FlasherOnFlag;
		light.Sprite = SpriteRect(TableLights[index]->RenderSprite);
	}

	View.InfoText = Text(pb::InfoTextBox);
	View.MissionText = Text(pb::MissTextBox);
	View.PlayerCount = Table->PlayerCount;
	View.CurrentPlayer = Table->CurrentPlayer;
	for (auto index = 0u; index < View.Scores.size(); index++)
	{
		auto scoreStruct = Table->PlayerScores[index].ScoreStruct;
		View.Scores[index] = scoreStruct ? scoreStruct->Score : 0;
	}
	View.BallCount = Table->BallCount;
	View.GameOver = pb::game_mode == GameModes::GameOver;
	View.Demo = winmain::DemoActive;
	View.Tilted = Table->TiltLockFlag != 0;
	View.Mission = MissionLight ? MissionLight->MessageField : -1;
	View.Rank = 0;
	if (RankCircle)
	{
		for (auto light : RankCircle->List)
		{
			if (light->LightOnFlag)
				View.Rank++;
		}
	}
}

void TableView::Clear()
{
	View = TableView{};
	Table = nullptr;
	TableLights.clear();
	MissionLight = nullptr;
	RankCircle = nullptr;
}
