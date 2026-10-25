#include "pch.h"
#include "DefinitiveFrontEnd.h"

#include "DefinitiveSettings.h"

#include "DefinitiveHud.h"
#include "DefinitiveTheme.h"
#include "DefinitiveUi.h"
#include "options.h"
#include "TableView.h"
#include "translations.h"

#include <string_view>

DefinitiveHost DefinitiveFrontEnd::Host{};
DefinitiveFrontEnd::Screen DefinitiveFrontEnd::Current = Screen::None;
DefinitiveFrontEnd::Screen DefinitiveFrontEnd::SettingsFrom = Screen::Title;
bool DefinitiveFrontEnd::Opened = false;
bool DefinitiveFrontEnd::ConfirmClear = false;
bool DefinitiveFrontEnd::SummaryShown = false;
bool DefinitiveFrontEnd::Focused = false;
DefinitiveHost::Record DefinitiveFrontEnd::Entry{};
int DefinitiveFrontEnd::EntryPlace = 0;
std::string DefinitiveFrontEnd::EntryName;
std::vector<std::string> DefinitiveFrontEnd::NewRecords;
float DefinitiveFrontEnd::CreditsScroll = 0;

namespace
{
	using Theme = DefinitiveTheme;
	using Ui = DefinitiveUi;

	const char* Text(Msg id)
	{
		return translations::GetTranslation(id);
	}

	ImU32 WithAlpha(ImU32 color, float alpha)
	{
		return (color & 0x00FFFFFFU) | static_cast<ImU32>(std::lround(alpha * 255.0f)) << 24U;
	}


	// A line of the credits, a heading or not
	struct CreditLine
	{
		bool Heading;
		std::string Text;
	};

	// The credits' lines, from text with one per line and the headings marked with a leading #
	std::vector<CreditLine> SplitCredits(std::string_view text)
	{
		std::vector<CreditLine> lines;
		while (true)
		{
			auto end = text.find('\n');
			auto line = text.substr(0, end);
			auto heading = !line.empty() && line.front() == '#';
			lines.push_back({heading, std::string{heading ? line.substr(1) : line}});
			if (end == std::string_view::npos)
				return lines;
			text.remove_prefix(end + 1);
		}
	}
} // namespace

void DefinitiveFrontEnd::SetHost(const DefinitiveHost& host)
{
	Host = host;
}

void DefinitiveFrontEnd::ShowTitle()
{
	Open(Screen::Title);
}

void DefinitiveFrontEnd::ShowSettings(bool controls)
{
	if (Current != Screen::Settings)
		SettingsFrom = Current == Screen::None ? Screen::Title : Current;
	Current = Screen::Settings;
	DefinitiveSettings::Open(controls ? DefinitiveSettings::Group::Controls : DefinitiveSettings::Group::Edition);
}

bool DefinitiveFrontEnd::ScreenOpen()
{
	return Current != Screen::None;
}

bool DefinitiveFrontEnd::PassesShortcut(bool escape)
{
	return Focused && (!escape || Current == Screen::Title);
}

bool DefinitiveFrontEnd::WaitsForBinding()
{
	return Current == Screen::Settings && DefinitiveSettings::WaitsForBinding();
}

void DefinitiveFrontEnd::Open(Screen screen)
{
	Current = screen;
	Opened = true;
	ConfirmClear = false;
	CreditsScroll = 0;
}

