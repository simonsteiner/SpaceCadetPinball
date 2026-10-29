#include "pch.h"
#include "pb.h"


#include "TableRules.h"
#include "fullscrn.h"
#include "high_score.h"
#include "proj.h"
#include "render.h"
#include "replay.h"
#include "loader.h"
#include "midi.h"
#include "nudge.h"
#include "options.h"
#include "timer.h"
#include "winmain.h"
#include "Sound.h"
#include "TBall.h"
#include "TDemo.h"
#include "TEdgeSegment.h"
#include "TLightGroup.h"
#include "TPlunger.h"
#include "TTableLayer.h"
#include "GroupData.h"
#include "partman.h"
#include "score.h"
#include "TFlipper.h"
#include "TPinballTable.h"
#include "TTextBox.h"
#include "TableView.h"
#include "translations.h"

#include <filesystem>

TPinballTable* pb::MainTable = nullptr;
DatFile* pb::record_table = nullptr;
int pb::time_ticks = 0;
GameModes pb::game_mode = GameModes::GameOver;
float pb::time_now = 0, pb::time_next = 0, pb::time_ticks_remainder = 0;
float pb::BallMaxSpeed, pb::BallHalfRadius, pb::BallToBallCollisionDistance;
float pb::IdleTimerMs = 0;
bool pb::UiHoldsInput = false;
bool pb::GameEnded = true;
Tables pb::Table = Tables::SpaceCadet3DPB;
std::optional<Tables> pb::RequestedTable;
std::vector<TableData> pb::FoundTables;
bool pb::FullTiltMode = false, pb::FullTiltDemoMode = false, pb::cheat_mode = false, pb::demo_mode = false, pb::CreditsActive = false;
std::string pb::DatFileName, pb::BasePath;
ImU32 pb::TextBoxColor;
int pb::quickFlag = 0;
TTextBox *pb::InfoTextBox, *pb::MissTextBox;


int pb::init()
{
	float projMat[12];

	if (DatFileName.empty())
		return 1;
	auto dataFilePath = make_path_name(DatFileName);
	record_table = partman::load_records(dataFilePath.c_str(), FullTiltMode);

	auto useBmpFont = 0;
	get_rc_int(Msg::TextBoxUseBitmapFont, &useBmpFont);
	if (useBmpFont)
		score::load_msg_font("pbmsg_ft");
	// The bonus tables' pictures, which aren't text, so in every language
	if (BonusTable())
		score::msg_font2p = score::load_msg_font_group("font2");

	if (!record_table)
		return 1;

	auto plt = (ColorRgba*)record_table->field_labeled("background", FieldTypes::Palette);
	gdrv::display_palette(plt);

	auto backgroundBmp = record_table->GetBitmap(record_table->record_labeled("background"));
	auto cameraInfoId = record_table->record_labeled("camera_info") + fullscrn::GetResolution();
	auto cameraInfo = (float*)record_table->field(cameraInfoId, FieldTypes::FloatArray);

	/*Full tilt: table size depends on resolution*/
	auto resInfo = &fullscrn::resolution_array[fullscrn::GetResolution()];

	if (cameraInfo)
	{
		memcpy(&projMat, cameraInfo, sizeof(float) * 4 * 3);
		cameraInfo += 12;

		auto projCenterX = resInfo->TableWidth * 0.5f;
		auto projCenterY = resInfo->TableHeight * 0.5f;
		auto projD = cameraInfo[0];
		auto zMin = cameraInfo[1];
		auto zScaler = cameraInfo[2];
		proj::init(projMat, projD, projCenterX, projCenterY, zMin, zScaler);
	}

	render::init(nullptr, resInfo->TableWidth, resInfo->TableHeight);
	gdrv::copy_bitmap(
		render::vscreen,
		backgroundBmp->Width,
		backgroundBmp->Height,
		backgroundBmp->XPosition,
		backgroundBmp->YPosition,
		backgroundBmp,
		0,
		0);
	render::InitialBackground = backgroundBmp;

	loader::loadfrom(record_table);

	mode_change(GameModes::InGame);

	time_ticks = 0;
	timer::init(150);
	score::init();

	MainTable = new TPinballTable();

	high_score::read();
	auto ball = MainTable->BallList.at(0);
	BallMaxSpeed = ball->Radius * 200.0f;
	BallHalfRadius = ball->Radius * 0.5f;
	BallToBallCollisionDistance = (ball->Radius + BallHalfRadius) * 2.0f;

	int red = 255, green = 255, blue = 255;
	auto fontColor = get_rc_string(Msg::TextBoxColor);
	if (fontColor)
		sscanf(fontColor, "%d %d %d", &red, &green, &blue);
	TextBoxColor = IM_COL32(red, green, blue, 255);

	return 0;
}

