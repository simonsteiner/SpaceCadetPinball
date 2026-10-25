#include "pch.h"
#include "DefinitiveGuide.h"

#include "DefinitiveHud.h"
#include "DefinitiveText.h"
#include "DefinitiveTheme.h"
#include "DefinitiveUi.h"
#include "TableView.h"
#include "translations.h"

#include <algorithm>
#include <array>
#include <utility>

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
		int FirstRank;
		int LastRank;
		std::array<int, 4> Missions;
	};

	constexpr auto Tiers = std::to_array<Tier>({
		{1, 1, {3, 4, 2, 5}},     // Cadet
		{2, 3, {9, 11, 10, 16}},  // Ensign and Lieutenant
		{4, 5, {6, 8, 7, 15}},    // Captain and Lt Commander
		{6, 7, {12, 13, 14, 17}}, // Commander and Commodore
		{8, 9, {15, 16, 17, 18}}, // Admiral and Fleet Admiral
	});

	constexpr auto Targets = std::to_array({Msg::Definitive_GuideUpper, Msg::Definitive_GuideMiddle,
											Msg::Definitive_GuideLower, Msg::Definitive_GuideAll});

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

	// What finishing each mission awards, from its controller in control.cpp: the points, and the lamps it lights of
	// the rank ring, 18 of which promote. By mission state from 2; Time Warp's are for warping forward.
	struct Award
	{
		int Points;
		int Lamps;
	};

	// clang-format off
	constexpr auto Awards = std::to_array<Award>({
		{500000, 6},  {500000, 6},   {500000, 6},   {750000, 9},   {1000000, 8},  {1000000, 8},
		{1000000, 8}, {750000, 7},   {750000, 7},   {750000, 7},   {1250000, 9},  {1250000, 9},
		{1250000, 9}, {1750000, 11}, {1500000, 10}, {2000000, 12}, {5000000, 18},
	});
	// clang-format on

	// The room between the award and the mission text box's line under it, at scale 1
	constexpr float FooterGap = 4;
	constexpr int RingLamps = 18;

	// The points, then the rank ring as a row of small lamps with the ones the mission lights lit: beside the points
	// when both fit in width, else under them, and right-aligned in a right-to-left language. Returns the height it
	// takes at scale 1; without a draw list it only measures.
	float DrawAward(ImDrawList* drawList, ImVec2 position, float width, const Award& award)
	{
		auto scale = Ui::Scale();
		auto points = Ui::Fill(Text(Msg::Definitive_GuidePoints), Ui::FormatScore(award.Points, 0));
		auto size = Ui::TextSize(Theme::HeadingFont(), Theme::LabelSize + 1, points.c_str());
		auto radius = 3.5f * scale;
		auto gap = 9.0f * scale;
		auto spacing = 14 * scale;
		auto lampsWidth = gap * static_cast<float>(RingLamps - 1) + radius * 2;
		auto beside = size.x + spacing + lampsWidth <= width;
		auto height = beside ? std::max(size.y, radius * 2) : size.y + 6 * scale + radius * 2;
		if (!drawList)
			return height / scale;

		Ui::DrawText(drawList, Theme::HeadingFont(), Theme::LabelSize + 1, position, Theme::Chrome200, points.c_str(),
					 width);
		auto rightToLeft = DefinitiveText::RightToLeft();
		auto y = beside ? position.y + size.y / 2 : position.y + size.y + 6 * scale + radius;
		auto x = position.x;
		if (beside)
			x = rightToLeft ? position.x + width - size.x - spacing - lampsWidth : position.x + size.x + spacing;
		else if (rightToLeft)
			x = position.x + width - lampsWidth;
		for (auto lamp = 0; lamp < RingLamps; lamp++)
			Theme::DrawLamp(drawList, ImVec2{x + radius + static_cast<float>(lamp) * gap, y}, radius,
							lamp < award.Lamps ? Theme::Lamp::Lit : Theme::Lamp::Dark);
		return height / scale;
	}

	const char* GuideText(int mission)
	{
		return Text(static_cast<Msg>(std::to_underlying(Msg::Definitive_GuideTargetPractice) + mission - 2));
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
	std::vector tabs{Text(Msg::Definitive_GuideBasics)};
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
	if (Current == 0)
		RenderBasics(row);
	else
		RenderMissions(view, row);
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

// ImGui gives a window that takes the focus its first item at the end of the frame, so the row asks again in the next
// few frames
void DefinitiveGuide::Paragraph(int& row, const char* id, const char* title, const char* aside, const char* text,
								const char* tag, float footer)
{
	auto focus = Focus == row++;
	if (focus)
		Ui::FocusNextRow();
	Ui::ParagraphRow(id, title, aside, text, tag, footer);
	if (!focus)
		return;
	// The window opens scrolled to the top, and taking the focus doesn't scroll; the row may be further down
	ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleEdgeY);
	FocusFrames--;
	if (FocusFrames <= 0)
		Focus = -1;
}

