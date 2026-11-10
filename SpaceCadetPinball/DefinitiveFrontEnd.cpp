#include "pch.h"
#include "DefinitiveFrontEnd.h"

#include "DefinitiveGuide.h"
#include "DefinitiveRecordings.h"
#include "DefinitiveSettings.h"
#include "DefinitiveStatsScreen.h"

#include "DefinitiveHud.h"
#include "DefinitiveTheme.h"
#include "DefinitiveUi.h"
#include "options.h"
#include "TableView.h"
#include "translations.h"

#include <format>
#include <string_view>
#include <utility>

DefinitiveHost DefinitiveFrontEnd::Host{};
DefinitiveFrontEnd::Screen DefinitiveFrontEnd::Current = Screen::None;
DefinitiveFrontEnd::Screen DefinitiveFrontEnd::SettingsFrom = Screen::Title;
DefinitiveFrontEnd::Screen DefinitiveFrontEnd::TablesFrom = Screen::Title;
DefinitiveFrontEnd::Screen DefinitiveFrontEnd::ExitFrom = Screen::Title;
bool DefinitiveFrontEnd::Opened = false;
bool DefinitiveFrontEnd::ConfirmClear = false;
bool DefinitiveFrontEnd::SummaryShown = false;
bool DefinitiveFrontEnd::Focused = false;
DefinitiveHost::Record DefinitiveFrontEnd::Entry{};
int DefinitiveFrontEnd::EntryPlace = 0;
std::string DefinitiveFrontEnd::EntryName;
std::vector<std::string> DefinitiveFrontEnd::NewRecords;
float DefinitiveFrontEnd::CreditsScroll = 0;
size_t DefinitiveFrontEnd::ConfirmTable = SIZE_MAX;
bool DefinitiveFrontEnd::FocusConfirmTable = false;

namespace
{
	using Theme = DefinitiveTheme;
	using Ui = DefinitiveUi;

