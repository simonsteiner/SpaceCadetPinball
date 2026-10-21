#include "pch.h"
#include "DefinitiveFrontEnd.h"

#include "DefinitiveText.h"
#include "DefinitiveTheme.h"
#include "DefinitiveUi.h"
#include "options.h"
#include "TableView.h"
#include "translations.h"

// The screens of a game in play and of one that has just ended: pause, the high score names and the summary

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

	const char* PlayerName(int player)
	{
		return Text(static_cast<Msg>(static_cast<int>(Msg::STRING127) + std::clamp(player, 0, 3)));
	}

	bool ConfirmRestart = false, ConfirmQuit = false, FocusConfirm = false;
	int PauseFrame = 0;

	// Asks once before ending the game: the question, then Cancel and OK; true when OK is chosen
	bool Confirm(ImDrawList* drawList, ImVec2 position, float width, bool& asking)
	{
		auto scale = Ui::Scale();
		Ui::DrawText(drawList, Theme::HeadingFont(), Theme::BodySize, ImVec2{position.x + 16 * scale, position.y},
					 Theme::Chrome50, Text(Msg::Definitive_EndGame), width - 32 * scale);
		ImGui::SetCursorScreenPos(ImVec2{position.x, position.y + 32 * scale});
		ImGui::BeginGroup();
		ImGui::PushItemWidth(width);
		// The rows below the one chosen took the focus request in that frame; the question asks again
		if (FocusConfirm)
			Ui::FocusNextRow();
		FocusConfirm = false;
		auto cancelled = Ui::MenuRow("cancel", Text(Msg::GenericCancel));
		auto confirmed = Ui::MenuRow("confirm", Text(Msg::GenericOk), true);
		ImGui::PopItemWidth();
		ImGui::EndGroup();
		if (cancelled || confirmed)
		{
			asking = false;
			Ui::FocusNextRow();
		}
		return confirmed;
	}

	// Escape, or the pad's Start or right button. Not in the frame the screen opened: the key that opened it reaches
	// ImGui in that frame too.
	bool ResumePressed()
	{
		return ImGui::GetFrameCount() > PauseFrame + 1 &&
			   (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) ||
				ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false));
	}
} // namespace

void DefinitiveFrontEnd::ShowPause()
{
	if (Host.Paused && !Host.Paused() && Host.Pause)
		Host.Pause();
	ConfirmRestart = ConfirmQuit = false;
	PauseFrame = ImGui::GetFrameCount();
	Open(Screen::Pause);
}

bool DefinitiveFrontEnd::NextName()
{
	if (!Host.NextRecord || !Host.NextRecord(Entry, EntryPlace))
		return false;
	EntryName.clear();
	Open(Screen::NameEntry);
	return true;
}

ImVec2 DefinitiveFrontEnd::BeginCentred(const char* name, ImVec2 size)
{
	auto viewport = ImGui::GetMainViewport();
	// Smaller in a small window, so the whole panel shows
	auto scale = std::min(Ui::Scale(), std::min(viewport->Size.x / (size.x + 48), viewport->Size.y / (size.y + 48)));
	Ui::SetScale(scale);
	ImVec2 scaled{size.x * scale, size.y * scale};
	ImVec2 min{viewport->Pos.x + (viewport->Size.x - scaled.x) / 2,
			   viewport->Pos.y + (viewport->Size.y - scaled.y) / 2};

	// The veil dims the game under every window
	ImGui::GetBackgroundDrawList()->AddRectFilled(
		viewport->Pos, ImVec2{viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y},
		WithAlpha(Theme::Space950, 0.55f));
	ImGui::SetNextWindowPos(min);
	ImGui::SetNextWindowSize(scaled);
	if (Opened || (!ImGui::GetTopMostPopupModal() && !GImGui->NavWindow))
		ImGui::SetNextWindowFocus();
	if (Opened)
		Ui::FocusNextRow();
	Opened = false;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{});
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, 2 * scale});
	ImGui::PushStyleColor(ImGuiCol_NavCursor, 0U);
	ImGui::Begin(name, nullptr,
				 Ui::Combine(ImGuiWindowFlags_NoDecoration, ImGuiWindowFlags_NoBackground, ImGuiWindowFlags_NoMove,
							 ImGuiWindowFlags_NoSavedSettings, ImGuiWindowFlags_NoScrollWithMouse));
	Theme::DrawPanel(ImGui::GetWindowDrawList(), min, ImVec2{min.x + scaled.x, min.y + scaled.y});
	return min;
}