void DefinitiveFrontEnd::Render(const TableView& view)
{
	Ui::NewFrame();
	// EndColumn sets it again while a screen shows
	Focused = false;
	// The score panel first, so the screens and their veils cover it
	if (Host.Layout)
	{
		SDL_FRect board, panel;
		Host.Layout(board, panel);
		DefinitiveHud::Render(view, board, panel);
	}

	auto playing = !view.GameOver && !view.Demo;
	auto paused = Host.Paused && Host.Paused();
	auto busy = Host.Busy && Host.Busy();
	auto fromPause = Current == Screen::Pause || (Current == Screen::Settings && SettingsFrom == Screen::Pause);
	if (playing)
	{
		// A game in play closes the other screens, however it started; while it is paused, the pause screen shows
		SummaryShown = false;
		NewRecords.clear();
		if (paused && !fromPause)
			ShowPause();
		else if (!paused)
			Current = Screen::None;
	}
	else if (view.GameOver && !view.Demo && !busy && (Current == Screen::None || fromPause))
	{
		// A game that has ended asks for a name for each new high score, then shows how everyone did
		if (!NextName() && !SummaryShown)
		{
			SummaryShown = true;
			Open(Screen::GameOver);
		}
	}
	// The demo plays behind the title and the screens it leads to, not behind a game that has just ended
	auto title = Current == Screen::Title || Current == Screen::Records || Current == Screen::Credits ||
				 (Current == Screen::Settings && SettingsFrom != Screen::Pause);
	if (title && view.GameOver && !busy && Host.StartDemo)
		Host.StartDemo();

	switch (Current)
	{
	case Screen::Title:
		RenderTitle(view);
		break;
	case Screen::Records:
		RenderRecords();
		break;
	case Screen::Settings:
		if (!DefinitiveSettings::Render(Host, playing))
			Open(SettingsFrom);
		break;
	case Screen::Credits:
		RenderCredits();
		break;
	case Screen::Pause:
		RenderPause(view);
		break;
	case Screen::GameOver:
		RenderGameOver(view);
		break;
	case Screen::NameEntry:
		RenderNameEntry();
		break;
	case Screen::None:
		break;
	}
}

float DefinitiveFrontEnd::BeginColumn(const char* name, float width)
{
	auto viewport = ImGui::GetMainViewport();
	auto scale = Ui::Scale();
	auto columnWidth = std::min(width * scale, viewport->Size.x);
	auto fade = std::min(120 * scale, viewport->Size.x - columnWidth);

	// Over the table, under every window
	auto background = ImGui::GetBackgroundDrawList();
	auto min = viewport->Pos;
	auto height = viewport->Size.y;
	auto shade = WithAlpha(Theme::Space950, 0.88f), clear = WithAlpha(Theme::Space950, 0);
	background->AddRectFilled(min, ImVec2{min.x + columnWidth, min.y + height}, shade);
	background->AddRectFilledMultiColor(ImVec2{min.x + columnWidth, min.y},
										ImVec2{min.x + columnWidth + fade, min.y + height}, shade, clear, clear, shade);

	ImGui::SetNextWindowPos(min);
	ImGui::SetNextWindowSize(ImVec2{columnWidth, height});
	// A screen that opens takes the keyboard and gamepad; so does one that lost them to a click on the table
	if (Opened || (!ImGui::GetTopMostPopupModal() && !GImGui->NavWindow))
		ImGui::SetNextWindowFocus();
	// So does one whose row lost the focus to ImGui's cancel, which keeps the window focused but no row lit
	auto window = ImGui::FindWindowByName(name);
	if (Opened || (window && GImGui->NavWindow == window && GImGui->NavId == 0))
		Ui::FocusNextRow();
	Opened = false;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{});
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, 2 * scale});
	// The lamps show the focus, not ImGui's rectangle
	ImGui::PushStyleColor(ImGuiCol_NavCursor, 0U);
	ImGui::Begin(name, nullptr,
				 Ui::Combine(ImGuiWindowFlags_NoDecoration, ImGuiWindowFlags_NoBackground, ImGuiWindowFlags_NoMove,
							 ImGuiWindowFlags_NoSavedSettings, ImGuiWindowFlags_NoScrollWithMouse));
	return columnWidth;
}

void DefinitiveFrontEnd::EndColumn()
{
	Focused = ImGui::IsWindowFocused() && !ImGui::GetTopMostPopupModal();
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(3);
}

bool DefinitiveFrontEnd::BackPressed()
{
	return ImGui::IsWindowFocused() &&
		   (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
			ImGui::IsMouseClicked(ImGuiMouseButton_Right));
}

