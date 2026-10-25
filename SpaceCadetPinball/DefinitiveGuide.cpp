#include "pch.h"
#include "DefinitiveGuide.h"

#include "DefinitiveHud.h"
#include "DefinitiveTheme.h"
#include "DefinitiveUi.h"
#include "TableView.h"
#include "translations.h"

int DefinitiveGuide::Current = 0;
int DefinitiveGuide::Focus = -1;
int DefinitiveGuide::FocusFrames = 0;
bool DefinitiveGuide::Opened = false;
bool DefinitiveGuide::BackChosen = false;

namespace
{
	using Theme = DefinitiveTheme;
	using Ui = DefinitiveUi;

	const char* Text(Msg id)
	{
		return translations::GetTranslation(id);
	}

	// The missions each pair of ranks is offered, as control.cpp's SelectMissionController picks them: by the upper,
	// middle and lower mission target, and by all three. The numbers are mission states, as DefinitiveHud::MissionOf.
	struct Tier
	{
		int FirstRank, LastRank;
		int Missions[4];
	};

	constexpr Tier Tiers[] = {
		{1, 1, {3, 4, 2, 5}},     // Cadet
		{2, 3, {9, 11, 10, 16}},  // Ensign and Lieutenant
		{4, 5, {6, 8, 7, 15}},    // Captain and Lt Commander
		{6, 7, {12, 13, 14, 17}}, // Commander and Commodore
		{8, 9, {15, 16, 17, 18}}, // Admiral and Fleet Admiral
	};

	constexpr Msg Targets[] = {Msg::Definitive_GuideUpper, Msg::Definitive_GuideMiddle, Msg::Definitive_GuideLower,
							   Msg::Definitive_GuideAll};

	// The tab of a rank's missions, after the basics' tab 0
	int TabOf(int rank)
	{
		auto tab = 1;
		for (const auto& tier : Tiers)
		{
			if (rank <= tier.LastRank)
				return tab;
			tab++;
		}
		return tab - 1;
	}

	const char* GuideText(int mission)
	{
		return Text(static_cast<Msg>(static_cast<int>(Msg::Definitive_GuideTargetPractice) + mission - 2));
	}
} // namespace

void DefinitiveGuide::Open(const TableView& view)
{
	Current = TabOf(view.Rank);
	// The row of the mission that runs, if the tab has it
	Focus = -1;
	auto running = DefinitiveHud::MissionOf(view.Mission);
	const auto& tier = Tiers[Current - 1];
	for (auto index = 0; index < 4; index++)
	{
		if (tier.Missions[index] == running)
			Focus = index;
	}
	FocusFrames = 3;
	Opened = true;
}

