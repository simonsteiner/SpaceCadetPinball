#include "pch.h"
#include "DefinitiveHud.h"

#include "DefinitiveTheme.h"
#include "DefinitiveUi.h"
#include "TableView.h"
#include "options.h"
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
	// The band under the table in a window taller than wide: no wider than this at scale 1, and its two columns side
	// by side from this width on
	constexpr float BandMaxWidth = 720;
	constexpr float BandColumnsWidth = 440;

	const char* Text(Msg id)
	{
		return translations::GetTranslation(id);
	}

	// The HUD's text colours, all white in high contrast
	ImU32 LabelColor()
	{
		return Theme::HighContrast() ? Theme::Chrome50 : Theme::Chrome400;
	}

	ImU32 NoteColor()
	{
		return Theme::HighContrast() ? Theme::Chrome50 : Theme::Chrome200;
	}

	// The info message's box, black with a white rim in high contrast
	void DrawMessageBox(ImDrawList* drawList, ImVec2 min, ImVec2 max, float scale)
	{
		auto contrast = Theme::HighContrast();
		drawList->AddRectFilled(min, max, contrast ? DefinitiveColor(0, 0, 0) : Theme::Space950, 4 * scale);
		drawList->AddRect(min, max, contrast ? Theme::Chrome50 : Theme::Chrome600, 4 * scale,
						  contrast ? 2 * scale : 1.0f);
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

	// <count> lamps in a row from a point, lit where <lit> has the lamp's bit, from bit 0 for the first
	void DrawLamps(ImDrawList* drawList, ImVec2 at, int count, unsigned lit, float scale)
	{
		for (auto lamp = 0; lamp < count; lamp++)
		{
			ImVec2 centre{at.x + (LampRadius + static_cast<float>(lamp) * (LampRadius * 2 + LampGap)) * scale,
						  at.y + LampRadius * scale};
			Theme::DrawLamp(drawList, centre, LampRadius * scale,
							lit & (1u << static_cast<unsigned>(lamp)) ? Theme::Lamp::Lit : Theme::Lamp::Dark);
		}
	}

	// The first <count> lamps lit, as a mask for DrawLamps
	unsigned FirstLamps(int count)
	{
		return (1u << static_cast<unsigned>(std::clamp(count, 0, 31))) - 1;
	}

	float LampsWidth(int count, float scale)
	{
		return (static_cast<float>(count) * (LampRadius * 2 + LampGap) - LampGap) * scale;
	}

	// The rank's nine lamps in a row from a point, lit up to the rank as the rank circle on the table is
	void DrawRankLamps(ImDrawList* drawList, ImVec2 at, int rank, float scale)
	{
		DrawLamps(drawList, at, 9, FirstLamps(std::min(rank, 9)), scale);
	}

	// Dragon's Keep's quests by their state, 4 to 8, and Slay Dragon, 9; null for another state
	const char* QuestName(int state)
	{
		if (state < 4 || state > 9)
			return nullptr;
		return Text(static_cast<Msg>(std::to_underlying(Msg::Definitive_DragonHoard) + state - 4));
	}

	// Dragon's Keep's multiball modes by their state, 13 to 15; null for another state
	const char* ModeName(int state)
	{
		if (state < 13 || state > 15)
			return nullptr;
		return Text(static_cast<Msg>(std::to_underlying(Msg::Dragon_TowerBarrage) + state - 13));
	}

	// A label on the left of a row and what <draw> puts at its right end, <width> wide and <height> high; returns the
	// row's bottom
	template <typename Draw>
	float DrawRow(ImDrawList* drawList, const char* label, ImVec2 at, float width, float contentWidth, float height,
				  Draw draw)
	{
		auto labelHeight = LabelSize * Ui::TextScale();
		auto rowHeight = std::max(height, labelHeight);
		Ui::DrawText(drawList, Theme::TextFont(), LabelSize, ImVec2{at.x, at.y + (rowHeight - labelHeight) / 2},
					 LabelColor(), label, width - contentWidth - LabelGap * Ui::Scale());
		draw(ImVec2{at.x + width - contentWidth, at.y + (rowHeight - height) / 2});
		return at.y + rowHeight;
	}

	// Dragon's Keep in place of the mission: the quest that runs, its restart, Slay Dragon or a multiball mode, with
	// its time left; what it needs or the quest picked next; and the five quests' lamps with the quests done. Returns
	// the bottom.
	float DrawDragonQuest(ImDrawList* drawList, const TableView& view, ImVec2 at, float width)
	{
		auto scale = Ui::Scale();
		const auto& dragon = view.Dragon;
		auto y = at.y;
		auto mode = ModeName(dragon.State);
		Ui::DrawText(drawList, Theme::TextFont(), LabelSize, ImVec2{at.x, y}, LabelColor(),
					 Text(mode ? Msg::Definitive_Multiball : Msg::Definitive_Quest), width);
		y += LabelSize * Ui::TextScale() + LabelGap * scale;

		std::string name;
		if (mode)
			name = mode;
		else if (dragon.State == 3 && QuestName(dragon.RestartQuest))
			name = Ui::Fill(Text(Msg::Definitive_RestartQuest), QuestName(dragon.RestartQuest));
		else if (auto quest = QuestName(dragon.State))
			name = quest;
		if (!name.empty())
		{
			// The seconds left at the end of the line, as the quest's own text counts them
			auto time = dragon.TimeLeft > 0 ? std::to_string(dragon.TimeLeft / 1000) : std::string{};
			auto timeWidth = time.empty() ? 0.0f : Theme::LedSize(Theme::HeadingSize * scale, time.c_str()).x;
			Ui::DrawText(drawList, Theme::HeadingFont(), Theme::HeadingSize, ImVec2{at.x, y}, Theme::Chrome50,
						 name.c_str(), width - timeWidth - (time.empty() ? 0 : LabelGap * scale));
			if (!time.empty())
				Theme::DrawLed(drawList, ImVec2{at.x + width - timeWidth, y}, Theme::HeadingSize * scale, time.c_str());
			y += Theme::HeadingSize * Ui::TextScale() + 6 * scale;
		}

		std::string note;
		if (dragon.State == 9)
			note = Ui::Fill(Text(Msg::Definitive_HitsLeft), std::to_string(dragon.HitsLeft));
		else if (auto picked = QuestName(dragon.PickedQuest); picked && dragon.State != dragon.PickedQuest)
			note = Ui::Fill(Text(Msg::Definitive_NextQuest), picked);
		if (!note.empty())
			y += Ui::DrawWrapped(drawList, Theme::TextFont(), NoteSize, ImVec2{at.x, y}, NoteColor(), note.c_str(),
								 width) +
				 LabelGap * scale;

		// The round's five quests lit as their lights on the table, and the game's count beside them
		auto lampsWidth = LampsWidth(5, scale);
		DrawLamps(drawList, ImVec2{at.x, y}, 5, dragon.QuestLights, scale);
		auto done = Ui::Fill(Text(Msg::Definitive_QuestsDone), std::to_string(dragon.QuestsDone));
		auto noteHeight = NoteSize * Ui::TextScale();
		auto lampsHeight = LampRadius * 2 * scale;
		Ui::DrawText(drawList, Theme::TextFont(), NoteSize,
					 ImVec2{at.x + lampsWidth + Gap * scale, y + (lampsHeight - noteHeight) / 2}, NoteColor(),
					 done.c_str(), width - lampsWidth - Gap * scale);
		return y + std::max(lampsHeight, noteHeight);
	}

	// Dragon's Keep in place of the rank: the jackpot and the second, and the dragon eggs, weapons and armor lit as
	// their lights on the table. Returns the bottom.
	float DrawDragonItems(ImDrawList* drawList, const TableView& view, ImVec2 at, float width)
	{
		auto scale = Ui::Scale();
		const auto& dragon = view.Dragon;
		auto y = at.y;
		for (auto [label, value] :
			 {std::pair{Msg::Definitive_Jackpot, dragon.Jackpot}, {Msg::Definitive_SuperJackpot, dragon.SuperJackpot}})
		{
			auto text = Ui::FormatScore(value, 0);
			auto size = Theme::LedSize(OtherScoreSize * scale, text.c_str());
			y = DrawRow(drawList, Text(label), ImVec2{at.x, y}, width, size.x, size.y, [&](ImVec2 position)
						{ Theme::DrawLed(drawList, position, OtherScoreSize * scale, text.c_str()); }) +
				OtherGap * scale;
		}
		for (auto [label, count, lit] :
			 {std::tuple{Msg::Definitive_DragonEggs, 8, FirstLamps(std::min(dragon.Eggs, 8))},
			  {Msg::Definitive_Weapons, 4, FirstLamps(std::min(dragon.Weapons, 4))},
			  {Msg::Definitive_Armor, 3, dragon.Armor}})
		{
			y = DrawRow(drawList, Text(label), ImVec2{at.x, y + OtherGap * scale}, width, LampsWidth(count, scale),
						LampRadius * 2 * scale,
						[&](ImVec2 position) { DrawLamps(drawList, position, count, lit, scale); });
		}
		return y;
	}

	// The info text box's message in a box, in the LED orange of the table's text: from the top of its place at the
	// earliest, else ending at bottom; returns the box's bottom. Without a draw list it only measures.
	float DrawMessage(ImDrawList* drawList, const TableView& view, ImVec2 at, float width, float bottom)
	{
		auto scale = Ui::Scale();
		ImVec2 inset{16 * scale, 12 * scale};
		auto textWidth = width - inset.x * 2;
		auto height = MessageHeight * scale;
		if (!view.InfoText.empty())
		{
			auto textHeight = Ui::DrawWrapped(nullptr, Theme::HeadingFont(), MessageSize, ImVec2{}, 0,
											  view.InfoText.c_str(), textWidth);
			height = std::max(height, textHeight + inset.y * 2);
		}
		ImVec2 boxMin{at.x, std::max(at.y, bottom - height)};
		ImVec2 boxMax{at.x + width, boxMin.y + height};
		if (!drawList)
			return boxMax.y;
		DrawMessageBox(drawList, boxMin, boxMax, scale);
		if (!view.InfoText.empty())
			Ui::DrawWrapped(drawList, Theme::HeadingFont(), MessageSize, ImVec2{boxMin.x + inset.x, boxMin.y + inset.y},
							Theme::LedOrange, view.InfoText.c_str(), textWidth);
		return boxMax.y;
	}
} // namespace

