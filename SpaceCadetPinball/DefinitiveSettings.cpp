#include "pch.h"
#include "DefinitiveSettings.h"

#include "DefinitiveFrontEnd.h"
#include "DefinitiveTheme.h"
#include "DefinitiveUi.h"
#include "options.h"
#include "translations.h"

#include <utility>

DefinitiveSettings::Group DefinitiveSettings::Current = Group::Edition;
bool DefinitiveSettings::Opened = false;
bool DefinitiveSettings::Languages = false;
bool DefinitiveSettings::ShownLanguages = false;
bool DefinitiveSettings::BackChosen = false;
bool DefinitiveSettings::FocusLanguage = false;
int DefinitiveSettings::Slot = 0;
GameInput* DefinitiveSettings::Waited = nullptr;
std::vector<std::string> DefinitiveSettings::Packs{};
bool DefinitiveSettings::PacksRead = false;

namespace
{
	using Theme = DefinitiveTheme;
	using Ui = DefinitiveUi;

	const char* Text(Msg id)
	{
		return translations::GetTranslation(id);
	}

	const Msg GroupNames[] = {
		Msg::Definitive_Edition, Msg::Definitive_Display,  Msg::Definitive_Graphics,
		Msg::Definitive_Audio,   Msg::Definitive_Controls, Msg::Definitive_Accessibility,
	};

	// Volumes are 0 to 128 in the options and percentages on screen
	int ToPercent(int volume)
	{
		return static_cast<int>(std::lround(volume * 100.0 / options::MaxVolume));
	}

	int FromPercent(int percent)
	{
		return static_cast<int>(std::lround(percent * options::MaxVolume / 100.0));
	}

	// An input as a slot shows it: the key, or the mouse or pad button
	std::string Describe(const GameInput& input)
	{
		switch (input.Type)
		{
		case InputTypes::Keyboard:
			return input.GetShortInputDescription();
		case InputTypes::Mouse:
			return "Mouse " + input.GetShortInputDescription();
		case InputTypes::GameController:
			return "Pad " + input.GetShortInputDescription();
		default:
			return "-";
		}
	}
} // namespace

void DefinitiveSettings::Open(Group group)
{
	Current = group;
	Opened = true;
	Languages = false;
	FocusLanguage = false;
	Slot = 0;
}

bool DefinitiveSettings::WaitsForBinding()
{
	return options::WaitingForInput() && options::InputWaitedFor() == Waited;
}