void DefinitiveFrontEnd::RenderTitle(const TableView& view)
{
	auto viewport = ImGui::GetMainViewport();
	auto rows = Host.Quit ? 7 : 6;
	// Margins, the name, the year, the rows and the hints, shrunk to fit a small window
	auto needed = 56 + 60 + 14 + 40 + 48 + rows * (Theme::RowHeight + 2) + 24 + 26 + 24;
	Ui::SetScale(std::min(Ui::Scale(), viewport->Size.y / needed));
	auto scale = Ui::Scale();

	auto width = BeginColumn("##DefinitiveTitle", 500);
	auto drawList = ImGui::GetWindowDrawList();
	auto margin = 48 * scale;
	ImVec2 origin{viewport->Pos.x + margin, viewport->Pos.y + 56 * scale};

	// The game's name is the same in every language
	drawList->AddText(Theme::HeadingFont(), Theme::DisplaySize * scale, ImVec2{origin.x + 16 * scale, origin.y},
					  Theme::Chrome50, "Space Cadet");
	Theme::DrawLed(drawList, ImVec2{origin.x + 18 * scale, origin.y + (60 + 14) * scale}, 40 * scale, "2026");

	ImGui::SetCursorScreenPos(ImVec2{origin.x, origin.y + (60 + 14 + 40 + 48) * scale});
	ImGui::BeginGroup();
	ImGui::PushItemWidth(width - margin - 32 * scale);
	if (Ui::MenuRow("play", Text(Msg::Definitive_Play)) && Host.Play)
	{
		Current = Screen::None;
		Host.Play();
	}

	// The players, from 1 to 4: left and right change them, as does a click on a digit; choosing the row counts up
	auto box = ImVec2{34 * scale, 32 * scale};
	auto gap = 4 * scale;
	auto digitsWidth = box.x * 4 + gap * 3;
	auto chosen = Ui::MenuRow("players", Text(Msg::Menu1_Select_Players), false, nullptr, true, digitsWidth + gap);
	auto row = Ui::LastRow();
	ImVec2 digitsMin{row.Max.x - 16 * scale - digitsWidth, (row.Min.y + row.Max.y - box.y) / 2};
	auto players = options::Options.Players.V;
	auto setPlayers = [](int count)
	{ options::toggle(static_cast<Menu1>(static_cast<int>(Menu1::OnePlayer) + count - 1)); };
	if (chosen)
	{
		auto mouse = ImGui::GetIO().MousePos;
		auto clicked = mouse.y >= digitsMin.y && mouse.y <= digitsMin.y + box.y && mouse.x >= digitsMin.x
						   ? static_cast<int>((mouse.x - digitsMin.x) / (box.x + gap)) + 1
						   : 0;
		setPlayers(clicked >= 1 && clicked <= 4 && ImGui::IsMouseReleased(0) ? clicked : players % 4 + 1);
	}
	else if (Ui::LastRowFocused())
	{
		if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft))
			setPlayers(std::max(players - 1, 1));
		if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight))
			setPlayers(std::min(players + 1, 4));
	}
	players = options::Options.Players.V;
	for (auto count = 1; count <= 4; count++)
	{
		ImVec2 min{digitsMin.x + static_cast<float>(count - 1) * (box.x + gap), digitsMin.y};
		ImVec2 max{min.x + box.x, min.y + box.y};
		char digit[2] = {static_cast<char>('0' + count), 0};
		auto size = Theme::HeadingFont()->CalcTextSizeA(Theme::BodySize * scale, FLT_MAX, 0, digit);
		ImVec2 textPosition{(min.x + max.x - size.x) / 2, (min.y + max.y - size.y) / 2};
		if (count == players)
		{
			drawList->AddRectFilled(ImVec2{min.x - 2 * scale, min.y - 2 * scale},
									ImVec2{max.x + 2 * scale, max.y + 2 * scale}, WithAlpha(Theme::LedOrange, 0.25f),
									6 * scale);
			drawList->AddRectFilled(min, max, Theme::LedOrange, 4 * scale);
			drawList->AddText(Theme::HeadingFont(), Theme::BodySize * scale, textPosition, Theme::Space950, digit);
		}
		else
		{
			drawList->AddRect(min, max, Theme::Chrome600, 4 * scale);
			drawList->AddText(Theme::TextFont(), Theme::BodySize * scale, textPosition, Theme::Chrome400, digit);
		}
	}

	if (Ui::MenuRow("settings", Text(Msg::Definitive_Settings)))
		ShowSettings();
	if (Ui::MenuRow("controls", Text(Msg::Definitive_Controls)))
		ShowSettings(true);
	if (Ui::MenuRow("records", Text(Msg::HIGHSCORES_Caption)))
		Open(Screen::Records);
	if (Ui::MenuRow("credits", Text(Msg::Definitive_Credits)))
		Open(Screen::Credits);
	if (Host.Quit && Ui::MenuRow("quit", Text(Msg::Menu1_Exit)))
		Host.Quit();
	ImGui::PopItemWidth();
	ImGui::EndGroup();

	Ui::DrawHints(drawList, ImVec2{origin.x + 16 * scale, viewport->Pos.y + viewport->Size.y - 50 * scale},
				  {{Ui::Action::Choose, Text(Msg::Definitive_Choose)},
				   {Ui::Action::Select, Text(Msg::Definitive_Select)},
				   {Ui::Action::Change, Text(Msg::Menu1_Select_Players)}});
	EndColumn();

	// The demo's lamp, in the corner of the window
	if (view.Demo)
	{
		auto foreground = ImGui::GetForegroundDrawList();
		auto label = Text(Msg::Menu1_Demo);
		auto size = Ui::TextSize(Theme::TextFont(), Theme::LabelSize, label);
		ImVec2 position{viewport->Pos.x + viewport->Size.x - 24 * scale - size.x,
						viewport->Pos.y + viewport->Size.y - 24 * scale - size.y};
		Ui::DrawText(foreground, Theme::TextFont(), Theme::LabelSize, position, Theme::Chrome200, label);
		Theme::DrawLamp(foreground, ImVec2{position.x - 12 * scale, position.y + size.y / 2}, 5 * scale,
						Theme::Lamp::Warning);
	}
}