int DefinitiveHud::MissionOf(int state)
{
	// control.cpp's MissionControl: states 2 to 18 are the missions in the order of MissionRcArray, from Target
	// Practice to Maelstrom, and the later states continue one of them: Alien Menace and Cosmic Plague in their second
	// part, the Secret mission's red and green stages, Time Warp's way back and Maelstrom's further parts
	if (state == 20)
		return 10;
	if (state == 21)
		return 15;
	if (state == 22 || state == 23)
		return 16;
	if (state == 24)
		return 17;
	if (state >= 25 && state <= 31)
		return 18;
	return state >= 2 && state <= 18 ? state : 0;
}

const char* DefinitiveHud::MissionName(int mission)
{
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
		Ui::DrawText(drawList, Theme::TextFont(), LabelSize, ImVec2{left, y}, LabelColor(), text, width);
		y += LabelSize * Ui::TextScale() + LabelGap * scale;
	};
	auto heading = [&](const char* text)
	{
		Ui::DrawText(drawList, Theme::HeadingFont(), Theme::HeadingSize, ImVec2{left, y}, Theme::Chrome50, text, width);
		y += Theme::HeadingSize * Ui::TextScale();
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
			auto rowHeight = std::max(size.y, NoteSize * Ui::TextScale());
			Ui::DrawText(drawList, Theme::TextFont(), NoteSize,
						 ImVec2{left, y + rowHeight - NoteSize * Ui::TextScale()}, NoteColor(), PlayerName(other));
			Theme::DrawLed(drawList, ImVec2{left + width - size.x, y + rowHeight - size.y}, OtherScoreSize * scale,
						   text.c_str(), NoteColor());
			y += rowHeight + OtherGap * scale;
		}
		y += (Gap - OtherGap) * scale;
	}

	drawList->AddLine(ImVec2{left, y}, ImVec2{left + width, y},
					  Theme::HighContrast() ? Theme::Chrome200 : Theme::WithAlpha(Theme::Chrome600, 0.6f));
	y += 1 + Gap * scale;

	if (view.Kind == TableView::TableKind::DragonsKeep)
	{
		y = DrawDragonQuest(drawList, view, ImVec2{left, y}, width) + Gap * scale;
		y = DrawDragonItems(drawList, view, ImVec2{left, y}, width) + Gap * scale;
		return DrawMessage(drawList, view, ImVec2{left, y}, width, max.y - Padding * scale) + Padding * scale - min.y;
	}

	// The mission that runs, and the mission text box, which says what it needs next or how to start one
	label(Text(Msg::Definitive_Mission));
	if (auto name = MissionName(MissionOf(view.Mission)))
	{
		heading(name);
		y += 6 * scale;
	}
	if (!view.MissionText.empty())
		y += Ui::DrawWrapped(drawList, Theme::TextFont(), NoteSize, ImVec2{left, y}, NoteColor(),
							 view.MissionText.c_str(), width);
	y += Gap * scale;

	// The rank and its nine lamps, lit up to it as the rank circle on the table is
	label(Text(Msg::Definitive_Rank));
	if (auto name = RankName(view.Rank))
	{
		heading(name);
		y += LabelGap * scale;
	}
	DrawRankLamps(drawList, ImVec2{left, y}, view.Rank, scale);
	y += (LampRadius * 2 + Gap) * scale;

	// The info text box's message at the bottom
	return DrawMessage(drawList, view, ImVec2{left, y}, width, max.y - Padding * scale) + Padding * scale - min.y;
}

