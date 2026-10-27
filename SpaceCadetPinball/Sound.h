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
	// Linear up to the knee, then a tanh curve that keeps the slope continuous and approaches the ceiling
	static float SoftClip(float sample, float knee, float ceiling);

	// Space Cadet 2026's sound, which Classic never has: a room's reverb and a wider stereo image on the sound
	// effects, not the music, and a sound pack. Set every frame from the edition and the options.
	static void SetEffects(bool reverb, bool wide, bool packs);
	// The folder sound packs are folders in: sounds in the user preferences folder
	static std::string PackFolder();
	// The packs there, by folder name, sorted
	static std::vector<std::string> Packs();
	// A pack's WAV files replace the table's sounds of the same file name, in any letter case; an empty name or a
	// missing folder leaves the table's own
	static void SetPack(const std::string& name);

private:
	static int num_channels;
	static bool enabled_flag;
	static int Volume;
	static MIX_Mixer* Mixer;
	static MIX_Group* EffectsGroup;
	// The file each loaded sound came from, and the pack's sound that replaces it
	static std::map<MIX_Audio*, std::string> Files;
	static std::map<MIX_Audio*, MIX_Audio*> Replacements;
	static std::string Pack;
	static bool PacksOn;

	static void DestroyTracks();
	static void LoadReplacement(MIX_Audio* original);
	static void FreeReplacements();
};
