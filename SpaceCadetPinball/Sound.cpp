#include "pch.h"
#include "options.h"
#include "Sound.h"
#include "maths.h"

#include <atomic>
#include <filesystem>

int Sound::num_channels;
bool Sound::enabled_flag = false;
std::vector<ChannelInfo> Sound::Channels{};
int Sound::Volume = options::MaxVolume;
MIX_Mixer* Sound::Mixer = nullptr;
MIX_Group* Sound::EffectsGroup = nullptr;
std::map<MIX_Audio*, std::string> Sound::Files{};
std::map<MIX_Audio*, MIX_Audio*> Sound::Replacements{};
std::string Sound::Pack{};
bool Sound::PacksOn = false;

// Sound effect tracks carry this tag, so that pausing them leaves the music alone
static constexpr const char* SoundTag = "sound";

// Sound effects are full scale, so a loud effect over the music or another effect goes past full scale; the final mix
// passes through a soft clipper instead of being cut off flat. Below the knee it passes unchanged.
static constexpr float MixClipKnee = 0.9f, MixClipCeiling = 0.999f;

namespace
{
	// Space Cadet 2026's effects, switched from the main thread and read on the audio thread
	std::atomic<bool> ReverbOn{false}, WideOn{false};

	// How much wider the stereo image gets: the difference between the channels is scaled by this
	constexpr float StereoWidth = 1.7f;

	// A small room after Freeverb (Jezar at Dreampoint, public domain): per channel, eight parallel combs with damped
	// feedback, then four allpasses in series, tuned at 44.1 kHz and scaled to the mix's rate. Short and quiet, so
	// the table sounds as if it stood in a room rather than in a hall.
	class RoomReverb
	{
	public:
		void Prepare(int rate)
		{
			if (rate == Rate)
				return;
			Rate = rate;
			constexpr int CombTuning[] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
			constexpr int AllpassTuning[] = {556, 441, 341, 225};
			constexpr int StereoSpread = 23;
			auto scale = static_cast<float>(rate) / 44100.0f;
			for (auto channel = 0; channel < 2; channel++)
			{
				for (size_t index = 0; index < std::size(CombTuning); index++)
					Combs[channel][index].Resize(Scaled(CombTuning[index] + channel * StereoSpread, scale));
				for (size_t index = 0; index < std::size(AllpassTuning); index++)
					Allpasses[channel][index].Resize(Scaled(AllpassTuning[index] + channel * StereoSpread, scale));
			}
		}

		void Clear()
		{
			for (auto& channel : Combs)
				for (auto& comb : channel)
					comb.Clear();
			for (auto& channel : Allpasses)
				for (auto& allpass : channel)
					allpass.Clear();
		}

		// Adds the room to out from the dry sound in, frame by frame; the first two channels only
		void Process(const float* in, float* out, int frames, int channels)
		{
			for (auto frame = 0; frame < frames; frame++)
			{
				auto input = 0.0f;
				for (auto channel = 0; channel < std::min(channels, 2); channel++)
					input += in[frame * channels + channel];
				input *= InputGain / static_cast<float>(std::min(channels, 2));
				for (auto channel = 0; channel < std::min(channels, 2); channel++)
				{
					auto sample = 0.0f;
					for (auto& comb : Combs[channel])
						sample += comb.Process(input, Feedback, Damping);
					for (auto& allpass : Allpasses[channel])
						sample = allpass.Process(sample);
					out[frame * channels + channel] += sample * Wet;
				}
			}
		}

	private:
		static constexpr float InputGain = 0.015f, Feedback = 0.74f, Damping = 0.35f, Wet = 1.4f;

		struct Comb
		{
			std::vector<float> Buffer;
			size_t Index = 0;
			float Store = 0;

			void Resize(size_t size)
			{
				Buffer.assign(size, 0);
				Index = 0;
				Store = 0;
			}
			void Clear()
			{
				std::fill(Buffer.begin(), Buffer.end(), 0.0f);
				Store = 0;
			}
			float Process(float input, float feedback, float damping)
			{
				auto output = Buffer[Index];
				Store = output * (1 - damping) + Store * damping;
				Buffer[Index] = input + Store * feedback;
				Index = (Index + 1) % Buffer.size();
				return output;
			}
		};

		struct Allpass
		{
			std::vector<float> Buffer;
			size_t Index = 0;

