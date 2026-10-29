#include "pch.h"
#include "winmain.h"

// The game has its own main function
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>

// The window fills the browser's viewport
static void FitWindowToPage(int width, int height)
{
	SDL_SetWindowSize(winmain::MainWindow, width, height);
}

static bool PageResized(int eventType, const EmscriptenUiEvent* event, void* userData)
{
	FitWindowToPage(event->windowInnerWidth, event->windowInnerHeight);
	return false;
}
#endif

#include "control.h"
#include "DefinitiveEdition.h"
#include "DefinitiveStats.h"
#include "RecordingViewer.h"
#include "DefinitiveFrontEnd.h"
#include "DefinitiveText.h"
#include "DefinitiveTheme.h"
#include "DefinitiveUi.h"
#include "EmbeddedData.h"
#include "fullscrn.h"
#include "midi.h"
#include "options.h"
#include "pb.h"
#include "PostEffect.h"
#include "Compositor.h"
#include "Upscale.h"
#include "render.h"
#include "replay.h"
#include "Sound.h"
#include "TableView.h"
#include "translations.h"
#include "font_selection.h"
#include "high_score.h"

#include <filesystem>
#include <mutex>
#include <new>
#include <optional>

constexpr const char* winmain::Version;

SDL_Window* winmain::MainWindow = nullptr;
SDL_Renderer* winmain::Renderer = nullptr;
ImGuiIO* winmain::ImIO = nullptr;

int winmain::return_value = 0;
bool winmain::bQuit = false;
bool winmain::activated = false;
bool winmain::DispFrameRate = false;
bool winmain::DispGRhistory = false;
bool winmain::single_step = false;
bool winmain::has_focus = true;
int winmain::last_mouse_x;
int winmain::last_mouse_y;
int winmain::mouse_down;
bool winmain::no_time_loss = false;

bool winmain::restart = false;

#ifdef __EMSCRIPTEN__
// True while a game runs and its settings may be saved
static bool GameRunning = false;

// The page calls this before it copies the preference folder to IndexedDB. The browser never lets MainLoop
// return, so the options, high scores and ImGui's .ini would otherwise only reach the file at a shutdown that
// doesn't happen.
extern "C" EMSCRIPTEN_KEEPALIVE void SaveSettings()
{
	if (!GameRunning || !winmain::ImIO || !winmain::ImIO->IniFilename)
		return;
	options::Save();
	high_score::write();
	ImGui::SaveIniSettingsToDisk(winmain::ImIO->IniFilename);
}
#endif

std::vector<float> winmain::gfrDisplay{};
unsigned winmain::gfrOffset = 0;
float winmain::gfrWindow = 5.0f;
bool winmain::ShowAboutDialog = false;
bool winmain::ShowImGuiDemo = false;
bool winmain::ShowSpriteViewer = false;
bool winmain::ShowExitPopup = false;
bool winmain::LaunchBallEnabled = true;
bool winmain::HighScoresEnabled = true;
bool winmain::DemoActive = false;
int winmain::MainMenuHeight = 0;
std::string winmain::FpsDetails, winmain::PrevSdlError;
unsigned winmain::PrevSdlErrorCount = 0;
double winmain::UpdateToFrameRatio;
winmain::DurationMs winmain::TargetFrameTime;
optionsStruct& winmain::Options = options::Options;
winmain::DurationMs winmain::SpinThreshold = DurationMs(0.005);
WelfordState winmain::SleepState{};
int winmain::CursorIdleCounter = 0;
std::unique_ptr<ImGuiStyle> winmain::BaseStyle{};
std::unique_ptr<ImGuiStyle> winmain::DefinitiveStyle{};
Editions winmain::StyledEdition = Editions::Classic;
bool winmain::DefinitiveFontsAdded = false;
bool winmain::SoftwareRenderer = false;
bool winmain::HighRefresh = false;
winmain::DurationMs winmain::UpdateTime;
winmain::DurationMs winmain::UpdateTimeAccumulator;
int winmain::CurrentFps = 0;
bool winmain::RefreshRateUnknown = false;
std::map<SDL_JoystickID, std::set<int>> winmain::GamepadHeld{};
std::map<std::pair<SDL_TouchID, SDL_FingerID>, GameBindings> winmain::TouchBindings{};
Uint64 winmain::EditionHintEnd = 0;

// Splits the command line at spaces and tabs; double quotes keep an argument with spaces together
static std::vector<std::string> SplitArgs(LPCSTR cmdLine)
{
	std::vector<std::string> args;
	std::string arg;
	bool quoted = false, inArg = false;
	for (auto ch = cmdLine; *ch; ch++)
	{
		if (*ch == '"')
		{
			quoted = !quoted;
			inArg = true;
		}
		else if ((*ch == ' ' || *ch == '\t') && !quoted)
		{
			if (inArg)
				args.push_back(arg);
			arg.clear();
			inArg = false;
		}
		else
		{
			arg += *ch;
			inArg = true;
		}
	}
	if (inArg)
		args.push_back(arg);
	return args;
}

// Matches whole arguments, so a flag inside a file name doesn't count
static bool HasArg(const std::vector<std::string>& args, LPCSTR flag)
{
	return std::find(args.begin(), args.end(), flag) != args.end();
}

// Returns the argument after flag, or an empty string
static std::string GetArgValue(const std::vector<std::string>& args, LPCSTR flag)
{
	auto arg = std::find(args.begin(), args.end(), flag);
	return arg != args.end() && ++arg != args.end() ? *arg : std::string();
}

// The folder chosen in the system's folder dialog, which may answer from another thread
static std::mutex ChosenFolderMutex;
static std::string ChosenFolder, FolderDialogError;
// Whether the last folder chosen had no game data, so the saved one stayed; only the main thread uses it
static bool ChosenFolderEmpty = false;

#ifndef __EMSCRIPTEN__
static void SDLCALL FolderChosen(void* userData, const char* const* files, int filter)
{
	std::lock_guard lock{ChosenFolderMutex};
	if (!files)
	{
		// Such as on a Linux desktop with neither zenity nor a portal
		FolderDialogError = SDL_GetError();
		printf("Could not show the folder dialog: %s\n", FolderDialogError.c_str());
		return;
	}
	if (files[0])
		ChosenFolder = files[0];
}
#endif

// Whether a folder was chosen since the last call that has game data; it becomes the Game Data Folder. A folder
// without data leaves the saved one as it was and sets ChosenFolderEmpty.
// The Screen Rotation chosen in Settings, until the next frame takes it; -1 for none
static int PendingRotation = -1;

static bool TakeChosenFolder()
{
	std::string folder;
	{
		std::lock_guard lock{ChosenFolderMutex};
		folder = std::move(ChosenFolder);
		ChosenFolder.clear();
	}
	if (folder.empty())
		return false;
	folder = options::FolderPath(std::move(folder));
	// While -table asks for a table, only a folder with that table's data has it
	ChosenFolderEmpty =
		std::ranges::none_of(pb::FindTables({folder.c_str()}), [](const TableData& data)
							 { return !pb::RequestedTable || pb::SameTable(data.Table, *pb::RequestedTable); });
	if (ChosenFolderEmpty)
	{
		printf("No game data in the folder chosen: %s\n", folder.c_str());
		return false;
	}
	options::Options.GameDataFolder.V = std::move(folder);
	return true;
}

// The folder the table was loaded from, for Settings
static std::string DataFolder()
{
	if (pb::BasePath.empty())
		return std::string{pb::get_rc_string(Msg::Definitive_DataStartFolder)};
	// A table in Full Tilt!'s layout can be found from the start folder, such as ../DRAGON/
	std::error_code error;
	auto path = std::filesystem::absolute(std::u8string{pb::BasePath.begin(), pb::BasePath.end()}, error);
	if (error)
		return pb::BasePath;
	auto text = path.lexically_normal().u8string();
	return std::string{text.begin(), text.end()};
}