bool DefinitiveSettings::Render(const DefinitiveHost& host, bool gameRunning)
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
	ImGui::Begin("##DefinitiveSettings", nullptr,
				 Ui::Combine(ImGuiWindowFlags_NoDecoration, ImGuiWindowFlags_NoBackground, ImGuiWindowFlags_NoMove,
							 ImGuiWindowFlags_NoSavedSettings, ImGuiWindowFlags_NoScrollWithMouse));
	auto drawList = ImGui::GetWindowDrawList();
	Theme::DrawPanel(drawList, min, ImVec2{min.x + size.x, min.y + size.y});
	auto padding = 24 * scale;
	auto innerWidth = size.x - padding * 2;
	auto title = Languages ? Text(Msg::Definitive_Language) : Text(Msg::Definitive_Settings);
	Ui::DrawText(drawList, Theme::HeadingFont(), Theme::TitleSize, ImVec2{min.x + padding, min.y + padding},
				 Theme::Chrome50, title, innerWidth);

	// The groups as tabs; Q and E, the shoulder buttons or a tap move between them
	auto tabsY = min.y + padding + 48 * scale;
	auto tabHeight = Ui::TabHeight * scale;
	auto previous = Current;
	auto showedLanguages = Languages;
	if (!Languages)
	{
		std::vector<const char*> names;
		for (auto name : GroupNames)
			names.push_back(Text(name));
		Current = static_cast<Group>(
			Ui::Tabs(drawList, ImVec2{min.x + padding, tabsY}, innerWidth, names, std::to_underlying(Current)));
	}

	// The rows scroll under the tabs, and the focus follows them
	auto rowsY = tabsY + tabHeight + 12 * scale;
	auto hintsHeight = 26 * scale + padding * 1.5f;
	ImGui::SetCursorScreenPos(ImVec2{min.x + padding / 2, rowsY});
	ImGui::BeginChild("##rows", ImVec2{size.x - padding, min.y + size.y - hintsHeight - rowsY},
					  ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
	// ImGui's Escape takes the focus off the rows too; the screen gives it back to the first
	// The sound packs are looked for again each time the screen or a tab opens, not every frame
	if (opened || Current != previous)
		PacksRead = false;
	if (opened || Current != previous ||
		(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !GImGui->NavId))
	{
		Ui::FocusNextRow();
		ImGui::SetScrollY(0);
		Slot = 0;
	}
	else if (FocusLanguage)
		ImGui::SetScrollY(0);
	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - (ImGui::GetScrollMaxY() > 0 ? 12 * scale : 0));
	if (Languages)
		RenderLanguages(host, Languages != ShownLanguages);
	else
	{
		switch (Current)
		{
		case Group::Edition:
			RenderEdition(host, gameRunning);
			break;
		case Group::Display:
			RenderDisplay(host, gameRunning);
			break;
		case Group::Graphics:
			RenderGraphics(host);
			break;
		case Group::Audio:
			RenderAudio(host);
			break;
		case Group::Controls:
			RenderControls();
			break;
		default:
			RenderAccessibility();
			break;
		}
	}
	// Every tab and the language list end in Back, for a mouse or touch, which have no hints
	if (Ui::MenuRow("back", Text(Msg::Definitive_Back)))
		BackChosen = true;
	ImGui::PopItemWidth();
	ImGui::EndChild();
	ShownLanguages = showedLanguages && Languages;

	auto back =
		BackChosen ||
		(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !options::WaitingForInput() &&
		 (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
		  ImGui::IsMouseClicked(ImGuiMouseButton_Right)));
	BackChosen = false;
	ImVec2 hints{min.x + padding, min.y + size.y - padding - 26 * scale};
	if (Languages)
		Ui::DrawHints(drawList, hints,
					  {{Ui::Action::Choose, Text(Msg::Definitive_Choose)},
					   {Ui::Action::Select, Text(Msg::Definitive_Select)},
					   {Ui::Action::Back, Text(Msg::Definitive_Back)}});
	// While a slot waits, those keys and buttons would be bound; its row says how to cancel instead
	else if (!WaitsForBinding())
		Ui::DrawHints(drawList, hints,
					  {{Ui::Action::Group, Text(Msg::Definitive_Group)},
					   {Ui::Action::Change, Text(Msg::Definitive_Change)},
					   {Ui::Action::Back, Text(Msg::Definitive_Back)}});
	ImGui::End();
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(3);

	// Back from the list lands on the Language row that opened it
	if (back && Languages)
	{
		Languages = false;
		FocusLanguage = true;
		return true;
	}
	if (back)
		options::WaitForInput(nullptr);
	return !back;
}

void DefinitiveSettings::RenderEdition(const DefinitiveHost& host, bool gameRunning)
{
	// Choosing Classic takes this screen away, as the switch does during play
	char note[160];
	auto key = host.EditionKey ? host.EditionKey() : std::string{};
	snprintf(note, sizeof note, Text(Msg::Definitive_EditionNote), key.empty() ? "-" : key.c_str());
	Ui::NextRowNote(note);
	auto edition = options::GetEdition() == Editions::Definitive ? 1 : 0;
	auto chosen = Ui::ChoiceRow("edition", Text(Msg::Definitive_Edition),
								{Text(Msg::Definitive_Classic), Text(Msg::Menu1_ToggleEdition)}, edition);
	if (chosen != edition)
		options::SetEdition(chosen ? Editions::Definitive : Editions::Classic);

	if (FocusLanguage)
		Ui::FocusNextRow();
	FocusLanguage = false;
	if (Ui::MenuRow("language", Text(Msg::Definitive_Language), false, translations::GetCurrentLanguage()->DisplayName))
		Languages = true;

	// The folder the data came from; choosing another reads the data from there, after a restart. Why a choice
	// changed nothing takes the folder's place until the next one: no dialog, or no data in the folder chosen.
	if (host.ChooseDataFolder)
	{
		auto folder = host.DataFolder ? host.DataFolder() : std::string{};
		auto dialogError = host.FolderDialogError ? host.FolderDialogError() : std::string{};
		auto failed = !dialogError.empty() || (host.ChosenFolderEmpty && host.ChosenFolderEmpty());
		char failure[512];
		if (!dialogError.empty())
			snprintf(failure, sizeof failure, Text(Msg::Definitive_DataDialogFailed), dialogError.c_str());
		else
			snprintf(failure, sizeof failure, "%s", Text(Msg::Definitive_DataNotThere));
		Ui::NextRowNote(failed ? failure : folder.c_str());
		// Without a dialog the row can't restart anything, and the error needs the room
		auto aside =
			dialogError.empty() ? Text(gameRunning ? Msg::Definitive_EndsGame : Msg::Definitive_Restarts) : nullptr;
		if (Ui::MenuRow("datafolder", Text(Msg::Definitive_DataFolder), failed, aside))
			host.ChooseDataFolder();
	}

	// Rows that restart the game change only when chosen, so a Left or Right that passes over them restarts nothing
	Ui::NextRowNote(Text(gameRunning ? Msg::Definitive_EndsGame : Msg::Definitive_Restarts));
	if (Ui::ToggleRow("prefer3dpb", Text(Msg::Definitive_Prefer3DPB), options::Options.Prefer3DPBGameData) &&
		!Ui::RowStep())
		options::toggle(Menu1::Prefer3DPBGameData);
}

