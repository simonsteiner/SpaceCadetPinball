#include "pch.h"
#include "options.h"
#include "Sound.h"
#include "maths.h"

int Sound::num_channels;
bool Sound::enabled_flag = false;
std::vector<ChannelInfo> Sound::Channels{};
int Sound::Volume = options::MaxVolume;
MIX_Mixer* Sound::Mixer = nullptr;

// Sound effect tracks carry this tag, so that pausing them leaves the music alone
static constexpr const char* SoundTag = "sound";

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

	return MIX_LoadAudio(Mixer, lpName.c_str(), true);
}

void Sound::FreeSound(MIX_Audio* wave)
{
	if (Mixer && wave)
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
				MIX_TagTrack(channel.Track, SoundTag);
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
