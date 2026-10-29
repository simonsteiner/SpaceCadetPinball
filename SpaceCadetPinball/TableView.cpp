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
#include "TableRules.h"
#include "TPinballTable.h"
#include "TTextBox.h"

#include <utility>

namespace
{
	TableView View{};

	// Found once per table, so each update only copies state
	TPinballTable* Table = nullptr;
	std::vector<TLight*> TableLights;
	TLight* MissionLight = nullptr;
	TLightGroup* RankCircle = nullptr;
	TLightGroup* ProgressRing = nullptr;
	// The info text box's message shown at the last update, and when it was to end
	const TTextBoxMessage* InfoMessage = nullptr;
	int InfoEnd = 0;

	SDL_Rect SpriteRect(const render_sprite* sprite)
	{
		if (!sprite || !sprite->Bmp)
			return {};
		const auto& rect = sprite->BmpRect;
		return {rect.XPosition, rect.YPosition, rect.Width, rect.Height};
	}

	std::string Text(const TTextBox* box)
	{
		if (box && box->Overlay)
			return *box->Overlay;
		if (!box || !box->CurrentMessage || !box->CurrentMessage->Text)
			return "";
		// A picture in the second font (Dragon's Keep's jackpot, spells, eggs) shows as the words it carries
		return box->CurrentMessage->FontIndex == 0 ? box->CurrentMessage->Text : box->CurrentMessage->Words;
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

	// FNV-1a, fed field by field so padding never counts
	struct Hasher
	{
		uint64_t Value = 14695981039346656037ull;

		void Bytes(const void* data, size_t size)
		{
			auto bytes = static_cast<const unsigned char*>(data);
			for (size_t index = 0; index < size; index++)
			{
				Value ^= bytes[index];
				Value *= 1099511628211ull;
			}
		}

		void Add(int value)
		{
			Bytes(&value, sizeof value);
		}
		void Add(bool value)
		{
			Add(static_cast<int>(value));
		}
		void Add(float value)
		{
			Bytes(&value, sizeof value);
		}

		void Add(const std::string& text)
		{
			Add(static_cast<int>(text.size()));
			Bytes(text.data(), text.size());
		}

		void Add(const SDL_Rect& rect)
		{
			Add(rect.x);
			Add(rect.y);
			Add(rect.w);
			Add(rect.h);
		}
	};

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
		ProgressRing = dynamic_cast<TLightGroup*>(FindComponent("outer_circle"));
		InfoMessage = nullptr;
		InfoEnd = 0;
	}
} // namespace

uint64_t TableView::Hash() const
{
	Hasher hash;
	hash.Add(static_cast<int>(Balls.size()));
	for (const auto& ball : Balls)
	{
		hash.Add(ball.Active);
		for (auto value : {ball.X, ball.Y, ball.Z, ball.DirectionX, ball.DirectionY, ball.Speed})
			hash.Add(value);
		hash.Add(ball.Depth);
		hash.Add(ball.Sprite);
		hash.Add(ball.AltBall);
	}
	hash.Add(static_cast<int>(Flippers.size()));
	for (const auto& flipper : Flippers)
	{
		hash.Add(flipper.Angle);
		hash.Add(flipper.AngleMax);
	}
	hash.Add(static_cast<int>(Lights.size()));
	for (const auto& light : Lights)
	{
		hash.Add(light.Name);
		hash.Add(light.On);
		hash.Add(light.Flashing);
		hash.Add(light.Sprite);
	}
	hash.Add(InfoText);
	hash.Add(MissionText);
	for (auto value : {PlayerCount, CurrentPlayer, Scores[0], Scores[1], Scores[2], Scores[3], BallCount, MaxBallCount,
					   Mission, Rank, RankProgress, InfoMessages, ExtraBalls})
		hash.Add(value);
	for (auto value : {GameOver, Demo, Tilted, FullTilt, CheatsUsed})
		hash.Add(value);
	hash.Add(std::to_underlying(Kind));
	for (auto value : {Dragon.State, Dragon.RestartQuest, Dragon.PickedQuest, Dragon.TimeLeft,
					   static_cast<int>(Dragon.QuestLights), Dragon.QuestsDone, Dragon.Jackpot, Dragon.SuperJackpot,
					   Dragon.Eggs, Dragon.Weapons, static_cast<int>(Dragon.Armor), Dragon.HitsLeft, Dragon.BallSaves})
		hash.Add(value);
	return hash.Value;
}

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
		auto sprite = ball->RenderSprite;
		View.Balls[index] = {ball->ActiveFlag != 0, ball->Position.X,
							 ball->Position.Y,      ball->Position.Z,
							 ball->Direction.X,     ball->Direction.Y,
							 ball->Speed,           sprite && sprite->Bmp ? sprite->Depth : 0,
							 SpriteRect(sprite),    sprite && sprite->AltBall};
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
		// As shown: a timed override shows a light on or off without changing LightOnFlag
		auto source = TableLights[index];
		light.On = !source->ToggledOffFlag && (source->ToggledOnFlag || source->LightOnFlag);
		light.Flashing = source->FlasherOnFlag;
		light.Sprite = SpriteRect(source->RenderSprite);
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
	View.MaxBallCount = Table->MaxBallCount;
	View.GameOver = pb::game_mode == GameModes::GameOver;
	View.Demo = winmain::DemoActive;
	View.Tilted = Table->TiltLockFlag != 0;
	View.FullTilt = pb::FullTiltMode;
	View.Mission = MissionLight ? MissionLight->MessageField : -1;
	auto litCount = [](const TLightGroup* group)
	{
		auto count = 0;
		if (group)
		{
			for (auto light : group->List)
			{
				if (light->LightOnFlag)
					count++;
			}
		}
		return count;
	};
	View.Rank = litCount(RankCircle);
	View.RankProgress = litCount(ProgressRing);
	// A new message, or the one shown refreshed, which TTextBox::Display does when it is shown again
	auto info = pb::InfoTextBox ? pb::InfoTextBox->CurrentMessage : nullptr;
	if (info && (info != InfoMessage || info->EndTicks != InfoEnd))
		View.InfoMessages++;
	InfoMessage = info;
	InfoEnd = info ? info->EndTicks : 0;
	View.ExtraBalls = Table->ExtraBalls;
	View.CheatsUsed = Table->CheatsUsed != 0;
	switch (pb::Table)
	{
	case Tables::DragonsKeep:
		View.Kind = TableView::TableKind::DragonsKeep;
		break;
	case Tables::Skulduggery:
		View.Kind = TableView::TableKind::Skulduggery;
		break;
	default:
		View.Kind = TableView::TableKind::SpaceCadet;
		break;
	}
	TableRules::Current().FillView(View);
}

void TableView::Clear()
{
	View = TableView{};
	Table = nullptr;
	TableLights.clear();
	MissionLight = nullptr;
	RankCircle = nullptr;
	ProgressRing = nullptr;
	InfoMessage = nullptr;
	InfoEnd = 0;
}
