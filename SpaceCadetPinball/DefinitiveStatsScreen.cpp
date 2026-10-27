#include "pch.h"
#include "DefinitiveStatsScreen.h"

#include "DefinitiveHud.h"
#include "DefinitiveStats.h"
#include "DefinitiveTheme.h"
#include "DefinitiveUi.h"
#include "translations.h"

int DefinitiveStatsScreen::Current = 0;
bool DefinitiveStatsScreen::Opened = false;
bool DefinitiveStatsScreen::ConfirmClear = false;
bool DefinitiveStatsScreen::BackChosen = false;

namespace
{
	using Theme = DefinitiveTheme;
	using Ui = DefinitiveUi;
	using Stat = DefinitiveStats::Stat;

	const char* Text(Msg id)
	{
		return translations::GetTranslation(id);
	}

	std::string Number(int value)
	{
		return Ui::FormatScore(value, 0);
	}

	// The day an achievement was unlocked, as year, month and day, which reads the same in every language
	std::string Day(long long seconds)
	{
		SDL_DateTime date{};
		if (!SDL_TimeToDateTime(SDL_SECONDS_TO_NS(seconds), &date, true))
			return {};
		char text[16];
		snprintf(text, sizeof text, "%04d-%02d-%02d", date.year, date.month, date.day);
		return text;
	}
} // namespace

void DefinitiveStatsScreen::Open()
{
	Current = 0;
	Opened = true;
	ConfirmClear = false;
}

bool DefinitiveStatsScreen::Render()
{
	auto scale = Ui::Scale();
	auto panel = Ui::BeginPanel("##DefinitiveStats", Text(Msg::Definitive_Stats), Opened);
	auto opened = Opened;
	Opened = false;

	auto tabsY = panel.Min.y + panel.Padding + 48 * scale;
	auto previous = Current;
	Current = Ui::Tabs(panel.DrawList, ImVec2{panel.Min.x + panel.Padding, tabsY}, panel.Size.x - panel.Padding * 2,
					   {Text(Msg::Definitive_Stats), Text(Msg::Definitive_Achievements)}, Current);

	// The rows scroll under the tabs, and the focus follows them; ImGui's Escape takes it off the rows too, and the
	// screen gives it back to the first
	if (Ui::BeginPanelRows(panel, tabsY + Ui::TabHeight * scale + 12 * scale) || opened || Current != previous)
	{
		Ui::FocusNextRow();
		ImGui::SetScrollY(0);
		ConfirmClear = false;
	}
	if (Current == 0)
		RenderStats();
	else
		RenderAchievements();
	if (Ui::MenuRow("back", Text(Msg::Definitive_Back)))
		BackChosen = true;
	Ui::EndPanelRows();

	auto back = BackChosen || Ui::PanelBack();
	BackChosen = false;
	Ui::EndPanel(panel, {{Ui::Action::Group, Text(Msg::Definitive_Group)},
						 {Ui::Action::Choose, Text(Msg::Definitive_Scroll)},
						 {Ui::Action::Back, Text(Msg::Definitive_Back)}});
	return !back;
}

void DefinitiveStatsScreen::RenderStats()
{
	auto scale = Ui::Scale();
	auto at = ImGui::GetCursorScreenPos();
	Ui::DrawText(ImGui::GetWindowDrawList(), Theme::TextFont(), Theme::LabelSize,
				 ImVec2{at.x + 16 * scale, at.y + 4 * scale}, Theme::Chrome400, Text(Msg::Definitive_StatsPlace),
				 ImGui::CalcItemWidth() - 32 * scale);
	ImGui::Dummy(ImVec2{0, Theme::LabelSize * Ui::TextScale() + 12 * scale});

	// Each stat is a row with its value at the end, so the keyboard and gamepad scroll through them
	auto bestRank = DefinitiveStats::Get(Stat::BestRank);
	auto rankName = DefinitiveHud::RankName(bestRank);
	const std::pair<Msg, std::string> rows[] = {
		{Msg::Definitive_StatGames, Number(DefinitiveStats::Get(Stat::Games))},
		{Msg::Definitive_StatBestScore, Number(DefinitiveStats::Get(Stat::BestScore))},
		{Msg::Definitive_StatMissions, Number(DefinitiveStats::Get(Stat::Missions))},
		{Msg::Definitive_StatBestRank, rankName ? rankName : "-"},
		{Msg::Definitive_StatBallsSaved, Number(DefinitiveStats::Get(Stat::BallsSaved))},
		{Msg::Definitive_StatExtraBalls, Number(DefinitiveStats::Get(Stat::ExtraBalls))},
		{Msg::Definitive_StatMultiballs, Number(DefinitiveStats::Get(Stat::Multiballs))},
	};
	auto index = 0;
	for (const auto& [label, value] : rows)
	{
		ImGui::PushID(index++);
		Ui::MenuRow("stat", Text(label), false, value.c_str());
		ImGui::PopID();
	}

	// Every mission by name, with how often it was finished
	ImGui::Dummy(ImVec2{0, 12 * scale});
	at = ImGui::GetCursorScreenPos();
	Ui::DrawText(ImGui::GetWindowDrawList(), Theme::TextFont(), Theme::LabelSize,
				 ImVec2{at.x + 16 * scale, at.y + 4 * scale}, Theme::Chrome400, Text(Msg::Definitive_StatEachMission),
				 ImGui::CalcItemWidth() - 32 * scale);
	ImGui::Dummy(ImVec2{0, Theme::LabelSize * Ui::TextScale() + 12 * scale});
	for (auto mission = 2; mission <= 18; mission++)
	{
		ImGui::PushID(mission);
		auto count = DefinitiveStats::MissionsFinished(mission);
		Ui::MenuRow("mission", DefinitiveHud::MissionName(mission), false, count ? Number(count).c_str() : "-");
		ImGui::PopID();
	}

	// Clearing asks once, as High Scores' does
	if (!ConfirmClear)
	{
		if (Ui::MenuRow("clear", Text(Msg::Definitive_ClearStats), true))
		{
			ConfirmClear = true;
			Ui::FocusNextRow();
		}
	}
	else
	{
		Ui::NextRowNote(Text(Msg::Definitive_ClearStatsConfirm));
		auto cancelled = Ui::MenuRow("cancel", Text(Msg::GenericCancel));
		if (Ui::MenuRow("confirm", Text(Msg::GenericOk), true))
		{
			DefinitiveStats::Clear();
			cancelled = true;
		}
		if (cancelled)
		{
			ConfirmClear = false;
			Ui::FocusNextRow();
		}
	}
}