void DefinitiveSettings::RenderLanguages(const DefinitiveHost& host, bool opened)
{
	// The list opens on the language in use
	auto current = translations::GetCurrentLanguage();
	for (auto& language : translations::Languages)
	{
		if (opened && &language == current)
		{
			Ui::FocusNextRow();
			ImGui::SetScrollHereY(0.5f);
		}
		ImGui::PushID(language.ShortName);
		// The language in use is marked; choosing another restarts the game in it
		if (Ui::MenuRow("language", language.DisplayName, false, &language == current ? "\xE2\x80\xA2" : nullptr) &&
			&language != current && host.SetLanguage)
			host.SetLanguage(language.ShortName);
		ImGui::PopID();
	}
}

void DefinitiveSettings::RenderDisplay(const DefinitiveHost& host, bool gameRunning)
{
	auto& options = options::Options;
	if (Ui::ToggleRow("fullscreen", Text(Msg::Menu1_Full_Screen), options.FullScreen))
		options::toggle(Menu1::Full_Screen);

	// Full Tilt's art comes in three sizes; changing it reloads the table, so only choosing the row does
	auto maxResolution = host.MaxResolution ? host.MaxResolution() : 0;
	if (maxResolution > 0)
	{
		Ui::NextRowNote(Text(gameRunning ? Msg::Definitive_EndsGame : Msg::Definitive_Restarts));
		auto current = options.Resolution.V < 0 ? 0 : options.Resolution.V + 1;
		auto chosen = Ui::ChoiceRow("resolution", Text(Msg::Menu1_Table_Resolution),
									{Text(Msg::Definitive_Largest), "640 x 480", "800 x 600", "1024 x 768"}, current);
		// A size the data lacks goes round to Largest, the largest it has; choosing the row steps on through the sizes
		if (chosen > maxResolution + 1)
			chosen = 0;
		if (chosen != current && !Ui::RowStep())
			options::toggle(chosen == 0 ? Menu1::MaximumResolution
										: static_cast<Menu1>(std::to_underlying(Menu1::R640x480) + chosen - 1));
	}

	if (Ui::ToggleRow("uniform", Text(Msg::Menu1_WindowUniformScale), options.UniformScaling))
		options::toggle(Menu1::WindowUniformScale);
	if (Ui::ToggleRow("integer", Text(Msg::Definitive_IntegerScaling), options.IntegerScaling))
		options::toggle(Menu1::WindowIntegerScale);
	if (Ui::ToggleRow("stacked", Text(Msg::Definitive_StackedLayout), options.StackedLayout, options.UniformScaling))
		options::toggle(Menu1::WindowStackedLayout);
	// For a cabinet's monitor on its side; a window taller than wide then puts the HUD under the table
	Ui::NextRowNote(Text(Msg::Definitive_RotationNote));
	if (auto rotation =
			Ui::ChoiceRow("rotation", Text(Msg::Definitive_Rotation), {"0°", "90°", "180°", "270°"}, options.Rotation);
		rotation != options.Rotation && host.SetRotation)
		host.SetRotation(rotation);

	auto rateChanged = false;
	if (Ui::ToggleRow("match", Text(Msg::Definitive_MatchRefresh), options.MatchRefreshRate))
	{
		options.MatchRefreshRate ^= true;
		rateChanged = true;
	}
	if (!options.MatchRefreshRate)
	{
		auto fps = Ui::SliderRow("fps", Text(Msg::Definitive_Fps), options.FramesPerSecond, options::MinFps,
								 options::MaxFps, 10);
		rateChanged |= fps != options.FramesPerSecond;
		options.FramesPerSecond = fps;
	}
	auto ups =
		Ui::SliderRow("ups", Text(Msg::Definitive_Ups), options.UpdatesPerSecond, options::MinUps, options::MaxUps, 10);
	rateChanged |= ups != options.UpdatesPerSecond;
	options.UpdatesPerSecond = ups;
	if (rateChanged && host.FrameRateChanged)
		host.FrameRateChanged();
	if (Ui::ToggleRow("uncapped", Text(Msg::Definitive_Uncapped), options.UncappedUpdatesPerSecond))
		options.UncappedUpdatesPerSecond ^= true;
	if (Ui::ToggleRow("sleep", Text(Msg::Definitive_PreciseSleep), options.HybridSleep))
	{
		options.HybridSleep ^= true;
		if (host.PreciseSleepChanged)
			host.PreciseSleepChanged();
	}
	if (Ui::ToggleRow("cursor", Text(Msg::Definitive_HideCursor), options.HideCursor))
		options.HideCursor ^= true;
}