int pb::uninit()
{
	TableView::Clear();
	score::unload_msg_font();
	loader::unload();
	delete record_table;
	high_score::write();
	delete MainTable;
	MainTable = nullptr;
	timer::uninit();
	render::uninit();
	return 0;
}

namespace
{
	std::string ToUpper(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(),
					   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		return text;
	}

	std::string ToLower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(),
					   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	// A table's DAT files, in the order they are tried
	std::vector<std::string> DatFileNames(Tables table)
	{
		if (table == Tables::DragonsKeep)
			return {"DRAGON.DAT"};
		// Default game data test order: CADET.DAT, PINBALL.DAT, DEMO.DAT
		std::vector<std::string> names{"CADET.DAT", "PINBALL.DAT", "DEMO.DAT"};
		if (options::Options.Prefer3DPBGameData)
			std::swap(names[0], names[1]);
		return names;
	}

	// The folders Full Tilt! installs its tables in, each with its EXE, DAT and SOUND folder
	constexpr std::array FullTiltFolders{"CADET", "DRAGON", "PIRATE"};

	const char* FullTiltFolder(Tables table)
	{
		return table == Tables::DragonsKeep ? FullTiltFolders[1] : FullTiltFolders[0];
	}

	bool IsSeparator(char c)
	{
#ifdef _WIN32
		return c == '/' || c == '\\';
#else
		return c == '/';
#endif
	}

	// The folders to look in for a table, given a folder that ends in a separator or is empty: the folder, the
	// table's Full Tilt! folder under it, and that folder next to it when the folder is one of Full Tilt!'s.
	// Full Tilt!'s CD has the folders in upper case, its installer wrote them capitalized ("Dragon").
	std::vector<std::string> TableFolders(const std::string& folder, Tables table)
	{
		std::string upper = FullTiltFolder(table);
		auto lower = ToLower(upper);
		std::array spellings{upper, lower, upper.substr(0, 1) + lower.substr(1)};

		std::vector<std::string> folders{folder};
		for (const auto& spelling : spellings)
			folders.push_back(folder + spelling + PathSeparator);

		// The working directory is the empty folder: its name comes from its path, and its parent is ".."
		std::string name;
		std::string parent;
		if (folder.empty())
		{
			std::error_code error;
			auto path = std::filesystem::current_path(error).filename().u8string();
			name = {path.begin(), path.end()};
			parent = std::string{".."} + PathSeparator;
		}
		else
		{
			auto end = folder.size() - 1;
			auto start = end;
			while (start > 0 && !IsSeparator(folder[start - 1]))
				start--;
			name = folder.substr(start, end - start);
			parent = folder.substr(0, start);
		}
		// A table's own folder isn't next to itself
		name = ToUpper(name);
		if (name == upper || std::ranges::find(FullTiltFolders, name) == FullTiltFolders.end())
			return folders;
		for (const auto& spelling : spellings)
			folders.push_back(parent + spelling + PathSeparator);
		return folders;
	}

	// The file in the folder, in upper case and then lower case: its name as found, or empty
	std::string FindFile(const std::string& folder, const std::string& upperName)
	{
		for (const auto& fileName : {upperName, ToLower(upperName)})
		{
			if (auto file = fopenu((folder + fileName).c_str(), "r"))
			{
				fclose(file);
				return fileName;
			}
		}
		return {};
	}