void DefinitiveFrontEnd::RenderRecords()
{
	auto viewport = ImGui::GetMainViewport();
	auto needed = 56 + 40 + 24 + 5 * 44 + 32 + 2 * (Theme::RowHeight + 2) + 24 + 26 + 24;
	Ui::SetScale(std::min(Ui::Scale(), viewport->Size.y / needed));
	auto scale = Ui::Scale();

	auto width = BeginColumn("##DefinitiveRecords", 560);
	auto drawList = ImGui::GetWindowDrawList();
	auto margin = 48 * scale;
	auto innerWidth = width - margin - 32 * scale;
	ImVec2 origin{viewport->Pos.x + margin, viewport->Pos.y + 56 * scale};
	Ui::DrawText(drawList, Theme::HeadingFont(), Theme::TitleSize, ImVec2{origin.x + 16 * scale, origin.y},
				 Theme::Chrome50, Text(Msg::HIGHSCORES_Caption), innerWidth - 32 * scale);

	// Rank, name and score, in the LED digits of the panel
	auto records = Host.Records ? Host.Records() : std::vector<DefinitiveHost::Record>{};
	auto y = origin.y + (40 + 24) * scale;
	for (size_t i = 0; i < 5; i++)
	{
		auto rowHeight = 44 * scale;
		auto left = origin.x + 16 * scale, right = origin.x + innerWidth - 16 * scale;
		drawList->AddLine(ImVec2{left, y}, ImVec2{right, y}, WithAlpha(Theme::Chrome600, 0.6f));
		auto centre = y + rowHeight / 2;
		char rank[4];
		snprintf(rank, sizeof rank, "%d", static_cast<int>(i) + 1);
		auto rankSize = Ui::TextSize(Theme::HeadingFont(), Theme::BodySize, rank);
		drawList->AddText(Theme::HeadingFont(), Theme::BodySize * scale, ImVec2{left, centre - rankSize.y / 2},
						  Theme::Chrome400, rank);
		if (i < records.size() && records[i].Score >= 0)
		{
			auto score = Ui::FormatScore(records[i].Score, 11);
			auto ledSize = Theme::LedSize(Theme::CounterSize * 1.2f * scale, score.c_str());
			Theme::DrawLed(drawList, ImVec2{right - ledSize.x, centre - ledSize.y / 2},
						   Theme::CounterSize * 1.2f * scale, score.c_str());
			auto& name = records[i].Name;
			auto nameSize = Ui::TextSize(Theme::TextFont(), Theme::BodySize, name.c_str());
			Ui::DrawText(drawList, Theme::TextFont(), Theme::BodySize,
						 ImVec2{left + 32 * scale, centre - nameSize.y / 2}, Theme::Chrome50, name.c_str());
		}
		y += rowHeight;
	}

	ImGui::SetCursorScreenPos(ImVec2{origin.x, y + 32 * scale});
	ImGui::BeginGroup();
	ImGui::PushItemWidth(innerWidth);
	if (ConfirmClear)
	{
		// Asks once before the records go
		Ui::DrawText(drawList, Theme::HeadingFont(), Theme::BodySize, ImVec2{origin.x + 16 * scale, y + 8 * scale},
					 Theme::Chrome50, Text(Msg::STRING141), innerWidth - 32 * scale);
	}
	if (!ConfirmClear)
	{
		if (Ui::MenuRow("back", Text(Msg::Definitive_Back)) || BackPressed())
			Open(Screen::Title);
		if (Ui::MenuRow("clear", Text(Msg::HIGHSCORES_Clear), true))
		{
			ConfirmClear = true;
			Ui::FocusNextRow();
		}
	}
	else
	{
		auto cancelled = Ui::MenuRow("cancel", Text(Msg::GenericCancel)) || BackPressed();
		if (Ui::MenuRow("confirm", Text(Msg::GenericOk), true))
		{
			if (Host.ClearRecords)
				Host.ClearRecords();
			cancelled = true;
		}
		if (cancelled)
		{
			ConfirmClear = false;
			Ui::FocusNextRow();
		}
	}
	ImGui::PopItemWidth();
	ImGui::EndGroup();
	Ui::DrawHints(drawList, ImVec2{origin.x + 16 * scale, viewport->Pos.y + viewport->Size.y - 50 * scale},
				  {{Ui::Action::Choose, Text(Msg::Definitive_Choose)},
				   {Ui::Action::Select, Text(Msg::Definitive_Select)},
				   {Ui::Action::Back, Text(Msg::Definitive_Back)}});
	EndColumn();
}