void DefinitiveSettings::RenderGraphics(const DefinitiveHost& host)
{
	auto& options = options::Options;
	if (Ui::ToggleRow("upscaled", Text(Msg::Menu1_ToggleUpscaledArt), options.UpscaledArt))
		options::toggle(Menu1::WindowUpscaledArt);
	auto factor = options.UpscaleFactor == 4 ? 1 : 0;
	auto chosen = Ui::ChoiceRow("factor", Text(Msg::Definitive_UpscaleSize), {"2x", "4x"}, factor, options.UpscaledArt);
	if (chosen != factor && host.SetUpscaleFactor)
		host.SetUpscaleFactor(chosen ? 4 : 2);
	if (Ui::ToggleRow("backdrop", Text(Msg::Definitive_Backdrop), options.Backdrop))
		options::toggle(Menu1::WindowBackdrop);
	Ui::NextRowNote(Text(Msg::Definitive_PanelArtNote));
	if (Ui::ToggleRow("panelart", Text(Msg::Definitive_PanelArt), options.PanelArt))
		options.PanelArt = !options.PanelArt;
	if (Ui::ToggleRow("filtering", Text(Msg::Definitive_LinearFiltering), options.LinearFiltering))
		options::toggle(Menu1::WindowLinearFilter);

	// The compositor's effects and the post effect need SDL's GPU renderer; without it the choices are kept for when it
	// is there
	// A note shows under the next row only, so each of the three gets it
	auto effectsUnavailable = host.EffectsUnavailable ? host.EffectsUnavailable() : nullptr;
	if (effectsUnavailable)
		Ui::NextRowNote(effectsUnavailable);
	if (Ui::ToggleRow("lighting", Text(Msg::Definitive_Lighting), options.Lighting))
		options.Lighting = !options.Lighting;
	if (effectsUnavailable)
		Ui::NextRowNote(effectsUnavailable);
	if (Ui::ToggleRow("glow", Text(Msg::Definitive_LampGlow), options.LampGlow))
		options.LampGlow = !options.LampGlow;
	if (effectsUnavailable)
		Ui::NextRowNote(effectsUnavailable);
	if (Ui::ToggleRow("ball", Text(Msg::Definitive_ShadedBall), options.ShadedBall))
		options.ShadedBall = !options.ShadedBall;

	if (auto unavailable = host.PostProcessingUnavailable ? host.PostProcessingUnavailable() : nullptr)
		Ui::NextRowNote(unavailable);
	auto effect = options.PostProcessing.V;
	auto chosenEffect = Ui::ChoiceRow(
		"effect", Text(Msg::Definitive_PostProcessing),
		{Text(Msg::Definitive_None), Text(Msg::Definitive_Crt), Text(Msg::Definitive_SmoothEdges)}, effect);
	if (chosenEffect != effect && host.SetPostProcessing)
		host.SetPostProcessing(chosenEffect);

	// A font for the text the built-in fonts lack, such as Chinese, Japanese and Korean; the row shows its file name
	const auto& font = options.FontFileName.V;
	auto slash = font.find_last_of("/\\");
	auto fontName =
		font.empty() ? Text(Msg::Definitive_None) : font.c_str() + (slash == std::string::npos ? 0 : slash + 1);
	Ui::NextRowNote(Text(Msg::Definitive_FontNote));
	if (Ui::MenuRow("font", Text(Msg::Definitive_FontFile), false, fontName) && host.ChangeFont)
		host.ChangeFont();
}