			void Resize(size_t size)
			{
				Buffer.assign(size, 0);
				Index = 0;
			}
			void Clear()
			{
				std::fill(Buffer.begin(), Buffer.end(), 0.0f);
			}
			float Process(float input)
			{
				auto buffered = Buffer[Index];
				Buffer[Index] = input + buffered * 0.5f;
				Index = (Index + 1) % Buffer.size();
				return buffered - input;
			}
		};

		static size_t Scaled(int samples, float scale)
		{
			return static_cast<size_t>(std::max(1.0f, std::round(static_cast<float>(samples) * scale)));
		}

		int Rate = 0;
		std::array<std::array<Comb, 8>, 2> Combs;
		std::array<std::array<Allpass, 4>, 2> Allpasses;
	};

	// Both callbacks run on the audio thread: the effects group's first, then the final mix's
	RoomReverb Reverb;
	// The sound effects of the mix being made, which the final mix feeds the reverb from
	std::vector<float> ReverbSend;
	bool ReverbWasOn = false;

	void SDLCALL EffectsMix(void*, MIX_Group*, const SDL_AudioSpec* spec, float* pcm, int samples)
	{
		if (WideOn && spec->channels == 2)
		{
			for (auto index = 0; index + 1 < samples; index += 2)
			{
				auto middle = (pcm[index] + pcm[index + 1]) / 2;
				auto side = (pcm[index] - pcm[index + 1]) / 2 * StereoWidth;
				pcm[index] = middle + side;
				pcm[index + 1] = middle - side;
			}
		}
		if (ReverbOn)
			ReverbSend.assign(pcm, pcm + samples);
	}
} // namespace

static void SDLCALL SoftClipMix(void*, MIX_Mixer*, const SDL_AudioSpec* spec, float* pcm, int samples)
{
	// The room goes on ringing after the last effect stops, so it runs while it is on, fed silence when no effect
	// played in this mix
	if (ReverbOn)
	{
		if (!ReverbWasOn)
			Reverb.Clear();
		Reverb.Prepare(spec->freq);
		if (ReverbSend.size() != static_cast<size_t>(samples))
			ReverbSend.assign(samples, 0);
		Reverb.Process(ReverbSend.data(), pcm, samples / std::max(spec->channels, 1), spec->channels);
		std::fill(ReverbSend.begin(), ReverbSend.end(), 0.0f);
	}
	ReverbWasOn = ReverbOn;
	for (auto i = 0; i < samples; i++)
		pcm[i] = Sound::SoftClip(pcm[i], MixClipKnee, MixClipCeiling);
}

// The gains that SDL2_mixer's Mix_SetPosition gave a sound with equal channels on a stereo device:
// the angle attenuates the side facing away from the sound, and the distance attenuates both.
static MIX_StereoGains PositionGains(int angle, int distance)
{
	angle %= 360;
	if (angle < 0)
		angle += 360;

	int left = 255, right = 255;
	if (angle < 90)
		left = 255 - static_cast<int>(255.0f * (static_cast<float>(angle) / 89.0f));
	else if (angle < 180)
		left = static_cast<int>(255.0f * (static_cast<float>(angle - 90) / 89.0f));
	else if (angle < 270)
		right = 255 - static_cast<int>(255.0f * (static_cast<float>(angle - 180) / 89.0f));
	else
		right = static_cast<int>(255.0f * (static_cast<float>(angle - 270) / 89.0f));
	left = Clamp(left, 0, 255);
	right = Clamp(right, 0, 255);

	auto distanceGain = static_cast<float>(255 - distance) / 255.0f;
	return {
		static_cast<float>(left) / 255.0f * distanceGain,
		static_cast<float>(right) / 255.0f * distanceGain
	};
}

void Sound::Init(MIX_Mixer* mixer, int channels, bool enableFlag, int volume)
{
	Mixer = mixer;
	Volume = volume;
	if (Mixer)
	{
		MIX_SetPostMixCallback(Mixer, SoftClipMix, nullptr);
		// The sound effects mix in a group of their own, so Space Cadet 2026's effects leave the music alone
		EffectsGroup = MIX_CreateGroup(Mixer);
		if (EffectsGroup)
			MIX_SetGroupPostMixCallback(EffectsGroup, EffectsMix, nullptr);
	}
	SetChannels(channels);
	Enable(enableFlag);
}

void Sound::Enable(bool enableFlag)
{
	enabled_flag = enableFlag;
	if (Mixer && !enableFlag)
		MIX_StopTag(Mixer, SoundTag, 0);
}