	// The table's DAT file in the folder: its name as found, or empty
	std::string FindDatFile(const std::string& folder, Tables table)
	{
		for (const auto& datFileName : DatFileNames(table))
		{
			auto fileName = FindFile(folder, datFileName);
			if (!fileName.empty())
				return fileName;
		}
		return {};
	}

	std::optional<TableData> FindTable(const std::vector<const char*>& dataSearchPaths, Tables table)
	{
		for (auto path : dataSearchPaths)
		{
			if (!path)
				continue;
			for (const auto& folder : TableFolders(path, table))
			{
				if (auto fileName = FindDatFile(folder, table); !fileName.empty())
					return TableData{pb::TableOf(fileName), folder, fileName};
			}
		}
		return {};
	}
} // namespace

std::vector<TableData> pb::FindTables(const std::vector<const char*>& dataSearchPaths)
{
	std::vector<TableData> found;
	for (auto table : {Tables::SpaceCadet3DPB, Tables::DragonsKeep})
	{
		if (auto data = FindTable(dataSearchPaths, table))
			found.push_back(std::move(*data));
	}
	return found;
}

void pb::SelectDatFile(const std::vector<const char*>& dataSearchPaths)
{
	DatFileName.clear();
	BasePath.clear();
	Table = Tables::SpaceCadet3DPB;
	FullTiltDemoMode = FullTiltMode = false;

	FoundTables = FindTables(dataSearchPaths);
	auto wanted = RequestedTable ? *RequestedTable : TableFromKey(options::Options.Table.V);
	auto chosen = std::find_if(FoundTables.begin(), FoundTables.end(),
							   [wanted](const TableData& data) { return SameTable(data.Table, wanted); });
	if (chosen == FoundTables.end())
	{
		// A table asked for without its data is missing data, as is no table at all
		if (RequestedTable || FoundTables.empty())
			return;
		// The player's choice stays saved, for when its data is back
		chosen = FoundTables.begin();
		printf("No data for the table %s, loading %s\n", options::Options.Table.V.c_str(), TableKey(chosen->Table));
	}

	BasePath = chosen->Folder;
	DatFileName = chosen->DatFileName;
	Table = chosen->Table;
	FullTiltMode = Table != Tables::SpaceCadet3DPB;
	FullTiltDemoMode = Table == Tables::FullTiltDemo;
	printf("Loading game from: %s\n", make_path_name(DatFileName).c_str());
}

void pb::SelectTable(Tables table)
{
	if (SameTable(table, Table))
		return;
	options::Options.Table.V = TableKey(table);
	// The choice, not -table's table, loads from now on
	RequestedTable.reset();
	winmain::Restart();
}

Tables pb::TableOf(const std::string& datFileName)
{
	auto upper = ToUpper(datFileName);
	if (upper == "CADET.DAT")
		return Tables::SpaceCadetFullTilt;
	if (upper == "DEMO.DAT")
		return Tables::FullTiltDemo;
	if (upper == "DRAGON.DAT")
		return Tables::DragonsKeep;
	return Tables::SpaceCadet3DPB;
}

bool pb::SameTable(Tables a, Tables b)
{
	return TableKey(a) == std::string_view{TableKey(b)};
}

const char* pb::TableKey(Tables table)
{
	switch (table)
	{
	case Tables::DragonsKeep:
		return "DragonsKeep";
	case Tables::Skulduggery:
		return "Skulduggery";
	default:
		return "SpaceCadet";
	}
}

Tables pb::TableFromKey(std::string_view key)
{
	if (key == TableKey(Tables::DragonsKeep))
		return Tables::DragonsKeep;
	if (key == TableKey(Tables::Skulduggery))
		return Tables::Skulduggery;
	return Tables::SpaceCadet3DPB;
}

void pb::reset_table()
{
	if (MainTable)
		MainTable->Message(MessageCode::Reset, 0.0);
}


void pb::firsttime_setup()
{
	render::update();
}