void DefinitiveFrontEnd::RenderPause(const TableView& view)
{
	// Five rows and a separator, or the question and two rows
	auto rowsHeight = ConfirmRestart || ConfirmQuit ? 32 + 2 * 50.0f : 5 * 50.0f + 12;
	auto min = BeginCentred("##DefinitivePause", ImVec2{420, 24 + 40 + 24 + 16 + rowsHeight + 24 + 44});
	auto scale = Ui::Scale();
	auto drawList = ImGui::GetWindowDrawList();
	auto padding = 16 * scale;
	auto width = ImGui::GetWindowSize().x - padding * 2;
	Ui::DrawText(drawList, Theme::HeadingFont(), Theme::TitleSize, ImVec2{min.x + padding * 2, min.y + 24 * scale},
				 Theme::Chrome50, Text(Msg::Definitive_Paused), width - padding * 2);
	Ui::DrawText(drawList, Theme::TextFont(), Theme::LabelSize, ImVec2{min.x + padding * 2, min.y + 66 * scale},
				 Theme::Chrome400, PlayerName(view.CurrentPlayer), width - padding * 2);

	ImVec2 rowsAt{min.x + padding, min.y + (24 + 40 + 24 + 16) * scale};
	auto resume = false;
	if (ConfirmRestart || ConfirmQuit)
	{
		// Restart and Quit to Title end the game in progress, and a recording with it
		auto& asking = ConfirmRestart ? ConfirmRestart : ConfirmQuit;
		auto restart = ConfirmRestart;
		if (Confirm(drawList, rowsAt, width, asking))
		{
			if (Host.Resume)
				Host.Resume();
			if (restart && Host.Play)
			{
				Current = Screen::None;
				Host.Play();
			}
			else if (!restart && Host.StartDemo)
			{
				// The demo replaces the game, and the title is back
				Host.StartDemo();
				ShowTitle();
			}
		}
		else if (ResumePressed())
		{
			asking = false;
			Ui::FocusNextRow();
		}
	}
	else
	{
		ImGui::SetCursorScreenPos(rowsAt);
		ImGui::BeginGroup();
		ImGui::PushItemWidth(width);
		resume = Ui::MenuRow("resume", Text(Msg::Definitive_Resume));
		if (Ui::MenuRow("settings", Text(Msg::Definitive_Settings)))
			ShowSettings();
		// Tab is ImGui's here, so the switch is an item; the game stays paused in Classic
		auto key = Host.EditionKey ? Host.EditionKey() : std::string{};
		if (Ui::MenuRow("classic", Text(Msg::Definitive_SwitchToClassic), false, key.empty() ? nullptr : key.c_str()))
			options::SetEdition(Editions::Classic);
		auto separator = ImGui::GetCursorScreenPos().y + 6 * scale;
		drawList->AddLine(ImVec2{rowsAt.x + padding, separator}, ImVec2{rowsAt.x + width - padding, separator},
						  WithAlpha(Theme::Chrome600, 0.6f));
		ImGui::Dummy(ImVec2{0, 12 * scale});
		if (Ui::MenuRow("restart", Text(Msg::Definitive_Restart), true))
			ConfirmRestart = FocusConfirm = true;
		if (Ui::MenuRow("quit", Text(Msg::Definitive_QuitToTitle), true))
			ConfirmQuit = FocusConfirm = true;
		ImGui::PopItemWidth();
		ImGui::EndGroup();
		// A tap on the dimmed game resumes too
		resume |=
			ResumePressed() || (ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered());
	}

	Ui::DrawHints(
		drawList, ImVec2{min.x + padding * 2, min.y + ImGui::GetWindowSize().y - 44 * scale},
		{{Ui::Action::Select, Text(Msg::Definitive_Select)},
		 {Ui::Action::Back, Text(ConfirmRestart || ConfirmQuit ? Msg::Definitive_Back : Msg::Definitive_Resume)}});
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(3);

	if (resume && Current == Screen::Pause)
	{
		Current = Screen::None;
		if (Host.Resume)
			Host.Resume();
	}
}