// What Space Cadet 2026's screens can ask for, through the entry points the Classic menus use
static DefinitiveHost MakeDefinitiveHost()
{
	DefinitiveHost host{};
	host.Play = [] { winmain::new_game(); };
	host.StartDemo = []
	{
		if (!winmain::DemoActive)
			pb::toggle_demo();
	};
#ifndef __EMSCRIPTEN__
	// A browser tab closes the game itself
	host.Quit = []
	{
		SDL_Event event{SDL_EVENT_QUIT};
		SDL_PushEvent(&event);
	};
#endif
	host.Records = []
	{
		std::vector<DefinitiveHost::Record> records;
		for (auto& entry : high_score::highscore_table)
			records.push_back({entry.Name, entry.Score});
		return records;
	};
	host.ClearRecords = [] { high_score::clear_table(); };
	host.Busy = [] { return !pb::GameEnded || ImGui::GetTopMostPopupModal() != nullptr; };
	host.NextRecord = [](DefinitiveHost::Record& record, int& place)
	{
		high_score_entry entry{};
		if (!high_score::TakeNextEntry(entry))
			return false;
		record = {entry.Entry.Name, entry.Entry.Score};
		place = entry.Position;
		return true;
	};
	static_assert(sizeof high_score_struct::Name == DefinitiveHost::Record::MaxNameBytes + 1);
	host.SaveRecord = [](const DefinitiveHost::Record& record, int place)
	{
		high_score_entry entry{{}, place};
		snprintf(entry.Entry.Name, sizeof entry.Entry.Name, "%s", record.Name.c_str());
		entry.Entry.Score = record.Score;
		high_score::place_new_score_into(entry);
	};
	host.Pause = [] { winmain::pause(false); };
	host.Resume = winmain::end_pause;
	host.Paused = [] { return winmain::single_step; };
	host.Version = winmain::Version;
	host.SetPostProcessing = [](int effect) { winmain::SetPostProcessing(static_cast<PostEffects>(effect)); };
	host.PostProcessingUnavailable = winmain::PostProcessingUnavailable;
	host.EffectsUnavailable = winmain::EffectsUnavailable;
	host.SetUpscaleFactor = [](int factor)
	{
		options::Options.UpscaleFactor = factor;
		if (options::Options.UpscaledArt)
			render::SetUpscale(factor);
	};
	host.SetSoundVolume = [](int volume)
	{
		options::Options.SoundVolume = volume;
		Sound::SetVolume(volume);
	};
	host.SetSoundChannels = [](int channels)
	{
		options::Options.SoundChannels = channels;
		Sound::SetChannels(channels);
	};
	host.SetMusicVolume = [](int volume)
	{
		options::Options.MusicVolume = volume;
		midi::SetVolume(volume);
	};
	host.FrameRateChanged = winmain::UpdateFrameRate;
	host.PreciseSleepChanged = winmain::ResetPreciseSleep;
	host.SetLanguage = [](const char* shortName)
	{
		translations::SetCurrentLanguage(shortName);
		winmain::Restart();
	};
	host.MaxResolution = fullscrn::GetMaxResolution;
	host.ChangeFont = font_selection::ShowDialog;
	host.EditionKey = [] { return options::Options.Key[~GameBindings::ToggleEdition].GetShortcutDescription(); };
#ifndef __EMSCRIPTEN__
	// The browser has its own way to give the game its files
	host.ChooseDataFolder = []
	{
		{
			std::lock_guard lock{ChosenFolderMutex};
			FolderDialogError.clear();
		}
		ChosenFolderEmpty = false;
		auto& folder = options::Options.GameDataFolder.V;
		SDL_ShowOpenFolderDialog(FolderChosen, nullptr, winmain::MainWindow, folder.empty() ? nullptr : folder.c_str(),
								 false);
	};
#endif
	host.FolderDialogError = []
	{
		std::lock_guard lock{ChosenFolderMutex};
		return FolderDialogError;
	};
	host.ChosenFolderEmpty = [] { return ChosenFolderEmpty; };
	host.DataFolder = DataFolder;
	// Taken at the start of the next frame, so ImGui lays out that whole frame for the new turn
	host.SetRotation = [](int quarterTurns) { PendingRotation = quarterTurns; };
#ifndef __EMSCRIPTEN__
	host.SoundPacks = [] { return Sound::Packs(); };
	host.SetSoundPack = [](const std::string& name)
	{
		options::Options.SoundPack.V = name;
		Sound::SetPack(name);
	};
#endif
#ifndef __EMSCRIPTEN__
	host.Recordings = []
	{
		std::vector<DefinitiveHost::Recording> recordings;
		for (const auto& recording : RecordingViewer::List())
			recordings.push_back({recording.Name, recording.Path, recording.Summary.Length,
								  static_cast<int>(recording.Summary.Games.size()), recording.Playable});
		return recordings;
	};
	host.DeleteRecording = [](const std::string& path) { RecordingViewer::Delete(path); };
	host.ViewRecording = [](const std::string& path) { RecordingViewer::Request(path); };
#endif
	host.Viewing = [] { return replay::IsViewing(); };
	host.ViewerState = []
	{
		const auto& summary = replay::Viewed();
		auto frame = replay::Frame();
		auto frames = static_cast<unsigned>(summary.FrameStarts.size());
		std::vector<float> games;
		games.reserve(summary.Games.size());
		for (auto game : summary.Games)
			games.push_back(game < frames ? summary.FrameStarts[game] : summary.Length);
		return DefinitiveHost::Viewer{frame,
									  frames,
									  frame < frames ? summary.FrameStarts[frame] : summary.Length,
									  summary.Length,
									  games,
									  RecordingViewer::Paused(),
									  RecordingViewer::Ended(),
									  replay::PlaybackFailed(),
									  RecordingViewer::Speed()};
	};
	host.ViewerSeek = [](unsigned frame) { RecordingViewer::Seek(frame); };
	host.ViewerFrameAt = [](float time)
	{
		const auto& starts = replay::Viewed().FrameStarts;
		return static_cast<unsigned>(std::upper_bound(starts.begin(), starts.end(), time) - starts.begin()) - 1;
	};
	host.ViewerPause = [](bool paused) { RecordingViewer::SetPaused(paused); };
	host.ViewerSpeed = [](float speed) { RecordingViewer::SetSpeed(speed); };
	host.StopViewing = [] { RecordingViewer::Stop(); };
	host.Layout = [](SDL_FRect& board, SDL_FRect& panel, bool& band)
	{
		// The layout is in pixels, ImGui in window coordinates
		auto fbScale = ImGui::GetIO().DisplayFramebufferScale;
		auto convert = [&](const SDL_Rect& rect)
		{
			return SDL_FRect{static_cast<float>(rect.x) / fbScale.x, static_cast<float>(rect.y) / fbScale.y,
							 static_cast<float>(rect.w) / fbScale.x, static_cast<float>(rect.h) / fbScale.y};
		};
		board = convert(render::BoardRect);
		panel = convert(render::SidebarRect);
		band = fullscrn::Portrait;
	};
	return host;
}

int winmain::WinMain(LPCSTR lpCmdLine)
{
	std::set_new_handler(memalloc_failure);

	printf("Game version: %s\n", Version);
	printf("Command line: %s\n", lpCmdLine);
	printf("Compiled with: SDL %d.%d.%d;", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
	printf(" SDL_mixer %d.%d.%d;", SDL_MIXER_MAJOR_VERSION, SDL_MIXER_MINOR_VERSION, SDL_MIXER_MICRO_VERSION);
	printf(" ImGui %s\n", IMGUI_VERSION);

	const auto args = SplitArgs(lpCmdLine);
	auto recordPath = GetArgValue(args, "-record");
	auto replayPath = GetArgValue(args, "-replay");
	// Playback runs Classic unless told otherwise, so its result doesn't depend on the saved settings
	auto editionArg = GetArgValue(args, "-edition");
	auto edition = replayPath.empty() ? std::optional<Editions>{} : Editions::Classic;
	if (editionArg == "classic")
		edition = Editions::Classic;
	else if (editionArg == "definitive")
		edition = Editions::Definitive;
	else if (!editionArg.empty() || HasArg(args, "-edition"))
	{
		auto message = (editionArg.empty() || editionArg[0] == '-' ? std::string{"-edition needs a value"}
																   : "Unknown edition: " + editionArg) +
					   "; use -edition classic or -edition definitive";
		// Windows builds have no console, so play shows a message box; headless playback only prints
		if (replayPath.empty())
			pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, "Could not start", message.c_str());
		else
			printf("%s\n", message.c_str());
		return 1;
	}
	// A test run of a bonus table, whichever table the player chose; Skulduggery isn't loadable yet (Phase F6)
	auto tableArg = GetArgValue(args, "-table");
	if (tableArg == "dragon")
		pb::RequestedTable = Tables::DragonsKeep;
	else if (!tableArg.empty() || HasArg(args, "-table"))
	{
		auto message = (tableArg.empty() || tableArg[0] == '-' ? std::string{"-table needs a value"}
															   : "Unknown table: " + tableArg) +
					   "; use -table dragon";
		if (replayPath.empty())
			pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, "Could not start", message.c_str());
		else
			printf("%s\n", message.c_str());
		return 1;
	}
	if (!replayPath.empty())
	{
		if (!replay::StartPlayback(replayPath))
			return 1;
		// A recording plays back on its own table, whichever the player chose, and a bonus table's without -table
		const auto& recordedDat = replay::RecordedDatFile();
		pb::RequestedTable = pb::TableOf(recordedDat.substr(0, recordedDat.find(' ')));
		// Playback is headless; the SDL_VIDEODRIVER environment variable still takes precedence
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
	}

	// SDL init
	SDL_SetMainReady();
	if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD))
	{
		pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, "Could not initialize SDL3", SDL_GetError());
		return 1;
	}

	pb::quickFlag = HasArg(args, "-quick");

	// SDL window
	SDL_Window* window = SDL_CreateWindow(pb::get_rc_string(Msg::STRING139), 800, 556,
										  SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	MainWindow = window;
	if (!window)
	{
		pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, "Could not create window", SDL_GetError());
		return 1;
	}

	// The GPU renderer when it can be had, for post-processing and Space Cadet 2026, so neither needs a restart.
	// Created once: options can't change it, and once it has failed it isn't tried again.
	SoftwareRenderer = HasArg(args, "-sw") || replay::IsPlaying();
	if (!(!SoftwareRenderer && CreateRenderer(true)) && !CreateRenderer(false))
	{
		pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, "Could not create renderer", SDL_GetError());
		return 1;
	}

	// Owned, so the early returns below free it too; SDL owns basePath
	const std::unique_ptr<char, decltype(&SDL_free)> prefPathOwner{SDL_GetPrefPath("", "SpaceCadetPinball"), &SDL_free};
	auto prefPath = prefPathOwner.get();
	auto basePath = SDL_GetBasePath();

	// SDL mixer init
	MIX_Mixer* mixer = nullptr;
	bool noAudio = HasArg(args, "-noaudio") || replay::IsPlaying();
	if (!noAudio)
	{
		if (!MIX_Init())
		{
			printf("Could not initialize SDL_mixer, continuing without audio.\nSDL Error: %s\n", SDL_GetError());
			SDL_ClearError();
			noAudio = true;
		}
		else if (!(mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr)))
		{
			printf("Could not open audio device, continuing without audio.\nSDL Error: %s\n", SDL_GetError());
			SDL_ClearError();
		}
	}
	auto mixOpened = mixer != nullptr;

	{
		// Load SDL Game Controller definitions from DB
		auto controllerDb = EmbeddedData::DecompressStb(EmbeddedData::SDL_GameControllerDB_compressed_data,
		                                                 EmbeddedData::SDL_GameControllerDB_compressed_size);
		auto io = SDL_IOFromConstMem(controllerDb.data(), controllerDb.size());
		const auto added = SDL_AddGamepadMappingsFromIO(io, true);
		if (added < 0)
		{
			printf("Could not load game controller DB.\nSDL Error: %s\n", SDL_GetError());
			SDL_ClearError();
		}
	}

	auto resetAllOptions = HasArg(args, "-reset");
#ifndef __EMSCRIPTEN__
	auto firstOpen = true;