void pb::mode_change(GameModes mode)
{
	if (CreditsActive)
		TableRules::Current().EndCredits();
	CreditsActive = false;
	IdleTimerMs = 0;

	switch (mode)
	{
	case GameModes::InGame:
		if (demo_mode)
		{
			winmain::LaunchBallEnabled = false;
			winmain::HighScoresEnabled = false;
			winmain::DemoActive = true;
			if (MainTable)
			{
				if (MainTable->Demo)
					MainTable->Demo->ActiveFlag = 1;
			}
		}
		else
		{
			winmain::LaunchBallEnabled = true;
			winmain::HighScoresEnabled = true;
			winmain::DemoActive = false;
			if (MainTable)
			{
				if (MainTable->Demo)
					MainTable->Demo->ActiveFlag = 0;
			}
		}
		break;
	case GameModes::GameOver:
		winmain::LaunchBallEnabled = false;
		if (!demo_mode)
		{
			winmain::HighScoresEnabled = true;
			winmain::DemoActive = false;
		}
		if (MainTable && MainTable->LightGroup)
			MainTable->LightGroup->Message(MessageCode::TLightGroupGameOverAnimation, 1.4f);
		break;
	}
	game_mode = mode;
}

void pb::toggle_demo()
{
	// Starting the demo reads the player count from the options, so the recording keeps it
	replay::Scope replayScope("t %d", options::Options.Players.V);
	if (demo_mode)
	{
		demo_mode = false;
		MainTable->Message(MessageCode::Reset, 0.0);
		mode_change(GameModes::GameOver);
		// A stopped demo has no high scores to check, so it ends here rather than in end_game
		GameEnded = true;
		if (MissTextBox)
			MissTextBox->Clear();
		InfoTextBox->Display(get_rc_string(Msg::STRING125), -1.0);
	}
	else
	{
		replay_level(true);
	}
}

void pb::replay_level(bool demoMode)
{
	replay::Scope replayScope("n %d %d", demoMode, options::Options.Players.V);
	demo_mode = demoMode;
	mode_change(GameModes::InGame);
	if (options::Options.Music)
		midi::music_play();
	MainTable->Message(MessageCode::NewGame, static_cast<float>(options::Options.Players));
	GameEnded = false;
}

void pb::ballset(float dx, float dy)
{
	// dx and dy are normalized to window, ideally in [-1, 1]
	static constexpr float sensitivity = 7000;
	replay::Scope replayScope("m %a %a", dx, dy);

	for (auto ball : MainTable->BallList)
	{
		if (ball->ActiveFlag)
		{
			ball->Direction.X = dx * sensitivity;
			ball->Direction.Y = dy * sensitivity;
			ball->Speed = maths::normalize_2d(ball->Direction);
			ball->LastActiveTime = time_ticks;
		}
	}
}

void pb::frame(float dtMilliSec)
{
	if (dtMilliSec > 100)
		dtMilliSec = 100;
	if (dtMilliSec <= 0)
		return;
	replay::Scope replayScope(dtMilliSec);

	if (FullTiltMode && !demo_mode && !UiHoldsInput)
	{
		IdleTimerMs += dtMilliSec;
		if (IdleTimerMs >= 60000 && !CreditsActive)
		{
			PushCheat("credits");
		}
	}

	float dtSec = dtMilliSec * 0.001f;
	time_next = time_now + dtSec;
	timed_frame(dtSec);
	time_now = time_next;

	dtMilliSec += time_ticks_remainder;
	auto dtWhole = static_cast<int>(dtMilliSec);
	time_ticks_remainder = dtMilliSec - static_cast<float>(dtWhole);
	time_ticks += dtWhole;

	if (nudge::nudged_left || nudge::nudged_right || nudge::nudged_up)
	{
		nudge::nudge_count = dtSec * 4.0f + nudge::nudge_count;
	}
	else
	{
		auto nudgeDec = nudge::nudge_count - dtSec;
		if (nudgeDec <= 0.0f)
			nudgeDec = 0.0;
		nudge::nudge_count = nudgeDec;
	}
	timer::check();
	render::update();
	score::update(MainTable->CurScoreStruct);
	if (!MainTable->TiltLockFlag)
	{
		if (nudge::nudge_count > 0.5f)
		{
			InfoTextBox->Display(get_rc_string(Msg::STRING126), 2.0);
		}
		if (nudge::nudge_count > 1.0f)
			MainTable->tilt(time_now);
	}
	TableView::Fill();
}