void DefinitiveFrontEnd::RenderGameOver(const TableView& view)
{
	auto players = std::clamp(view.PlayerCount, 1, 4);
	auto min =
		BeginCentred("##DefinitiveGameOver", ImVec2{560, 24 + 40 + 24 + players * 56.0f + 24 + 2 * 50 + 24 + 32});
	auto scale = Ui::Scale();
	auto drawList = ImGui::GetWindowDrawList();
	auto padding = 16 * scale;
	auto width = ImGui::GetWindowSize().x - padding * 2;
	Ui::DrawText(drawList, Theme::HeadingFont(), Theme::TitleSize, ImVec2{min.x + padding * 2, min.y + 24 * scale},
				 Theme::Chrome50, Text(Msg::STRING135), width - padding * 2);

	// Each player's score, the ones that made the high score table marked and lit
	auto y = min.y + (24 + 40 + 24) * scale;
	for (auto player = 0; player < players; player++)
	{
		auto score = view.Scores[static_cast<size_t>(player)];
		auto record = std::find(NewRecords.begin(), NewRecords.end(), score) != NewRecords.end() && score > 0;
		auto rowHeight = 56 * scale;
		auto left = min.x + padding * 2, right = min.x + padding + width - padding;
		drawList->AddLine(ImVec2{left, y}, ImVec2{right, y}, WithAlpha(Theme::Chrome600, 0.6f));
		auto centre = y + rowHeight / 2;
		Theme::DrawLamp(drawList, ImVec2{left + 6 * scale, centre}, 6 * scale,
						record ? Theme::Lamp::Lit : Theme::Lamp::Off);
		auto name = PlayerName(player);
		auto nameSize = Ui::TextSize(Theme::TextFont(), Theme::BodySize, name);
		Ui::DrawText(drawList, Theme::TextFont(), Theme::BodySize, ImVec2{left + 24 * scale, centre - nameSize.y / 2},
					 Theme::Chrome50, name);
		if (record)
		{
			auto tag = Text(Msg::Definitive_NewHighScore);
			auto tagSize = Ui::TextSize(Theme::TextFont(), Theme::LabelSize, tag);
			Ui::DrawText(drawList, Theme::TextFont(), Theme::LabelSize,
						 ImVec2{left + 24 * scale + nameSize.x + 12 * scale, centre - tagSize.y / 2}, Theme::LedOrange,
						 tag);
		}
		auto digits = Ui::FormatScore(score, 11);
		auto ledSize = Theme::LedSize(26 * scale, digits.c_str());
		Theme::DrawLed(drawList, ImVec2{right - ledSize.x, centre - ledSize.y / 2}, 26 * scale, digits.c_str());
		y += rowHeight;
	}

	ImGui::SetCursorScreenPos(ImVec2{min.x + padding, y + 24 * scale});
	ImGui::BeginGroup();
	ImGui::PushItemWidth(width);
	if (Ui::MenuRow("again", Text(Msg::Definitive_PlayAgain)) && Host.Play)
	{
		Current = Screen::None;
		Host.Play();
	}
	if (Ui::MenuRow("title", Text(Msg::Definitive_ToTitle)))
		ShowTitle();
	ImGui::PopItemWidth();
	ImGui::EndGroup();
	Ui::DrawHints(
		drawList, ImVec2{min.x + padding * 2, min.y + ImGui::GetWindowSize().y - 44 * scale},
		{{Ui::Action::Choose, Text(Msg::Definitive_Choose)}, {Ui::Action::Select, Text(Msg::Definitive_Select)}});
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(3);
}

