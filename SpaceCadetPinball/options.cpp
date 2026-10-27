#include "pch.h"
#include "options.h"

#include "PostEffect.h"
#include "DefinitiveEdition.h"

#include "fullscrn.h"
#include "midi.h"
#include "pb.h"
#include "render.h"
#include "Sound.h"
#include "winmain.h"
#include "translations.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

constexpr int options::MaxUps, options::MaxFps, options::MinUps, options::MinFps, options::DefUps, options::DefFps;
constexpr int options::MaxSoundChannels, options::MinSoundChannels, options::DefSoundChannels;
constexpr int options::MaxVolume, options::MinVolume, options::DefVolume;

std::unordered_map<std::string, std::string> options::settings{};
bool options::ShowDialog = false;
bool options::MigrateKeycodes = false;
GameInput* options::ControlWaitingForInput = nullptr;
std::vector<OptionBase*> options::AllOptions{};

// The settings table is aligned by hand
// clang-format off
optionsStruct options::Options
{
	// Keyboard inputs are scancodes, so a binding stays on the same physical key in any layout
	{
		{
			"Left Flipper key",
			Msg::KEYMAPPER_FlipperL,
			{InputTypes::Keyboard, SDL_SCANCODE_Z},
			{InputTypes::Mouse, SDL_BUTTON_LEFT},
			{InputTypes::GameController, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}
		},
		{
			"Right Flipper key",
			Msg::KEYMAPPER_FlipperR,
			{InputTypes::Keyboard, SDL_SCANCODE_SLASH},
			{InputTypes::Mouse,SDL_BUTTON_RIGHT},
			{InputTypes::GameController, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}
		},
		{
			"Plunger key",
			Msg::KEYMAPPER_Plunger,
			{InputTypes::Keyboard, SDL_SCANCODE_SPACE},
			{InputTypes::Mouse,SDL_BUTTON_MIDDLE},
			{InputTypes::GameController, SDL_GAMEPAD_BUTTON_SOUTH}
		},
		{
			"Left Table Bump key",
			Msg::KEYMAPPER_BumpLeft,
			{InputTypes::Keyboard, SDL_SCANCODE_X},
			{InputTypes::Mouse,SDL_BUTTON_X1},
			{InputTypes::GameController, SDL_GAMEPAD_BUTTON_DPAD_LEFT}
		},
		{
			"Right Table Bump key",
			Msg::KEYMAPPER_BumpRight,
			{InputTypes::Keyboard, SDL_SCANCODE_PERIOD},
			{InputTypes::Mouse,SDL_BUTTON_X2},
			{InputTypes::GameController, SDL_GAMEPAD_BUTTON_DPAD_RIGHT}
		},
		{
			"Bottom Table Bump key",
			Msg::KEYMAPPER_BumpBottom,
			{InputTypes::Keyboard, SDL_SCANCODE_UP},
			{InputTypes::Mouse,SDL_BUTTON_X2 + 1},
			{InputTypes::GameController, SDL_GAMEPAD_BUTTON_DPAD_UP}
		},
		{
			"New Game",
			Msg::Menu1_New_Game,
			{InputTypes::Keyboard, SDL_SCANCODE_F2},
			{},
			{}
		},
		{
			"Toggle Pause",
			Msg::Menu1_Pause_Resume_Game,
			{InputTypes::Keyboard, SDL_SCANCODE_F3},
			{},
			{InputTypes::GameController, SDL_GAMEPAD_BUTTON_START}
		},
		{
			"Toggle FullScreen",
			Msg::Menu1_Full_Screen,
			{InputTypes::Keyboard, SDL_SCANCODE_F4},
			{},
			{}
		},
		{
			"Toggle Sounds",
			Msg::Menu1_Sounds,
			{InputTypes::Keyboard, SDL_SCANCODE_F5},
			{},
			{}
		},
		{
			"Toggle Music",
			Msg::Menu1_Music,
			{InputTypes::Keyboard, SDL_SCANCODE_F6},
			{},
			{}
		},
		{
			"Show Control Dialog",
			Msg::Menu1_Player_Controls,
			{InputTypes::Keyboard, SDL_SCANCODE_F8},
			{},
			{}
		},
		{
			"Toggle Menu Display",
			Msg::Menu1_ToggleShowMenu,
			{InputTypes::Keyboard, SDL_SCANCODE_F9},
			{},
			{}
		},
		{
			"Exit",
			Msg::Menu1_Exit,
			{InputTypes::Keyboard, SDL_SCANCODE_ESCAPE},
			{},
			{InputTypes::GameController, SDL_GAMEPAD_BUTTON_BACK}
		},
		{
			"Toggle Upscaled Art",
			Msg::Menu1_ToggleUpscaledArt,
			{InputTypes::Keyboard, SDL_SCANCODE_F7},
			{},
			{}
		},
		{
			"Toggle Edition",
			Msg::Menu1_ToggleEdition,
			{InputTypes::Keyboard, SDL_SCANCODE_TAB},
			{},
			{}
		},
	},
	{"Sounds", true},
	{"Music", true},
	{"FullScreen", false},
	{"Players", 1},
	{"Screen Resolution", -1},
	{"UI Scale", 1.0f},
	{"Uniform scaling", true},
	{"Linear Filtering", false, false},
	{"Frames Per Second", DefFps},
	{"Updates Per Second", DefUps},
	{"ShowMenu", true},
	{"Uncapped Updates Per Second", false},
	{"Sound Channels", DefSoundChannels},
	{"HybridSleep", false},
	{"Prefer 3DPB Game Data", false},
	{"Integer Scaling", true, true},
	{"Sound Volume", DefVolume},
	{"Music Volume", DefVolume},
	{"Stereo Sound Effects", false},
	{"Debug Overlay", false},
	{"Debug Overlay Grid", true},
	{"Debug Overlay All Edges", true},
	{"Debug Overlay Ball Position", true},
	{"Debug Overlay Ball Edges", true},
	{"Debug Overlay Collision Mask", true},
	{"Debug Overlay Sprites", true},
	{"Debug Overlay Sounds", true},
	{"Debug Overlay Ball Depth Grid", true},
	{"Debug Overlay AABB", true},
	{"FontFileName", ""},
	{"Language", translations::GetCurrentLanguage()->ShortName},
	{"Hide Cursor", false},
	{"Backdrop", true, true},
	{"Stacked Layout", true, true},
	{"Post Processing", 0},
	{"Gamepad Rumble", true},
	{"Match Display Refresh Rate", false},
	{"Upscaled Art", false, true},
	{"Upscale Factor", 2, 2},
	{"Key Bindings Version", KeyBindingsScancodes},
	// Settings without it, from before editions existed, also start in Definitive
	{"Edition", static_cast<int>(Editions::Definitive)},
	{"Edition Hint Shown", false},
	{"Game Data Folder", ""},
	{"Lighting", true},
	{"Lamp Glow", true},
	{"Shaded Ball", true},
	{"Reduced Motion", false},
	{"Screen Rotation", 0},
	{"Text Size", 100},
	{"High Contrast HUD", false},
	{"Colour Vision", 0},
	{"Room Reverb", false},
	{"Wide Stereo", false},
	{"Sound Pack", ""},
	{"Record Sessions", false},
	{"Reduced Motion Chosen", false},
};
// clang-format on

