#include "pch.h"
#include "replay.h"

#include <cstdarg>

#include "fullscrn.h"
#include "options.h"
#include "pb.h"
#include "score.h"
#include "winmain.h"
#include "TBall.h"
#include "TPinballTable.h"

namespace
{
	constexpr const char* Magic = "SpaceCadetPinball replay 1";

	// Reads one line without the line break; returns false at the end of the file
	bool ReadLine(FILE* file, std::string& line)
	{
		line.clear();
		int ch;
		while ((ch = fgetc(file)) != EOF && ch != '\n')
		{
			if (ch != '\r')
				line += static_cast<char>(ch);
		}
		return ch != EOF || !line.empty();
	}

	// Splits "key rest" at the first space
	std::string SplitFirst(const std::string& line, std::string& rest)
	{
		auto space = line.find(' ');
		if (space == std::string::npos)
		{
			rest.clear();
			return line;
		}
		rest = line.substr(space + 1);
		return line.substr(0, space);
	}
}

FILE *replay::Output = nullptr, *replay::Input = nullptr;
int replay::Depth = 0;
unsigned replay::FrameIndex = 0;
unsigned replay::HeaderSeed = 1;
int replay::HeaderResolution = 0;
std::string replay::HeaderDatFile;
bool replay::Failed = false;

replay::Scope::Scope(float frameDtMilliSec) : IsFrame(true), FrameDt(frameDtMilliSec)
{
	Depth++;
}

replay::Scope::Scope(const char* format, ...)
{
	if (Output && Depth == 0)
	{
		va_list args;
		va_start(args, format);
		vfprintf(Output, format, args);
		va_end(args);
		fputc('\n', Output);
	}
	Depth++;
}

replay::Scope::~Scope()
{
	Depth--;
	if (IsFrame && Output && Depth == 0)
	{
		fprintf(Output, "f %a %s\n", FrameDt, FormatState().c_str());
		FrameIndex++;
	}
}

bool replay::StartRecording(const std::string& path)
{
	Output = fopenu(path.c_str(), "wb");
	if (!Output)
	{
		printf("Could not open replay file for writing: %s\n", path.c_str());
		return false;
	}

	// The C library starts unseeded generators at 1, so recording with seed 1 changes nothing
	HeaderSeed = 1;
	HeaderResolution = fullscrn::GetResolution();
	fprintf(Output, "%s\n", Magic);
	fprintf(Output, "game %s\n", winmain::Version);
	fprintf(Output, "dat %s %s\n", pb::DatFileName.c_str(), DatFileHash().c_str());
	fprintf(Output, "seed %u\n", HeaderSeed);
	fprintf(Output, "resolution %d\n", HeaderResolution);
	fprintf(Output, "start\n");
	printf("Recording replay to %s\n", path.c_str());
	return true;
}

bool replay::StartPlayback(const std::string& path)
{
	Input = fopenu(path.c_str(), "rb");
	if (!Input)
	{
		printf("Could not open replay file: %s\n", path.c_str());
		return false;
	}

	std::string line, rest;
	if (!ReadLine(Input, line) || line != Magic)
	{
		printf("Not a replay file, or a version this build can't read: %s\n", path.c_str());
		Stop();
		return false;
	}

	while (ReadLine(Input, line) && line != "start")
	{
		auto key = SplitFirst(line, rest);
		if (key == "dat")
			HeaderDatFile = rest;
		else if (key == "seed")
			HeaderSeed = static_cast<unsigned>(std::stoul(rest));
		else if (key == "resolution")
			HeaderResolution = std::stoi(rest);
	}
	if (line != "start")
	{
		printf("Replay file has no start line: %s\n", path.c_str());
		Stop();
		return false;
	}

	printf("Playing replay %s\n", path.c_str());
	return true;
}

void replay::Stop()
{
	if (Output)
		fclose(Output);
	if (Input)
		fclose(Input);
	Output = Input = nullptr;
}

bool replay::CheckDatFile()
{
	auto actual = pb::DatFileName + " " + DatFileHash();
	if (actual == HeaderDatFile)
		return true;

	printf("Replay was recorded with different game data.\nRecorded: %s\nLoaded:   %s\n",
	       HeaderDatFile.c_str(), actual.c_str());
	return false;
}