void pb::timed_frame(float timeDelta)
{
	for (auto ball : MainTable->BallList)
	{
		if (!ball->ActiveFlag || ball->HasGroupFlag || ball->CollisionComp || ball->Speed >= 0.8f)
		{
			if (ball->StuckCounter > 0)
			{
				vector2 dist{ball->Position.X - ball->PrevPosition.X, ball->Position.Y - ball->PrevPosition.Y};
				auto radiusX2 = ball->Radius * 2.0f;
				if (radiusX2 * radiusX2 < maths::magnitudeSq(dist))
					ball->StuckCounter = 0;
			}
			ball->LastActiveTime = time_ticks;
		}
		else if (time_ticks - ball->LastActiveTime > 500)
		{
			vector2 dist{ball->Position.X - ball->PrevPosition.X, ball->Position.Y - ball->PrevPosition.Y};
			auto radiusD2 = ball->Radius / 2.0f;
			ball->PrevPosition = ball->Position;
			if (radiusD2 * radiusD2 < maths::magnitudeSq(dist))
				ball->StuckCounter = 0;
			else
				ball->StuckCounter++;
			TableRules::Current().UnstuckBall(*ball, time_ticks - ball->LastActiveTime);
		}
	}

	int ballSteps[20]{};
	float ballStepsDistance[20]{};
	int maxStep = -1;
	for (auto index = 0u; index < MainTable->BallList.size(); index++)
	{
		auto ball = MainTable->BallList[index];
		ballSteps[index] = -1;
		if (ball->ActiveFlag != 0)
		{
			vector2 vecDst{};
			ball->TimeDelta = timeDelta;
			if (ball->TimeDelta > 0.01f && ball->Speed < 0.8f)
				ball->TimeDelta = 0.01f;
			ball->CollisionDisabledFlag = false;
			if (ball->CollisionComp)
			{
				ball->CollisionComp->FieldEffect(ball, &vecDst);
			}
			else
			{
				TTableLayer::edge_manager->FieldEffects(ball, &vecDst);
				vecDst.X *= ball->TimeDelta;
				vecDst.Y *= ball->TimeDelta;
				ball->Direction.X *= ball->Speed;
				ball->Direction.Y *= ball->Speed;
				maths::vector_add(ball->Direction, vecDst);
				ball->Speed = maths::normalize_2d(ball->Direction);
				if (ball->Speed > BallMaxSpeed)
					ball->Speed = BallMaxSpeed;

				ballStepsDistance[index] = ball->Speed * ball->TimeDelta;
				auto ballStep = static_cast<int>(std::ceil(ballStepsDistance[index] / BallHalfRadius)) - 1;
				ballSteps[index] = ballStep;
				if (ballStep > maxStep)
					maxStep = ballStep;
			}
		}
	}

	float deltaAngle[4]{};
	int flipperSteps[4]{};
	for (auto index = 0u; index < MainTable->FlipperList.size(); index++)
	{
		auto flipStep = MainTable->FlipperList[index]->GetFlipperStepAngle(timeDelta, &deltaAngle[index]) - 1;
		flipperSteps[index] = flipStep;
		if (flipStep > maxStep)
			maxStep = flipStep;
	}

	ray_type ray{};
	ray.MinDistance = 0.002f;
	for (auto step = 0; step <= maxStep; step++)
	{
		for (auto ballIndex = 0u; ballIndex < MainTable->BallList.size(); ballIndex++)
		{
			auto ball = MainTable->BallList[ballIndex];
			if (!ball->CollisionDisabledFlag && ballSteps[ballIndex] >= step)
			{
				ray.CollisionMask = ball->CollisionMask;

				for (auto distanceSum = 0.0f; distanceSum < BallHalfRadius;)
				{
					ray.Origin = ball->Position;
					ray.Direction = ball->Direction;
					if (ballSteps[ballIndex] <= step)
					{
						ray.MaxDistance = ballStepsDistance[ballIndex] - ballSteps[ballIndex] * BallHalfRadius;
					}
					else
					{
						ray.MaxDistance = BallHalfRadius;
					}

					TEdgeSegment* edge = nullptr;
					auto distance = TTableLayer::edge_manager->FindCollisionDistance(&ray, ball, &edge);
					if (distance > 0.0f)
					{
						distance = BallToBallCollision(ray, *ball, &edge, distance);
					}
					if (ball->EdgeCollisionResetFlag)
					{
						ball->EdgeCollisionResetFlag = false;
					}
					else
					{
						ball->EdgeCollisionCount = 0;
						ball->EdgeCollisionResetFlag = true;
					}
					if (distance >= 1e9f)
					{
						ball->Position.X += ray.MaxDistance * ray.Direction.X;
						ball->Position.Y += ray.MaxDistance * ray.Direction.Y;
						break;
					}

					edge->EdgeCollision(ball, distance);
					if (distance <= 0.0f || ball->CollisionDisabledFlag)
						break;
					distanceSum += distance;
				}
			}
		}

		for (auto flipIndex = 0u; flipIndex < MainTable->FlipperList.size(); flipIndex++)
		{
			if (flipperSteps[flipIndex] >= step)
				MainTable->FlipperList[flipIndex]->FlipperCollision(deltaAngle[flipIndex]);
		}
	}

	for (const auto flipper : MainTable->FlipperList)
	{
		flipper->UpdateSprite();
	}

	for (auto ball : MainTable->BallList)
	{
		if (ball->ActiveFlag)
			ball->Repaint();
	}
}