namespace
{
	// The keyboard defaults of Options.Key before Key Bindings Version, as keycodes, in the same order
	constexpr SDL_Keycode KeycodeDefaults[] = {
		SDLK_Z,  SDLK_SLASH, SDLK_SPACE, SDLK_X,  SDLK_PERIOD, SDLK_UP,     SDLK_F2, SDLK_F3,
		SDLK_F4, SDLK_F5,    SDLK_F6,    SDLK_F8, SDLK_F9,     SDLK_ESCAPE, SDLK_F7, SDLK_TAB,
	};
	static_assert(std::size(KeycodeDefaults) == static_cast<size_t>(~GameBindings::Max));

	void ShowEditionOptions(Editions edition)
	{
		for (const auto opt : options::AllOptions)
			opt->ShowEdition(edition);
	}
} // namespace

void options::InitPrimary()
{
	auto imContext = ImGui::GetCurrentContext();
	ImGuiSettingsHandler ini_handler;
	ini_handler.TypeName = "Pinball";
	ini_handler.TypeHash = ImHashStr(ini_handler.TypeName);
	ini_handler.ReadOpenFn = MyUserData_ReadOpen;
	ini_handler.ReadLineFn = MyUserData_ReadLine;
	ini_handler.WriteAllFn = MyUserData_WriteAll;
	imContext->SettingsHandlers.push_back(ini_handler);

	// Settings are loaded from disk on the first frame
	if (!imContext->SettingsLoaded)
	{
		ImGui::LoadIniSettingsFromDisk(imContext->IO.IniFilename);
		imContext->SettingsLoaded = true;
	}

	// Settings saved before Key Bindings Version existed hold keyboard bindings as keycodes
	MigrateKeycodes = !HasSetting(Options.KeyBindingsVersion.Name);
	auto& toggleEdition = Options.Key[~GameBindings::ToggleEdition];
	auto newToggleEdition = !HasSetting(std::string{toggleEdition.Name} + " 0 input");
	for (const auto opt : AllOptions)
		opt->Load();
#ifdef __EMSCRIPTEN__
	// Until the player sets it, Reduced Motion follows the browser's, which follows the system's, at every start
	if (!Options.ReducedMotionChosen)
		Options.ReducedMotion =
			EM_ASM_INT({ return matchMedia('(prefers-reduced-motion: reduce)').matches ? 1 : 0; }) != 0;
#endif
	if (newToggleEdition)
	{
		// Settings from before the switch existed may already use its default key; then it starts unbound
		const auto& tab = toggleEdition.Defaults[0];
		for (const auto& control : Options.Key)
		{
			if (&control != &toggleEdition &&
				std::find(std::begin(control.Inputs), std::end(control.Inputs), tab) != std::end(control.Inputs))
				toggleEdition.Inputs[0] = {};
		}
	}
	if (MigrateKeycodes)
	{
		// Write the scancodes now, as ImGui may save the new version before options::Save runs
		MigrateKeycodes = false;
		for (const auto& control : Options.Key)
			control.Save();
	}
	PostProcessOptions();
}