void DefinitiveSettings::RenderAudio(const DefinitiveHost& host)
{
	auto& options = options::Options;
	if (Ui::ToggleRow("sounds", Text(Msg::Menu1_Sounds), options.Sounds))
		options::toggle(Menu1::Sounds);
	auto volume = Ui::SliderRow("soundvolume", Text(Msg::Definitive_SoundVolume), ToPercent(options.SoundVolume), 0,
								100, 5, "%d%%");
	if (volume != ToPercent(options.SoundVolume) && host.SetSoundVolume)
		host.SetSoundVolume(FromPercent(volume));
	if (Ui::ToggleRow("stereo", Text(Msg::Definitive_Stereo), options.SoundStereo))
		options::toggle(Menu1::SoundStereo);
	auto channels = Ui::SliderRow("channels", Text(Msg::Definitive_SoundChannels), options.SoundChannels,
								  options::MinSoundChannels, options::MaxSoundChannels, 1);
	if (channels != options.SoundChannels && host.SetSoundChannels)
		host.SetSoundChannels(channels);

	if (Ui::ToggleRow("music", Text(Msg::Menu1_Music), options.Music))
		options::toggle(Menu1::Music);
	auto music = Ui::SliderRow("musicvolume", Text(Msg::Definitive_MusicVolume), ToPercent(options.MusicVolume), 0, 100,
							   5, "%d%%");
	if (ToPercent(options.MusicVolume) != music && host.SetMusicVolume)
		host.SetMusicVolume(FromPercent(music));

	// Space Cadet 2026's own, on the sound effects only
	Ui::NextRowNote(Text(Msg::Definitive_RoomReverbNote));
	if (Ui::ToggleRow("reverb", Text(Msg::Definitive_RoomReverb), options.RoomReverb))
		options.RoomReverb = !options.RoomReverb;
	Ui::NextRowNote(Text(Msg::Definitive_WideStereoNote));
	if (Ui::ToggleRow("wide", Text(Msg::Definitive_WideStereo), options.WideStereo && options.SoundStereo,
					  options.SoundStereo))
		options.WideStereo = !options.WideStereo;

	// The packs found, after None; choosing the row or right steps on, left steps back
	if (!host.SoundPacks || !host.SetSoundPack)
		return;
	if (!PacksRead)
	{
		Packs = host.SoundPacks();
		Packs.insert(Packs.begin(), std::string{});
		PacksRead = true;
	}
	const auto& packs = Packs;
	auto current = std::find(packs.begin(), packs.end(), options.SoundPack.V) - packs.begin();
	if (current == static_cast<long>(packs.size()))
		current = 0;
	const auto& name = packs[current];
	Ui::NextRowNote(Text(Msg::Definitive_SoundPackNote));
	auto chosen = Ui::MenuRow("pack", Text(Msg::Definitive_SoundPack), false,
							  name.empty() ? Text(Msg::Definitive_None) : name.c_str());
	auto step = chosen ? 1 : Ui::RowStep();
	if (step)
	{
		auto count = static_cast<long>(packs.size());
		host.SetSoundPack(packs[(current + step + count) % count]);
	}
}