void Sound::Activate()
{
	if (Mixer)
		MIX_ResumeTag(Mixer, SoundTag);
}

void Sound::Deactivate()
{
	if (Mixer)
		MIX_PauseTag(Mixer, SoundTag);
}

void Sound::Close()
{
	Enable(false);
	DestroyTracks();
	Channels.clear();
	FreeReplacements();
	Files.clear();
	if (EffectsGroup)
		MIX_DestroyGroup(EffectsGroup);
	EffectsGroup = nullptr;
}

void Sound::DestroyTracks()
{
	for (auto& channel : Channels)
	{
		if (channel.Track)
			MIX_DestroyTrack(channel.Track);
		channel.Track = nullptr;
	}
}

void Sound::PlaySound(MIX_Audio* wavePtr, int time, TPinballComponent* soundSource, const char* info)
{
	if (Mixer && wavePtr && enabled_flag)
	{
		// Take the first free track, or the one that started playing first
		int channel = -1;
		for (int index = 0; index < num_channels; index++)
		{
			auto track = Channels[index].Track;
			if (track && !MIX_TrackPlaying(track) && !MIX_TrackPaused(track))
			{
				channel = index;
				break;
			}
		}
		if (channel == -1)
		{
			auto cmp = [](const ChannelInfo& a, const ChannelInfo& b)
			{
				return a.TimeStamp < b.TimeStamp;
			};
			auto min = std::min_element(Channels.begin(), Channels.end(), cmp);
			channel = static_cast<int>(std::distance(Channels.begin(), min));
			MIX_StopTrack(Channels[channel].Track, 0);
		}

		auto track = Channels[channel].Track;
		if (!track)
			return;
		// A sound pack's sound in Space Cadet 2026
		if (auto replacement = Replacements.find(wavePtr); PacksOn && replacement != Replacements.end())
			wavePtr = replacement->second;

		// The panning is set before the track starts, because it changes the track's output format
		if (options::Options.SoundStereo)
		{
			// Positional audio uses collision grid 2D coordinates normalized to [0, 1]
			// Point (0, 0) is bottom left table corner; point (1, 1) is top right table corner.
			// Z is defined as: 0 at table level, positive axis goes up from table surface.

			// Get the source sound position.
			// Sound without position are assumed to be at the center top of the table.
			vector3 soundPos{};
			if (soundSource)
			{
				auto soundPos2D = soundSource->get_coordinates();
				soundPos = {soundPos2D.X, soundPos2D.Y, 0.0f};
			}
			else
			{
				soundPos = {0.5f, 1.0f, 0.0f};
			}
			Channels[channel].Position = soundPos;

			// Listener is positioned at the bottom center of the table,
			// at 0.5 height, so roughly a table half - length.
			vector3 playerPos = {0.5f, 0.0f, 0.5f};
			auto soundDir = maths::vector_sub(soundPos, playerPos);

			// Find sound angle from positive Y axis in clockwise direction with atan2
			// Remap atan2 output from (-Pi, Pi] to [0, 2 * Pi)
			auto angle = fmodf(atan2(soundDir.X, soundDir.Y) + Pi * 2, Pi * 2);
			auto angleDeg = angle * 180.0f / Pi;
			auto angleSdl = static_cast<Sint16>(angleDeg);

			// Distance from listener to the sound position is roughly in the [0, ~1.22] range.
			// Remap to [0, 122] by multiplying by 100 and cast to an integer.
			auto distance = static_cast<Uint8>(100.0f * maths::magnitude(soundDir));

			// The angle is in degrees, where angle 0 is due north, and rotates clockwise as the value increases.
			// The distance goes from 0 (near) to 255 (far).
			auto gains = PositionGains(angleSdl, distance);
			MIX_SetTrackStereo(track, &gains);

			// Output position of each sound emitted so we can verify
			// the sanity of the implementation.
			/*printf("X: %3.3f Y: %3.3f Angle: %3.3f Distance: %3d, Object: %s\n",
			       soundPos.X,
			       soundPos.Y,
			       angleDeg,
			       distance,
			       info
			);*/
		}
		else
		{
			MIX_SetTrackStereo(track, nullptr);
		}

		if (MIX_SetTrackAudio(track, wavePtr) && MIX_PlayTrack(track, 0))
			Channels[channel].TimeStamp = time;
	}
}