#endif
	do
	{
		restart = false;
		// Space Cadet 2026's Recordings play a recording in the window, from a fresh start of the table
		RecordingViewer::Begin();

		// ImGui init
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		ImIO = &io;
		auto iniPath = std::string(prefPath) + "imgui_pb.ini";
		io.IniFilename = iniPath.c_str();

		// First option initialization step: just load settings from .ini. Needs ImGui context.
		options::InitPrimary();
		// Playback may override options that affect play, so it must not save them
		if (replay::IsPlaying())
			io.IniFilename = nullptr;
		if (resetAllOptions)
		{
			resetAllOptions = false;
			options::ResetAllOptions();
		}
		// Only the first run; a restart keeps the edition the player switched to
		if (edition)
		{
			options::SelectEdition(*edition);
			edition.reset();
		}
		// tools/check-replays.sh checks that playback ran the edition it asked for
		if (replay::IsPlaying())
			printf("Edition: %s\n", options::GetEdition() == Editions::Definitive ? "definitive" : "classic");

		// Post-processing needs SDL's GPU renderer, the only one that takes custom shaders; without it the table is
		// drawn plain
		PostEffect::Init(Renderer);
		PostEffect::SetEffect(static_cast<PostEffects>(Options.PostProcessing.V));
		Compositor::Init(Renderer);

		if (!Options.FontFileName.V.empty())
		{
			ImFontConfig fontConfig{};

			// ToDo: further tweak font options, maybe try imgui_freetype
			fontConfig.OversampleV = 2;
			fontConfig.OversampleH = 4;

			// ToDo: improve font file test, checking if file exists is not enough
			auto fontLoaded = false;
			auto fileName = Options.FontFileName.V.c_str();
			auto fileHandle = fopenu(fileName, "rb");
			if (fileHandle)
			{
				fclose(fileHandle);

				// ImGui loads glyphs as text needs them, so no glyph ranges are passed
				if (io.Fonts->AddFontFromFileTTF(fileName, 13.f, &fontConfig))
					fontLoaded = true;
			}

			if (!fontLoaded)
				printf("Failed to load font: %s, using embedded font.\n", fileName);
		}
		ImGui_ImplSDLRenderer3_Init(Renderer);
		ImGui::StyleColorsDark();
		BaseStyle = std::make_unique<ImGuiStyle>(ImGui::GetStyle());
		DefinitiveStyle = std::make_unique<ImGuiStyle>(*BaseStyle);
		DefinitiveTheme::ApplyStyle(*DefinitiveStyle);
		DefinitiveFontsAdded = false;
		ApplyDisplayScale();

		ImGui_ImplSDL3_InitForSDLRenderer(window, Renderer);
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
		DefinitiveFrontEnd::SetHost(MakeDefinitiveHost());

		// Data search order: WD, executable path, user pref path, platform specific paths.
		std::vector<const char*> searchPaths
		{
			{
				"",
				basePath,
				prefPath
			}
		};
		// The folder the player chose comes first
		if (!Options.GameDataFolder.V.empty())
			searchPaths.insert(searchPaths.begin(), Options.GameDataFolder.V.c_str());
		searchPaths.insert(searchPaths.end(), std::begin(PlatformDataPaths), std::end(PlatformDataPaths));
		pb::SelectDatFile(searchPaths);
		SDL_SetWindowTitle(window, pb::get_rc_string(pb::Caption()));

		// Second step: run updates that depend on .DAT file selection
		options::InitSecondary();

		if (replay::IsViewing() && !replay::CheckDatFile())
		{
			replay::Stop();
			RecordingViewer::Forget();
			// A normal start, which saves as one does
			io.IniFilename = iniPath.c_str();
		}
		if (replay::IsPlaying())
		{
			if (!replay::CheckDatFile())
				return 1;
			fullscrn::SetResolution(replay::Resolution());
		}
		else if (!recordPath.empty())
		{
			// Only the first run that loads the table records; a restart from the options ends the recording
			if (!replay::StartRecording(recordPath))
				return 1;
		}
		else
			RecordingViewer::StartSessionRecording();
		if (replay::IsRecording() || replay::IsPlaying())
			srand(replay::Seed());

		Sound::Init(mixer, Options.SoundChannels, Options.Sounds, Options.SoundVolume);
#ifndef __EMSCRIPTEN__
		Sound::SetPack(Options.SoundPack.V);
#endif
		if (!mixOpened)
			Options.Sounds = false;

		// Keep Options.Music as saved: playback is a no-op without a loaded track,
		// and clearing it here would persist Music=0 after a -noaudio or -quick run.
		midi::music_init(mixer, Options.MusicVolume);

		if (pb::init())
		{
#ifndef __EMSCRIPTEN__
			// Space Cadet 2026 asks for the folder instead of ending with an error. Not pb::uninit: it would write
			// over the saved high scores with a table that was never read.
			if (options::GetEdition() == Editions::Definitive && !replay::IsPlaying())
			{
				// The recording has no table to name; the run with the folder chosen writes it again from the start
				RecordingViewer::EndSessionRecording();
				auto chosen = FindGameData(searchPaths);
				options::uninit();
				midi::music_shutdown();
				Sound::Close();
				ImGui_ImplSDLRenderer3_Shutdown();
				ImGui_ImplSDL3_Shutdown();
				ImGui::DestroyContext();
				if (!chosen)
					break;
				restart = true;
				continue;
			}
#endif
			std::string message = "The .dat file is missing.\n"
				"Make sure that the game data is present in any of the following locations:\n";
			for (auto path : searchPaths)
			{
				if (path)
				{
					message = message + (path[0] ? path : "working directory") + "\n";
				}
			}
			pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, "Could not load game data", message.c_str());
			return 1;
		}
		recordPath.clear();

		fullscrn::init();
		render::SetUpscale(Options.UpscaledArt ? Options.UpscaleFactor.V : 0);

		pb::reset_table();
		pb::firsttime_setup();

		if (HasArg(args, "-fullscreen"))
		{
			Options.FullScreen = true;
		}

#ifndef __EMSCRIPTEN__
		// Also in full screen, where it sets the size the window returns to.
		// Only the first open centres it, so a restart keeps where the player put the window.
		// Not into or out of a recording, which restarts the game only to load the table afresh
		if (!RecordingViewer::TakeRestarted())
			fullscrn::ResetWindowSize(firstOpen);
		firstOpen = false;
#endif
		if (replay::IsPlaying() && !replay::IsViewing())
		{
			// The recording holds the command that started the game
			while (replay::PlayStep())
			{
			}
			if (replay::PlaybackFailed())
				return_value = 1;
		}
		else
		{
			SDL_ShowWindow(window);
#ifdef __EMSCRIPTEN__
			FitWindowToPage(EM_ASM_INT({ return window.innerWidth; }), EM_ASM_INT({ return window.innerHeight; }));
			emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, PageResized);
#endif
			fullscrn::set_screen_mode(Options.FullScreen);

			// Space Cadet 2026 opens on its title, with the demo behind it. Not toggle_demo: after a restart its flag
			// still says the demo of the run before is on. A recording being viewed starts the way it was recorded.
			if (replay::IsViewing())
			{
				RecordingViewer::Prepare();
				DefinitiveFrontEnd::ShowViewer();
			}
			else if (options::GetEdition() == Editions::Definitive)
			{
				pb::replay_level(true);
				DefinitiveFrontEnd::ShowTitle();
			}
			else if (HasArg(args, "-demo"))
				pb::toggle_demo();
			else
				pb::replay_level(false);

#ifdef __EMSCRIPTEN__
			GameRunning = true;
			MainLoop();
			GameRunning = false;
#else
			MainLoop();
#endif
		}
		auto played = replay::IsPlaying();
		// A recording's games end in its own high scores, which the player's table doesn't take
		if (replay::IsViewing())
			high_score::ClearQueue();
		RecordingViewer::EndSessionRecording();

		if (!played)
			options::uninit();
		midi::music_shutdown();
		Sound::Close();
		pb::uninit();
		DefinitiveEdition::Reset();
		DefinitiveStats::Reset();

		ImGui_ImplSDLRenderer3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
	}
	while (restart);

	if (!noAudio)
	{
		if (mixer)
			MIX_DestroyMixer(mixer);
		MIX_Quit();
	}

	Compositor::Uninit();
	PostEffect::Uninit();
	fullscrn::ReleaseFrame();
	SDL_DestroyRenderer(Renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return return_value;
}

void winmain::MainLoop()
{
	bQuit = false;
	unsigned updateCounter = 0, frameCounter = 0;
	auto frameStart = Clock::now();
	double UpdateToFrameCounter = 0;
	DurationMs sleepRemainder(0), frameDuration(TargetFrameTime);
	auto prevTime = frameStart;

	while (true)
	{
		// A game data folder chosen in Settings: the game restarts and loads from it
		if (TakeChosenFolder())
			Restart();

		if (DispFrameRate)
		{
			auto curTime = Clock::now();
			if (curTime - prevTime > DurationMs(1000))
			{
				char buf[60];
				auto elapsedSec = DurationMs(curTime - prevTime).count() * 0.001;
				snprintf(buf, sizeof buf, "Updates/sec = %02.02f Frames/sec = %02.02f ",
				         updateCounter / elapsedSec, frameCounter / elapsedSec);
				SDL_SetWindowTitle(MainWindow, buf);
				FpsDetails = buf;
				frameCounter = updateCounter = 0;
				prevTime = curTime;
			}
		}

		if (!ProcessWindowMessages() || bQuit)
			break;

		if (has_focus)
		{
			if (mouse_down)
			{
				float mouseX, mouseY;
				int w, h;
				SDL_GetMouseState(&mouseX, &mouseY);
				auto x = static_cast<int>(mouseX), y = static_cast<int>(mouseY);
				SDL_GetWindowSize(MainWindow, &w, &h);
				float dx = static_cast<float>(last_mouse_x - x) / static_cast<float>(w);
				float dy = static_cast<float>(y - last_mouse_y) / static_cast<float>(h);
				pb::ballset(dx, dy);

				// Original creates continuous mouse movement with mouse capture.
				// Alternative solution: mouse warp at window edges.
				int xMod = 0, yMod = 0;
				if (x == 0 || x >= w - 1)
					xMod = w - 2;
				if (y == 0 || y >= h - 1)
					yMod = h - 2;
				if (xMod != 0 || yMod != 0)
				{
					// Mouse warp does not work over remote desktop or in some VMs
					x = abs(x - xMod);
					y = abs(y - yMod);
					SDL_WarpMouseInWindow(MainWindow, static_cast<float>(x), static_cast<float>(y));
				}

				last_mouse_x = x;
				last_mouse_y = y;
			}
			auto ballAlpha = 1.0f;
			// A recording plays at its own pace, and nothing else moves the table
			if (replay::IsViewing())
				RecordingViewer::Advance(static_cast<double>(frameDuration.count()));
			else if (!single_step && !no_time_loss)
			{
				// Normally one update per loop. With more frames than updates per second, the loop runs at the frame
				// rate and the table still advances in steps of 1000/UPS ms, so the physics see the same time steps.
				auto updates = 1;
				auto dt = static_cast<float>(frameDuration.count());
				if (HighRefresh)
				{
					dt = static_cast<float>(UpdateTime.count());
					UpdateTimeAccumulator += frameDuration;
					updates = static_cast<int>(UpdateTimeAccumulator / UpdateTime);
					// At most 2: the accumulator was below UpdateTime and frameDuration is capped at 2 * UpdateTime
					UpdateTimeAccumulator -= UpdateTime * updates;
					ballAlpha = static_cast<float>(UpdateTimeAccumulator / UpdateTime);
				}
				for (auto update = 0; update < updates; update++)
				{
					TableFrame(dt);
					if (DispGRhistory)
					{
						auto targetSize =
							static_cast<unsigned>(static_cast<float>(Options.UpdatesPerSecond) * gfrWindow);
						if (gfrDisplay.size() != targetSize)
						{
							gfrDisplay.resize(targetSize, static_cast<float>(UpdateTime.count()));
							gfrOffset = 0;
						}
						gfrDisplay[gfrOffset] = dt;
						gfrOffset = (gfrOffset + 1) % gfrDisplay.size();
					}
					updateCounter++;
				}
			}
			else
				UpdateTimeAccumulator = DurationMs::zero();
			no_time_loss = false;

			if (UpdateToFrameCounter >= UpdateToFrameRatio)
			{
				if (Options.HideCursor && CursorIdleCounter <= 0)
					ImGui::SetMouseCursor(ImGuiMouseCursor_None);
				if (PendingRotation >= 0)
				{
					Options.Rotation = PendingRotation;
					PendingRotation = -1;
					fullscrn::window_size_changed();
				}
				ImGui_ImplSDLRenderer3_NewFrame();
				ImGui_ImplSDL3_NewFrame();
				// ImGui lays out the picture, which a quarter turn makes taller than the window or wider. The backend
				// gives it the pointer's position unturned while the pointer is outside the window, so it is put
				// outside the picture instead.
				if (fullscrn::Rotation())
				{
					if (fullscrn::Rotation() % 2)
						std::swap(ImIO->DisplaySize.x, ImIO->DisplaySize.y);
					if (!SDL_GetMouseFocus())
						ImIO->AddMousePosEvent(-FLT_MAX, -FLT_MAX);
				}
				ApplyEditionLook();
				ImGui::NewFrame();
				RenderUi();

				fullscrn::BeginFrame();
				SDL_RenderClear(Renderer);
				// Alternative clear hack, clear might fail on some systems
				// Todo: remove original clear, if save for all platforms
				SDL_RenderFillRect(Renderer, nullptr);
				if (HighRefresh && ballAlpha < 1.0f)
					render::InterpolateBalls(ballAlpha);
				render::PresentVScreen(HighRefresh ? ballAlpha : 1.0f);
				render::RestoreBalls();

				ImGui::Render();
				// ImGui draws in window coordinates and leaves scaling them to pixels to the render scale;
				// the table above is drawn in pixels, so the scale applies to the UI only
				auto fbScale = ImIO->DisplayFramebufferScale;
				SDL_SetRenderScale(Renderer, fbScale.x, fbScale.y);
				ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), Renderer);
				SDL_SetRenderScale(Renderer, 1.0f, 1.0f);
				fullscrn::EndFrame();

				SDL_RenderPresent(Renderer);
				frameCounter++;
				UpdateToFrameCounter -= UpdateToFrameRatio;
			}

			auto sdlError = SDL_GetError();
			if (sdlError[0] || !PrevSdlError.empty())
			{
				if (sdlError[0])
					SDL_ClearError();

				// Rate limit duplicate SDL error messages.
				if (sdlError != PrevSdlError)
				{
					PrevSdlError = sdlError;
					if (PrevSdlErrorCount > 0)
					{
						printf("SDL Error: ^ Previous Error Repeated %u Times\n", PrevSdlErrorCount + 1);
						PrevSdlErrorCount = 0;
					}

					if (sdlError[0])
						printf("SDL Error: %s\n", sdlError);
				}
				else
				{
					PrevSdlErrorCount++;
				}
			}

			auto updateEnd = Clock::now();
			auto targetTimeDelta = TargetFrameTime - DurationMs(updateEnd - frameStart) - sleepRemainder;

			TimePoint frameEnd;
#ifdef __EMSCRIPTEN__
			// The browser draws the canvas and delivers events only while the loop yields, so it yields every time
			if (targetTimeDelta > DurationMs::zero() && !Options.UncappedUpdatesPerSecond)
				emscripten_sleep(static_cast<unsigned>(targetTimeDelta.count()));
			else
				emscripten_sleep(0);
			frameEnd = Clock::now();
#else
			if (targetTimeDelta > DurationMs::zero() && !Options.UncappedUpdatesPerSecond)
			{
				if (Options.HybridSleep)
					HybridSleep(targetTimeDelta);
				else
					std::this_thread::sleep_for(targetTimeDelta);
				frameEnd = Clock::now();
			}
			else
			{
				frameEnd = updateEnd;
			}
#endif

			// Limit duration to 2 * target time
			sleepRemainder = Clamp(DurationMs(frameEnd - updateEnd) - targetTimeDelta, -TargetFrameTime,
			                       TargetFrameTime);
			frameDuration = std::min<DurationMs>(DurationMs(frameEnd - frameStart),
												 2 * (HighRefresh ? UpdateTime : TargetFrameTime));
			frameStart = frameEnd;
			UpdateToFrameCounter++;

			CursorIdleCounter = std::max(CursorIdleCounter - static_cast<int>(frameDuration.count()), 0);
		}
	}

	if (PrevSdlErrorCount > 0)
	{
		printf("SDL Error: ^ Previous Error Repeated %u Times\n", PrevSdlErrorCount);
	}
}