float DefinitiveHud::DrawBand(ImDrawList* drawList, const TableView& view, ImVec2 min, ImVec2 max)
{
	auto scale = Ui::Scale();
	auto left = min.x + Padding * scale;
	auto width = max.x - Padding * scale - left;
	auto y = min.y + Padding * scale;
	auto label = [&](const char* text, float x, float columnWidth, float top)
	{
		Ui::DrawText(drawList, Theme::TextFont(), LabelSize, ImVec2{x, top}, LabelColor(), text, columnWidth);
		return top + LabelSize * Ui::TextScale() + LabelGap * scale;
	};

	// The score of the player playing and the ball side by side, the ball at the end
	auto player = std::clamp(view.CurrentPlayer, 0, static_cast<int>(view.Scores.size()) - 1);
	auto ball = view.BallCount > 0 ? std::to_string(view.MaxBallCount - view.BallCount + 1) : std::string{"!"};
	auto ballSize = Theme::LedSize(BallSize * scale, ball.c_str());
	auto ballWidth = std::max(ballSize.x, Ui::TextSize(Theme::TextFont(), LabelSize, Text(Msg::Definitive_Ball)).x);
	auto scoreWidth = width - ballWidth - Gap * scale;
	auto ledTop = label(PlayerName(player), left, scoreWidth, y);
	label(Text(Msg::Definitive_Ball), left + width - ballWidth, ballWidth, y);
	auto score = Ui::FormatScore(view.Scores[player], ScorePlaces);
	auto scoreSize = FittedLedSize(ScoreSize * scale, scoreWidth, score.c_str());
	Theme::DrawLed(drawList, ImVec2{left, ledTop}, scoreSize, score.c_str());
	Theme::DrawLed(drawList, ImVec2{left + width - ballSize.x, ledTop}, BallSize * scale, ball.c_str(), true);
	y = ledTop + std::max(Theme::LedSize(scoreSize, score.c_str()).y, ballSize.y) + Gap * scale;

	// The others' scores in a row of up to three, smaller and in chrome
	if (view.PlayerCount > 1)
	{
		auto others = std::min(view.PlayerCount, static_cast<int>(view.Scores.size())) - 1;
		auto cellWidth = (width - Gap * scale * static_cast<float>(others - 1)) / static_cast<float>(others);
		auto x = left;
		auto rowHeight = 0.0f;
		for (auto other = 0; other <= others; other++)
		{
			if (other == player)
				continue;
			auto text = Ui::FormatScore(view.Scores[other], 0);
			auto size = FittedLedSize(OtherScoreSize * scale, cellWidth, text.c_str());
			auto ledTopOther = label(PlayerName(other), x, cellWidth, y);
			Theme::DrawLed(drawList, ImVec2{x, ledTopOther}, size, text.c_str(), NoteColor());
			rowHeight = std::max(rowHeight, ledTopOther + Theme::LedSize(size, text.c_str()).y - y);
			x += cellWidth + Gap * scale;
		}
		y += rowHeight + Gap * scale;
	}

	drawList->AddLine(ImVec2{left, y}, ImVec2{left + width, y},
					  Theme::HighContrast() ? Theme::Chrome200 : Theme::WithAlpha(Theme::Chrome600, 0.6f));
	y += 1 + Gap * scale;

	// The mission and the rank, side by side where the band is wide enough, else one under the other
	auto columns = width >= BandColumnsWidth * scale;
	auto columnWidth = columns ? (width - Gap * scale) / 2 : width;
	if (view.Kind == TableView::TableKind::DragonsKeep)
	{
		auto questY = DrawDragonQuest(drawList, view, ImVec2{left, y}, columnWidth);
		auto itemsLeft = columns ? left + columnWidth + Gap * scale : left;
		auto itemsY =
			DrawDragonItems(drawList, view, ImVec2{itemsLeft, columns ? y : questY + Gap * scale}, columnWidth);
		y = std::max(questY, itemsY) + Gap * scale;
		return DrawMessage(drawList, view, ImVec2{left, y}, width, max.y - Padding * scale) + Padding * scale - min.y;
	}
	auto missionY = label(Text(Msg::Definitive_Mission), left, columnWidth, y);
	if (auto name = MissionName(MissionOf(view.Mission)))
	{
		Ui::DrawText(drawList, Theme::HeadingFont(), Theme::HeadingSize, ImVec2{left, missionY}, Theme::Chrome50, name,
					 columnWidth);
		missionY += Theme::HeadingSize * Ui::TextScale() + 6 * scale;
	}
	if (!view.MissionText.empty())
		missionY += Ui::DrawWrapped(drawList, Theme::TextFont(), NoteSize, ImVec2{left, missionY}, NoteColor(),
									view.MissionText.c_str(), columnWidth);

	auto rankLeft = columns ? left + columnWidth + Gap * scale : left;
	auto rankY = label(Text(Msg::Definitive_Rank), rankLeft, columnWidth, columns ? y : missionY + Gap * scale);
	if (auto name = RankName(view.Rank))
	{
		Ui::DrawText(drawList, Theme::HeadingFont(), Theme::HeadingSize, ImVec2{rankLeft, rankY}, Theme::Chrome50, name,
					 columnWidth);
		rankY += Theme::HeadingSize * Ui::TextScale() + LabelGap * scale;
	}
	DrawRankLamps(drawList, ImVec2{rankLeft, rankY}, view.Rank, scale);
	rankY += LampRadius * 2 * scale;
	y = std::max(missionY, rankY) + Gap * scale;

	// The info text box's message, across the band at its bottom
	return DrawMessage(drawList, view, ImVec2{left, y}, width, max.y - Padding * scale) + Padding * scale - min.y;
}