MIX_Audio* Sound::LoadWaveFile(const std::string& lpName)
{
	if (!Mixer)
		return nullptr;

	auto wavFile = fopenu(lpName.c_str(), "r");
	if (!wavFile)
		return nullptr;
	fclose(wavFile);

	auto audio = MIX_LoadAudio(Mixer, lpName.c_str(), true);
	if (audio)
	{
		Files[audio] = lpName;
		LoadReplacement(audio);
	}
	return audio;
}

void Sound::FreeSound(MIX_Audio* wave)
{
	if (!Mixer || !wave)
		return;
	if (auto replacement = Replacements.find(wave); replacement != Replacements.end())
	{
		MIX_DestroyAudio(replacement->second);
		Replacements.erase(replacement);
	}
	Files.erase(wave);
	MIX_DestroyAudio(wave);
}

void Sound::SetChannels(int channels)
{
	if (channels <= 0)
		channels = 8;

	DestroyTracks();
	num_channels = channels;
	Channels.assign(num_channels, ChannelInfo{});
	if (Mixer)
	{
		for (auto& channel : Channels)
		{
			channel.Track = MIX_CreateTrack(Mixer);
			if (channel.Track)
			{
				MIX_TagTrack(channel.Track, SoundTag);
				if (EffectsGroup)
					MIX_SetTrackGroup(channel.Track, EffectsGroup);
			}
		}
	}
	SetVolume(Volume);
}

void Sound::SetVolume(int volume)
{
	Volume = volume;
	for (auto& channel : Channels)
	{
		if (channel.Track)
			MIX_SetTrackGain(channel.Track, static_cast<float>(volume) / options::MaxVolume);
	}
}

float Sound::SoftClip(float sample, float knee, float ceiling)
{
	auto level = std::fabs(sample);
	if (level <= knee)
		return sample;
	auto range = ceiling - knee;
	return std::copysign(knee + range * std::tanh((level - knee) / range), sample);
}

void Sound::SetEffects(bool reverb, bool wide, bool packs)
{
	ReverbOn = reverb;
	WideOn = wide;
	PacksOn = packs;
}

// A path from UTF-8, as the game keeps paths, also on Windows
static std::filesystem::path Utf8Path(const std::string& text)
{
	return std::u8string{text.begin(), text.end()};
}

std::string Sound::PackFolder()
{
	auto prefPath = SDL_GetPrefPath("", "SpaceCadetPinball");
	std::string folder = prefPath ? prefPath : "";
	SDL_free(prefPath);
	return folder + "sounds" + PathSeparator;
}

std::vector<std::string> Sound::Packs()
{
	std::vector<std::string> packs;
	std::error_code error;
	for (const auto& entry : std::filesystem::directory_iterator(Utf8Path(PackFolder()), error))
	{
		if (entry.is_directory(error))
		{
			auto name = entry.path().filename().u8string();
			packs.emplace_back(name.begin(), name.end());
		}
	}
	std::sort(packs.begin(), packs.end());
	return packs;
}

void Sound::SetPack(const std::string& name)
{
	if (name == Pack)
		return;
	Pack = name;
	FreeReplacements();
	for (const auto& file : Files)
		LoadReplacement(file.first);
}

void Sound::LoadReplacement(MIX_Audio* original)
{
	if (Pack.empty() || !Mixer)
		return;
	auto file = Files.find(original);
	if (file == Files.end())
		return;

	// The pack's file with the same name, in any letter case, as the game data's own names vary in case
	auto name = Utf8Path(file->second).filename().u8string();
	auto lower = [](std::u8string text)
	{
		std::transform(text.begin(), text.end(), text.begin(),
					   [](char8_t ch) { return static_cast<char8_t>(std::tolower(static_cast<unsigned char>(ch))); });
		return text;
	};
	std::error_code error;
	for (const auto& entry : std::filesystem::directory_iterator(Utf8Path(PackFolder() + Pack), error))
	{
		if (lower(entry.path().filename().u8string()) != lower(name))
			continue;
		auto path = entry.path().u8string();
		if (auto audio = MIX_LoadAudio(Mixer, reinterpret_cast<const char*>(path.c_str()), true))
			Replacements[original] = audio;
		else
			printf("Sound pack %s: could not load %s: %s\n", Pack.c_str(), reinterpret_cast<const char*>(path.c_str()),
				   SDL_GetError());
		break;
	}
}

void Sound::FreeReplacements()
{
	for (const auto& replacement : Replacements)
		MIX_DestroyAudio(replacement.second);
	Replacements.clear();
}