void options::InitSecondary()
{
	// The table is known now
	Options.Resolution.Load();
	auto maxRes = fullscrn::GetMaxResolution();
	if (Options.Resolution >= 0 && Options.Resolution > maxRes)
		Options.Resolution = maxRes;
	fullscrn::SetResolution(Options.Resolution == -1 ? maxRes : Options.Resolution);
}

void options::uninit()
{
	Save();
}

void options::Save()
{
	Options.Language.V = translations::GetCurrentLanguage()->ShortName;
	for (const auto opt : AllOptions)
	{
		opt->Save();
	}
}


int options::get_int(LPCSTR lpValueName, int defaultValue)
{
	auto value = GetSetting(lpValueName, std::to_string(defaultValue));
	return std::stoi(value);
}

void options::set_int(LPCSTR lpValueName, int data)
{
	SetSetting(lpValueName, std::to_string(data));
}

float options::get_float(LPCSTR lpValueName, float defaultValue)
{
	auto value = GetSetting(lpValueName, std::to_string(defaultValue));
	return std::stof(value);
}

void options::set_float(LPCSTR lpValueName, float data)
{
	SetSetting(lpValueName, std::to_string(data));
}

void options::GetInput(const std::string& rowName, GameInput (&values)[3])
{
	for (auto i = 0u; i <= 2; i++)
	{
		auto name = rowName + " " + std::to_string(i);
		auto inputType = static_cast<InputTypes>(get_int((name + " type").c_str(), -1));
		auto input = get_int((name + " input").c_str(), -1);
		if (inputType <= InputTypes::GameController && input != -1)
			values[i] = {inputType, input};
	}
}

void options::SetInput(const std::string& rowName, GameInput (&values)[3])
{
	for (auto i = 0u; i <= 2; i++)
	{
		auto input = values[i];
		auto name = rowName + " " + std::to_string(i);
		set_int((name + " type").c_str(), static_cast<int>(input.Type));
		set_int((name + " input").c_str(), input.Value);
	}
}