void DefinitiveGuide::RenderBasics(int& row)
{
	using enum Msg;
	Paragraph(row, "missions", Text(Definitive_GuideMissionsTitle), nullptr, Text(Definitive_GuideMissions), nullptr);
	Paragraph(row, "ranks", Text(Definitive_GuideRanksTitle), nullptr, Text(Definitive_GuideRanks), nullptr);
	Paragraph(row, "fulltilt", Text(Definitive_GuideFullTiltTitle), nullptr, Text(Definitive_GuideFullTilt), nullptr);
}

// Which ranks the tab is for, then its four missions by the target that chooses them
void DefinitiveGuide::RenderMissions(const TableView& view, int& row)
{
	auto scale = Ui::Scale();
	const auto& tier = Tiers[Current - 1];
	std::string ranks = DefinitiveHud::RankName(tier.FirstRank);
	if (tier.LastRank != tier.FirstRank)
		ranks = ranks + " · " + DefinitiveHud::RankName(tier.LastRank);
	auto offered = Ui::Fill(Text(Msg::Definitive_GuideOffered), ranks);
	auto at = ImGui::GetCursorScreenPos();
	Ui::DrawText(ImGui::GetWindowDrawList(), Theme::TextFont(), Theme::LabelSize,
				 ImVec2{at.x + 16 * scale, at.y + 4 * scale}, Theme::Chrome400, offered.c_str(),
				 ImGui::CalcItemWidth() - 32 * scale);
	ImGui::Dummy(ImVec2{0, (Theme::LabelSize + 12) * scale});
	auto running = DefinitiveHud::MissionOf(view.Mission);
	for (auto index = 0; index < 4; index++)
	{
		auto mission = tier.Missions[index];
		ImGui::PushID(index);
		// The award under the text, and for the mission that runs what the mission text box says it needs now
		auto now = mission == running;
		std::string progress = now ? view.MissionText : std::string{};
		std::replace(progress.begin(), progress.end(), '\n', ' ');
		const auto& award = Awards[mission - 2];
		auto width = Ui::ParagraphWidth();
		auto awardHeight = DrawAward(nullptr, ImVec2{}, width, award);
		auto progressHeight = 0.0f;
		if (!progress.empty())
			progressHeight = FooterGap + Ui::DrawWrapped(nullptr, Theme::TextFont(), Theme::LabelSize + 1, ImVec2{}, 0,
														 progress.c_str(), width) /
											 scale;
		Paragraph(row, "mission", DefinitiveHud::MissionName(mission), Text(Targets[index]), GuideText(mission),
				  now ? Text(Msg::Definitive_GuideNow) : nullptr, awardHeight + progressHeight);
		auto footer = Ui::FooterTop();
		DrawAward(ImGui::GetWindowDrawList(), footer, width, award);
		if (!progress.empty())
			Ui::DrawWrapped(ImGui::GetWindowDrawList(), Theme::TextFont(), Theme::LabelSize + 1,
							ImVec2{footer.x, footer.y + (awardHeight + FooterGap) * scale}, Theme::LedOrange,
							progress.c_str(), width);
		ImGui::PopID();
	}
}