void pb::pause_continue()
{
	replay::Scope replayScope("p");
	winmain::single_step ^= true;
	InfoTextBox->Clear();
	if (MissTextBox)
		MissTextBox->Clear();
	if (winmain::single_step)
	{
		if (MainTable)
			MainTable->Message(MessageCode::Pause, time_now);
		InfoTextBox->Display(get_rc_string(Msg::STRING123), -1.0);
		midi::music_stop();
		Sound::Deactivate();
	}
	else
	{
		if (MainTable)
			MainTable->Message(MessageCode::Resume, 0.0);
		if (!demo_mode)
		{
			const char* text;
			float textTime;
			if (game_mode == GameModes::GameOver)
			{
				textTime = -1.0;
				text = get_rc_string(Msg::STRING125);
			}
			else
			{
				textTime = 5.0;
				text = get_rc_string(Msg::STRING124);
			}
			InfoTextBox->Display(text, textTime);
		}
		if (options::Options.Music && !winmain::single_step)
			midi::music_play();
		Sound::Activate();
	}
}

void pb::toggle_single_step()
{
	replay::Scope replayScope("s");
	winmain::single_step ^= true;
}

void pb::loose_focus()
{
	replay::Scope replayScope("l");
	if (MainTable)
		MainTable->Message(MessageCode::LooseFocus, time_now);
}

void pb::InputUp(GameInput input)
{
	BindingsUp(input, options::MapGameInput(input));
}

void pb::BindingUp(GameBindings binding)
{
	BindingsUp(GameInput{}, {binding});
}

void pb::BindingDown(GameBindings binding)
{
	if (options::WaitingForInput())
		return;

	BindingsDown(GameInput{}, {binding});
}

bool pb::TableTakesInput()
{
	return game_mode == GameModes::InGame && !winmain::single_step && !demo_mode;
}

void pb::BindingsUp(GameInput input, const std::vector<GameBindings>& bindings)
{
	if (!TableTakesInput())
		return;

	replay::RecordInput(false, input, bindings);
	GameInputUp(bindings);
}

void pb::BindingsDown(GameInput input, const std::vector<GameBindings>& bindings)
{
	for (const auto binding : bindings)
	{
		winmain::HandleGameBinding(binding, true);
	}

	if (!TableTakesInput())
		return;

	replay::RecordInput(true, input, bindings);
	GameInputDown(input, bindings);
}

