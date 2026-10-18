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
	static void Stop();
	static bool IsRecording() { return Output != nullptr; }
	static bool IsPlaying() { return Input != nullptr; }

	// Seed and resolution to apply before pb::init, from the recording when playing back
	static unsigned Seed() { return HeaderSeed; }
	static int Resolution() { return HeaderResolution; }
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

	static std::string FormatState();
	static std::string DatFileHash();
};
