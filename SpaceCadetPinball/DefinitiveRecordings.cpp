#include "pch.h"
#include "DefinitiveRecordings.h"

#include "DefinitiveTheme.h"
#include "DefinitiveUi.h"
#include "options.h"
#include "translations.h"

std::vector<DefinitiveHost::Recording> DefinitiveRecordings::List{};
int DefinitiveRecordings::Selected = -1;
bool DefinitiveRecordings::ConfirmDelete = false;
bool DefinitiveRecordings::Opened = false;
bool DefinitiveRecordings::BackChosen = false;
bool DefinitiveRecordings::Refocus = false;

namespace
{
	using Theme = DefinitiveTheme;
	using Ui = DefinitiveUi;

	const char* Text(Msg id)
	{
		return translations::GetTranslation(id);
	}

	// Minutes and seconds, or hours too for a long session
	std::string Duration(float milliseconds)
	{
		auto seconds = static_cast<int>(milliseconds / 1000);
		char text[32];
		if (seconds >= 3600)
			snprintf(text, sizeof text, "%d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
		else
			snprintf(text, sizeof text, "%d:%02d", seconds / 60, seconds % 60);
		return text;
	}

	// The speeds a recording plays at
	constexpr float Speeds[] = {0.25f, 0.5f, 1, 2, 4};
	constexpr const char* SpeedNames[] = {"0.25x", "0.5x", "1x", "2x", "4x"};

	int SpeedIndex(float speed)
	{
		auto index = 0;
		for (auto each = 0; each < static_cast<int>(std::size(Speeds)); each++)
		{
			if (std::abs(Speeds[each] - speed) < std::abs(Speeds[index] - speed))
				index = each;
		}
		return index;
	}

	// How far one press seeks, in milliseconds of the recording
	constexpr float SeekStep = 10000;
} // namespace

void DefinitiveRecordings::Open(const DefinitiveHost& host)
{
	List = host.Recordings ? host.Recordings() : std::vector<DefinitiveHost::Recording>{};
	Selected = -1;
	ConfirmDelete = false;
	Opened = true;
}

bool DefinitiveRecordings::Render(const DefinitiveHost& host)
{
	auto scale = Ui::Scale();
	auto title = Selected >= 0 ? List[static_cast<size_t>(Selected)].Name.c_str() : Text(Msg::Definitive_Recordings);
	auto panel = Ui::BeginPanel("##DefinitiveRecordings", title, Opened);
	auto opened = Opened;
	Opened = false;

	// The first row takes the focus when the screen opens and when it changes between the list and a recording's
	// actions, which happens part way through a frame, so it waits for the next; ImGui's Escape takes it off too
	if (Ui::BeginPanelRows(panel, panel.Min.y + panel.Padding + 56 * scale) || opened || Refocus)
	{
		Ui::FocusNextRow();
		ImGui::SetScrollY(0);
		Refocus = false;
	}

	auto back = false;
	if (Selected < 0)
	{
		// Recording is off by default; it starts with the next start of the game
		auto& record = options::Options.RecordSessions;
		Ui::NextRowNote(Text(Msg::Definitive_RecordSessionsNote));
		if (Ui::ToggleRow("record", Text(Msg::Definitive_RecordSessions), record))
			record = !record;

		if (List.empty())
		{
			auto at = ImGui::GetCursorScreenPos();
			auto height = Ui::DrawWrapped(ImGui::GetWindowDrawList(), Theme::TextFont(), Theme::BodySize,
										  ImVec2{at.x + 16 * scale, at.y + 12 * scale}, Theme::Chrome400,
										  Text(Msg::Definitive_NoRecordings), ImGui::CalcItemWidth() - 32 * scale);
			ImGui::Dummy(ImVec2{0, height + 24 * scale});
		}
		for (size_t index = 0; index < List.size(); index++)
		{
			const auto& recording = List[index];
			auto aside = Duration(recording.Length) + " \xC2\xB7 " +
						 (recording.Games == 1
							  ? std::string{Text(Msg::Definitive_RecordingGame)}
							  : Ui::Fill(Text(Msg::Definitive_RecordingGames), std::to_string(recording.Games)));
			if (!recording.Playable)
				Ui::NextRowNote(Text(Msg::Definitive_OtherGameData));
			ImGui::PushID(static_cast<int>(index));
			// One recorded with other data can't play, but can be deleted
			if (Ui::MenuRow("recording", recording.Name.c_str(), false, aside.c_str()))
			{
				Selected = static_cast<int>(index);
				ConfirmDelete = false;
				Refocus = true;
			}
			ImGui::PopID();
		}
		if (Ui::MenuRow("back", Text(Msg::Definitive_Back)))
			BackChosen = true;
	}
	else
	{
		// A recording's actions: play it, or delete it after a confirmation
		const auto& recording = List[static_cast<size_t>(Selected)];
		if (!recording.Playable)
			Ui::NextRowNote(Text(Msg::Definitive_OtherGameData));
		if (Ui::MenuRow("play", Text(Msg::Definitive_PlayRecording), false, nullptr, recording.Playable) &&
			host.ViewRecording)
			host.ViewRecording(recording.Path);
		if (!ConfirmDelete)
		{
			if (Ui::MenuRow("delete", Text(Msg::Definitive_DeleteRecording), true))
			{
				ConfirmDelete = true;
				Ui::FocusNextRow();
			}
		}
		else
		{
			Ui::NextRowNote(Text(Msg::Definitive_DeleteRecordingConfirm));
			if (Ui::MenuRow("confirm", Text(Msg::GenericOk), true) && host.DeleteRecording)
			{
				host.DeleteRecording(recording.Path);
				Open(host);
				Opened = false;
				Refocus = true;
			}
		}
		if (Ui::MenuRow("back", Text(Msg::Definitive_Back)))
			back = true;
	}
	Ui::EndPanelRows();

	auto cancel = Ui::PanelBack();
	// Back from a recording's actions goes to the list, from the list to the title
	auto leave = false;
	if (back || (cancel && Selected >= 0))
	{
		Selected = -1;
		ConfirmDelete = false;
		Refocus = true;
	}
	else if (BackChosen || cancel)
		leave = true;
	BackChosen = false;
	Ui::EndPanel(panel, {{Ui::Action::Choose, Text(Msg::Definitive_Choose)},
						 {Ui::Action::Select, Text(Msg::Definitive_Select)},
						 {Ui::Action::Back, Text(Msg::Definitive_Back)}});
	return !leave;
}

void DefinitiveRecordings::RenderViewer(const DefinitiveHost& host)
{
	if (!host.ViewerState)
		return;
	auto state = host.ViewerState();
	auto viewport = ImGui::GetMainViewport();
	auto scale = Ui::Scale();
	auto& io = ImGui::GetIO();

	// The keys and buttons, wherever the focus is; the table takes none of them while a recording plays
	auto seekBy = [&](float milliseconds)
	{
		if (host.ViewerFrameAt && host.ViewerSeek)
			host.ViewerSeek(host.ViewerFrameAt(std::clamp(state.Time + milliseconds, 0.0f, state.Length)));
	};
	auto speed = SpeedIndex(state.Speed);
	auto setSpeed = [&](int index)
	{
		if (host.ViewerSpeed)
			host.ViewerSpeed(Speeds[std::clamp(index, 0, static_cast<int>(std::size(Speeds)) - 1)]);
	};
	auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };
	auto togglePause = pressed(ImGuiKey_Space) || pressed(ImGuiKey_Enter) || pressed(ImGuiKey_GamepadFaceDown);
	if (pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_GamepadDpadLeft))
		seekBy(-SeekStep);
	if (pressed(ImGuiKey_RightArrow) || pressed(ImGuiKey_GamepadDpadRight))
		seekBy(SeekStep);
	if (pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_GamepadR1))
		setSpeed(speed + 1);
	if (pressed(ImGuiKey_DownArrow) || pressed(ImGuiKey_GamepadL1))
		setSpeed(speed - 1);
	if (pressed(ImGuiKey_Home) && host.ViewerSeek)
		host.ViewerSeek(0);
	auto stop = pressed(ImGuiKey_Escape) || pressed(ImGuiKey_GamepadFaceRight);

	// A panel across the bottom: the timeline, the time, and the controls for a mouse or a finger
	auto margin = 24 * scale;
	auto padding = 20 * scale;
	auto height = (Theme::LabelSize + 12 + 18 + 16 + Theme::BodySize + 12) * scale + padding * 2;
	ImVec2 min{viewport->Pos.x + margin, viewport->Pos.y + viewport->Size.y - margin - height};
	ImVec2 max{viewport->Pos.x + viewport->Size.x - margin, viewport->Pos.y + viewport->Size.y - margin};
	ImGui::SetNextWindowPos(min);
	ImGui::SetNextWindowSize(ImVec2{max.x - min.x, max.y - min.y});
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{});
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::Begin("##DefinitiveViewer", nullptr,
				 Ui::Combine(ImGuiWindowFlags_NoDecoration, ImGuiWindowFlags_NoBackground, ImGuiWindowFlags_NoMove,
							 ImGuiWindowFlags_NoSavedSettings, ImGuiWindowFlags_NoNav));
	auto drawList = ImGui::GetWindowDrawList();
	Theme::DrawPanel(drawList, min, max);
	auto left = min.x + padding, right = max.x - padding;
	auto y = min.y + padding;

	// Recording, playing or paused, and the time
	auto status = state.Failed   ? Text(Msg::Definitive_RecordingDiverged)
				  : state.Ended  ? Text(Msg::Definitive_RecordingEnded)
				  : state.Paused ? Text(Msg::Definitive_Paused)
								 : Text(Msg::Definitive_Recording);
	Ui::DrawText(drawList, Theme::HeadingFont(), Theme::LabelSize, ImVec2{left + 18 * scale, y}, Theme::Chrome200,
				 status);
	Theme::DrawLamp(drawList, ImVec2{left + 5 * scale, y + Theme::LabelSize * Ui::TextScale() / 2}, 5 * scale,
					state.Paused || state.Ended ? Theme::Lamp::Dark : Theme::Lamp::Lit);
	auto time = Duration(state.Time) + " / " + Duration(state.Length) + "   " + SpeedNames[speed];
	auto timeSize = Ui::TextSize(Theme::TextFont(), Theme::LabelSize, time.c_str());
	Ui::DrawText(drawList, Theme::TextFont(), Theme::LabelSize, ImVec2{right - timeSize.x, y}, Theme::Chrome200,
				 time.c_str());
	y += Theme::LabelSize * Ui::TextScale() + 12 * scale;

	// The timeline, with a mark where each game starts; a click or a drag seeks
	auto trackHeight = 6 * scale;
	ImVec2 trackMin{left, y + 6 * scale}, trackMax{right, y + 6 * scale + trackHeight};
	auto played = state.Length > 0 ? state.Time / state.Length : 0;
	drawList->AddRectFilled(trackMin, trackMax, Theme::Space950, 3 * scale);
	drawList->AddRectFilled(trackMin, ImVec2{trackMin.x + (trackMax.x - trackMin.x) * played, trackMax.y},
							Theme::LedOrange, 3 * scale);
	for (auto game : state.Games)
	{
		auto x = trackMin.x + (trackMax.x - trackMin.x) * (state.Length > 0 ? game / state.Length : 0);
		drawList->AddLine(ImVec2{x, trackMin.y - 5 * scale}, ImVec2{x, trackMax.y + 5 * scale}, Theme::Chrome50,
						  2 * scale);
	}
	ImVec2 knob{trackMin.x + (trackMax.x - trackMin.x) * played, (trackMin.y + trackMax.y) / 2};
	drawList->AddCircleFilled(knob, 8 * scale, Theme::Chrome50);
	ImGui::SetCursorScreenPos(ImVec2{trackMin.x, trackMin.y - 8 * scale});
	ImGui::InvisibleButton("timeline", ImVec2{trackMax.x - trackMin.x, trackHeight + 16 * scale});
	// Seeking back starts the recording again, so a drag seeks once, where it is let go
	if (ImGui::IsItemDeactivated() || (ImGui::IsItemClicked() && !ImGui::IsMouseDragging(0)))
	{
		auto at = std::clamp((io.MousePos.x - trackMin.x) / (trackMax.x - trackMin.x), 0.0f, 1.0f);
		if (host.ViewerFrameAt && host.ViewerSeek)
			host.ViewerSeek(host.ViewerFrameAt(at * state.Length));
	}
	if (ImGui::IsItemActive())
	{
		auto at = std::clamp((io.MousePos.x - trackMin.x) / (trackMax.x - trackMin.x), 0.0f, 1.0f);
		drawList->AddCircle(ImVec2{trackMin.x + (trackMax.x - trackMin.x) * at, knob.y}, 10 * scale, Theme::LedOrange,
							0, 2 * scale);
	}
	y += 18 * scale + 16 * scale;

	// Buttons for a pointer, with the keys beside them in the hints
	auto buttonHeight = (Theme::BodySize + 12) * scale;
	auto button = [&](const char* id, const char* label, float x)
	{
		auto size = Ui::TextSize(Theme::TextFont(), Theme::BodySize, label);
		ImVec2 at{x, y};
		ImVec2 end{x + size.x + 24 * scale, y + buttonHeight};
		ImGui::SetCursorScreenPos(at);
		auto clicked = ImGui::InvisibleButton(id, ImVec2{end.x - at.x, end.y - at.y});
		drawList->AddRectFilled(at, end, ImGui::IsItemHovered() ? Theme::Space700 : Theme::Space800, 4 * scale);
		drawList->AddRect(at, end, Theme::Chrome600, 4 * scale);
		Ui::DrawText(drawList, Theme::TextFont(), Theme::BodySize,
					 ImVec2{at.x + 12 * scale, at.y + (buttonHeight - size.y) / 2}, Theme::Chrome50, label);
		return std::pair{clicked, end.x + 8 * scale};
	};
	auto x = left;
	auto pauseLabel = state.Paused || state.Ended ? Text(Msg::Definitive_Resume) : Text(Msg::Definitive_Pause);
	auto [pause, afterPause] = button("pause", pauseLabel, x);
	auto [back, afterBack] = button("back10", "-10 s", afterPause);
	auto [forward, afterForward] = button("forward10", "+10 s", afterBack);
	auto [slower, afterSlower] = button("slower", "-", afterForward);
	auto [faster, afterFaster] = button("faster", "+", afterSlower);
	(void)afterFaster;
	auto quitLabel = Text(Msg::Definitive_QuitToTitle);
	auto quitWidth = Ui::TextSize(Theme::TextFont(), Theme::BodySize, quitLabel).x + 24 * scale;
	auto [quit, afterQuit] = button("quit", quitLabel, right - quitWidth);
	(void)afterQuit;
	togglePause |= pause;
	if (back)
		seekBy(-SeekStep);
	if (forward)
		seekBy(SeekStep);
	if (slower)
		setSpeed(speed - 1);
	if (faster)
		setSpeed(speed + 1);
	stop |= quit;
	ImGui::End();
	ImGui::PopStyleVar(2);

	Ui::DrawHints(ImGui::GetForegroundDrawList(), ImVec2{min.x + 4 * scale, min.y - 34 * scale},
				  {{Ui::Action::Select, Text(Msg::Definitive_Pause)},
				   {Ui::Action::Change, Text(Msg::Definitive_Seek)},
				   {Ui::Action::Back, Text(Msg::Definitive_QuitToTitle)}});

	if (togglePause && host.ViewerPause)
	{
		// At the end, playing again starts from the beginning
		host.ViewerPause(!state.Ended && !state.Paused);
		if (state.Ended && host.ViewerSeek)
			host.ViewerSeek(0);
	}
	if (stop && host.StopViewing)
		host.StopViewing();
}