void options::toggle(Menu1 uIDCheckItem)
{
	switch (uIDCheckItem)
	{
	case Menu1::Sounds:
		Options.Sounds ^= true;
		Sound::Enable(Options.Sounds);
		return;
	case Menu1::SoundStereo:
		Options.SoundStereo ^= true;
		return;
	case Menu1::Music:
		Options.Music ^= true;
		if (!Options.Music)
			midi::music_stop();
		else
			midi::music_play();
		return;
	case Menu1::Show_Menu:
		Options.ShowMenu ^= true;
		fullscrn::window_size_changed();
		return;
	case Menu1::Full_Screen:
		Options.FullScreen ^= true;
		fullscrn::set_screen_mode(Options.FullScreen);
		return;
	case Menu1::OnePlayer:
	case Menu1::TwoPlayers:
	case Menu1::ThreePlayers:
	case Menu1::FourPlayers:
		Options.Players = static_cast<int>(uIDCheckItem) - static_cast<int>(Menu1::OnePlayer) + 1;
		break;
	case Menu1::MaximumResolution:
	case Menu1::R640x480:
	case Menu1::R800x600:
	case Menu1::R1024x768:
		{
			auto restart = false;
			int newResolution = static_cast<int>(uIDCheckItem) - static_cast<int>(Menu1::R640x480);
			if (uIDCheckItem == Menu1::MaximumResolution)
			{
				restart = fullscrn::GetResolution() != fullscrn::GetMaxResolution();
				Options.Resolution = -1;
			}
			else if (newResolution <= fullscrn::GetMaxResolution())
			{
				restart = newResolution != (Options.Resolution == -1
					                            ? fullscrn::GetMaxResolution()
					                            : fullscrn::GetResolution());
				Options.Resolution = newResolution;
			}

			if (restart)
				winmain::Restart();
			break;
		}
	case Menu1::WindowUniformScale:
		Options.UniformScaling ^= true;
		fullscrn::window_size_changed();
		break;
	case Menu1::WindowLinearFilter:
		Options.LinearFiltering ^= true;
		render::recreate_screen_texture();
		break;
	case Menu1::Prefer3DPBGameData:
		Options.Prefer3DPBGameData ^= true;
		winmain::Restart();
		break;
	case Menu1::WindowIntegerScale:
		Options.IntegerScaling ^= true;
		fullscrn::window_size_changed();
		break;
	case Menu1::WindowBackdrop:
		Options.Backdrop ^= true;
		break;
	case Menu1::WindowUpscaledArt:
		Options.UpscaledArt ^= true;
		render::SetUpscale(Options.UpscaledArt ? Options.UpscaleFactor.V : 0);
		break;
	case Menu1::WindowStackedLayout:
		Options.StackedLayout ^= true;
		fullscrn::window_size_changed();
		break;
	default:
		break;
	}
}

void options::InputDown(GameInput input)
{
	if (ControlWaitingForInput)
	{
		*ControlWaitingForInput = input;
		ControlWaitingForInput = nullptr;
	}
}

void options::ShowControlDialog()
{
	if (!ShowDialog)
	{
		ControlWaitingForInput = nullptr;
		ShowDialog = true;
		// Save previous controls in KVP storage.
		for (const auto& control : Options.Key)
		{
			control.Save();
		}
	}
}

