#include "pch.h"
#include "RecordingViewer.h"

#include "options.h"
#include "nudge.h"
#include "pb.h"
#include "Sound.h"
#include "winmain.h"

#include <filesystem>

std::string RecordingViewer::RequestedPath, RecordingViewer::SessionPath;
unsigned RecordingViewer::RequestedFrame = 0;
bool RecordingViewer::IsPaused = false;
bool RecordingViewer::HasEnded = false;
bool RecordingViewer::Restarted = false;
float RecordingViewer::PlaySpeed = 1;
double RecordingViewer::Clock = 0;

namespace
{
	// A path from UTF-8, as the game keeps paths, also on Windows
	std::filesystem::path Utf8Path(const std::string& text)
	{
		return std::u8string{text.begin(), text.end()};
	}

	std::string Utf8(const std::filesystem::path& path)
	{
		auto text = path.u8string();
		return {text.begin(), text.end()};
	}

	// Reading a recording through takes a moment, so each is read once while it stays the same
	struct CachedSummary
	{
		std::uintmax_t Size;
		std::filesystem::file_time_type Written;
		replay::Summary Summary;
	};
	std::map<std::string, CachedSummary> Summaries;

	// Hashing the data file reads all of it, so it is done once per data file
	std::string LoadedDat, LoadedDatName;
} // namespace

std::string RecordingViewer::Folder()
{
	auto prefPath = SDL_GetPrefPath("", "SpaceCadetPinball");
	std::string folder = prefPath ? prefPath : "";
	SDL_free(prefPath);
	return folder + "recordings" + PathSeparator;
}

std::vector<RecordingViewer::Recording> RecordingViewer::List()
{
	auto name = pb::BasePath + pb::DatFileName;
	if (name != LoadedDatName)
	{
		LoadedDatName = name;
		LoadedDat = replay::LoadedDatFile();
	}

	std::vector<Recording> recordings;
	std::error_code error;
	for (const auto& entry : std::filesystem::directory_iterator(Utf8Path(Folder()), error))
	{
		if (!entry.is_regular_file(error) || entry.path().extension() != ".replay")
			continue;
		auto path = Utf8(entry.path());
		auto size = entry.file_size(error);
		auto written = entry.last_write_time(error);
		auto& cached = Summaries[path];
		if (cached.Size != size || cached.Written != written || cached.Summary.FrameStarts.empty())
			cached = {size, written, replay::Summarize(path)};
		if (cached.Summary.FrameStarts.empty())
			continue;
		recordings.push_back({Utf8(entry.path().stem()), path, cached.Summary, cached.Summary.DatFile == LoadedDat});
	}
	// The names are the times they were recorded at, so the newest sorts last
	std::sort(recordings.begin(), recordings.end(),
			  [](const Recording& a, const Recording& b) { return a.Name > b.Name; });
	return recordings;
}

void RecordingViewer::Delete(const std::string& path)
{
	// Only recordings in the folder, which the game made
	if (Utf8Path(path).parent_path() != Utf8Path(Folder()).parent_path())
		return;
	std::error_code error;
	std::filesystem::remove(Utf8Path(path), error);
	Summaries.erase(path);
}

void RecordingViewer::StartSessionRecording()
{
	if (!options::Options.RecordSessions || replay::IsPlaying() || replay::IsRecording())
		return;

	// Named for the local time it started, which sorts by time too
	SDL_Time now = 0;
	SDL_DateTime date{};
	if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &date, true))
		return;
	char name[64];
	snprintf(name, sizeof name, "%04d-%02d-%02d %02d-%02d-%02d.replay", date.year, date.month, date.day, date.hour,
			 date.minute, date.second);
	SDL_CreateDirectory(Folder().c_str());
	SessionPath = Folder() + name;
	if (!replay::StartRecording(SessionPath))
		SessionPath.clear();
}

void RecordingViewer::EndSessionRecording()
{
	auto unused = !SessionPath.empty() && replay::IsRecording() && !replay::RecordedGame();
	replay::Stop();
	if (unused)
		Delete(SessionPath);
	SessionPath.clear();
}

void RecordingViewer::Request(const std::string& path, unsigned frame)
{
	if (path != RequestedPath)
	{
		IsPaused = false;
		PlaySpeed = 1;
	}
	RequestedPath = path;
	RequestedFrame = frame;
	Restarted = true;
	winmain::Restart();
}

void RecordingViewer::Stop()
{
	RequestedPath.clear();
	Restarted = true;
	winmain::Restart();
}

bool RecordingViewer::Begin()
{
	if (RequestedPath.empty())
		return false;
	if (!replay::StartViewing(RequestedPath))
	{
		RequestedPath.clear();
		return false;
	}
	return true;
}

void RecordingViewer::Prepare()
{
	// A recording starts from a fresh start of the game, and the table's clock carries on across a restart: its
	// fraction of a tick and the time pb::init leaves alone are as a new process has them, and so is the nudge
	pb::time_now = pb::time_next = pb::time_ticks_remainder = 0;
	nudge::nudged_left = nudge::nudged_right = nudge::nudged_up = 0;
	nudge::nudge_count = 0;
	HasEnded = false;
	Clock = 0;
	RunTo(RequestedFrame);
}

void RecordingViewer::RunTo(unsigned frame)
{
	// Quietly, or every sound on the way would play at once
	const auto& summary = replay::Viewed();
	Sound::Enable(false);
	while (replay::Frame() < frame && replay::PlayStep())
	{
	}
	Sound::Enable(options::Options.Sounds);
	auto played = replay::Frame();
	HasEnded = played >= summary.FrameStarts.size();
	Clock = played < summary.FrameStarts.size() ? summary.FrameStarts[played] : summary.Length;
}

void RecordingViewer::Advance(double elapsed)
{
	if (IsPaused || HasEnded)
		return;

	// Each frame runs once the clock has passed its end, as the recording ran it; a slow machine catches up a little
	// at a time
	const auto& starts = replay::Viewed().FrameStarts;
	Clock += elapsed * PlaySpeed;
	for (auto steps = 0; steps < 1000; steps++)
	{
		auto frame = replay::Frame();
		if (frame >= starts.size())
		{
			HasEnded = true;
			return;
		}
		auto end = frame + 1 < starts.size() ? starts[frame + 1] : replay::Viewed().Length;
		if (end > Clock)
			return;
		if (!replay::PlayStep())
		{
			HasEnded = true;
			return;
		}
	}
}

void RecordingViewer::Seek(unsigned frame)
{
	const auto& starts = replay::Viewed().FrameStarts;
	frame = std::min(frame, static_cast<unsigned>(starts.size()));
	if (frame >= replay::Frame())
		RunTo(frame);
	else
		Request(RequestedPath, frame);
}

void RecordingViewer::SetPaused(bool paused)
{
	IsPaused = paused;
}

void RecordingViewer::SetSpeed(float speed)
{
	PlaySpeed = std::clamp(speed, 0.25f, 8.0f);
}
