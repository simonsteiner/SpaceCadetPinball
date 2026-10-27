#pragma once

#include "replay.h"

// Space Cadet 2026's Recordings: sessions recorded into the recordings folder of the user preferences folder, and
// playing one back in the window with a timeline. Playback is the replay harness's (docs/replays.md): the table loads
// afresh with the recording's seed and runs its frames and commands, so it plays exactly as recorded. Seeking forward
// runs the frames quietly; seeking back loads the table again and runs from the start.
class RecordingViewer
{
public:
	struct Recording
	{
		std::string Name;
		std::string Path;
		replay::Summary Summary;
		// Recorded with the game data loaded now, so it can be played
		bool Playable;
	};

	static std::string Folder();
	// The recordings in the folder, newest first
	static std::vector<Recording> List();
	static void Delete(const std::string& path);

	// A new recording of this session, when the Record Sessions option is on; called before the table loads
	static void StartSessionRecording();
	// Before the recording stops: a session that never started a game isn't kept
	static void EndSessionRecording();

	// Asks for a recording to be played from a frame; the game restarts into it
	static void Request(const std::string& path, unsigned frame = 0);
	static void Stop();
	// Drops the request without a restart, when the recording turns out not to fit the game data
	static void Forget()
	{
		RequestedPath.clear();
	}
	// Called by winmain before the table loads: starts the playback asked for, if any; false if there is none or it
	// can't be played
	static bool Begin();
	// After the table has loaded: runs quietly to the frame asked for
	static void Prepare();

	// Each frame while viewing, with the time since the last one in milliseconds
	static void Advance(double elapsed);
	static void Seek(unsigned frame);
	static void SetPaused(bool paused);
	static void SetSpeed(float speed);
	// Whether the restart was the viewer's, which keeps the window as it is
	static bool TakeRestarted()
	{
		auto restarted = Restarted;
		Restarted = false;
		return restarted;
	}
	static bool Paused()
	{
		return IsPaused;
	}
	static bool Ended()
	{
		return HasEnded;
	}
	static float Speed()
	{
		return PlaySpeed;
	}

private:
	static std::string RequestedPath, SessionPath;
	static unsigned RequestedFrame;
	static bool IsPaused, HasEnded, Restarted;
	static float PlaySpeed;
	// Milliseconds of the recording played
	static double Clock;

	static void RunTo(unsigned frame);
};