	const char* Text(Msg id)
	{
		return translations::GetTranslation(id);
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
			lines.emplace_back(heading, std::string{heading ? line.substr(1) : line});
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

// Opens Settings, on the controls when controls is true, and remembers the screen Back returns to
void DefinitiveFrontEnd::ShowSettings(bool controls)
{
	using enum Screen;
	if (Current != Settings)
		SettingsFrom = Current == None ? Title : Current;
	Current = Settings;
	DefinitiveSettings::Open(controls ? DefinitiveSettings::Group::Controls : DefinitiveSettings::Group::Edition);
}

// Opens the list of tables when more than one was found: from a game in play, which it pauses first, or from the
// title and the screens it leads to; does nothing from a game that is ending or its summary
void DefinitiveFrontEnd::ShowTables()
{
	using enum Screen;
	if (!Host.TableList || Host.TableList().size() < 2)
		return;
	// No screen shows in a game in play
	auto fromPause = Current == None || FromPause();
	if (!fromPause && !UnderTitle())
		return;
	if (fromPause && Current != Tables)
		ShowPause();
	TablesFrom = fromPause ? Pause : Title;
	Open(Tables);
}

void DefinitiveFrontEnd::ShowViewer()
{
	Open(Screen::Viewer);
}

bool DefinitiveFrontEnd::ScreenOpen()
{
	return Current != Screen::None;
}

// Whether a key bound to a shortcut still reaches the game: while a screen had the focus in the last frame, and for
// Escape only on the title
bool DefinitiveFrontEnd::PassesShortcut(bool escape)
{
	return Focused && (!escape || Current == Screen::Title);
}

// Whether Settings is open and waits for a key or button to bind to a control
bool DefinitiveFrontEnd::WaitsForBinding()
{
	return Current == Screen::Settings && DefinitiveSettings::WaitsForBinding();
}

void DefinitiveFrontEnd::Open(Screen screen)
{
	Current = screen;
	Opened = true;
	ConfirmClear = false;
	ConfirmTable = SIZE_MAX;
	CreditsScroll = 0;
}

// Whether the screen showing belongs to a game in play: the pause screen, the guide, or Settings or the list of
// tables opened from the pause screen
bool DefinitiveFrontEnd::FromPause()
{
	using enum Screen;
	return Current == Pause || Current == Guide || (Current == Settings && SettingsFrom == Pause) ||
		   (Current == Tables && TablesFrom == Pause);
}

// Whether the screen showing is the title or one it leads to; the exit question counts as the screen it was opened
// from
bool DefinitiveFrontEnd::UnderTitle()
{
	using enum Screen;
	// The exit question belongs where it was opened from
	auto screen = Current == Exit ? ExitFrom : Current;
	return screen == Title || screen == Records || screen == Credits || screen == Stats || screen == Recordings ||
		   (screen == Tables && TablesFrom != Pause) || (screen == Settings && SettingsFrom != Pause);
}

// Opens and closes the screens as a game starts, pauses and ends, and runs the demo behind the title
void DefinitiveFrontEnd::FollowGame(const TableView& view, bool playing)
{
	auto paused = Host.Paused && Host.Paused();
	auto busy = Host.Busy && Host.Busy();
	auto fromPause = FromPause();
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
	// A game that has ended asks for a name for each new high score, then shows how everyone did
	else if (view.GameOver && !view.Demo && !busy && (Current == Screen::None || fromPause) && !NextName() &&
			 !SummaryShown)
	{
		SummaryShown = true;
		Open(Screen::GameOver);
	}
	// The demo plays behind the title and the screens it leads to, not behind a game that has just ended
	if (UnderTitle() && view.GameOver && !busy && Host.StartDemo)
		Host.StartDemo();
}

// Each frame: the score panel, the touch deck while no screen shows, then the open screen or the recording's bar
void DefinitiveFrontEnd::Render(const TableView& view)
{
	using enum Screen;
	Ui::NewFrame();
	// EndColumn sets it again while a screen shows
	Focused = false;
	// The score panel first, so the screens and their veils cover it
	if (Host.Layout)
	{
		SDL_FRect board;
		SDL_FRect panel;
		auto band = false;
		Host.Layout(board, panel, band);
		DefinitiveHud::Render(view, board, panel, band, Host.PanelArt ? Host.PanelArt() : nullptr);
	}
	// Only in play: a screen takes the whole window, and its rows the touches
	if (DefinitiveDeck deck; Current == None && Host.TouchDeck && Host.TouchDeck(deck))
		DefinitiveTouchDeck::Render(deck);

	// A recording that plays has its own bar and none of the game's screens, which would follow the game it shows
	if (Host.Viewing && Host.Viewing())
	{
		Current = Viewer;
		DefinitiveRecordings::RenderViewer(Host);
		return;
	}
	if (Current == Viewer)
		Current = Title;

	auto playing = !view.GameOver && !view.Demo;
	FollowGame(view, playing);

	switch (Current)
	{
	case Title:
		RenderTitle(view);
		break;
	case Records:
		RenderRecords();
		break;
	case Tables:
		RenderTables();
		break;
	case Settings:
		if (!DefinitiveSettings::Render(Host, playing))
			Open(SettingsFrom);
		break;
	case Credits:
		RenderCredits();
		break;
	case Stats:
		if (!DefinitiveStatsScreen::Render(view))
			Open(Title);
		break;
	case Recordings:
		if (!DefinitiveRecordings::Render(Host))
			Open(Title);
		break;
	case Viewer:
		break;
	case Guide:
		if (!DefinitiveGuide::Render(view))
			Open(Pause);
		break;
	case Pause:
		RenderPause(view);
		break;
	case Exit:
		RenderExit();
		break;
	case GameOver:
		RenderGameOver(view);
		break;
	case NameEntry:
		RenderNameEntry();
		break;
	case None:
		break;
	}
	DefinitiveStatsScreen::RenderNotice();
}

// Opens a full-height column at the window's left over a shade of the table, the whole width upright; returns its width
float DefinitiveFrontEnd::BeginColumn(const char* name, float width)
{
	auto viewport = ImGui::GetMainViewport();
	const auto& safe = Ui::SafeArea();
	auto scale = Ui::Scale();
	// In a window taller than wide, such as a phone held upright, the column and its shade take the whole width
	auto upright = safe.Size.y > safe.Size.x;
	auto columnWidth = upright ? safe.Size.x : std::min(width * scale, safe.Size.x);

	// Over the table, under every window; the shade covers the bars and the cutout too, the column keeps clear of them
	auto background = ImGui::GetBackgroundDrawList();
	auto min = viewport->Pos;
	ImVec2 max{min.x + viewport->Size.x, min.y + viewport->Size.y};
	auto shadeRight = upright ? max.x : safe.Pos.x + columnWidth;
	auto fade = std::min(120 * scale, max.x - shadeRight);
	auto shade = Theme::WithAlpha(Theme::Space950, 0.88f);
	auto clear = Theme::WithAlpha(Theme::Space950, 0);
	background->AddRectFilled(min, ImVec2{shadeRight, max.y}, shade);
	background->AddRectFilledMultiColor(ImVec2{shadeRight, min.y}, ImVec2{shadeRight + fade, max.y}, shade, clear,
										clear, shade);

	ImGui::SetNextWindowPos(safe.Pos);
	ImGui::SetNextWindowSize(ImVec2{columnWidth, safe.Size.y});
	// A screen that opens takes the keyboard and gamepad; so does one that lost them to a click on the table
	if (Opened || (!ImGui::GetTopMostPopupModal() && !GImGui->NavWindow))
		ImGui::SetNextWindowFocus();
	// So does one whose row lost the focus to ImGui's cancel, which keeps the window focused but no row lit
	if (auto window = ImGui::FindWindowByName(name);
		Opened || (window && GImGui->NavWindow == window && GImGui->NavId == 0))
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

// Closes the column BeginColumn opened, and notes whether it had the focus, for PassesShortcut
void DefinitiveFrontEnd::EndColumn()
{
	Focused = ImGui::IsWindowFocused() && !ImGui::GetTopMostPopupModal();
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(3);
}

// Whether Escape, the gamepad's right face button or the right mouse button was pressed while the window has the focus
bool DefinitiveFrontEnd::BackPressed()
{
	return ImGui::IsWindowFocused() &&
		   (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
			ImGui::IsMouseClicked(ImGuiMouseButton_Right));
}

// The title: the game's name, the main menu and the hints, scaled to fit the window and centred in it upright
void DefinitiveFrontEnd::RenderTitle(const TableView& view)
{
	const auto* viewport = &Ui::SafeArea();
	// A choice of tables only when the data of more than one was found
	auto tables = Host.TableList ? Host.TableList() : std::vector<DefinitiveHost::TableChoice>{};
	auto chooseTable = tables.size() > 1;
	auto rows = (QuitRow() ? 8 : 7) + (Host.Recordings ? 1 : 0) + (chooseTable ? 1 : 0);
	// Margins, the name, the year, the rows and the hints, shrunk to fit a small window
	auto needed = 56 + 60 + 14 + 40 + 48 + rows * (Ui::RowHeight() + 2) + 24 + 26 + 24;
	constexpr float ColumnWidth = 500;
	// Upright, the menu grows to the window's width, as far as its height allows, and sits in the middle of it, so
	// the rows are thumb-sized and the screen isn't left half empty
	auto upright = viewport->Size.y > viewport->Size.x;
	auto scale = upright ? std::min(viewport->Size.x / ColumnWidth, viewport->Size.y / needed)
						 : std::min(Ui::Scale(), viewport->Size.y / needed);
	Ui::SetScale(scale);

	auto width = BeginColumn("##DefinitiveTitle", ColumnWidth);
	auto drawList = ImGui::GetWindowDrawList();
	auto margin = 48 * scale;
	auto top = upright ? std::max(0.0f, (viewport->Size.y - needed * scale) / 2) : 0.0f;
	ImVec2 origin{viewport->Pos.x + margin, viewport->Pos.y + top + 56 * scale};

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
	if (chooseTable)
	{
		// The row names the table loaded and opens the list
		auto loaded = std::ranges::find_if(tables, &DefinitiveHost::TableChoice::Loaded);
		if (Ui::MenuRow("table", Text(Msg::Definitive_Table), false,
						loaded != tables.end() ? loaded->Name.c_str() : nullptr))
		{
			TablesFrom = Screen::Title;
			Open(Screen::Tables);
		}
	}

	// The players, from 1 to 4: left and right change them, as does a click on a digit; choosing the row counts up
	auto box = ImVec2{34 * scale, 32 * scale};
	auto gap = 4 * scale;
	auto digitsWidth = box.x * 4 + gap * 3;
	auto chosen = Ui::MenuRow("players", Text(Msg::Menu1_Select_Players), false, nullptr, true, digitsWidth + gap);
	auto row = Ui::LastRow();
	ImVec2 digitsMin{row.Max.x - 16 * scale - digitsWidth, (row.Min.y + row.Max.y - box.y) / 2};
	auto players = options::Options.Players.V;
	auto setPlayers = [](int count) { options::Set(options::Options.Players, count); };
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
		std::array<char, 2> digit{static_cast<char>('0' + count), 0};
		auto size = Theme::HeadingFont()->CalcTextSizeA(Theme::BodySize * scale, FLT_MAX, 0, digit.data());
		ImVec2 textPosition{(min.x + max.x - size.x) / 2, (min.y + max.y - size.y) / 2};
		if (count == players)
		{
			drawList->AddRectFilled(ImVec2{min.x - 2 * scale, min.y - 2 * scale},
									ImVec2{max.x + 2 * scale, max.y + 2 * scale},
									Theme::WithAlpha(Theme::LedOrange, 0.25f), 6 * scale);
			drawList->AddRectFilled(min, max, Theme::LedOrange, 4 * scale);
			drawList->AddText(Theme::HeadingFont(), Theme::BodySize * scale, textPosition, Theme::Space950,
							  digit.data());
		}
		else
		{
			drawList->AddRect(min, max, Theme::Chrome600, 4 * scale);
			drawList->AddText(Theme::TextFont(), Theme::BodySize * scale, textPosition, Theme::Chrome400, digit.data());
		}
	}

	if (Ui::MenuRow("settings", Text(Msg::Definitive_Settings)))
		ShowSettings();
	if (Ui::MenuRow("controls", Text(Msg::Definitive_Controls)))
		ShowSettings(true);
	if (Ui::MenuRow("records", Text(Msg::HIGHSCORES_Caption)))
		Open(Screen::Records);
	if (Ui::MenuRow("stats", Text(Msg::Definitive_Stats)))
	{
		Open(Screen::Stats);
		DefinitiveStatsScreen::Open();
	}
	if (Host.Recordings && Ui::MenuRow("recordings", Text(Msg::Definitive_Recordings)))
	{
		Open(Screen::Recordings);
		DefinitiveRecordings::Open(Host);
	}
	if (Ui::MenuRow("credits", Text(Msg::Definitive_Credits)))
		Open(Screen::Credits);
	if (QuitRow() && Ui::MenuRow("quit", Text(Msg::Menu1_Exit)))
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

// The high scores: five rows of rank, name and score, a row to go back and one to clear them, which asks once first
void DefinitiveFrontEnd::RenderRecords()
{
	const auto* viewport = &Ui::SafeArea();
	auto needed = 56 + 40 + 24 + 5 * 44 + 32 + 2 * (Ui::RowHeight() + 2) + 24 + 26 + 24;
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
		auto left = origin.x + 16 * scale;
		auto right = origin.x + innerWidth - 16 * scale;
		drawList->AddLine(ImVec2{left, y}, ImVec2{right, y}, Theme::WithAlpha(Theme::Chrome600, 0.6f));
		auto centre = y + rowHeight / 2;
		std::array<char, 4> rank;
		snprintf(rank.data(), rank.size(), "%d", static_cast<int>(i) + 1);
		auto rankSize = Ui::TextSize(Theme::HeadingFont(), Theme::BodySize, rank.data());
		Ui::DrawText(drawList, Theme::HeadingFont(), Theme::BodySize, ImVec2{left, centre - rankSize.y / 2},
					 Theme::Chrome400, rank.data());
		if (i < records.size() && records[i].Score >= 0)
		{
			auto score = Ui::FormatScore(records[i].Score, 11);
			auto ledSize = Theme::LedSize(Theme::CounterSize * 1.2f * scale, score.c_str());
			Theme::DrawLed(drawList, ImVec2{right - ledSize.x, centre - ledSize.y / 2},
						   Theme::CounterSize * 1.2f * scale, score.c_str());
			const auto& name = records[i].Name;
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

// The list of tables with the focused table's picture under it, or, from a game in play, the question before the
// game ends for another table
void DefinitiveFrontEnd::RenderTables()
{
	auto tables = Host.TableList ? Host.TableList() : std::vector<DefinitiveHost::TableChoice>{};
	const auto* viewport = &Ui::SafeArea();
	// Full Tilt!'s pictures are 120 pixels square
	constexpr auto pictureSize = 160.0f;
	// From a game in play, choosing another table asks before ending the game, in place of the list
	auto confirming = ConfirmTable < tables.size();
	auto rows = confirming ? 2.0f : static_cast<float>(tables.size() + 1);
	auto needed = 56 + 40 + 24 + (confirming ? 32 : 0) + rows * (Ui::RowHeight() + 2) + 24 + pictureSize + 24 + 26 + 24;
	Ui::SetScale(std::min(Ui::Scale(), viewport->Size.y / needed));
	auto scale = Ui::Scale();

	auto width = BeginColumn("##DefinitiveTables", 560);
	auto drawList = ImGui::GetWindowDrawList();
	auto margin = 48 * scale;
	auto innerWidth = width - margin - 32 * scale;
	ImVec2 origin{viewport->Pos.x + margin, viewport->Pos.y + 56 * scale};
	Ui::DrawText(drawList, Theme::HeadingFont(), Theme::TitleSize, ImVec2{origin.x + 16 * scale, origin.y},
				 Theme::Chrome50, Text(Msg::Definitive_Table), innerWidth - 32 * scale);

	auto top = origin.y + (40 + 24) * scale;
	if (confirming)
	{
		Ui::DrawText(drawList, Theme::HeadingFont(), Theme::BodySize, ImVec2{origin.x + 16 * scale, top},
					 Theme::Chrome50, Text(Msg::Definitive_EndGame), innerWidth - 32 * scale);
		top += 32 * scale;
	}
	ImGui::SetCursorScreenPos(ImVec2{origin.x, top});
	ImGui::BeginGroup();
	ImGui::PushItemWidth(innerWidth);
	// Each table with the game its data comes from and its best score; choosing another restarts the game with it
	auto picture = confirming ? RenderConfirmTable(tables) : RenderTableRows(tables);
	ImGui::PopItemWidth();
	ImGui::EndGroup();

	// The focused table's picture, framed as the score panel is
	if (picture)
	{
		ImVec2 min{origin.x + 16 * scale, ImGui::GetItemRectMax().y + 24 * scale};
		ImVec2 max{min.x + pictureSize * scale, min.y + pictureSize * scale};
		drawList->AddImage(reinterpret_cast<ImTextureID>(picture), min, max);
		drawList->AddRect(ImVec2{min.x - 1, min.y - 1}, ImVec2{max.x + 1, max.y + 1}, Theme::Chrome600, 2 * scale);
	}

	Ui::DrawHints(drawList, ImVec2{origin.x + 16 * scale, viewport->Pos.y + viewport->Size.y - 50 * scale},
				  {{Ui::Action::Choose, Text(Msg::Definitive_Choose)},
				   {Ui::Action::Select, Text(Msg::Definitive_Select)},
				   {Ui::Action::Back, Text(Msg::Definitive_Back)}});
	EndColumn();
}

// Draws Cancel and OK under the question; OK resumes the game and restarts it with the table chosen. Returns that
// table's picture, which may be null.
SDL_Texture* DefinitiveFrontEnd::RenderConfirmTable(const std::vector<DefinitiveHost::TableChoice>& tables)
{
	auto picture = tables[ConfirmTable].Picture;
	if (FocusConfirmTable)
		Ui::FocusNextRow();
	FocusConfirmTable = false;
	auto cancelled = Ui::MenuRow("cancel", Text(Msg::GenericCancel)) || BackPressed();
	if (Ui::MenuRow("confirm", Text(Msg::GenericOk), true))
	{
		// As Restart does, the game resumes before it goes
		if (Host.Resume)
			Host.Resume();
		if (Host.SelectTable)
			Host.SelectTable(ConfirmTable);
		cancelled = true;
	}
	if (cancelled)
	{
		ConfirmTable = SIZE_MAX;
		Ui::FocusNextRow();
	}
	return picture;
}

// Draws a row for each table, with its source and best score as a note, and one to go back. Choosing the loaded table
// goes back, another one selects it, or from a game in play asks first. Returns the focused table's picture, or null.
SDL_Texture* DefinitiveFrontEnd::RenderTableRows(const std::vector<DefinitiveHost::TableChoice>& tables)
{
	SDL_Texture* picture = nullptr;
	for (size_t i = 0; i < tables.size(); i++)
	{
		const auto& table = tables[i];
		auto note = table.Source;
		if (table.BestScore >= 0)
			note = std::format("{} · {}", note,
							   Ui::Fill(Text(Msg::Definitive_TableBest), Ui::FormatScore(table.BestScore, 0)));
		Ui::NextRowNote(note.c_str());
		auto id = std::format("table{}", i);
		auto chosen = Ui::MenuRow(id.c_str(), table.Name.c_str(), false,
								  table.Loaded ? Text(Msg::Definitive_TableCurrent) : nullptr);
		if (Ui::LastRowFocused())
			picture = table.Picture;
		if (chosen && table.Loaded)
			Open(TablesFrom);
		else if (chosen && TablesFrom == Screen::Pause)
		{
			ConfirmTable = i;
			FocusConfirmTable = true;
		}
		else if (chosen && Host.SelectTable)
			Host.SelectTable(i);
	}
	if (Ui::MenuRow("back", Text(Msg::Definitive_Back)) || BackPressed())
		Open(TablesFrom);
	return picture;
}

// The credits: the version and a list of headings and lines that scrolls with the arrow keys, the d-pad, the wheel
// and a drag, over a row to go back
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

	const auto* viewport = &Ui::SafeArea();
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
		std::array<char, 128> version;
		snprintf(version.data(), version.size(), Text(Msg::Definitive_Version), Host.Version);
		Ui::DrawText(drawList, Theme::TextFont(), Theme::LabelSize,
					 ImVec2{origin.x + 16 * scale, origin.y + 44 * scale}, Theme::Chrome400, version.data(),
					 innerWidth - 32 * scale);
	}

	// The list scrolls with the arrow keys, the d-pad and the wheel, under the title and over the back row
	auto top = origin.y + (40 + 32) * scale;
	auto bottom = viewport->Pos.y + viewport->Size.y - (Ui::RowHeight() + 24 + 26 + 40) * scale;
	// Lines grow with the Text Size option
	auto lineHeight = 26 * Ui::TextScale();
	auto contentHeight = 0.0f;
	for (auto& line : credits)
		contentHeight += line.Heading ? lineHeight * 1.3f : lineHeight;
	const auto& io = ImGui::GetIO();
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
		Ui::DrawText(drawList, font, size, ImVec2{origin.x + 16 * scale, y},
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

// Draws the loading screen: the game's name and year in the middle of the window, and a line that says it is loading
void DefinitiveFrontEnd::RenderLoading()
{
	Ui::NewFrame();
	// The whole window in the colour of the screens, the game's name and year in the middle, and what is happening
	auto viewport = ImGui::GetMainViewport();
	ImGui::GetBackgroundDrawList()->AddRectFilled(
		viewport->Pos, ImVec2{viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y}, Theme::Space950);
	// Upright, as large as the title that follows it, which grows to the window's width
	const auto& safe = Ui::SafeArea();
	if (safe.Size.y > safe.Size.x)
		Ui::SetScale(safe.Size.x / 500);
	auto scale = Ui::Scale();
	auto name = Theme::HeadingFont()->CalcTextSizeA(Theme::DisplaySize * scale, FLT_MAX, 0, "Space Cadet");
	auto height = name.y + (14 + 40 + 32) * scale + Ui::TextSize(Theme::TextFont(), Theme::BodySize, "Ag").y;
	ImVec2 origin{safe.Pos.x + (safe.Size.x - name.x) / 2, safe.Pos.y + (safe.Size.y - height) / 2};
	auto drawList = ImGui::GetForegroundDrawList();
	drawList->AddText(Theme::HeadingFont(), Theme::DisplaySize * scale, origin, Theme::Chrome50, "Space Cadet");
	Theme::DrawLed(drawList, ImVec2{origin.x + 2 * scale, origin.y + name.y + 14 * scale}, 40 * scale, "2026");
	Ui::DrawText(drawList, Theme::TextFont(), Theme::BodySize,
				 ImVec2{origin.x + 2 * scale, origin.y + name.y + (14 + 40 + 32) * scale}, Theme::Chrome400,
				 Text(Msg::Definitive_Loading));
}

// Draws the screen for when no game data was found: the places in looked, a note when the chosen folder had none or
// the folder dialog failed, and rows to choose a folder and to exit
void DefinitiveFrontEnd::RenderMissingData(const std::vector<std::string>& looked, bool chosenFolderFailed)
{
	const auto* viewport = &Ui::SafeArea();
	Ui::NewFrame();
	auto lines = static_cast<float>(looked.size());
	auto needed =
		56 + 60 + 14 + 40 + 40 + 40 + 72 + 26 + lines * 22 + 28 + 40 + 2 * (Ui::RowHeight() + 2) + 40 + 26 + 24;
	Ui::SetScale(std::min(Ui::Scale(), viewport->Size.y / needed));
	auto scale = Ui::Scale();

	static auto opened = true;
	Opened = opened;
	opened = false;
	auto width = BeginColumn("##DefinitiveMissingData", 680);
	// No game runs behind this screen to take Escape to its Exit binding, as on the title; Back lights Exit instead,
	// over the first row that ImGui's cancel would light. Without the Exit row, as on Android, Back quits, as it leaves
	// an app's first screen there.
	auto back = BackPressed() && QuitRow();
	if (back)
		Ui::TakeFocusRequest();
	else if (BackPressed() && Host.Quit && !Host.QuitInMenus)
		Host.Quit();
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
		Ui::DrawText(drawList, Theme::TextFont(), Theme::LabelSize, ImVec2{left + 18 * scale, y}, Theme::Chrome400,
					 path.c_str());
		y += 22 * Ui::TextScale();
	}
	if (chosenFolderFailed)
	{
		y += 8 * scale;
		Ui::DrawText(drawList, Theme::TextFont(), Theme::LabelSize, ImVec2{left, y}, Theme::LedRed,
					 Text(Msg::Definitive_DataNotThere), inner - 32 * scale);
		y += 20 * scale;
	}
	// Without a folder dialog the player has to put the data in one of the places above
	if (auto dialogError = Host.FolderDialogError ? Host.FolderDialogError() : std::string{}; !dialogError.empty())
	{
		std::array<char, 512> note;
		snprintf(note.data(), note.size(), Text(Msg::Definitive_DataNoDialog), dialogError.c_str());
		y += 8 * scale;
		y += Ui::DrawWrapped(drawList, Theme::TextFont(), Theme::LabelSize, ImVec2{left, y}, Theme::LedRed, note.data(),
							 inner - 32 * scale);
	}

	ImGui::SetCursorScreenPos(ImVec2{origin.x, y + 24 * scale});
	ImGui::BeginGroup();
	ImGui::PushItemWidth(inner);
	if (Ui::MenuRow("choose", Text(Msg::Definitive_DataChoose)) && Host.ChooseDataFolder)
		Host.ChooseDataFolder();
	if (back)
		Ui::FocusNextRow();
	if (QuitRow() && Ui::MenuRow("quit", Text(Msg::Menu1_Exit)))
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