void DefinitiveFrontEnd::RenderNameEntry()
{
	// The letters a pad or a finger picks from; a keyboard types the name instead
	static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -.!";
	constexpr int columns = 10, letterRows = 4;
	constexpr size_t maxLength = 16;
	auto min = BeginCentred("##DefinitiveNameEntry",
							ImVec2{520, 24 + 40 + 28 + 24 + 52 + 24 + (letterRows + 1) * 44.0f + 24 + 32});
	auto scale = Ui::Scale();
	auto drawList = ImGui::GetWindowDrawList();
	auto padding = 32 * scale;
	auto width = ImGui::GetWindowSize().x - padding * 2;
	Ui::DrawText(drawList, Theme::HeadingFont(), Theme::TitleSize, ImVec2{min.x + padding, min.y + 24 * scale},
				 Theme::Chrome50, Text(Msg::Definitive_NewHighScore), width);

	// Who it is for, the score and the place it takes
	char place[64];
	snprintf(place, sizeof place, Text(Msg::Definitive_Place), EntryPlace + 1);
	auto y = min.y + (24 + 44) * scale;
	Ui::DrawText(drawList, Theme::TextFont(), Theme::LabelSize, ImVec2{min.x + padding, y}, Theme::Chrome200, place,
				 width);
	auto digits = Ui::FormatScore(Entry.Score, 0);
	auto ledSize = Theme::LedSize(Theme::CounterSize * scale, digits.c_str());
	Theme::DrawLed(drawList, ImVec2{min.x + padding + width - ledSize.x, y - 2 * scale}, Theme::CounterSize * scale,
				   digits.c_str());

	auto& io = ImGui::GetIO();
	auto typedSpace = false;
	for (auto character : io.InputQueueCharacters)
	{
		if (character < 0x20 || character == 0x7F || EntryName.size() >= maxLength)
			continue;
		char utf8[5];
		EntryName.append(utf8, static_cast<size_t>(ImTextCharToUtf8(utf8, character)));
		typedSpace |= character == ' ';
	}
	auto erase = []
	{
		// The last whole UTF-8 character
		while (!EntryName.empty() && (static_cast<unsigned char>(EntryName.back()) & 0xC0U) == 0x80U)
			EntryName.pop_back();
		if (!EntryName.empty())
			EntryName.pop_back();
	};
	if (ImGui::IsKeyPressed(ImGuiKey_Backspace) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight))
		erase();
	auto keyboardDone = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
	auto done = keyboardDone || ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false);

	// The name so far, with the place for the next letter lit
	y += 28 * scale + 24 * scale;
	ImVec2 fieldMin{min.x + padding, y}, fieldMax{min.x + padding + width, y + 52 * scale};
	drawList->AddRectFilled(fieldMin, fieldMax, Theme::Space950, 6 * scale);
	drawList->AddRect(fieldMin, fieldMax, Theme::LedOrange, 6 * scale, 2 * scale);
	auto shown = EntryName.empty() ? Entry.Name.c_str() : EntryName.c_str();
	auto nameSize = Ui::TextSize(Theme::HeadingFont(), Theme::HeadingSize, shown);
	auto textX = fieldMin.x + 16 * scale;
	Ui::DrawText(drawList, Theme::HeadingFont(), Theme::HeadingSize,
				 ImVec2{textX, (fieldMin.y + fieldMax.y - nameSize.y) / 2},
				 EntryName.empty() ? Theme::Chrome600 : Theme::Chrome50, shown, width - 32 * scale);
	auto caretX = DefinitiveText::RightToLeft() ? fieldMax.x - 16 * scale - (EntryName.empty() ? 0 : nameSize.x)
												: textX + (EntryName.empty() ? 0 : nameSize.x) + 2 * scale;
	// Typed letters arrive as text input, which SDL sends only while asked for it, as an InputText asks; an input
	// method's window opens at the caret
	auto& ime = GImGui->PlatformImeData;
	ime.WantVisible = ime.WantTextInput = true;
	ime.InputPos = ImVec2{caretX, fieldMin.y};
	ime.InputLineHeight = fieldMax.y - fieldMin.y;
	ime.ViewportId = ImGui::GetMainViewport()->ID;
	if (static_cast<int>(ImGui::GetTime() * 2) % 2 == 0)
	{
		drawList->AddRectFilled(ImVec2{caretX, fieldMax.y - 14 * scale},
								ImVec2{caretX + 14 * scale, fieldMax.y - 11 * scale}, Theme::LedOrange);
	}

	// The letter grid, then Delete and Done across its last row
	y += (52 + 24) * scale;
	auto gap = 4 * scale;
	auto cellWidth = (width - gap * (columns - 1)) / columns, cellHeight = 40 * scale;
	auto opened = Ui::TakeFocusRequest();
	auto cell = [&](int index, ImVec2 at, ImVec2 size, const char* label, bool focusFirst)
	{
		ImGui::SetCursorScreenPos(at);
		ImGui::PushID(index);
		auto chosen = ImGui::InvisibleButton("cell", size, ImGuiButtonFlags_EnableNav);
		ImGui::PopID();
		if (focusFirst || (ImGui::IsItemHovered() && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)))
		{
			ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
			ImGui::SetNavCursorVisible(true);
		}
		auto focused = ImGui::IsItemFocused();
		ImVec2 max{at.x + size.x, at.y + size.y};
		drawList->AddRectFilled(at, max, focused ? Theme::Space600 : WithAlpha(Theme::Space950, 0.6f), 4 * scale);
		if (focused)
			drawList->AddRect(at, max, Theme::LedOrange, 4 * scale);
		auto labelSize = Theme::HeadingFont()->CalcTextSizeA(Theme::BodySize * scale, FLT_MAX, 0, label);
		drawList->AddText(Theme::HeadingFont(), Theme::BodySize * scale,
						  ImVec2{(at.x + max.x - labelSize.x) / 2, (at.y + max.y - labelSize.y) / 2},
						  focused ? Theme::Chrome50 : Theme::Chrome200, label);
		// Space and Enter on a keyboard type and finish the name; only a pad, a tap or a click picks a cell
		return chosen && !typedSpace && !keyboardDone;
	};
	for (auto index = 0; index < columns * letterRows; index++)
	{
		auto row = index / columns, column = index % columns;
		ImVec2 at{min.x + padding + static_cast<float>(column) * (cellWidth + gap),
				  y + static_cast<float>(row) * (cellHeight + gap)};
		char label[2] = {letters[index] == ' ' ? '_' : letters[index], 0};
		if (cell(index, at, ImVec2{cellWidth, cellHeight}, label, opened && index == 0) && EntryName.size() < maxLength)
			EntryName += letters[index];
	}
	auto lastRow = y + static_cast<float>(letterRows) * (cellHeight + gap);
	auto half = (width - gap) / 2;
	if (cell(100, ImVec2{min.x + padding, lastRow}, ImVec2{half, cellHeight}, Text(Msg::Definitive_Delete), false))
		erase();
	if (cell(101, ImVec2{min.x + padding + half + gap, lastRow}, ImVec2{half, cellHeight}, Text(Msg::Definitive_Done),
			 false))
		done = true;

	// A keyboard types the letters, and its Enter finishes, so it has no use for the grid's Select
	ImVec2 hints{min.x + padding, min.y + ImGui::GetWindowSize().y - 44 * scale};
	if (Ui::LastDevice() == Ui::Device::Keyboard)
		Ui::DrawHints(
			drawList, hints,
			{{Ui::Action::Erase, Text(Msg::Definitive_Delete)}, {Ui::Action::Finish, Text(Msg::Definitive_Done)}});
	else
		Ui::DrawHints(drawList, hints,
					  {{Ui::Action::Select, Text(Msg::Definitive_Select)},
					   {Ui::Action::Erase, Text(Msg::Definitive_Delete)},
					   {Ui::Action::Finish, Text(Msg::Definitive_Done)}});
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(3);

	if (done)
	{
		// An empty name keeps the player's default one
		DefinitiveHost::Record record{EntryName.empty() ? Entry.Name : EntryName, Entry.Score};
		if (Host.SaveRecord)
			Host.SaveRecord(record, EntryPlace);
		NewRecords.push_back(Entry.Score);
		if (!NextName())
		{
			SummaryShown = true;
			Open(Screen::GameOver);
		}
	}
}