void DefinitiveFrontEnd::RenderCredits()
{
	// One line per credit; a heading starts with #
	static const std::vector<CreditLine> credits = SplitCredits(R"(#Space Cadet 2026
A definitive edition of 3D Pinball for Windows - Space Cadet,
built on the decompiled game.
github.com/simonsteiner/SpaceCadetPinball

#Decompiled and ported to SDL
k4zmu2a and contributors
github.com/k4zmu2a/SpaceCadetPinball

#Original game
Full Tilt! Pinball, created by Cinematronics for Maxis;
3D Pinball for Windows - Space Cadet, Microsoft

#Cinematronics
Programming: Michael Sandige, John Taylor
Art: John Frantz, Ryan Medeiros
Design: Kevin Gliner
Design consultant: Mark Sprenger
Producer: Kevin Gliner
Sound effects: Matt Ridgeway, Donald S. Griffin
Music: Matt Ridgeway
Voices: Mike McGeary, William Rice
Grand poobah: David Stafford
Special thanks: Paula Sandige, Alex St. John, Brad Silverberg,
Jeff Camp, Danny Thorpe, Greg Hospelhorn, Keith Johnson,
Sean Grant, Bob McAnn, Michael Kelley, Rob Rosenhouse,
Lisa Acton, Dan and Mitchell Roth

#Maxis
Producer: John Csicsery
Product manager: Larry Lee
Lead tester and QA manager: Scott Shicoff
Additional testing: Cathy Castro, Robin Hines,
John "Jussi" Ylinen, Keith Meyer, Marc Meyer, Owen Nelson,
Joe Longworth, Peter Saylor, Michael Gilmartin
Additional art: Ocean Quigley, Rick Macaraeg, Charlie Aquilina
Art director: Sharon Barr
Install program: Kevin O'Hare
Intro music: Brian Conrad, John Csicsery
Documentation: David Caggiano, Michael Bremer, Bob Sombrio
Special thanks: Sam Poole, Joe Scirica, Jeff Braun, Bob Derber,
Ashley Csicsery, Tom Forge, Will "Burr" Wright

#Fonts
Exo 2: The Exo 2 Project Authors
Noto Sans: The Noto Project Authors
DSEG7 Classic: keshikan
All under the SIL Open Font License 1.1

#Libraries
SDL and SDL_mixer, Dear ImGui, TinySoundFont, stb_vorbis,
SDL_native_midi, MMPX)");

	auto viewport = ImGui::GetMainViewport();
	auto scale = Ui::Scale();
	auto width = BeginColumn("##DefinitiveCredits", 640);
	auto drawList = ImGui::GetWindowDrawList();
	auto margin = 48 * scale;
	auto innerWidth = width - margin - 32 * scale;
	ImVec2 origin{viewport->Pos.x + margin, viewport->Pos.y + 56 * scale};
	Ui::DrawText(drawList, Theme::HeadingFont(), Theme::TitleSize, ImVec2{origin.x + 16 * scale, origin.y},
				 Theme::Chrome50, Text(Msg::Definitive_Credits), innerWidth - 32 * scale);
	if (Host.Version)
	{
		char version[64];
		snprintf(version, sizeof version, Text(Msg::Definitive_Version), Host.Version);
		Ui::DrawText(drawList, Theme::TextFont(), Theme::LabelSize,
					 ImVec2{origin.x + 16 * scale, origin.y + 44 * scale}, Theme::Chrome400, version,
					 innerWidth - 32 * scale);
	}

	// The list scrolls with the arrow keys, the d-pad and the wheel, under the title and over the back row
	auto top = origin.y + (40 + 32) * scale;
	auto bottom = viewport->Pos.y + viewport->Size.y - (Theme::RowHeight + 24 + 26 + 40) * scale;
	auto lineHeight = 26 * scale;
	auto contentHeight = 0.0f;
	for (auto& line : credits)
		contentHeight += line.Heading ? lineHeight * 1.3f : lineHeight;
	auto& io = ImGui::GetIO();
	auto step = lineHeight * 2;
	if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown))
		CreditsScroll += step;
	if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp))
		CreditsScroll -= step;
	if (ImGui::IsWindowHovered())
		CreditsScroll -= io.MouseWheel * step;
	// A finger or a held button drags the list
	if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(0))
		CreditsScroll -= io.MouseDelta.y;
	CreditsScroll = std::clamp(CreditsScroll, 0.0f, std::max(contentHeight - (bottom - top), 0.0f));

	drawList->PushClipRect(ImVec2{origin.x, top}, ImVec2{origin.x + innerWidth, bottom}, true);
	auto y = top - CreditsScroll;
	for (auto& line : credits)
	{
		auto font = line.Heading ? Theme::HeadingFont() : Theme::TextFont();
		auto size = line.Heading ? Theme::BodySize * 1.1f : Theme::LabelSize * 1.1f;
		if (line.Heading)
			y += lineHeight * 0.3f;
		drawList->AddText(font, size * scale, ImVec2{origin.x + 16 * scale, y},
						  line.Heading ? Theme::LedOrange : Theme::Chrome200, line.Text.c_str());
		y += lineHeight;
	}
	drawList->PopClipRect();

	ImGui::SetCursorScreenPos(ImVec2{origin.x, bottom + 16 * scale});
	ImGui::PushItemWidth(innerWidth);
	if (Ui::MenuRow("back", Text(Msg::Definitive_Back)) || BackPressed())
		Open(Screen::Title);
	ImGui::PopItemWidth();
	Ui::DrawHints(drawList, ImVec2{origin.x + 16 * scale, viewport->Pos.y + viewport->Size.y - 50 * scale},
				  {{Ui::Action::Choose, Text(Msg::Definitive_Scroll)}, {Ui::Action::Back, Text(Msg::Definitive_Back)}});
	EndColumn();
}

