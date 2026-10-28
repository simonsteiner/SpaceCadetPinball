#pragma once

struct GameInput;
enum class GameBindings;

// Records a session as the frame time steps and the game commands that drive pb, together with
// the score and ball state after every frame. Playing a recording back runs the same steps and
// commands and reports the first frame whose state differs. See docs/replays.md.
class replay
{
public:
	// Opens a scope for one pb entry point. Only the outermost scope records its command,
	// so a command that calls another entry point is replayed once, not twice.
	class Scope
	{
	public:
		Scope(const char* format, ...);
		// For pb::frame: records the time step and the resulting state when the scope ends
		explicit Scope(float frameDtMilliSec);
		~Scope();
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
	private:
		bool IsFrame = false;
		float FrameDt = 0;
	};

	static bool StartRecording(const std::string& path);
	static bool StartPlayback(const std::string& path);
	// Plays a recording back in the window, for Space Cadet 2026's Recordings: as playback, but quiet, without the
	// recorded edition switches, and with the frames counted first for a timeline
	static bool StartViewing(const std::string& path);
	static void Stop();
	static bool IsRecording() { return Output != nullptr; }
	static bool IsPlaying() { return Input != nullptr; }
	static bool IsViewing()
	{
		return Input != nullptr && Viewing;
	}

	// What a recording holds, read without playing it: its frames, where each starts in milliseconds of play, and
	// the frames at which a game (not the demo) starts. Empty when the file isn't a recording.
	struct Summary
	{
		std::string DatFile;
		std::vector<float> FrameStarts;
		float Length = 0;
		std::vector<unsigned> Games;
	};
	static Summary Summarize(const std::string& path);
	// The summary of the recording being viewed, and the frames played so far
	static const Summary& Viewed()
	{
		return ViewedSummary;
	}
	static unsigned Frame()
	{
		return FrameIndex;
	}
	// The game data file name and hash this one is, as a recording's dat line holds them
	static std::string LoadedDatFile();
	// Whether the recording being made has started a game yet; one that never does isn't worth keeping
	static bool RecordedGame()
	{
		return GameRecorded;
	}

	// Seed and resolution to apply before pb::init, from the recording when playing back
	static unsigned Seed() { return HeaderSeed; }
	static int Resolution() { return HeaderResolution; }
	// The game data file name and hash the recording being played back was made with
	static const std::string& RecordedDatFile()
	{
		return HeaderDatFile;
	}
	static bool CheckDatFile();

	static void RecordInput(bool down, GameInput input, const std::vector<GameBindings>& bindings);

	// Runs the recording up to and including its next frame. Returns false at the end of the
	// recording or at the first difference, after printing the result.
	static bool PlayStep();
	static bool PlaybackFailed() { return Failed; }
private:
	static FILE *Output, *Input;
	static int Depth;
	static unsigned FrameIndex;
	static unsigned HeaderSeed;
	static int HeaderResolution;
	static std::string HeaderDatFile;
	static bool Failed;
	static bool Viewing;
	static bool GameRecorded;
	static Summary ViewedSummary;

	static std::string FormatState();
	static std::string DatFileHash();
};