void winmain::TableFrame(float dtMilliSec)
{
	if (!replay::IsPlaying())
		pb::SetUiHoldsInput(ImGui::GetTopMostPopupModal() != nullptr ||
							(options::GetEdition() == Editions::Definitive && DefinitiveFrontEnd::ScreenOpen()));
	pb::frame(dtMilliSec);
	// Stats count the game in either edition, so switching during a game loses nothing
	DefinitiveStats::Update(TableView::Get());
	if (options::GetEdition() == Editions::Definitive)
		DefinitiveEdition::Update(TableView::Get());
}

void winmain::ApplyEditionLook()
{
	// ImGui picks its default font in the first NewFrame, from the display scale the backend has just set, and only
	// if the atlas is empty; picking it here the same way keeps Classic's font as it was before Space Cadet 2026 had
	// fonts of its own
	if (!DefinitiveFontsAdded)
	{
		if (ImIO->Fonts->Sources.empty())
			ImIO->Fonts->AddFontDefault();
		DefinitiveTheme::AddFonts(ImIO->Fonts, Options.FontFileName.V);
		DefinitiveFontsAdded = true;
	}

	auto edition = options::GetEdition();
	if (edition != StyledEdition)
		ApplyDisplayScale();
	ImIO->FontDefault = edition == Editions::Definitive ? DefinitiveTheme::TextFont() : nullptr;
}

void winmain::RenderUi()
{
	// Space Cadet 2026's screens take the menu bar's place
	auto definitive = options::GetEdition() == Editions::Definitive;
	// And its sound, which Classic never has
	Sound::SetEffects(definitive && Options.RoomReverb, definitive && Options.WideStereo && Options.SoundStereo,
					  definitive);
	if (definitive)
		DefinitiveFrontEnd::Render(TableView::Get());

	// Transparent menu bar with a button for preventing menu lockout.
	ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4{});
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{});
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	if (!definitive && !Options.ShowMenu && ImGui::BeginMainMenuBar())
	{
		if (ImGui::MenuItem("Menu"))
		{
			options::toggle(Menu1::Show_Menu);
			ImGui::FocusWindow(nullptr);
		}
		ImGui::EndMainMenuBar();
	}
	ImGui::PopStyleVar(1);
	ImGui::PopStyleColor(2);

	// No demo window in release to save space
#ifndef NDEBUG
	if (ShowImGuiDemo)
		ImGui::ShowDemoWindow(&ShowImGuiDemo);
#endif

	if (MenuBarShown() && ImGui::BeginMainMenuBar())
	{
		int currentMenuHeight = static_cast<int>(ImGui::GetWindowSize().y);
		if (MainMenuHeight != currentMenuHeight)
		{
			// Get the height of the main menu bar and update screen coordinates
			MainMenuHeight = currentMenuHeight;
			fullscrn::window_size_changed();
		}

		if (ImGui::BeginMenu(pb::get_rc_string(Msg::Menu1_Game)))
		{
			ImGuiMenuItemWShortcut(GameBindings::NewGame);
			if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_Launch_Ball), nullptr, false, LaunchBallEnabled))
			{
				end_pause();
				pb::launch_ball();
			}
			ImGuiMenuItemWShortcut(GameBindings::TogglePause);
			ImGui::Separator();

			if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_High_Scores), nullptr, false, HighScoresEnabled))
			{
				pause(false);
				pb::high_scores();
			}
			if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_Demo), nullptr, DemoActive))
			{
				end_pause();
				pb::toggle_demo();
			}
#ifndef __EMSCRIPTEN__
			// A browser tab closes the game itself
			ImGuiMenuItemWShortcut(GameBindings::Exit);