void DefinitiveFrontEnd::RenderMissingData(const std::vector<std::string>& looked, bool chosenFolderFailed)
{
	auto viewport = ImGui::GetMainViewport();
	Ui::NewFrame();
	auto lines = static_cast<float>(looked.size());
	auto needed =
		56 + 60 + 14 + 40 + 40 + 40 + 72 + 26 + lines * 22 + 28 + 40 + 2 * (Theme::RowHeight + 2) + 40 + 26 + 24;
	Ui::SetScale(std::min(Ui::Scale(), viewport->Size.y / needed));
	auto scale = Ui::Scale();

	static auto opened = true;
	Opened = opened;
	opened = false;
	auto width = BeginColumn("##DefinitiveMissingData", 680);
	// No game runs behind this screen to take Escape to its Exit binding, as on the title; Back lights Exit instead,
	// over the first row that ImGui's cancel would light
	auto back = BackPressed() && Host.Quit;
	if (back)
		Ui::TakeFocusRequest();
	auto drawList = ImGui::GetWindowDrawList();
	auto margin = 48 * scale;
	auto inner = width - margin - 32 * scale;
	ImVec2 origin{viewport->Pos.x + margin, viewport->Pos.y + 56 * scale};
	auto left = origin.x + 16 * scale;
	drawList->AddText(Theme::HeadingFont(), Theme::DisplaySize * scale, ImVec2{left, origin.y}, Theme::Chrome50,
					  "Space Cadet");
	Theme::DrawLed(drawList, ImVec2{left + 2 * scale, origin.y + (60 + 14) * scale}, 40 * scale, "2026");

	// What the game needs and where it looked for it
	auto y = origin.y + (60 + 14 + 40 + 40) * scale;
	Ui::DrawText(drawList, Theme::HeadingFont(), Theme::TitleSize, ImVec2{left, y}, Theme::Chrome50,
				 Text(Msg::Definitive_DataTitle), inner - 32 * scale);
	y += 52 * scale;
	y += Ui::DrawWrapped(drawList, Theme::TextFont(), Theme::BodySize, ImVec2{left, y}, Theme::Chrome200,
						 Text(Msg::Definitive_DataExplain), inner - 32 * scale) +
		 12 * scale;
	for (auto& path : looked)
	{
		Theme::DrawLamp(drawList, ImVec2{left + 5 * scale, y + 9 * scale}, 4 * scale, Theme::Lamp::Off);
		drawList->AddText(Theme::TextFont(), Theme::LabelSize * scale, ImVec2{left + 18 * scale, y}, Theme::Chrome400,
						  path.c_str());
		y += 22 * scale;
	}
	if (chosenFolderFailed)
	{
		y += 8 * scale;
		Ui::DrawText(drawList, Theme::TextFont(), Theme::LabelSize, ImVec2{left, y}, Theme::LedRed,
					 Text(Msg::Definitive_DataNotThere), inner - 32 * scale);
		y += 20 * scale;
	}
	// Without a folder dialog the player has to put the data in one of the places above
	auto dialogError = Host.FolderDialogError ? Host.FolderDialogError() : std::string{};
	if (!dialogError.empty())
	{
		char note[512];
		snprintf(note, sizeof note, Text(Msg::Definitive_DataNoDialog), dialogError.c_str());
		y += 8 * scale;
		y += Ui::DrawWrapped(drawList, Theme::TextFont(), Theme::LabelSize, ImVec2{left, y}, Theme::LedRed, note,
							 inner - 32 * scale);
	}

	ImGui::SetCursorScreenPos(ImVec2{origin.x, y + 24 * scale});
	ImGui::BeginGroup();
	ImGui::PushItemWidth(inner);
	if (Ui::MenuRow("choose", Text(Msg::Definitive_DataChoose)) && Host.ChooseDataFolder)
		Host.ChooseDataFolder();
	if (back)
		Ui::FocusNextRow();
	if (Host.Quit && Ui::MenuRow("quit", Text(Msg::Menu1_Exit)))
		Host.Quit();
	ImGui::PopItemWidth();
	ImGui::EndGroup();
	y = ImGui::GetCursorScreenPos().y + 16 * scale;
	Ui::DrawWrapped(drawList, Theme::TextFont(), Theme::LabelSize, ImVec2{left, y}, Theme::Chrome400,
					Text(Msg::Definitive_DataNoCopy), inner - 32 * scale);

	Ui::DrawHints(
		drawList, ImVec2{left, viewport->Pos.y + viewport->Size.y - 50 * scale},
		{{Ui::Action::Choose, Text(Msg::Definitive_Choose)}, {Ui::Action::Select, Text(Msg::Definitive_Select)}});
	EndColumn();
}