void options::RenderControlDialog()
{
	if (!ShowDialog)
		return;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2{550, 550});
	if (ImGui::Begin(pb::get_rc_string(Msg::KEYMAPPER_Caption), &ShowDialog))
	{
		if (ImGui::TreeNode(pb::get_rc_string(Msg::KEYMAPPER_Groupbox2)))
		{
			ImGui::TextWrapped("%s", pb::get_rc_string(Msg::KEYMAPPER_Help1));
			ImGui::TextWrapped("%s", pb::get_rc_string(Msg::KEYMAPPER_Help2));
			ImGui::TreePop();
		}

		ImGui::Spacing();
		ImGui::TextUnformatted(pb::get_rc_string(Msg::KEYMAPPER_Groupbox1));

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{5, 10});
		if (ImGui::BeginTable("Controls", 4,
		                      ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders |
		                      ImGuiTableFlags_SizingStretchSame))
		{
			ImGui::TableSetupColumn("Control");
			ImGui::TableSetupColumn("Binding 1");
			ImGui::TableSetupColumn("Binding 2");
			ImGui::TableSetupColumn("Binding 3");
			ImGui::TableHeadersRow();

			int rowHash = 0;
			for (auto& option : Options.Key)
			{
				ImGui::TableNextColumn();
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.5, 0, 0, 1});
				if (ImGui::Button(pb::get_rc_string(option.Description), ImVec2(-1, 0)))
				{
					for (auto& input : option.Inputs)
						input = {};
				}
				ImGui::PopStyleColor(1);

				for (auto& input : option.Inputs)
				{
					ImGui::TableNextColumn();
					if (ControlWaitingForInput == &input)
					{
						if (ImGui::Button("Press the key", ImVec2(-1, 0)))
						{
							ControlWaitingForInput = &input;
						}
					}
					else
					{
						auto inputDescription = input.GetFullInputDescription();
						if (ImGui::Button((inputDescription + "##" + std::to_string(rowHash++)).c_str(),
						                  ImVec2(-1, 0)))
						{
							ControlWaitingForInput = &input;
						}
					}
				}
			}
			ImGui::EndTable();
		}
		ImGui::PopStyleVar();
		ImGui::Spacing();

		if (ImGui::Button(pb::get_rc_string(Msg::GenericOk)))
		{
			ShowDialog = false;
		}

		ImGui::SameLine();
		if (ImGui::Button(pb::get_rc_string(Msg::GenericCancel)))
		{
			for (auto& control : Options.Key)
			{
				control.Load();
			}
			ShowDialog = false;
		}

		ImGui::SameLine();
		if (ImGui::Button(pb::get_rc_string(Msg::KEYMAPPER_Default)))
		{
			for (auto& control : Options.Key)
			{
				control.Reset();
			}
			ControlWaitingForInput = nullptr;
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();

	if (!ShowDialog)
		ControlWaitingForInput = nullptr;
}

std::vector<GameBindings> options::MapGameInput(GameInput key)
{
	std::vector<GameBindings> result;
	for (auto inputId = GameBindings::Min; inputId < GameBindings::Max; inputId++)
	{
		for (auto& inputValue : Options.Key[~inputId].Inputs)
		{
			if (key == inputValue)
			{
				result.push_back(inputId);
				break;
			}
		}
	}
	return result;
}

void options::ResetAllOptions()
{
	for (const auto opt : AllOptions)
		opt->Reset();
	PostProcessOptions();
}

Editions options::GetEdition()
{
	return static_cast<Editions>(Options.Edition.V);
}

void options::SetEdition(Editions edition)
{
	if (edition == GetEdition())
		return;

	auto linearFiltering = Options.LinearFiltering.V;
	SelectEdition(edition);
	// Its state from before was left behind while Classic was on
	DefinitiveEdition::Reset();

	// What toggling each drawing option that differs would do
	render::SetUpscale(Options.UpscaledArt ? Options.UpscaleFactor.V : 0);
	if (Options.LinearFiltering != linearFiltering)
		render::recreate_screen_texture();
	fullscrn::window_size_changed();
}

void options::SelectEdition(Editions edition)
{
	Options.Edition = static_cast<int>(edition);
	ShowEditionOptions(edition);
}

void options::MyUserData_ReadLine(ImGuiContext* ctx, ImGuiSettingsHandler* handler, void* entry, const char* line)
{
	auto& keyValueStore = *static_cast<std::unordered_map<std::string, std::string>*>(entry);
	std::string keyValue = line;
	auto separatorPos = keyValue.find('=');
	if (separatorPos != std::string::npos)
	{
		auto key = keyValue.substr(0, separatorPos);
		auto value = keyValue.substr(separatorPos + 1, keyValue.size());
		keyValueStore[key] = value;
	}
}

void* options::MyUserData_ReadOpen(ImGuiContext* ctx, ImGuiSettingsHandler* handler, const char* name)
{
	// There is only one custom entry
	return strcmp(name, "Settings") == 0 ? &settings : nullptr;
}