void pb::GameInputUp(const std::vector<GameBindings>& bindings)
{
	for (const auto binding : bindings)
	{
		switch (binding)
		{
		case GameBindings::LeftFlipper:
			MainTable->Message(MessageCode::LeftFlipperInputReleased, time_now);
			break;
		case GameBindings::RightFlipper:
			MainTable->Message(MessageCode::RightFlipperInputReleased, time_now);
			break;
		case GameBindings::Plunger:
			MainTable->Message(MessageCode::PlungerInputReleased, time_now);
			break;
		case GameBindings::LeftTableBump:
			nudge::un_nudge_right(0, nullptr);
			break;
		case GameBindings::RightTableBump:
			nudge::un_nudge_left(0, nullptr);
			break;
		case GameBindings::BottomTableBump:
			nudge::un_nudge_up(0, nullptr);
			break;
		default: break;
		}
	}
}

void pb::InputDown(GameInput input)
{
	InputDown(input, input);
}

void pb::InputDown(GameInput recorded, GameInput bound)
{
	if (options::WaitingForInput())
	{
		options::InputDown(bound);
		return;
	}

	BindingsDown(recorded, options::MapGameInput(bound));
}

void pb::KeyUp(SDL_Scancode scancode, SDL_Keycode key)
{
	BindingsUp({InputTypes::Keyboard, static_cast<int>(key)}, options::MapGameInput({InputTypes::Keyboard, scancode}));
}

void pb::KeyDown(SDL_Scancode scancode, SDL_Keycode key)
{
	InputDown({InputTypes::Keyboard, static_cast<int>(key)}, {InputTypes::Keyboard, scancode});
}

void pb::GameInputDown(GameInput input, const std::vector<GameBindings>& bindings)
{
	if (CreditsActive)
		TableRules::Current().EndCredits();
	CreditsActive = false;
	IdleTimerMs = 0;

	if (input.Type == InputTypes::Keyboard)
		TableRules::Current().Cheat(input.Value);

	for (const auto binding : bindings)
	{
		switch (binding)
		{
		case GameBindings::LeftFlipper:
			MainTable->Message(MessageCode::LeftFlipperInputPressed, time_now);
			break;
		case GameBindings::RightFlipper:
			MainTable->Message(MessageCode::RightFlipperInputPressed, time_now);
			break;
		case GameBindings::Plunger:
			MainTable->Message(MessageCode::PlungerInputPressed, time_now);
			break;
		case GameBindings::LeftTableBump:
			if (!MainTable->TiltLockFlag)
				nudge::nudge_right();
			break;
		case GameBindings::RightTableBump:
			if (!MainTable->TiltLockFlag)
				nudge::nudge_left();
			break;
		case GameBindings::BottomTableBump:
			if (!MainTable->TiltLockFlag)
				nudge::nudge_up();
			break;
		default: break;
		}
	}

	if (cheat_mode && input.Type == InputTypes::Keyboard)
	{
		switch (input.Value)
		{
		case 'b':
			{
				vector2 pos{6.0f, 7.0f};
				if (!MainTable->BallCountInRect(pos, MainTable->CollisionCompOffset * 1.2f) && MainTable->AddBall(pos))
					MainTable->MultiballCount++;
				break;
			}
		case 'h':
			{
				high_score_struct entry{{0}, 1000000000};
				strncpy(entry.Name, get_rc_string(Msg::STRING127), sizeof entry.Name - 1);
				high_score::show_and_set_high_score_dialog({entry, 1});
				break;
			}
		case 'r':
			TableRules::Current().CheatBumpRank();
			break;
		case 's':
			MainTable->AddScore(static_cast<int>(RandFloat() * 1000000.0f));
			break;
		case SDLK_F12:
			MainTable->port_draw();
			break;
		case 'i':
			MainTable->LightGroup->Message(MessageCode::TLightFtTmpOverrideOn, 1.0f);
			break;
		case 'j':
			MainTable->LightGroup->Message(MessageCode::TLightFtTmpOverrideOff, 1.0f);
			break;
		}
	}
}

void pb::launch_ball()
{
	replay::Scope replayScope("b");
	MainTable->Plunger->Message(MessageCode::PlungerLaunchBall, 0.0f);
}