#endif
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(pb::get_rc_string(Msg::Menu1_Options)))
		{
			ImGuiMenuItemWShortcut(GameBindings::ToggleEdition, options::GetEdition() == Editions::Definitive);
			ImGui::Separator();
			ImGuiMenuItemWShortcut(GameBindings::ToggleMenuDisplay, Options.ShowMenu);
			ImGuiMenuItemWShortcut(GameBindings::ToggleFullScreen, Options.FullScreen);
			if (ImGui::BeginMenu(pb::get_rc_string(Msg::Menu1_Select_Players)))
			{
				if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_1Player), nullptr, Options.Players == 1))
				{
					options::toggle(Menu1::OnePlayer);
					new_game();
				}
				if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_2Players), nullptr, Options.Players == 2))
				{
					options::toggle(Menu1::TwoPlayers);
					new_game();
				}
				if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_3Players), nullptr, Options.Players == 3))
				{
					options::toggle(Menu1::ThreePlayers);
					new_game();
				}
				if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_4Players), nullptr, Options.Players == 4))
				{
					options::toggle(Menu1::FourPlayers);
					new_game();
				}
				ImGui::EndMenu();
			}
			ImGuiMenuItemWShortcut(GameBindings::ShowControlDialog);
			if (ImGui::MenuItem("Gamepad Rumble", nullptr, Options.Rumble))
				Options.Rumble ^= true;
			if (ImGui::BeginMenu("Language"))
			{
				auto currentLanguage = translations::GetCurrentLanguage();
				for (auto& item : translations::Languages)
				{
					if (ImGui::MenuItem(item.DisplayName, nullptr, currentLanguage->Language == item.Language))
					{
						if (currentLanguage->Language != item.Language)
						{
							translations::SetCurrentLanguage(item.ShortName);
							Restart();
						}
					}
				}
				ImGui::EndMenu();
			}
			ImGui::Separator();

			if (ImGui::BeginMenu("Audio"))
			{
				ImGuiMenuItemWShortcut(GameBindings::ToggleSounds, Options.Sounds);
				if (ImGui::MenuItem("Stereo Sound Effects", nullptr, Options.SoundStereo))
				{
					options::toggle(Menu1::SoundStereo);
				}
				ImGui::TextUnformatted("Sound Volume");
				if (ImGui::SliderInt("##Sound Volume", &Options.SoundVolume.V, options::MinVolume, options::MaxVolume,
				                     "%d",
				                     ImGuiSliderFlags_AlwaysClamp))
				{
					Sound::SetVolume(Options.SoundVolume);
				}
				ImGui::TextUnformatted("Sound Channels");
				if (ImGui::SliderInt("##Sound Channels", &Options.SoundChannels.V, options::MinSoundChannels,
				                     options::MaxSoundChannels, "%d", ImGuiSliderFlags_AlwaysClamp))
				{
					Sound::SetChannels(Options.SoundChannels);
				}
				ImGui::Separator();

				ImGuiMenuItemWShortcut(GameBindings::ToggleMusic, Options.Music);
				ImGui::TextUnformatted("Music Volume");
				if (ImGui::SliderInt("##Music Volume", &Options.MusicVolume.V, options::MinVolume, options::MaxVolume,
				                     "%d",
				                     ImGuiSliderFlags_AlwaysClamp))
				{
					midi::SetVolume(Options.MusicVolume);
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Graphics"))
			{
				if (ImGui::MenuItem(pb::get_rc_string(Msg::Menu1_WindowUniformScale), nullptr, Options.UniformScaling))
				{
					options::toggle(Menu1::WindowUniformScale);
				}
				if (ImGui::MenuItem("Linear Filtering", nullptr, Options.LinearFiltering))
				{
					options::toggle(Menu1::WindowLinearFilter);
				}
				if (ImGui::MenuItem("Integer Scaling", nullptr, Options.IntegerScaling))
				{
					options::toggle(Menu1::WindowIntegerScale);
				}
				if (ImGui::MenuItem("Backdrop", nullptr, Options.Backdrop))
				{
					options::toggle(Menu1::WindowBackdrop);
				}
				if (ImGui::MenuItem("Panel Under Table in Tall Windows", nullptr, Options.StackedLayout))
				{
					options::toggle(Menu1::WindowStackedLayout);
				}
				ImGuiMenuItemWShortcut(GameBindings::ToggleUpscaledArt, Options.UpscaledArt);
				if (ImGui::BeginMenu("Upscaled Art Size"))
				{
					for (auto factor : {2, 4})
					{
						char label[8];
						snprintf(label, sizeof label, "%dx", factor);
						if (ImGui::MenuItem(label, nullptr, Options.UpscaleFactor == factor))
						{
							Options.UpscaleFactor = factor;
							if (Options.UpscaledArt)
								render::SetUpscale(factor);
						}
					}
#if SDL_VERSION_ATLEAST(3, 4, 0)
					if (ImGui::MenuItem("Export Original Sprites"))
					{
						auto count = Upscale::ExportOriginals();
						auto folder = Upscale::OriginalsFolder();
						auto message = count ? "Exported " + std::to_string(count) + " sprites to " + folder
											 : "Could not export the sprites to " + folder;
						SDL_ShowSimpleMessageBox(count ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_WARNING,
												 "Export Original Sprites", message.c_str(), MainWindow);
					}
#else
					ImGui::MenuItem("Export Original Sprites", nullptr, false, false);
					ImGui::TextDisabled("Needs SDL 3.4 or newer");
#endif
					ImGui::EndMenu();
				}
#if SDL_VERSION_ATLEAST(3, 4, 0)
				if (ImGui::BeginMenu("Post-Processing"))
				{
					constexpr const char* names[] = {"None", "CRT", "Smooth Edges"};
					for (int i = 0; i < static_cast<int>(PostEffects::Max); i++)
					{
						if (ImGui::MenuItem(names[i], nullptr, Options.PostProcessing == i))
							SetPostProcessing(static_cast<PostEffects>(i));
					}
					if (SoftwareRenderer)
						ImGui::TextDisabled("Not with the software renderer");
					else if (PostEffect::GpuRendererFailed())
						ImGui::TextDisabled("Needs Vulkan, Direct3D 12 or Metal");
					ImGui::EndMenu();
				}
#endif
				if (ImGui::DragFloat("UI Scale", &Options.UIScale.V, 0.005f, 0.8f, 5,
				                     "%.2f", ImGuiSliderFlags_AlwaysClamp))
				{
					ImGui::GetStyle().FontScaleMain = Options.UIScale;
				}
				ImGui::Separator();

				char buffer[80]{};
				auto changed = false;
				if (ImGui::MenuItem("Set Default UPS/FPS"))
				{
					changed = true;
					Options.UpdatesPerSecond = options::DefUps;
					Options.FramesPerSecond = options::DefFps;
				}
				if (ImGui::SliderInt("UPS", &Options.UpdatesPerSecond.V, options::MinUps, options::MaxUps, "%d",
				                     ImGuiSliderFlags_AlwaysClamp))
				{
					changed = true;
				}
				ImGui::BeginDisabled(Options.MatchRefreshRate);
				if (ImGui::SliderInt("FPS", &Options.FramesPerSecond.V, options::MinFps, options::MaxFps, "%d",
				                     ImGuiSliderFlags_AlwaysClamp))
				{
					changed = true;
				}
				ImGui::EndDisabled();
				if (ImGui::MenuItem("Match Display Refresh Rate", nullptr, Options.MatchRefreshRate))
				{
					Options.MatchRefreshRate ^= true;
					changed = true;
				}
				// The frame rate in use: the display's, the slider's if the display's is unknown, or above UPS
				if (Options.MatchRefreshRate && RefreshRateUnknown)
					ImGui::TextDisabled("Refresh rate unknown, using %d FPS", CurrentFps);
				else if (HighRefresh)
					ImGui::TextDisabled("%d FPS, the ball drawn between updates", CurrentFps);
				else if (Options.MatchRefreshRate)
					ImGui::TextDisabled("%d FPS", CurrentFps);
				// With FPS above UPS, the updates keep their pace and only the frames are uncapped
				if (HighRefresh)
					snprintf(buffer, sizeof buffer - 1, "Uncapped FPS");
				else
					snprintf(buffer, sizeof buffer - 1, "Uncapped UPS (FPS ratio %02.02f)", UpdateToFrameRatio);
				if (ImGui::MenuItem(buffer, nullptr, Options.UncappedUpdatesPerSecond))
				{
					Options.UncappedUpdatesPerSecond ^= true;
				}
				if (ImGui::MenuItem("Precise Sleep", nullptr, Options.HybridSleep))
				{
					Options.HybridSleep ^= true;
					SleepState = WelfordState{};
					SpinThreshold = DurationMs::zero();
				}

				if (changed)
				{
					UpdateFrameRate();
				}
				ImGui::Separator();

				if (ImGui::MenuItem("Hide Cursor", nullptr, Options.HideCursor))
				{
					Options.HideCursor ^= true;
				}
				if (ImGui::MenuItem("Change Font..."))
				{
					font_selection::ShowDialog();
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu(pb::get_rc_string(Msg::Menu1_Table_Resolution)))
			{
				char buffer[20]{};
				auto resolutionStringId = Msg::Menu1_UseMaxResolution_640x480;

				switch (fullscrn::GetMaxResolution())
				{
				case 0: resolutionStringId = Msg::Menu1_UseMaxResolution_640x480;
					break;
				case 1: resolutionStringId = Msg::Menu1_UseMaxResolution_800x600;
					break;
				case 2: resolutionStringId = Msg::Menu1_UseMaxResolution_1024x768;
					break;
				}

				auto maxResText = pb::get_rc_string(resolutionStringId);
				if (ImGui::MenuItem(maxResText, nullptr, Options.Resolution == -1))
				{
					options::toggle(Menu1::MaximumResolution);
				}
				for (auto i = 0; i <= fullscrn::GetMaxResolution(); i++)
				{
					auto& res = fullscrn::resolution_array[i];
					snprintf(buffer, sizeof buffer - 1, "%d x %d", res.ScreenWidth, res.ScreenHeight);
					if (ImGui::MenuItem(buffer, nullptr, Options.Resolution == i))
					{
						options::toggle(static_cast<Menu1>(static_cast<int>(Menu1::R640x480) + i));
					}
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Game Data"))
			{
				if (ImGui::MenuItem("Prefer 3DPB Data", nullptr, Options.Prefer3DPBGameData))
				{
					options::toggle(Menu1::Prefer3DPBGameData);
				}
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Reset All Options"))
			{
				options::ResetAllOptions();
				Restart();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(pb::get_rc_string(Msg::Menu1_Help)))
		{
#ifndef NDEBUG
			if (ImGui::MenuItem("ImGui Demo", nullptr, ShowImGuiDemo))
			{
				ShowImGuiDemo ^= true;
			}
#endif
			if (ImGui::MenuItem("Sprite Viewer", nullptr, ShowSpriteViewer))
			{
				if (!ShowSpriteViewer)
					pause(false);
				ShowSpriteViewer ^= true;
			}
			if (pb::cheat_mode && ImGui::MenuItem("Frame Times", nullptr, DispGRhistory))
			{
				DispGRhistory ^= true;
			}
			if (ImGui::MenuItem("Debug Overlay", nullptr, Options.DebugOverlay))
			{
				Options.DebugOverlay ^= true;
			}
			if (Options.DebugOverlay && ImGui::BeginMenu("Overlay Options"))
			{
				if (ImGui::MenuItem("Box Grid", nullptr, Options.DebugOverlayGrid))
					Options.DebugOverlayGrid ^= true;
				if (ImGui::MenuItem("Ball Depth Grid", nullptr, Options.DebugOverlayBallDepthGrid))
					Options.DebugOverlayBallDepthGrid ^= true;
				if (ImGui::MenuItem("Sprite Positions", nullptr, Options.DebugOverlaySprites))
					Options.DebugOverlaySprites ^= true;
				if (ImGui::MenuItem("All Edges", nullptr, Options.DebugOverlayAllEdges))
					Options.DebugOverlayAllEdges ^= true;
				if (ImGui::MenuItem("Component AABB", nullptr, Options.DebugOverlayAabb))
					Options.DebugOverlayAabb ^= true;
				if (ImGui::MenuItem("Ball Position", nullptr, Options.DebugOverlayBallPosition))
					Options.DebugOverlayBallPosition ^= true;
				if (ImGui::MenuItem("Ball Box Edges", nullptr, Options.DebugOverlayBallEdges))
					Options.DebugOverlayBallEdges ^= true;
				if (ImGui::MenuItem("Sound Positions", nullptr, Options.DebugOverlaySounds))
					Options.DebugOverlaySounds ^= true;
				if (ImGui::MenuItem("Apply Collision Mask", nullptr, Options.DebugOverlayCollisionMask))
					Options.DebugOverlayCollisionMask ^= true;
				ImGui::EndMenu();
			}
			if (pb::Table == Tables::DragonsKeep && ImGui::BeginMenu("Cheats"))
			{
				// DRAGON.EXE's, which takes a Tab in "hidden test" and turns cheat mode only on
				if (ImGui::MenuItem("hidden test", nullptr, pb::cheat_mode))
					pb::PushCheat("hidden\ttest");
				if (ImGui::MenuItem("1max"))
					pb::PushCheat("1max");
				if (ImGui::MenuItem("nmax"))
					pb::PushCheat("nmax");
				if (ImGui::MenuItem("quote"))
					pb::PushCheat("quote");
				ImGui::EndMenu();
			}
			else if (pb::Table != Tables::DragonsKeep && ImGui::BeginMenu("Cheats"))
			{
				if (ImGui::MenuItem("hidden test", nullptr, pb::cheat_mode))
					pb::PushCheat("hidden test");
				if (ImGui::MenuItem("1max"))
					pb::PushCheat("1max");
				if (ImGui::MenuItem("bmax", nullptr, control::table_unlimited_balls))
					pb::PushCheat("bmax");
				if (ImGui::MenuItem("gmax"))
					pb::PushCheat("gmax");
				if (ImGui::MenuItem("rmax"))
					pb::PushCheat("rmax");
				if (pb::FullTiltMode && ImGui::MenuItem("quote"))
					pb::PushCheat("quote");
				if (ImGui::MenuItem("easy mode", nullptr, control::easyMode))
					pb::PushCheat("easy mode");

				ImGui::EndMenu();
			}
			ImGui::Separator();

			if (ImGui::MenuItem(pb::get_rc_string(pb::FullTiltMode ? Msg::FullTilt_About : Msg::Menu1_About_Pinball)))
			{
				pause(false);
				ShowAboutDialog = true;
			}
			ImGui::EndMenu();
		}
		if (DispFrameRate && !FpsDetails.empty())
			if (ImGui::BeginMenu(FpsDetails.c_str()))
				ImGui::EndMenu();
		ImGui::EndMainMenuBar();
	}

	a_dialog();
	// Space Cadet 2026 asks for high score names itself
	if (!definitive)
		high_score::RenderHighScoreDialog();
	font_selection::RenderDialog();
	if (ShowSpriteViewer)
		render::SpriteViewer(&ShowSpriteViewer);
	options::RenderControlDialog();
	if (DispGRhistory)
		RenderFrameTimeDialog();
	RenderEditionHint();

	const auto exitText = translations::GetTranslation(Msg::Menu1_Exit);
	if (ShowExitPopup)
	{
		ShowExitPopup = false;
		pause(false);
		ImGui::OpenPopup(exitText);
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	}
	if (ImGui::BeginPopupModal(exitText, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Exit the game?");
		ImGui::Separator();

		if (ImGui::Button(pb::get_rc_string(Msg::GenericOk), ImVec2(120, 0)))
		{
			SDL_Event event{SDL_EVENT_QUIT};
			SDL_PushEvent(&event);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::IsWindowAppearing())
		{
			ImGui::SetKeyboardFocusHere(0);
		}
		if (ImGui::Button(pb::get_rc_string(Msg::GenericCancel), ImVec2(120, 0)))
		{
			end_pause();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::EndPopup();
	}

	// Print game texts on the sidebar; Space Cadet 2026's score panel shows them instead
	if (!definitive)
		gdrv::grtext_draw_ttext_in_box();
}

// Whether a key or gamepad button goes to the game although Space Cadet 2026's screen has the keyboard: one bound to a
// shortcut such as the edition switch, but not to the table, which ignores input on the title anyway, and none the
// screens move or choose with, so nothing is handled twice
static bool FrontEndPassesShortcut(const SDL_Event& event)
{
	if (options::GetEdition() != Editions::Definitive)
		return false;
	GameInput input;
	switch (event.type)
	{
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		switch (event.key.key)
		{
		case SDLK_UP:
		case SDLK_DOWN:
		case SDLK_LEFT:
		case SDLK_RIGHT:
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
		case SDLK_SPACE:
			return false;
		default:
			break;
		}
		if (!DefinitiveFrontEnd::PassesShortcut(event.key.key == SDLK_ESCAPE))
			return false;
		input = {InputTypes::Keyboard, event.key.scancode};
		break;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		switch (event.gbutton.button)
		{
		case SDL_GAMEPAD_BUTTON_DPAD_UP:
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
		case SDL_GAMEPAD_BUTTON_SOUTH:
		case SDL_GAMEPAD_BUTTON_EAST:
		case SDL_GAMEPAD_BUTTON_WEST:
		case SDL_GAMEPAD_BUTTON_NORTH:
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
			return false;
		default:
			break;
		}
		if (!DefinitiveFrontEnd::PassesShortcut(false))
			return false;
		input = {InputTypes::GameController, event.gbutton.button};
		break;
	default:
		return false;
	}
	auto bindings = options::MapGameInput(input);
	return std::any_of(bindings.begin(), bindings.end(),
					   [](GameBindings binding) { return binding > GameBindings::BottomTableBump; });
}

// Whether an event cancels Space Cadet 2026's wait for a binding instead of being bound: Escape, which Settings goes
// back with, and a tap, which can't be bound. Classic's controls dialog keeps binding them.
static bool CancelsBindingWait(const SDL_Event& event)
{
	if (options::GetEdition() != Editions::Definitive || !DefinitiveFrontEnd::WaitsForBinding())
		return false;
	switch (event.type)
	{
	case SDL_EVENT_KEY_DOWN:
		return event.key.key == SDLK_ESCAPE;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		return event.button.which == SDL_TOUCH_MOUSEID;
	default:
		return false;
	}
}

int winmain::event_handler(const SDL_Event* event)
{
	// Neither ImGui nor the game sees it, so the cancel neither goes back nor taps a row
	if (CancelsBindingWait(*event))
	{
		options::WaitForInput(nullptr);
		return 1;
	}

	// Goes past ImGui to the game, as when no window has the keyboard
	auto shortcut = FrontEndPassesShortcut(*event);
	auto inputDown = false;
	switch (event->type)
	{
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		inputDown = true;
		break;
	default: break;
	}
	if ((!options::WaitingForInput() || !inputDown) && !shortcut)
	{
		// ImGui lays out the picture, which Space Cadet 2026 may turn for a monitor on its side
		auto turned = *event;
		fullscrn::TurnPointerEvent(turned);
		ImGui_ImplSDL3_ProcessEvent(&turned);
	}

	bool mouseEvent;
	switch (event->type)
	{
	case SDL_EVENT_MOUSE_MOTION:
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
	case SDL_EVENT_MOUSE_WHEEL:
		CursorIdleCounter = 1000;
		mouseEvent = true;
		break;
	default:
		mouseEvent = false;
		break;
	}

	if (ImIO->WantCaptureMouse && !options::WaitingForInput())
	{
		if (mouse_down)
		{
			mouse_down = 0;
			SDL_SetWindowMouseGrab(MainWindow, false);
		}
		if (mouseEvent)
			return 1;
	}
	if (ImIO->WantCaptureKeyboard && !options::WaitingForInput() && !shortcut)
	{
		switch (event->type)
		{
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
			return 1;
		default: ;
		}
	}

	// While a recording plays, only its own commands reach the table; Space Cadet 2026's viewer has the keys
	if (replay::IsViewing())
	{
		switch (event->type)
		{
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		case SDL_EVENT_FINGER_DOWN:
		case SDL_EVENT_FINGER_UP:
		case SDL_EVENT_FINGER_CANCELED:
			return 1;
		default:
			break;
		}
	}

	switch (event->type)
	{
	case SDL_EVENT_QUIT:
		end_pause();
		bQuit = true;
		fullscrn::shutdown();
		return_value = 0;
		return 0;
	case SDL_EVENT_KEY_UP:
		pb::KeyUp(event->key.scancode, event->key.key);
		break;
	case SDL_EVENT_KEY_DOWN:
		if (event->key.repeat)
			break;

		pb::KeyDown(event->key.scancode, event->key.key);
		if (!pb::cheat_mode)
			break;

		switch (event->key.key)
		{
		case SDLK_G:
			DispGRhistory ^= true;
			break;
		case SDLK_O:
			{
				auto plt = new ColorRgba[4 * 256];
				auto pltPtr = &plt[10]; // first 10 entries are system colors hardcoded in display_palette()
				for (int i1 = 0, i2 = 0; i1 < 256 - 10; ++i1, i2 += 8)
				{
					auto blue = static_cast<unsigned char>(i2), redGreen = static_cast<unsigned char>(i2);
					if (i2 > 255)
					{
						blue = 255;
						redGreen = static_cast<unsigned char>(i1);
					}

					*pltPtr++ = ColorRgba{blue, redGreen, redGreen, 0};
				}
				gdrv::display_palette(plt);
				delete[] plt;
			}
			break;
		case SDLK_Y:
			SDL_SetWindowTitle(MainWindow, "Pinball");
			DispFrameRate ^= true;
			break;
		case SDLK_F1:
			TableFrame(10);
			break;
		case SDLK_F10:
			pb::toggle_single_step();
			if (!single_step)
				no_time_loss = true;
			break;
		default:
			break;
		}
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		{
			// SDL also reports touches as mouse clicks; ImGui uses those, the table takes the touches themselves
			if (event->button.which == SDL_TOUCH_MOUSEID)
				break;
			bool noInput = false;
			switch (event->button.button)
			{
			case SDL_BUTTON_LEFT:
				if (pb::cheat_mode)
				{
					mouse_down = 1;
					last_mouse_x = static_cast<int>(event->button.x);
					last_mouse_y = static_cast<int>(event->button.y);
					SDL_SetWindowMouseGrab(MainWindow, true);
					noInput = true;
				}
				break;
			default:
				break;
			}

			if (!noInput)
				pb::InputDown({InputTypes::Mouse, event->button.button});
		}
		break;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		{
			if (event->button.which == SDL_TOUCH_MOUSEID)
				break;
			bool noInput = false;
			switch (event->button.button)
			{
			case SDL_BUTTON_LEFT:
				if (mouse_down)
				{
					mouse_down = 0;
					SDL_SetWindowMouseGrab(MainWindow, false);
					noInput = true;
				}
				break;
			default:
				break;
			}

			if (!noInput)
				pb::InputUp({InputTypes::Mouse, event->button.button});
		}
		break;
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
	case SDL_EVENT_WINDOW_SHOWN:
		activated = true;
		Sound::Activate();
		if (Options.Music && !single_step)
			midi::music_play();
		no_time_loss = true;
		has_focus = true;
		break;
	case SDL_EVENT_WINDOW_FOCUS_LOST:
	case SDL_EVENT_WINDOW_HIDDEN:
		activated = false;
		fullscrn::activate(0);
		Options.FullScreen = false;
		Sound::Deactivate();
		midi::music_stop();
		has_focus = false;
		if (!replay::IsViewing())
			pb::loose_focus();
		break;
	case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
	case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
		if (Options.MatchRefreshRate)
			UpdateFrameRate();
		break;
	case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
		ApplyDisplayScale();
		fullscrn::window_size_changed();
		break;
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
	case SDL_EVENT_WINDOW_RESIZED:
		fullscrn::window_size_changed();
		break;
	case SDL_EVENT_GAMEPAD_ADDED:
		SDL_OpenGamepad(event->gdevice.which);
		break;
	case SDL_EVENT_GAMEPAD_REMOVED:
		// SDL normally sends the button up and axis 0 events before the removal; this releases whatever is left
		if (auto held = GamepadHeld.find(event->gdevice.which); held != GamepadHeld.end())
		{
			for (auto value : held->second)
				pb::InputUp({InputTypes::GameController, value});
			GamepadHeld.erase(held);
		}
		if (auto gamepad = SDL_GetGamepadFromID(event->gdevice.which))
			SDL_CloseGamepad(gamepad);
		break;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		GamepadInput(event->gbutton.which, event->gbutton.button, true);
		break;
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		GamepadInput(event->gbutton.which, event->gbutton.button, false);
		break;
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		GamepadTrigger(event->gaxis);
		break;
	case SDL_EVENT_FINGER_DOWN:
	case SDL_EVENT_FINGER_UP:
	case SDL_EVENT_FINGER_CANCELED:
		TouchInput(event->tfinger);
		break;
	default: ;
	}

	return 1;
}

void winmain::GamepadTrigger(const SDL_GamepadAxisEvent& event)
{
	// A trigger is pressed past half way and released below a quarter, so it doesn't chatter in between
	if (event.axis != SDL_GAMEPAD_AXIS_LEFT_TRIGGER && event.axis != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
		return;
	auto value = event.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? GamepadLeftTrigger : GamepadRightTrigger;
	auto pressed = GamepadHeld[event.which].count(value) > 0;
	if (!pressed && event.value > SDL_JOYSTICK_AXIS_MAX / 2)
		GamepadInput(event.which, value, true);
	else if (pressed && event.value < SDL_JOYSTICK_AXIS_MAX / 4)
		GamepadInput(event.which, value, false);
}

void winmain::GamepadInput(SDL_JoystickID gamepad, int value, bool down)
{
	// Remembers what is held, so unplugging the gamepad can release it
	auto& held = GamepadHeld[gamepad];
	if (down)
	{
		held.insert(value);
		pb::InputDown({InputTypes::GameController, value});
	}
	else
	{
		held.erase(value);
		pb::InputUp({InputTypes::GameController, value});
	}
}

void winmain::TouchInput(const SDL_TouchFingerEvent& event)
{
	// The left half of the table works the left flipper, the right half the right one, and the score panel the
	// plunger, held to pull it back. Each finger keeps what it pressed until it lifts, and a binding is held while
	// any finger is on it, so it goes down with the first finger and up with the last.
	// Finger IDs are unique only per touch device
	auto key = std::pair{event.touchID, event.fingerID};
	auto held = [](GameBindings binding)
	{
		return std::any_of(TouchBindings.begin(), TouchBindings.end(),
						   [binding](const auto& finger) { return finger.second == binding; });
	};
	if (event.type != SDL_EVENT_FINGER_DOWN)
	{
		auto finger = TouchBindings.find(key);
		if (finger != TouchBindings.end())
		{
			auto binding = finger->second;
			TouchBindings.erase(finger);
			if (!held(binding))
				pb::BindingUp(binding);
		}
		return;
	}
	if (TouchBindings.count(key))
		return;
	if (ImIO->WantCaptureMouse)
		return;

	// Where the finger is in the picture, which Space Cadet 2026 may turn for a monitor on its side
	int width, height;
	fullscrn::OutputSize(width, height);
	auto position = fullscrn::ToPicture({event.x, event.y});
	SDL_Point point{static_cast<int>(position.x * static_cast<float>(width)),
					static_cast<int>(position.y * static_cast<float>(height))};
	// Only the table and the panel bind, not the menu bar or the margins: the ImGui capture flag is last frame's
	auto& board = render::BoardRect;
	GameBindings binding;
	if (SDL_PointInRect(&point, &render::SidebarRect))
		binding = GameBindings::Plunger;
	else if (SDL_PointInRect(&point, &board))
		binding = point.x < board.x + board.w / 2 ? GameBindings::LeftFlipper : GameBindings::RightFlipper;
	else
		return;

	auto first = !held(binding);
	TouchBindings[key] = binding;
	if (first)
		pb::BindingDown(binding);
}

void winmain::SetPostProcessing(PostEffects effect)
{
	// The GPU renderer switches effects on the fly; without it the option is only saved
	Options.PostProcessing = static_cast<int>(effect);
	PostEffect::SetEffect(effect);
}

void winmain::RenderEditionHint()
{
	// Once, at the first start in Space Cadet 2026, also for settings from before editions existed
	if (!Options.EditionHintShown)
	{
		Options.EditionHintShown = true;
		if (options::GetEdition() == Editions::Definitive)
			EditionHintEnd = SDL_GetTicks() + 10000;
	}
	if (!EditionHintEnd)
		return;
	if (SDL_GetTicks() >= EditionHintEnd || options::GetEdition() != Editions::Definitive)
	{
		EditionHintEnd = 0;
		return;
	}

	// Names the key, or the menu item when the switch has no key
	auto toggle = Options.Key[~GameBindings::ToggleEdition].GetShortcutDescription();
	if (toggle.empty())
		toggle =
			std::string{pb::get_rc_string(Msg::Menu1_Options)} + " > " + pb::get_rc_string(Msg::Menu1_ToggleEdition);
	char formatted[256];
	snprintf(formatted, sizeof formatted, pb::get_rc_string(Msg::EditionHint), toggle.c_str());
	auto text = DefinitiveText::Visual(formatted);

	// Centred over the top of the table and kept inside the window; the board is in pixels, ImGui in window
	// coordinates
	auto viewport = ImGui::GetMainViewport();
	auto& board = render::BoardRect;
	auto fbScale = ImIO->DisplayFramebufferScale;
	auto centre = (static_cast<float>(board.x) + static_cast<float>(board.w) / 2) / fbScale.x;
	auto top = (static_cast<float>(board.y) + static_cast<float>(board.h) / 8) / fbScale.y;
	auto padding = ImGui::GetStyle().WindowPadding;
	// A Space Cadet 2026 panel, with a lit lamp before the text
	auto lampRadius = ImGui::GetFontSize() * 0.3f;
	auto lampSpace = lampRadius * 2 + padding.x;
	auto wrapWidth = viewport->Size.x - padding.x * 4 - lampSpace;
	auto textSize = ImGui::CalcTextSize(text, nullptr, false, wrapWidth);
	// Over a Space Cadet 2026 screen, low down above its key hints, so it leaves the screen's name in view
	if (DefinitiveFrontEnd::ScreenOpen())
		top = viewport->Pos.y + viewport->Size.y - 64 * DefinitiveUi::Scale() - textSize.y / 2 - padding.y;
	auto width = lampSpace + textSize.x;
	auto left = Clamp(centre - width / 2, padding.x * 2, viewport->Size.x - padding.x * 2 - width);
	ImVec2 panelMin{viewport->Pos.x + left - padding.x, top - textSize.y / 2 - padding.y};
	ImVec2 textPos{panelMin.x + padding.x + lampSpace, panelMin.y + padding.y};
	// Under every window, so Space Cadet 2026's screens cover it
	auto drawList = ImGui::GetBackgroundDrawList();
	DefinitiveTheme::DrawPanel(drawList, panelMin,
							   ImVec2{textPos.x + textSize.x + padding.x, textPos.y + textSize.y + padding.y});
	DefinitiveTheme::DrawLamp(drawList,
							  ImVec2{panelMin.x + padding.x + lampRadius, textPos.y + ImGui::GetFontSize() / 2},
							  lampRadius, DefinitiveTheme::Lamp::Lit);
	drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), textPos, ImGui::GetColorU32(ImGuiCol_Text), text, nullptr,
					  wrapWidth);
}

bool winmain::CreateRenderer(bool gpu)
{
	// The GPU renderer, for post-processing; otherwise the default, falling back to the software renderer
	Compositor::Uninit();
	PostEffect::Uninit();
	fullscrn::ReleaseFrame();
	if (Renderer)
		SDL_DestroyRenderer(Renderer);
	Renderer = nullptr;
	if (gpu)
		Renderer = PostEffect::CreateGpuRenderer(MainWindow);
	for (int i = SoftwareRenderer ? 1 : 0; i < 2 && !Renderer && !gpu; i++)
		Renderer = SDL_CreateRenderer(MainWindow, i == 0 ? nullptr : SDL_SOFTWARE_RENDERER);
	if (!Renderer)
		return false;

	if (auto rendererName = SDL_GetRendererName(Renderer))
		printf("Using SDL renderer: %s\n", rendererName);
	SDL_SetRenderDrawColor(Renderer, 0, 0, 0, 255);
	return true;
}

void winmain::ApplyDisplayScale()
{
	// Scale the UI for the display, from the unscaled style; the UI Scale option stays on top of it
	auto& style = ImGui::GetStyle();
	auto fontScale = style.FontScaleMain;
	auto scale = fullscrn::ContentScale();
	StyledEdition = options::GetEdition();
	style = StyledEdition == Editions::Definitive ? *DefinitiveStyle : *BaseStyle;
	style.ScaleAllSizes(scale);
	style.FontScaleDpi = scale;
	style.FontScaleMain = fontScale;
}

bool winmain::FindGameData(const std::vector<const char*>& searchPaths)
{
	std::vector<std::string> looked;
	for (auto path : searchPaths)
	{
		if (path)
			looked.emplace_back(path[0] ? path : pb::get_rc_string(Msg::Definitive_DataStartFolder));
	}
	// A folder chosen before that has lost its data gets the same note as one chosen now without any
	ChosenFolderEmpty = !Options.GameDataFolder.V.empty();

	// The window was waiting for the table to size it
	SDL_SetWindowSize(MainWindow, 1100, 700);
	SDL_SetWindowPosition(MainWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	SDL_ShowWindow(MainWindow);
	while (true)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			ImGui_ImplSDL3_ProcessEvent(&event);
			if (event.type == SDL_EVENT_QUIT)
				return false;
		}
		if (TakeChosenFolder())
			return true;

		ImGui_ImplSDLRenderer3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ApplyEditionLook();
		ImGui::NewFrame();
		DefinitiveFrontEnd::RenderMissingData(looked, ChosenFolderEmpty);
		ImGui::Render();
		SDL_RenderClear(Renderer);
		auto fbScale = ImIO->DisplayFramebufferScale;
		SDL_SetRenderScale(Renderer, fbScale.x, fbScale.y);
		ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), Renderer);
		SDL_SetRenderScale(Renderer, 1.0f, 1.0f);
		SDL_RenderPresent(Renderer);
		SDL_Delay(16);
	}
}

bool winmain::DefinitivePause()
{
	// Space Cadet 2026's pause screen, for a game in play, which the pause key also closes; the demo has none
	if (options::GetEdition() != Editions::Definitive || DemoActive)
		return false;
	// Nor has a game that is ending, between its last ball and the summary: Classic's pause would hold the summary
	// back, and Classic's exit question would open over it
	if (pb::game_mode != GameModes::InGame)
		return !pb::GameEnded;
	DefinitiveFrontEnd::TogglePause();
	return true;
}

const char* winmain::PostProcessingUnavailable()
{
#ifdef __EMSCRIPTEN__
	return pb::get_rc_string(Msg::Definitive_NoGpuWeb);
#elif !SDL_VERSION_ATLEAST(3, 4, 0)
	return pb::get_rc_string(Msg::Definitive_OldSdl);
#else
	if (SoftwareRenderer)
		return pb::get_rc_string(Msg::Definitive_NoGpuSoftware);
	if (PostEffect::GpuRendererFailed())
		return pb::get_rc_string(Msg::Definitive_NoGpu);
	return nullptr;
#endif
}

const char* winmain::EffectsUnavailable()
{
	// Where post-processing can't show, the compositor can't either; it can also fail on its own, with its shaders
	if (auto reason = PostProcessingUnavailable())
		return reason;
	return Compositor::Available() ? nullptr : pb::get_rc_string(Msg::Definitive_EffectsFailed);
}

void winmain::ResetPreciseSleep()
{
	SleepState = WelfordState{};
	SpinThreshold = DurationMs::zero();
}

bool winmain::MenuBarShown()
{
	return Options.ShowMenu && options::GetEdition() == Editions::Classic;
}

float winmain::ExpectedMenuHeight()
{
	// The main menu bar is one frame high; before the first frame ImGui can't measure it,
	if (MainMenuHeight > 0)
		return static_cast<float>(MainMenuHeight);
	// and the style gets its font size only then, so take it from the font: ImGui's own is 13 pixels
	const auto& style = ImGui::GetStyle();
	auto fonts = ImGui::GetIO().Fonts;
	auto fontSize = style.FontSizeBase > 0  ? style.FontSizeBase
					: fonts->Fonts.Size > 0 ? fonts->Fonts[0]->LegacySize
											: 13.0f;
	return std::ceil(fontSize * style.FontScaleMain * style.FontScaleDpi + style.FramePadding.y * 2);
}

int winmain::ProcessWindowMessages()
{
	static auto idleWait = 0;
	SDL_Event event;
	if (has_focus)
	{
		idleWait = static_cast<int>(TargetFrameTime.count());
		while (SDL_PollEvent(&event))
		{
			if (!event_handler(&event))
				return 0;
		}

		return 1;
	}

	// Progressively wait longer when transitioning to idle
	idleWait = std::min(idleWait + static_cast<int>(TargetFrameTime.count()), 500);
	if (SDL_WaitEventTimeout(&event, idleWait))
	{
		idleWait = static_cast<int>(TargetFrameTime.count());
		return event_handler(&event);
	}
	return 1;
}

void winmain::memalloc_failure()
{
	midi::music_stop();
	Sound::Close();
	const char* caption = pb::get_rc_string(Msg::STRING270);
	const char* text = pb::get_rc_string(Msg::STRING279);
	pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, caption, text);
	std::exit(1);
}

void winmain::a_dialog()
{
	auto aboutTitle = pb::get_rc_string(pb::FullTiltMode ? Msg::FullTilt_About : Msg::STRING204);
	if (ShowAboutDialog == true)
	{
		ShowAboutDialog = false;
		ImGui::OpenPopup(aboutTitle);
	}

	bool unused_open = true;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2{ 600, 300 });
	if (ImGui::BeginPopupModal(aboutTitle, &unused_open, ImGuiWindowFlags_None))
	{
		if (ImGui::BeginTabBar("AboutTabBar", ImGuiTabBarFlags_None))
		{
			if (ImGui::BeginTabItem("3DPB"))
			{
				ImGui::TextUnformatted(pb::get_rc_string(pb::Caption()));
				ImGui::TextUnformatted("Original game by Cinematronics, Microsoft");
				ImGui::Separator();

				ImGui::TextUnformatted("Decompiled -> Ported to SDL");
				ImGui::Text("Version %s", Version);
				if (ImGui::SmallButton("Project home: https://github.com/k4zmu2a/SpaceCadetPinball"))
				{
					SDL_OpenURL("https://github.com/k4zmu2a/SpaceCadetPinball");
				}
				if (ImGui::SmallButton("This fork: https://github.com/simonsteiner/SpaceCadetPinball"))
				{
					SDL_OpenURL("https://github.com/simonsteiner/SpaceCadetPinball");
				}
				ImGui::EndTabItem();
			}
			ImGui::PushStyleColor(ImGuiCol_Button, 0);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0);
			if (ImGui::BeginTabItem("Full Tilt!"))
			{
				const ImVec2 buttonCenter = { -1, 0 };
				ImGui::Button("Full Tilt! was created by Cinematronics for Maxis.", buttonCenter);
				ImGui::Button("Version 1.1", buttonCenter);

				auto tableRow = [](LPCSTR textA, LPCSTR textB)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(textA);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(textB);
				};
				if (ImGui::BeginTable("Full Tilt!", 2))
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					// Both columns have a "SPECIAL THANKS" button, so give each its own ID scope.
					ImGui::PushID("Cinematronics");
					ImGui::Button("Cinematronics", buttonCenter);
					ImGui::Separator();
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("PROGRAMMING", "ART");
						tableRow("Michael Sandige", "John Frantz");
						tableRow("John Taylor", "Ryan Medeiros");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("DESIGN", "SOUND EFFECTS");
						tableRow("Kevin Gliner", "Matt Ridgeway");
						tableRow(nullptr, "Donald S. Griffin");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("DESIGN CONSULTANT", "MUSIC");
						tableRow("Mark Sprenger", "Matt Ridgeway");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("PRODUCER", "VOICES");
						tableRow("Kevin Gliner", "Mike McGeary");
						tableRow(nullptr, "William Rice");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("GRAND POOBAH", nullptr);
						tableRow("David Stafford", nullptr);
						ImGui::EndTable();
					}
					ImGui::Separator();
					ImGui::Button("SPECIAL THANKS", buttonCenter);
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("Paula Sandige", "Alex St. John");
						tableRow("Brad Silverberg", "Jeff Camp");
						tableRow("Danny Thorpe", "Greg Hospelhorn");
						tableRow("Keith Johnson", "Sean Grant");
						tableRow("Bob McAnn", "Michael Kelley");
						tableRow("Rob Rosenhouse", "Lisa Acton");
						ImGui::EndTable();
					}
					ImGui::TextUnformatted("Dan and Mitchell Roth");
					ImGui::PopID();

					ImGui::TableNextColumn();
					ImGui::PushID("Maxis");
					ImGui::Button("Maxis", buttonCenter);
					ImGui::Separator();
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("PRODUCER", "PRODUCT MANAGER");
						tableRow("John Csicsery", "Larry Lee");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("LEAD TESTER", "QA MANAGER");
						tableRow("Scott Shicoff", "Scott Shicoff");
						ImGui::EndTable();
					}
					ImGui::Separator();
					ImGui::Button("ADDITIONAL TESTING", buttonCenter);
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("Cathy Castro", "Robin Hines");
						tableRow("John \"Jussi\" Ylinen", "Keith Meyer");
						tableRow("Marc Meyer", "Owen Nelson");
						tableRow("Joe Longworth", "Peter Saylor");
						tableRow("Michael Gilmartin", "Robin Hines");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("ADDITIONAL ART", "ART DIRECTOR");
						tableRow("Ocean Quigley", "Sharon Barr");
						tableRow("Rick Macaraeg", "INSTALL PROGRAM");
						tableRow("Charlie Aquilina", "Kevin O'Hare");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("INTRO MUSIC", "DOCUMENTATION");
						tableRow("Brian Conrad", "David Caggiano");
						tableRow("John Csicsery", "Michael Bremer");
						tableRow(nullptr, "Bob Sombrio");
						ImGui::EndTable();
					}
					ImGui::Separator();
					ImGui::Button("SPECIAL THANKS", buttonCenter);
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("Sam Poole", "Joe Scirica");
						tableRow("Jeff Braun", "Bob Derber");
						tableRow("Ashley Csicsery", "Tom Forge");
						ImGui::EndTable();
					}
					ImGui::Button("Will \"Burr\" Wright", buttonCenter);
					ImGui::PopID();
					ImGui::EndTable();
				}

				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
			ImGui::PopStyleColor(3);
		}

		ImGui::Separator();
		if (ImGui::Button("Ok"))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();
}