void replay::RecordInput(bool down, GameInput input, const std::vector<GameBindings>& bindings)
{
	if (!Output || Depth != 0)
		return;

	fprintf(Output, "%c %d %d ", down ? 'd' : 'u', static_cast<int>(input.Type), input.Value);
	if (bindings.empty())
		fputc('-', Output);
	for (auto index = 0u; index < bindings.size(); index++)
		fprintf(Output, index ? ",%d" : "%d", static_cast<int>(bindings[index]));
	fputc('\n', Output);
}

bool replay::PlayStep()
{
	std::string line, rest;
	while (ReadLine(Input, line))
	{
		if (line.empty())
			continue;
		auto key = SplitFirst(line, rest);
		if (key == "f")
		{
			std::string expected;
			auto dt = std::strtof(SplitFirst(rest, expected).c_str(), nullptr);
			pb::frame(dt);
			auto actual = FormatState();
			if (actual != expected)
			{
				printf("Replay diverged at frame %u.\nExpected: %s\nActual:   %s\n",
				       FrameIndex, expected.c_str(), actual.c_str());
				Failed = true;
				return false;
			}
			FrameIndex++;
			return true;
		}

		if (key == "d" || key == "u")
		{
			int type = 0, value = 0;
			char bindingList[128]{};
			if (sscanf(rest.c_str(), "%d %d %127s", &type, &value, bindingList) != 3)
				break;

			std::vector<GameBindings> bindings;
			for (auto token = strtok(bindingList, ","); token; token = strtok(nullptr, ","))
			{
				if (strcmp(token, "-") != 0)
					bindings.push_back(static_cast<GameBindings>(atoi(token)));
			}

			GameInput input{static_cast<InputTypes>(type), value};
			if (key == "d")
				pb::GameInputDown(input, bindings);
			else
				pb::GameInputUp(bindings);
		}
		else if (key == "n")
		{
			int demo = 0, players = 1;
			if (sscanf(rest.c_str(), "%d %d", &demo, &players) != 2)
				break;
			// Replay mode never saves options, so this doesn't change the player's settings
			options::Options.Players = players;
			pb::replay_level(demo != 0);
		}
		else if (key == "m")
		{
			// strtof reads the hex floats that %a writes; not every C library's scanf does
			char* end;
			auto dx = std::strtof(rest.c_str(), &end);
			auto dy = std::strtof(end, nullptr);
			pb::ballset(dx, dy);
		}
		else if (key == "t")
			pb::toggle_demo();
		else if (key == "p")
			pb::pause_continue();
		else if (key == "l")
			pb::loose_focus();
		else if (key == "b")
			pb::launch_ball();
		else if (key == "c")
			pb::PushCheat(rest);
		else
			break;
	}

	if (!line.empty())
	{
		printf("Replay file has a line this build can't read: %s\n", line.c_str());
		Failed = true;
	}
	else
	{
		printf("Replay matched all %u frames.\n", FrameIndex);
	}
	return false;
}

std::string replay::FormatState()
{
	auto table = pb::MainTable;
	char buffer[256];
	std::string state;

	// The table keeps the current player's ball count; PlayerScores only holds it while another player plays
	snprintf(buffer, sizeof buffer, "t=%d m=%d p=%d n=%d", pb::time_ticks, static_cast<int>(pb::game_mode),
	         table->CurrentPlayer, table->BallCount);
	state += buffer;

	for (auto index = 0; index < table->PlayerCount; index++)
	{
		auto scoreStruct = table->PlayerScores[index].ScoreStruct;
		snprintf(buffer, sizeof buffer, " s%d=%d", index, scoreStruct ? scoreStruct->Score : 0);
		state += buffer;
	}

	for (auto ball : table->BallList)
	{
		if (ball->ActiveFlag)
		{
			snprintf(buffer, sizeof buffer, " b=%a,%a,%a,%a,%a,%a", ball->Position.X, ball->Position.Y,
			         ball->Position.Z, ball->Direction.X, ball->Direction.Y, ball->Speed);
			state += buffer;
		}
		else
		{
			state += " b=-";
		}
	}
	return state;
}

std::string replay::DatFileHash()
{
	// 64-bit FNV-1a over the whole file: enough to tell data files apart
	uint64_t hash = 14695981039346656037ull;
	auto file = fopenu(pb::make_path_name(pb::DatFileName).c_str(), "rb");
	if (file)
	{
		unsigned char buffer[4096];
		size_t count;
		while ((count = fread(buffer, 1, sizeof buffer, file)) > 0)
		{
			for (size_t index = 0; index < count; index++)
			{
				hash ^= buffer[index];
				hash *= 1099511628211ull;
			}
		}
		fclose(file);
	}

	char text[17];
	snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(hash));
	return text;
}
