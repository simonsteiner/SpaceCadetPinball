#include "pch.h"
#include "DefinitiveHud.h"

#include "DefinitiveTheme.h"
#include "DefinitiveUi.h"
#include "TableView.h"
#include "translations.h"

#include <utility>

namespace
{
	using Theme = DefinitiveTheme;
	using Ui = DefinitiveUi;

	// The panel's width at scale 1, from the B1 mockup; the margin keeps it off the table and the window's edge
	constexpr float PanelWidth = 332;
	constexpr float Margin = 24;
	constexpr float Padding = 24;
	constexpr float Gap = 24;
	constexpr float LabelSize = 13;
	constexpr float LabelGap = 10;
	constexpr float NoteSize = 15;
	constexpr float MessageSize = 19;
	constexpr float ScoreSize = 30;
	constexpr float BallSize = 34;
	constexpr float OtherScoreSize = 18;
	constexpr float OtherGap = 8;
	constexpr float LampRadius = 6;
	constexpr float LampGap = 8;
	constexpr float MessageHeight = 92;
	// Room for 999,999,999, as the table's own score field has
	constexpr size_t ScorePlaces = 11;

	const char* Text(Msg id)
	{
		return translations::GetTranslation(id);
	}

	const char* PlayerName(int player)
	{
		return Text(static_cast<Msg>(std::to_underlying(Msg::STRING127) + std::clamp(player, 0, 3)));
	}

	// LED digits at a size that fits the width
	float FittedLedSize(float size, float width, const char* text)
	{
		auto needed = Theme::LedSize(size, text).x;
		return needed > width ? size * width / needed : size;
	}
} // namespace

const char* DefinitiveHud::MissionName(int mission)
{
	// control.cpp's MissionControl: states 2 to 18 are the missions in the order of MissionRcArray, from Target
	// Practice to Maelstrom, and the later states continue one of them: Alien Menace and Cosmic Plague in their second
	// part, the Secret mission's red and green stages, Time Warp's way back and Maelstrom's further parts
	if (mission == 20)
		mission = 10;
	else if (mission == 21)
		mission = 15;
	else if (mission == 22 || mission == 23)
		mission = 16;
	else if (mission == 24)
		mission = 17;
	else if (mission >= 25 && mission <= 31)
		mission = 18;
	if (mission < 2 || mission > 18)
		return nullptr;
	return Text(static_cast<Msg>(std::to_underlying(Msg::STRING161) + mission - 2));
}

const char* DefinitiveHud::RankName(int rank)
{
	if (rank < 1 || rank > 9)
		return nullptr;
	return Text(static_cast<Msg>(std::to_underlying(Msg::STRING185) + rank - 1));
}