void winmain::end_pause()
{
	if (single_step)
	{
		pb::pause_continue();
		no_time_loss = true;
	}
}

void winmain::new_game()
{
	end_pause();
	pb::replay_level(false);
}

void winmain::pause(bool toggle)
{
	if (toggle || !single_step)
	{
		pb::pause_continue();
		no_time_loss = true;
	}
}

void winmain::Restart()
{
	restart = true;
	SDL_Event event{SDL_EVENT_QUIT};
	SDL_PushEvent(&event);
}

void winmain::UpdateFrameRate()
{
	auto fps = Options.FramesPerSecond.V, ups = Options.UpdatesPerSecond.V;
	RefreshRateUnknown = false;
	if (Options.MatchRefreshRate)
	{
		// The display's refresh rate, if SDL knows it, within the FPS limits
		auto mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(MainWindow));
		if (mode && mode->refresh_rate > 0)
			fps = Clamp(static_cast<int>(std::lround(mode->refresh_rate)), options::MinFps, options::MaxFps);
		else
		{
			RefreshRateUnknown = true;
			SDL_ClearError();
		}
	}

	// With FPS above UPS, the loop runs at the frame rate and the table updates every 1000/UPS ms of it
	HighRefresh = fps > ups;
	UpdateToFrameRatio = HighRefresh ? 1.0 : static_cast<double>(ups) / fps;
	TargetFrameTime = DurationMs(1000.0 / std::max(ups, fps));
	UpdateTime = DurationMs(1000.0 / ups);
	UpdateTimeAccumulator = DurationMs::zero();
	CurrentFps = fps;
}