void options::MyUserData_WriteAll(ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
{
	buf->appendf("[%s][%s]\n", handler->TypeName, "Settings");
	for (const auto& setting : settings)
	{
		buf->appendf("%s=%s\n", setting.first.c_str(), setting.second.c_str());
	}
	buf->append("\n");
}

void options::PostProcessOptions()
{
	ImGui::GetStyle().FontScaleMain = Options.UIScale;
	Options.FramesPerSecond = Clamp(Options.FramesPerSecond.V, MinFps, MaxFps);
	Options.UpdatesPerSecond = Clamp(Options.UpdatesPerSecond.V, MinUps, MaxUps);
	Options.SoundChannels = Clamp(Options.SoundChannels.V, MinSoundChannels, MaxSoundChannels);
	Options.SoundVolume = Clamp(Options.SoundVolume.V, MinVolume, MaxVolume);
	Options.PostProcessing = Clamp(Options.PostProcessing.V, 0, static_cast<int>(PostEffects::Max) - 1);
	Options.MusicVolume = Clamp(Options.MusicVolume.V, MinVolume, MaxVolume);
	Options.KeyBindingsVersion = KeyBindingsScancodes;
	Options.Edition = Clamp(Options.Edition.V, 0, static_cast<int>(Editions::Max) - 1);
	Options.Rotation = Clamp(Options.Rotation.V, 0, 3);
	// 100, 125 or 150 percent, rounding down
	if (Options.TextSize.V >= 150)
		Options.TextSize = 150;
	else if (Options.TextSize.V >= 125)
		Options.TextSize = 125;
	else
		Options.TextSize = 100;
	Options.ColourVision = Clamp(Options.ColourVision.V, 0, 1);
	// A sound pack is a folder name in the sounds folder, never a path out of it
	auto& pack = Options.SoundPack.V;
	if (pack.find_first_of("/\\") != std::string::npos || pack == "." || pack == "..")
		pack.clear();
	// Also a folder typed into the settings file by hand
	Options.GameDataFolder.V = FolderPath(Options.GameDataFolder.V);
	// The drawing options of each edition are clamped while that edition's are on show
	for (auto edition : {Editions::Classic, Editions::Definitive})
	{
		ShowEditionOptions(edition);
		Options.UpscaleFactor = Options.UpscaleFactor.V >= 4 ? 4 : 2;
	}
	ShowEditionOptions(GetEdition());
	translations::SetCurrentLanguage(Options.Language.V.c_str());
	winmain::UpdateFrameRate();
}

std::string options::FolderPath(std::string folder)
{
#ifdef _WIN32
	auto separated = !folder.empty() && (folder.back() == '/' || folder.back() == '\\');
#else
	auto separated = !folder.empty() && folder.back() == '/';
#endif
	if (!folder.empty() && !separated)
		folder += '/';
	return folder;
}

std::string GameInput::GetFullInputDescription() const
{
	std::string prefix;
	switch (Type)
	{
	case InputTypes::Keyboard:
		prefix = "Keyboard\n";
		break;
	case InputTypes::Mouse:
		prefix = "Mouse\n";
		break;
	case InputTypes::GameController:
		prefix = "Controller\n";
		break;
	case InputTypes::None:
	default:
		return "Unused";
	}

	return prefix + GetShortInputDescription();
}

std::string GameInput::GetShortInputDescription() const
{
	static LPCSTR mouseButtons[]
	{
		nullptr,
		"Left",
		"Middle",
		"Right",
		"X1",
		"X2",
	};

	static LPCSTR controllerButtons[] =
	{
		"A",
		"B",
		"X",
		"Y",
		"Back",
		"Guide",
		"Start",
		"LeftStick",
		"RightStick",
		"LeftShoulder",
		"RightShoulder",
		"DpUp",
		"DpDown",
		"DpLeft",
		"DpRight",
		"Misc1",
		"Paddle1",
		"Paddle2",
		"Paddle3",
		"Paddle4",
		"Touchpad",
	};

	std::string keyName;
	switch (Type)
	{
	case InputTypes::Keyboard:
		{
			// Name the key by what it types in the current layout
			auto scancode = static_cast<SDL_Scancode>(Value);
			keyName = SDL_GetKeyName(SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false));
			if (keyName.empty())
				keyName = SDL_GetScancodeName(scancode);
			break;
		}
	case InputTypes::Mouse:
		if (Value >= SDL_BUTTON_LEFT && Value <= SDL_BUTTON_X2)
			keyName = mouseButtons[Value];
		else
			keyName = "MButton" + std::to_string(Value);
		break;
	case InputTypes::GameController:
		if (Value >= SDL_GAMEPAD_BUTTON_SOUTH && Value < std::min(static_cast<int>(SDL_GAMEPAD_BUTTON_COUNT), 21))
			keyName = controllerButtons[Value];
		else if (Value == GamepadLeftTrigger)
			keyName = "LeftTrigger";
		else if (Value == GamepadRightTrigger)
			keyName = "RightTrigger";
		else
			keyName = "CButton" + std::to_string(Value);
		break;
	case InputTypes::None:
	default:
		break;
	}

	return keyName;
}