bool pb::end_game()
{
	bool highScore = false;
	int scores[4]{};
	int scoreIndex[4]{};

	mode_change(GameModes::GameOver);
	GameEnded = true;
	int playerCount = MainTable->PlayerCount;

	score_struct_super* scorePtr = MainTable->PlayerScores;
	for (auto index = 0; index < playerCount; ++index)
	{
		scores[index] = scorePtr->ScoreStruct->Score;
		scoreIndex[index] = index;
		++scorePtr;
	}

	for (auto i = 0; i < playerCount; ++i)
	{
		for (auto j = i + 1; j < playerCount; ++j)
		{
			if (scores[j] > scores[i])
			{
				int score = scores[j];
				scores[j] = scores[i];
				scores[i] = score;

				int index = scoreIndex[j];
				scoreIndex[j] = scoreIndex[i];
				scoreIndex[i] = index;
			}
		}
	}

	if (!demo_mode && !MainTable->CheatsUsed)
	{
		for (auto i = 0; i < playerCount; ++i)
		{
			int position = high_score::get_score_position(scores[i]);
			if (position >= 0)
			{
				high_score_struct entry{{0}, scores[i]};
				const char* playerName;

				switch (scoreIndex[i])
				{
				default:
				case 0: playerName = get_rc_string(Msg::STRING127);
					break;
				case 1: playerName = get_rc_string(Msg::STRING128);
					break;
				case 2: playerName = get_rc_string(Msg::STRING129);
					break;
				case 3: playerName = get_rc_string(Msg::STRING130);
					break;
				}

				strncpy(entry.Name, playerName, sizeof entry.Name - 1);
				high_score::show_and_set_high_score_dialog({entry, -1});
				highScore = true;
			}
		}
	}
	return highScore;
}

void pb::high_scores()
{
	high_score::show_high_score_dialog();
}

void pb::tilt_no_more()
{
	if (MainTable->TiltLockFlag)
		InfoTextBox->Clear();
	MainTable->TiltLockFlag = 0;
	nudge::nudge_count = -2.0;
}

bool pb::chk_highscore()
{
	if (demo_mode)
		return false;
	for (auto i = 0; i < MainTable->PlayerCount; ++i)
	{
		if (high_score::get_score_position(MainTable->PlayerScores[i].ScoreStruct->Score) >= 0)
			return true;
	}
	return false;
}

void pb::SetUiHoldsInput(bool holds)
{
	if (holds == UiHoldsInput)
		return;
	replay::Scope replayScope("h %d", holds);
	UiHoldsInput = holds;
}

void pb::PushCheat(const std::string& cheat)
{
	replay::Scope replayScope("c %s", cheat.c_str());
	for (auto ch : cheat)
		TableRules::Current().Cheat(ch);
}

LPCSTR pb::get_rc_string(Msg uID)
{
	return translations::GetTranslation(uID);
}

int pb::get_rc_int(Msg uID, int* dst)
{
	*dst = atoi(get_rc_string(uID));
	return 1;
}

std::string pb::make_path_name(const std::string& fileName)
{
	return BasePath + fileName;
}

void pb::ShowMessageBox(Uint32 flags, LPCSTR title, LPCSTR message)
{
	fprintf(flags == SDL_MESSAGEBOX_ERROR ? stderr : stdout, "BL error: %s\n%s\n", title, message);
	SDL_ShowSimpleMessageBox(flags, title, message, winmain::MainWindow);
}

float pb::BallToBallCollision(const ray_type& ray, const TBall& ball, TEdgeSegment** edge, float collisionDistance)
{
	for (const auto curBall : MainTable->BallList)
	{
		if (curBall->ActiveFlag && curBall != &ball && (curBall->CollisionMask & ball.CollisionMask) != 0 &&
			std::abs(curBall->Position.X - ball.Position.X) < BallToBallCollisionDistance &&
			std::abs(curBall->Position.Y - ball.Position.Y) < BallToBallCollisionDistance)
		{
			auto distance = curBall->FindCollisionDistance(ray);
			if (distance < 1e9f)
			{
				distance = std::max(0.0f, distance - 0.002f);
				if (distance < collisionDistance)
				{
					collisionDistance = distance;
					*edge = curBall;
				}
			}
		}
	}

	return collisionDistance;
}