// Whether pausing would stop the demo behind Space Cadet 2026's screens, which plays on there with no game to pause; a
// table paused before the switch from Classic still resumes
static bool DefinitiveKeepsPlaying()
{
	return options::GetEdition() == Editions::Definitive && !winmain::single_step &&
		   (winmain::DemoActive || DefinitiveFrontEnd::ScreenOpen());
}

void winmain::HandleGameBinding(GameBindings binding, bool shortcut)
{
	switch (binding)
	{
	case GameBindings::TogglePause:
		if (!DefinitiveKeepsPlaying() && !DefinitivePause())
			pause();
		break;
	case GameBindings::NewGame:
		new_game();
		break;
	case GameBindings::ToggleFullScreen:
		options::toggle(Menu1::Full_Screen);
		break;
	case GameBindings::ToggleSounds:
		options::toggle(Menu1::Sounds);
		break;
	case GameBindings::ToggleUpscaledArt:
		options::toggle(Menu1::WindowUpscaledArt);
		break;
	case GameBindings::ToggleEdition:
		options::SetEdition(options::GetEdition() == Editions::Definitive ? Editions::Classic : Editions::Definitive);
		break;
	case GameBindings::ToggleMusic:
		options::toggle(Menu1::Music);
		break;
	case GameBindings::ShowControlDialog:
		if (!DefinitiveKeepsPlaying())
			pause(false);
		options::ShowControlDialog();
		break;
	case GameBindings::ToggleMenuDisplay:
		options::toggle(Menu1::Show_Menu);
		break;
	case GameBindings::Exit:
		if (shortcut && DefinitivePause())
			break;
#ifdef __EMSCRIPTEN__
		break;
#endif
		if (!shortcut)
		{
			SDL_Event event{SDL_EVENT_QUIT};
			SDL_PushEvent(&event);
		}
		else
			ShowExitPopup = true;
		break;
	default:
		break;
	}
}

