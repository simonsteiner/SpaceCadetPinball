#pragma once
#include "maths.h"
#include "TPinballComponent.h"

struct ChannelInfo
{
	int TimeStamp;
	vector2 Position;
	MIX_Track* Track;
};

class Sound
{
public:
	static std::vector<ChannelInfo> Channels;

	static void Init(MIX_Mixer* mixer, int channels, bool enableFlag, int volume);
	static void Enable(bool enableFlag);
	static void Activate();
	static void Deactivate();
	static void Close();
	static void PlaySound(MIX_Audio* wavePtr, int time, TPinballComponent *soundSource, const char* info);
	static MIX_Audio* LoadWaveFile(const std::string& lpName);
	static void FreeSound(MIX_Audio* wave);
	static void SetChannels(int channels);
	static void SetVolume(int volume);
private:
	static int num_channels;
	static bool enabled_flag;
	static int Volume;
	static MIX_Mixer* Mixer;

	static void DestroyTracks();
};