void DefinitiveSettings::RenderControls()
{
	auto scale = Ui::Scale();
	auto slotWidth = 100 * scale, slotHeight = 32 * scale, gap = 6 * scale;
	auto slotsWidth = slotWidth * 3 + gap * 2;
	auto waiting = options::WaitingForInput();
	for (auto binding = GameBindings::Min; binding < GameBindings::Max; binding++)
	{
		auto& control = options::Options.Key[~binding];
		ImGui::PushID(~binding);
		auto waited = options::InputWaitedFor();
		if (waiting && waited >= std::begin(control.Inputs) && waited < std::end(control.Inputs))
			Ui::NextRowNote(Text(Msg::Definitive_CancelInput));
		auto chosen = Ui::MenuRow("binding", Text(control.Description), false, nullptr, true, slotsWidth + 8 * scale);
		auto row = Ui::LastRow();
		auto focused = Ui::LastRowFocused();
		auto step = Ui::RowStep();
		if (focused && !waiting)
			Slot = std::clamp(Slot + step, 0, 2);

		auto drawList = ImGui::GetWindowDrawList();
		ImVec2 slotsMin{row.Max.x - 16 * scale - slotsWidth, (row.Min.y + row.Max.y - slotHeight) / 2};
		auto mouse = ImGui::GetIO().MousePos;
		auto tapped = -1;
		for (auto slot = 0; slot < 3; slot++)
		{
			auto& input = control.Inputs[slot];
			ImVec2 slotMin{slotsMin.x + static_cast<float>(slot) * (slotWidth + gap), slotsMin.y};
			ImVec2 slotMax{slotMin.x + slotWidth, slotMin.y + slotHeight};
			if (mouse.x >= slotMin.x && mouse.x < slotMax.x && mouse.y >= slotMin.y && mouse.y < slotMax.y)
				tapped = slot;
			auto isWaiting = waiting && options::InputWaitedFor() == &input;
			drawList->AddRectFilled(slotMin, slotMax, Theme::Space950, 4 * scale);
			auto outline = isWaiting ? Theme::LedOrange : focused && slot == Slot ? Theme::Chrome200 : Theme::Chrome600;
			drawList->AddRect(slotMin, slotMax, outline, 4 * scale, isWaiting ? 2 * scale : 1);
			auto label = isWaiting ? std::string{Text(Msg::Definitive_PressInput)} : Describe(input);
			auto size = Ui::TextSize(Theme::TextFont(), Theme::LabelSize, label.c_str());
			drawList->PushClipRect(slotMin, slotMax, true);
			Ui::DrawText(drawList, Theme::TextFont(), Theme::LabelSize,
						 ImVec2{std::max(slotMin.x + 4 * scale, (slotMin.x + slotMax.x - size.x) / 2),
								(slotMin.y + slotMax.y - size.y) / 2},
						 isWaiting ? Theme::LedOrange : Theme::Chrome200, label.c_str());
			drawList->PopClipRect();
		}

		// Choosing a slot waits for the next key, mouse or pad button; Delete or the pad's left button clears it
		if (chosen && !waiting)
		{
			if (tapped >= 0 && ImGui::IsMouseReleased(0))
				Slot = tapped;
			Waited = &control.Inputs[Slot];
			options::WaitForInput(Waited);
		}
		if (focused && !waiting &&
			(ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false)))
			control.Inputs[Slot] = {};
		ImGui::PopID();
	}

	if (Ui::ToggleRow("rumble", Text(Msg::Definitive_Rumble), options::Options.Rumble))
		options::Options.Rumble ^= true;
	if (Ui::MenuRow("defaults", Text(Msg::Definitive_DefaultControls)))
	{
		for (auto& control : options::Options.Key)
			control.Reset();
	}
}

void DefinitiveSettings::RenderAccessibility()
{
	// The UI Scale option sizes Classic's menus and these screens alike
	Ui::NextRowNote(Text(Msg::Definitive_UiScaleNote));
	auto percent = static_cast<int>(std::lround(options::Options.UIScale * 100));
	// The same range as Classic's UI Scale, so opening this doesn't change a larger value
	auto chosen = Ui::SliderRow("uiscale", Text(Msg::Definitive_UiScale), percent, 80, 500, 10, "%d%%");
	if (chosen != percent)
	{
		options::Options.UIScale.V = static_cast<float>(chosen) / 100;
		ImGui::GetStyle().FontScaleMain = options::Options.UIScale;
	}

	auto& options = options::Options;
	Ui::NextRowNote(Text(Msg::Definitive_TextSizeNote));
	constexpr std::array sizes{100, 125, 150};
	auto size = static_cast<int>(std::ranges::find(sizes, options.TextSize.V) - sizes.begin());
	if (auto chosenSize = Ui::ChoiceRow("textsize", Text(Msg::Definitive_TextSize), {"100%", "125%", "150%"}, size);
		chosenSize != size)
		options.TextSize = sizes[static_cast<size_t>(chosenSize)];

	Ui::NextRowNote(Text(Msg::Definitive_HighContrastNote));
	if (Ui::ToggleRow("contrast", Text(Msg::Definitive_HighContrast), options.HighContrastHud))
		options.HighContrastHud = !options.HighContrastHud;

	Ui::NextRowNote(Text(Msg::Definitive_ColoursNote));
	options.ColourVision =
		Ui::ChoiceRow("colours", Text(Msg::Definitive_Colours),
					  {Text(Msg::Definitive_ColoursStandard), Text(Msg::Definitive_ColoursSafe)}, options.ColourVision);

	Ui::NextRowNote(Text(Msg::Definitive_ReducedMotionNote));
	if (Ui::ToggleRow("motion", Text(Msg::Definitive_ReducedMotion), options::Options.ReducedMotion))
	{
		options::Options.ReducedMotion = !options::Options.ReducedMotion;
		options::Options.ReducedMotionChosen = true;
	}
}