void winmain::RenderFrameTimeDialog()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2{300, 70});
	if (ImGui::Begin("Frame Times", &DispGRhistory, ImGuiWindowFlags_NoScrollbar))
	{
		auto target = static_cast<float>(TargetFrameTime.count());
		auto yMax = target * 2;

		auto spin = Options.HybridSleep ? static_cast<float>(SpinThreshold.count()) : 0;
		ImGui::Text("YMin:0ms, Target frame time:%03.04fms, YMax:%03.04fms, SpinThreshold:%03.04fms",
		            target, yMax, spin);

		static bool scrollPlot = true;
		ImGui::Checkbox("Scroll Plot", &scrollPlot);

		ImGui::SameLine();
		ImGui::SliderFloat("Window Size", &gfrWindow, 0.1f, 15, "%.3fsec", ImGuiSliderFlags_AlwaysClamp);

		{
			float average = 0.0f, dev = 0.0f;
			for (auto n : gfrDisplay)
			{
				average += n;
				dev += std::abs(target - n);
			}
			average /= static_cast<float>(gfrDisplay.size());
			dev /= static_cast<float>(gfrDisplay.size());
			char overlay[64];
			snprintf(overlay, sizeof overlay, "avg %.3fms, dev %.3fms", average, dev);

			auto region = ImGui::GetContentRegionAvail();
			ImGui::PlotLines("Lines", gfrDisplay.data(), static_cast<int>(gfrDisplay.size()),
			                 scrollPlot ? gfrOffset : 0, overlay, 0, yMax, region);
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
}

void winmain::HybridSleep(DurationMs sleepTarget)
{
	static constexpr double StdDevFactor = 0.5;

	// This nice concept is from https://blat-blatnik.github.io/computerBear/making-accurate-sleep-function/
	// Sacrifices some CPU time for smaller frame time jitter
	while (sleepTarget > SpinThreshold)
	{
		auto start = Clock::now();
		std::this_thread::sleep_for(DurationMs(1));
		auto end = Clock::now();

		auto actualDuration = DurationMs(end - start);
		sleepTarget -= actualDuration;

		// Update expected sleep duration using Welford's online algorithm
		// With bad timer, this will run away to 100% spin
		SleepState.Advance(actualDuration.count());
		SpinThreshold = DurationMs(SleepState.mean + SleepState.GetStdDev() * StdDevFactor);
	}

	// spin lock
	for (auto start = Clock::now(); DurationMs(Clock::now() - start) < sleepTarget;);
}

void winmain::ImGuiMenuItemWShortcut(GameBindings binding, bool selected)
{
	const auto& keyDef = Options.Key[~binding];
	if (ImGui::MenuItem(pb::get_rc_string(keyDef.Description), keyDef.GetShortcutDescription().c_str(), selected))
	{
		HandleGameBinding(binding, false);
	}
}