const std::string& options::GetSetting(const std::string& key, const std::string& defaultValue)
{
	auto setting = settings.find(key);
	if (setting == settings.end())
	{
		settings[key] = defaultValue;
		if (ImGui::GetCurrentContext())
			ImGui::MarkIniSettingsDirty();
		return defaultValue;
	}
	return setting->second;
}

bool options::HasSetting(const std::string& key)
{
	return settings.contains(key);
}

void options::RemoveSetting(const std::string& key)
{
	if (settings.erase(key) && ImGui::GetCurrentContext())
		ImGui::MarkIniSettingsDirty();
}

const char* options::TableKeyPrefix()
{
	if (pb::FullTiltDemoMode)
		return FullTiltDemoKeyPrefix;
	if (pb::FullTiltMode)
		return FullTiltKeyPrefix;
	return SpaceCadetKeyPrefix;
}

std::string options::TableKeyPrefixToRead(const std::string& key)
{
	// Before tables had their own keys, every table saved under the unprefixed ones.
	// A table that never saved its own value starts from that one; saving then writes it under its prefix.
	std::string prefix = TableKeyPrefix();
	if (!prefix.empty() && !HasSetting(prefix + key) && HasSetting(key))
		prefix.clear();
	return prefix;
}

void options::SetSetting(const std::string& key, const std::string& value)
{
	settings[key] = value;
	if (ImGui::GetCurrentContext())
		ImGui::MarkIniSettingsDirty();
}

void ControlOption::Load()
{
	for (auto i = 0u; i <= 2; i++)
	{
		auto name = std::string{Name} + " " + std::to_string(i);
		auto saved = options::HasSetting(name + " input");
		Inputs[i].Type =
			static_cast<InputTypes>(options::get_int((name + " type").c_str(), static_cast<int>(Defaults[i].Type)));
		Inputs[i].Value = options::get_int((name + " input").c_str(), Defaults[i].Value);

		if (saved && options::MigrateKeycodes && Inputs[i].Type == InputTypes::Keyboard)
		{
			// Old settings hold every binding, defaults included; an old default becomes the new one
			auto slot = 0u;
			while (&options::Options.Key[slot] != this)
				slot++;
			if (Defaults[i].Type == InputTypes::Keyboard && Inputs[i].Value == static_cast<int>(KeycodeDefaults[slot]))
			{
				Inputs[i] = Defaults[i];
				continue;
			}

			// The key that types this keycode in the current layout
			auto scancode = SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(Inputs[i].Value), nullptr);
			if (scancode != SDL_SCANCODE_UNKNOWN)
				Inputs[i].Value = scancode;
			else
				Inputs[i] = {};
		}
	}
}

OptionBase::OptionBase(LPCSTR name): Name(name)
{
	options::AllOptions.push_back(this);
}

OptionBase::~OptionBase()
{
	auto& vec = options::AllOptions;
	auto position = std::find(vec.begin(), vec.end(), this);
	if (position != vec.end())
		vec.erase(position);
}

void TableIntOption::Load()
{
	auto key = options::TableKeyPrefixToRead(Name) + Name;
	V = options::HasSetting(key) ? options::get_int(key.c_str(), DefaultValue) : DefaultValue;
}

void TableIntOption::Save() const
{
	options::set_int((options::TableKeyPrefix() + std::string{Name}).c_str(), V);
}

void TableIntOption::Reset()
{
	V = DefaultValue;
	for (const auto prefix : options::TableKeyPrefixes)
		options::RemoveSetting(prefix + std::string{Name});
}

std::string ControlOption::GetShortcutDescription() const
{
	std::string result;
	for (const auto& input : Inputs)
	{
		if (input.Type != InputTypes::None)
		{
			result = input.GetShortInputDescription();
			break;
		}
	}
	return result;
}