void DefinitiveStatsScreen::RenderAchievements()
{
	auto achievements = DefinitiveStats::Achievements();
	for (size_t index = 0; index < achievements.size(); index++)
	{
		const auto& achievement = achievements[index];
		auto unlocked = DefinitiveStats::UnlockedAt(index);
		// An unlocked one says when in the LED orange of a tag, a locked one says so in chrome
		auto day = unlocked ? Ui::Fill(Text(Msg::Definitive_Unlocked), Day(unlocked)) : std::string{};
		ImGui::PushID(static_cast<int>(index));
		Ui::ParagraphRow("achievement", Text(achievement.Name), unlocked ? nullptr : Text(Msg::Definitive_Locked),
						 Text(achievement.Description), unlocked ? day.c_str() : nullptr);
		ImGui::PopID();
	}
}

void DefinitiveStatsScreen::RenderNotice()
{
	// Each notice shows for a few seconds from when it first draws, then the next one waiting
	constexpr Uint64 NoticeTime = 4000;
	static auto shownSince = Uint64{0};
	auto recent = DefinitiveStats::Unannounced();
	if (recent < 0)
		return;
	auto now = SDL_GetTicks();
	if (!shownSince)
		shownSince = now;
	if (now - shownSince > NoticeTime)
	{
		DefinitiveStats::Announced();
		shownSince = 0;
		return;
	}

	// A panel over the top of the window with a lit lamp, the kind of notice the edition hint is; it neither slides
	// nor fades, so Reduced Motion has nothing to hold still
	const auto& achievement = DefinitiveStats::Achievements()[static_cast<size_t>(recent)];
	auto viewport = ImGui::GetMainViewport();
	auto scale = Ui::Scale();
	auto padding = 16 * scale;
	auto lampRadius = 7 * scale;
	auto heading = Text(Msg::Definitive_AchievementUnlocked);
	auto name = Text(achievement.Name);
	auto headingSize = Ui::TextSize(Theme::TextFont(), Theme::LabelSize, heading);
	auto nameSize = Ui::TextSize(Theme::HeadingFont(), Theme::HeadingSize, name);
	auto textWidth = std::max(headingSize.x, nameSize.x);
	ImVec2 size{padding * 3 + lampRadius * 2 + textWidth, padding * 2 + headingSize.y + 4 * scale + nameSize.y};
	ImVec2 min{viewport->Pos.x + (viewport->Size.x - size.x) / 2, viewport->Pos.y + 24 * scale};
	auto drawList = ImGui::GetForegroundDrawList();
	Theme::DrawPanel(drawList, min, ImVec2{min.x + size.x, min.y + size.y});
	Theme::DrawLamp(drawList, ImVec2{min.x + padding + lampRadius, min.y + size.y / 2}, lampRadius, Theme::Lamp::Lit);
	auto textX = min.x + padding * 2 + lampRadius * 2;
	Ui::DrawText(drawList, Theme::TextFont(), Theme::LabelSize, ImVec2{textX, min.y + padding}, Theme::Chrome400,
				 heading, textWidth);
	Ui::DrawText(drawList, Theme::HeadingFont(), Theme::HeadingSize,
				 ImVec2{textX, min.y + padding + headingSize.y + 4 * scale}, Theme::Chrome50, name, textWidth);
}