// Draws what the panel shows from min down, at the scale DefinitiveUi has, with the message box at the bottom of the
// panel's height; returns the height the panel needs for it
float DefinitiveHud::DrawContents(ImDrawList* drawList, const TableView& view, ImVec2 min, ImVec2 max)
{
	auto scale = Ui::Scale();
	auto left = min.x + Padding * scale;
	auto width = max.x - Padding * scale - left;
	auto y = min.y + Padding * scale;
	auto label = [&](const char* text)
	{
		Ui::DrawText(drawList, Theme::TextFont(), LabelSize, ImVec2{left, y}, Theme::Chrome400, text, width);
		y += (LabelSize + LabelGap) * scale;
	};
	auto heading = [&](const char* text)
	{
		Ui::DrawText(drawList, Theme::HeadingFont(), Theme::HeadingSize, ImVec2{left, y}, Theme::Chrome50, text, width);
		y += Theme::HeadingSize * scale;
	};

	// The score of the player playing, as large as the panel allows
	auto player = std::clamp(view.CurrentPlayer, 0, static_cast<int>(view.Scores.size()) - 1);
	label(PlayerName(player));
	auto score = Ui::FormatScore(view.Scores[player], ScorePlaces);
	auto scoreSize = FittedLedSize(ScoreSize * scale, width, score.c_str());
	Theme::DrawLed(drawList, ImVec2{left, y}, scoreSize, score.c_str());
	y += Theme::LedSize(scoreSize, score.c_str()).y + Gap * scale;

	// The ball, as the table's panel numbers it, and blank where it shows none
	label(Text(Msg::Definitive_Ball));
	auto ball = view.BallCount > 0 ? std::to_string(view.MaxBallCount - view.BallCount + 1) : std::string{"!"};
	Theme::DrawLed(drawList, ImVec2{left, y}, BallSize * scale, ball.c_str(), true);
	y += Theme::LedSize(BallSize * scale, ball.c_str()).y + Gap * scale;

	// The others' scores, smaller and in chrome, so the eye stays on the one playing
	if (view.PlayerCount > 1)
	{
		for (auto other = 0; other < std::min(view.PlayerCount, static_cast<int>(view.Scores.size())); other++)
		{
			if (other == player)
				continue;
			auto text = Ui::FormatScore(view.Scores[other], 0);
			auto size = Theme::LedSize(OtherScoreSize * scale, text.c_str());
			auto rowHeight = std::max(size.y, NoteSize * scale);
			Ui::DrawText(drawList, Theme::TextFont(), NoteSize, ImVec2{left, y + rowHeight - NoteSize * scale},
						 Theme::Chrome200, PlayerName(other));
			Theme::DrawLed(drawList, ImVec2{left + width - size.x, y + rowHeight - size.y}, OtherScoreSize * scale,
						   text.c_str(), Theme::Chrome200);
			y += rowHeight + OtherGap * scale;
		}
		y += (Gap - OtherGap) * scale;
	}

	drawList->AddLine(ImVec2{left, y}, ImVec2{left + width, y}, Theme::WithAlpha(Theme::Chrome600, 0.6f));
	y += 1 + Gap * scale;

	// The mission that runs, and the mission text box, which says what it needs next or how to start one
	label(Text(Msg::Definitive_Mission));
	if (auto name = MissionName(view.Mission))
	{
		heading(name);
		y += 6 * scale;
	}
	if (!view.MissionText.empty())
		y += Ui::DrawWrapped(drawList, Theme::TextFont(), NoteSize, ImVec2{left, y}, Theme::Chrome200,
							 view.MissionText.c_str(), width);
	y += Gap * scale;

	// The rank and its nine lamps, lit up to it as the rank circle on the table is
	label(Text(Msg::Definitive_Rank));
	if (auto name = RankName(view.Rank))
	{
		heading(name);
		y += LabelGap * scale;
	}
	for (auto lamp = 0; lamp < 9; lamp++)
	{
		ImVec2 centre{left + (LampRadius + static_cast<float>(lamp) * (LampRadius * 2 + LampGap)) * scale,
					  y + LampRadius * scale};
		Theme::DrawLamp(drawList, centre, LampRadius * scale, lamp < view.Rank ? Theme::Lamp::Lit : Theme::Lamp::Dark);
	}
	y += (LampRadius * 2 + Gap) * scale;

	// The info text box's message at the bottom, in the LED orange of the table's text
	auto messageInset = ImVec2{16 * scale, 12 * scale};
	auto messageWidth = width - messageInset.x * 2;
	auto messageHeight = MessageHeight * scale;
	if (!view.InfoText.empty())
	{
		auto textHeight = Ui::DrawWrapped(nullptr, Theme::HeadingFont(), MessageSize, ImVec2{}, 0,
										  view.InfoText.c_str(), messageWidth);
		messageHeight = std::max(messageHeight, textHeight + messageInset.y * 2);
	}
	ImVec2 boxMin{left, std::max(y, max.y - Padding * scale - messageHeight)};
	ImVec2 boxMax{left + width, boxMin.y + messageHeight};
	drawList->AddRectFilled(boxMin, boxMax, Theme::Space950, 4 * scale);
	drawList->AddRect(boxMin, boxMax, Theme::Chrome600, 4 * scale);
	if (!view.InfoText.empty())
		Ui::DrawWrapped(drawList, Theme::HeadingFont(), MessageSize,
						ImVec2{boxMin.x + messageInset.x, boxMin.y + messageInset.y}, Theme::LedOrange,
						view.InfoText.c_str(), messageWidth);

	return boxMax.y + Padding * scale - min.y;
}

void DefinitiveHud::Render(const TableView& view, SDL_FRect board, SDL_FRect panel)
{
	if (panel.w < 1 || panel.h < 1)
		return;

	// The height the contents take at scale 1, drawn into a list that is thrown away
	auto screenScale = Ui::Scale();
	Ui::SetScale(1);
	ImDrawList scratch{ImGui::GetDrawListSharedData()};
	scratch._ResetForNewFrame();
	scratch.PushClipRectFullScreen();
	auto needed = DrawContents(&scratch, view, ImVec2{}, ImVec2{PanelWidth, 0});

	// The screens' scale, unless the room beside or under the table is smaller: the table's panel and the window's
	// margin beyond it
	auto viewport = ImGui::GetMainViewport();
	auto right = viewport->Pos.x + viewport->Size.x;
	auto bottom = viewport->Pos.y + viewport->Size.y;
	auto stacked = panel.y >= board.y + board.h - 1;
	ImVec2 room = stacked ? ImVec2{viewport->Size.x, bottom - panel.y} : ImVec2{right - panel.x, viewport->Size.y};
	auto scale = std::min({screenScale, room.x / (PanelWidth + Margin * 2), room.y / (needed + Margin)});
	Ui::SetScale(scale);

	// Beside the table and as tall as it is, unless the contents need more; under it in the stacked layout
	ImVec2 size{PanelWidth * scale, std::max(stacked ? panel.h - Margin * 2 * scale : board.h, needed * scale)};
	ImVec2 min{panel.x + Margin * scale, board.y + (board.h - size.y) / 2};
	if (stacked)
		min = ImVec2{board.x + (board.w - size.x) / 2, panel.y + Margin * scale};
	min.x = std::clamp(min.x, viewport->Pos.x, right - size.x);
	min.y = std::clamp(min.y, viewport->Pos.y, bottom - size.y);
	ImVec2 max{min.x + size.x, std::min(min.y + size.y, bottom)};

	auto drawList = ImGui::GetBackgroundDrawList();
	Theme::DrawPanel(drawList, min, max);
	drawList->PushClipRect(min, max, true);
	DrawContents(drawList, view, min, max);
	drawList->PopClipRect();
	Ui::SetScale(screenScale);
}