bool DefinitiveGuide::Render(const TableView& view)
{
	auto viewport = ImGui::GetMainViewport();
	auto scale = Ui::Scale();
	auto margin = 24 * scale;
	ImVec2 min{viewport->Pos.x + margin, viewport->Pos.y + margin};
	ImVec2 size{std::min(640 * scale, viewport->Size.x - margin * 2), viewport->Size.y - margin * 2};

	ImGui::SetNextWindowPos(min);
	ImGui::SetNextWindowSize(size);
	if (Opened || (!ImGui::GetTopMostPopupModal() && !GImGui->NavWindow))
		ImGui::SetNextWindowFocus();
	auto opened = Opened;
	Opened = false;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{});
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, 2 * scale});
	ImGui::PushStyleColor(ImGuiCol_NavCursor, 0U);
	ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, 0U);
	ImGui::Begin("##DefinitiveGuide", nullptr,
				 Ui::Combine(ImGuiWindowFlags_NoDecoration, ImGuiWindowFlags_NoBackground, ImGuiWindowFlags_NoMove,
							 ImGuiWindowFlags_NoSavedSettings, ImGuiWindowFlags_NoScrollWithMouse));
	auto drawList = ImGui::GetWindowDrawList();
	Theme::DrawPanel(drawList, min, ImVec2{min.x + size.x, min.y + size.y});
	auto padding = 24 * scale;
	auto innerWidth = size.x - padding * 2;
	Ui::DrawText(drawList, Theme::HeadingFont(), Theme::TitleSize, ImVec2{min.x + padding, min.y + padding},
				 Theme::Chrome50, Text(Msg::Definitive_MissionGuide), innerWidth);

	// The basics, then each pair of ranks by the name of the first
	auto tabsY = min.y + padding + 48 * scale;
	std::vector<const char*> tabs{Text(Msg::Definitive_GuideBasics)};
	for (const auto& tier : Tiers)
		tabs.push_back(DefinitiveHud::RankName(tier.FirstRank));
	auto previous = Current;
	Current = Ui::Tabs(drawList, ImVec2{min.x + padding, tabsY}, innerWidth, tabs, Current);
	if (Current != previous)
	{
		Focus = 0;
		FocusFrames = 3;
	}

	// The rows scroll under the tabs, and the focus follows them
	auto rowsY = tabsY + Ui::TabHeight * scale + 12 * scale;
	auto hintsHeight = 26 * scale + padding * 1.5f;
	ImGui::SetCursorScreenPos(ImVec2{min.x + padding / 2, rowsY});
	ImGui::BeginChild("##rows", ImVec2{size.x - padding, min.y + size.y - hintsHeight - rowsY},
					  ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
	if (opened || Current != previous)
		ImGui::SetScrollY(0);
	// ImGui's Escape takes the focus off the rows too; the screen gives it back to the first
	if ((opened || (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !GImGui->NavId)) && Focus < 0)
	{
		Focus = 0;
		FocusFrames = 3;
	}
	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - (ImGui::GetScrollMaxY() > 0 ? 12 * scale : 0));
	auto row = 0;
	// ImGui gives a window that takes the focus its first item at the end of the frame, so the row asks again in the
	// next few frames
	auto paragraph = [&](const char* id, const char* title, const char* aside, const char* text, const char* tag)
	{
		auto focus = Focus == row++;
		if (focus)
			Ui::FocusNextRow();
		Ui::ParagraphRow(id, title, aside, text, tag);
		if (focus && --FocusFrames <= 0)
			Focus = -1;
	};

	if (Current == 0)
	{
		paragraph("missions", Text(Msg::Definitive_GuideMissionsTitle), nullptr, Text(Msg::Definitive_GuideMissions),
				  nullptr);
		paragraph("ranks", Text(Msg::Definitive_GuideRanksTitle), nullptr, Text(Msg::Definitive_GuideRanks), nullptr);
		// Only with Full Tilt's data, whose rules score missions differently
		if (view.FullTilt)
			paragraph("fulltilt", Text(Msg::Definitive_GuideFullTiltTitle), nullptr,
					  Text(Msg::Definitive_GuideFullTilt), nullptr);
	}
	else
	{
		// Which ranks the tab is for, then its four missions by the target that chooses them
		const auto& tier = Tiers[Current - 1];
		std::string ranks = DefinitiveHud::RankName(tier.FirstRank);
		if (tier.LastRank != tier.FirstRank)
			ranks = ranks + " · " + DefinitiveHud::RankName(tier.LastRank);
		char offered[128];
		snprintf(offered, sizeof offered, Text(Msg::Definitive_GuideOffered), ranks.c_str());
		auto at = ImGui::GetCursorScreenPos();
		Ui::DrawText(ImGui::GetWindowDrawList(), Theme::TextFont(), Theme::LabelSize,
					 ImVec2{at.x + 16 * scale, at.y + 4 * scale}, Theme::Chrome400, offered,
					 ImGui::CalcItemWidth() - 32 * scale);
		ImGui::Dummy(ImVec2{0, (Theme::LabelSize + 12) * scale});
		auto running = DefinitiveHud::MissionOf(view.Mission);
		for (auto index = 0; index < 4; index++)
		{
			auto mission = tier.Missions[index];
			ImGui::PushID(index);
			paragraph("mission", DefinitiveHud::MissionName(mission), Text(Targets[index]), GuideText(mission),
					  mission == running ? Text(Msg::Definitive_GuideNow) : nullptr);
			ImGui::PopID();
		}
	}
	// Back, for a mouse or touch, which have no hints
	if (Ui::MenuRow("back", Text(Msg::Definitive_Back)))
		BackChosen = true;
	ImGui::PopItemWidth();
	ImGui::EndChild();

	auto back = BackChosen || (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
							   (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
								ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
								ImGui::IsMouseClicked(ImGuiMouseButton_Right)));
	BackChosen = false;
	Ui::DrawHints(drawList, ImVec2{min.x + padding, min.y + size.y - padding - 26 * scale},
				  {{Ui::Action::Group, Text(Msg::Definitive_Group)},
				   {Ui::Action::Choose, Text(Msg::Definitive_Scroll)},
				   {Ui::Action::Back, Text(Msg::Definitive_Back)}});
	ImGui::End();
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(3);
	return !back;
}