void DefinitiveHud::RenderBand(const TableView& view, SDL_FRect panel)
{
	// The screens' scale, unless the band is too low for the contents at its width; a smaller scale leaves more width
	// at scale 1 and so less wrapping, so the second pass fits
	auto screenScale = Ui::Scale();
	ImDrawList scratch{ImGui::GetDrawListSharedData()};
	scratch._ResetForNewFrame();
	scratch.PushClipRectFullScreen();
	auto scale = screenScale;
	auto width = 0.0f;
	auto needed = 0.0f;
	for (auto pass = 0; pass < 2; pass++)
	{
		width = std::min(panel.w / scale - Margin * 2, BandMaxWidth);
		Ui::SetScale(1);
		needed = DrawBand(&scratch, view, ImVec2{}, ImVec2{width, 0});
		// Within the band's width too, which the scale before set
		scale = std::min({screenScale, panel.h / (needed + Margin * 2), panel.w / (width + Margin * 2)});
	}
	Ui::SetScale(scale);

	// Centred across the band and just under the table, as high as its contents
	ImVec2 size{width * scale, std::min(needed * scale, panel.h - Margin * 2 * scale)};
	ImVec2 min{panel.x + (panel.w - size.x) / 2, panel.y + Margin * scale};
	ImVec2 max{min.x + size.x, min.y + size.y};
	auto drawList = ImGui::GetBackgroundDrawList();
	Theme::DrawPanel(drawList, min, max);
	drawList->PushClipRect(min, max, true);
	DrawBand(drawList, view, min, max);
	drawList->PopClipRect();
	Ui::SetScale(screenScale);
}

void DefinitiveHud::Render(const TableView& view, SDL_FRect board, SDL_FRect panel, bool band)
{
	if (panel.w < 1 || panel.h < 1)
		return;
	// The panel's pieces in high contrast while it draws, and the screens' as before after it
	struct Contrast
	{
		Contrast()
		{
			Theme::SetHighContrast(options::Options.HighContrastHud);
		}
		~Contrast()
		{
			Theme::SetHighContrast(false);
		}
		Contrast(const Contrast&) = delete;
		Contrast& operator=(const Contrast&) = delete;
	};
	Contrast contrast;
	if (band)
	{
		RenderBand(view, panel);
		return;
	}

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
