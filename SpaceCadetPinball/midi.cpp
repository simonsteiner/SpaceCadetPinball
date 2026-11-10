#include "pch.h"
#include "midi.h"


#include "options.h"
#include "pb.h"
#include "Sound.h"
#include "SoundFontFilter.h"

#include <atomic>
#include <mutex>

#include "tml.h"
#include "tsf.h"
#ifdef SCP_NATIVE_MIDI
#include <SDL3_native_midi/SDL_native_midi.h>
#else
struct NativeMidi_Song;
#endif

// One music track, loaded for the backend that plays it
struct MidiSong
{
	NativeMidi_Song* Native = nullptr;
	tml_message* Messages = nullptr;
	MIX_Audio* Audio = nullptr;
	// The MIDI file, kept only while the SoundFont still loads, in case none loads and SDL_mixer has to decode it
	std::vector<uint8_t> Data;
};

std::vector<MidiSong*> midi::LoadedTracks{};
MidiSong *midi::track1, *midi::track2, *midi::track3, *midi::track4, *midi::track5, *midi::track6, *midi::track7;
MidiTracks midi::active_track, midi::NextTrack;
int midi::Volume = options::MaxVolume;
bool midi::IsPlaying = false;
MIX_Mixer* midi::Mixer = nullptr;
MIX_Track* midi::MusicTrack = nullptr;
MidiBackend midi::Backend = MidiBackend::None;

constexpr uint32_t FOURCC(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
	return static_cast<uint32_t>((d << 24) | (c << 16) | (b << 8) | a);
}

int ToVariableLen(uint32_t value, uint32_t& dst)
{
	auto count = 1;
	dst = value & 0x7F;

	while ((value >>= 7))
	{
		dst <<= 8;
		dst |= ((value & 0x7F) | 0x80);
		count++;
	}

	return count;
}

namespace
{
	// TinySoundFont renders the music on the audio thread, in the callback of the stream that feeds the music track.
	// The main thread changes the song only while it holds the stream's lock.
	constexpr int SynthRate = 44100;
	// Frames per render call; TinySoundFont updates its effects every 64 frames
	constexpr int SynthBlock = 64;
	// At -2 dB the theme's loudest chords reach +3 dBFS with FluidR3, so SoftClip limits them. 3D Pinball's theme
	// spends under 1% of its samples above the knee; below it the music passes unchanged.
	constexpr float SynthGainDb = -2.0f;
	constexpr float SoftClipKnee = 0.7f;
	// -1 dBFS, about the theme's peak at the old -6 dB, which leaves sound effects the same headroom
	constexpr float SoftClipCeiling = 0.891f;
	// Voices are allocated once at init, so the audio thread never reallocates them. The theme needs at most 49 with
	// FluidR3; past the limit, a new note takes the voice furthest into its release.
	constexpr int SynthMaxVoices = 64;
	tsf* Synth = nullptr;
	SDL_AudioStream* SynthStream = nullptr;
	tml_message* SynthSong = nullptr; // First message of the playing song, null when stopped
	tml_message* SynthNext = nullptr;
	double SynthMsec = 0;

	void SynthMessage(const tml_message* message)
	{
		switch (message->type)
		{
		case TML_PROGRAM_CHANGE:
			// Channel 10 has the drums
			tsf_channel_set_presetnumber(Synth, message->channel, message->program, message->channel == 9);
			break;
		case TML_NOTE_ON:
			tsf_channel_note_on(Synth, message->channel, message->key, message->velocity / 127.0f);
			break;
		case TML_NOTE_OFF:
			tsf_channel_note_off(Synth, message->channel, message->key);
			break;
		case TML_PITCH_BEND:
			tsf_channel_set_pitchwheel(Synth, message->channel, message->pitch_bend);
			break;
		case TML_CONTROL_CHANGE:
			tsf_channel_midi_control(Synth, message->channel, message->control, message->control_value);
			break;
		default:
			break;
		}
	}

	void SDLCALL RenderSynth(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount)
	{
		if (!SynthSong || !Synth)
			return;

		float buffer[SynthBlock * 2];
		for (auto frames = additionalAmount / static_cast<int>(sizeof buffer[0] * 2); frames > 0;)
		{
			auto block = std::min(frames, SynthBlock);
			for (SynthMsec += block * 1000.0 / SynthRate; SynthNext && SynthMsec >= SynthNext->time;
			     SynthNext = SynthNext->next)
			{
				SynthMessage(SynthNext);
			}
			// The music loops, as with native MIDI
			if (!SynthNext)
			{
				SynthNext = SynthSong;
				SynthMsec = 0;
			}

			tsf_render_float(Synth, buffer, block, 0);
			for (auto i = 0; i < block * 2; i++)
				buffer[i] = Sound::SoftClip(buffer[i], SoftClipKnee, SoftClipCeiling);
			SDL_PutAudioStreamData(stream, buffer, block * static_cast<int>(sizeof buffer[0] * 2));
			frames -= block;
		}
	}

	void SetSynthSong(tml_message* song)
	{
		SDL_LockAudioStream(SynthStream);
		// No synth while the SoundFont still loads; midi::music_update starts the song then
		if (Synth)
		{
			tsf_reset(Synth);
			tsf_channel_set_bank_preset(Synth, 9, 128, 0);
		}
		else
			song = nullptr;
		SynthSong = SynthNext = song;
		SynthMsec = 0;
		SDL_ClearAudioStream(SynthStream);
		SDL_UnlockAudioStream(SynthStream);
	}

	// SDL_SOUNDFONTS first, as SDL2_mixer read it, then where Linux distributions install a General MIDI SoundFont.
	// Only the samples of the presets given are decoded.
	tsf* LoadSoundFont(const SoundFontPresets& presets)
	{
		std::vector<std::string> paths;
		if (auto envPaths = SDL_getenv("SDL_SOUNDFONTS"))
		{
			std::string list = envPaths;
			for (size_t start = 0, end; start <= list.size(); start = end + 1)
			{
				end = list.find_first_of(PathSeparator == '/' ? ";:" : ";", start);
				if (end == std::string::npos)
					end = list.size();
				if (end > start)
					paths.push_back(list.substr(start, end - start));
			}
		}
		for (auto path : {
#ifdef __ANDROID__
				 // The APK's assets. SDL_LoadFile looks for a relative path in the app's files/ first, then in the
				 // assets.
				 "FluidR3Mono_GM.sf3",
#endif
				 "/usr/share/sounds/sf3/default-GM.sf3",
				 "/usr/share/sounds/sf2/default-GM.sf2",
				 "/usr/share/sounds/sf2/FluidR3_GM.sf2",
				 "/usr/share/soundfonts/default.sf2",
				 "/usr/share/soundfonts/FluidR3_GM.sf2",
				 "/usr/share/sounds/sf2/TimGM6mb.sf2",
			 })
		{
			paths.emplace_back(path);
		}

		for (const auto& path : paths)
		{
			// tsf_load_filename uses fopen, which can't open UTF-8 paths on Windows; SDL_LoadFile can
			size_t size;
			auto data = SDL_LoadFile(path.c_str(), &size);
			if (!data)
			{
				SDL_ClearError();
				continue;
			}
			auto start = std::chrono::steady_clock::now();
			auto filtered = SoundFontFilter::KeepPresets(static_cast<uint8_t*>(data), size, presets);
			auto synth = tsf_load_memory(data, static_cast<int>(size));
			SDL_free(data);
			if (synth)
			{
				tsf_set_output(synth, TSF_STEREO_INTERLEAVED, SynthRate, SynthGainDb);
				if (tsf_set_max_voices(synth, SynthMaxVoices))
				{
					auto elapsed = std::chrono::steady_clock::now() - start;
					printf("Playing music with SoundFont: %s (loaded in %d ms, %d of %d compressed samples)\n",
						   path.c_str(),
						   static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()),
						   filtered.Kept, filtered.Samples);
					return synth;
				}
				tsf_close(synth);
			}
			printf("Could not load SoundFont: %s\n", path.c_str());
		}
		return nullptr;
	}

	// Loads the SoundFont on a thread of its own: decoding its Vorbis samples takes time the table and the menu
	// shouldn't wait for, about a second on a PC and seven on a phone for all of FluidR3Mono. Only the presets asked
	// for are decoded, a tenth of that for one table's music. The synth outlives midi::music_shutdown, so a restart
	// for another table or edition doesn't wait for a load in progress, and loads again only when the new table's
	// music needs presets the synth doesn't have; it then gets those on top of the ones it had. The web build has no
	// threads and loads within Request.
	class SoundFontLoader
	{
	public:
		SoundFontLoader() = default;
		SoundFontLoader(const SoundFontLoader&) = delete;
		SoundFontLoader& operator=(const SoundFontLoader&) = delete;

		~SoundFontLoader()
		{
			Join();
			if (Font)
				tsf_close(Font);
		}

		// Waits for a load in progress to end
		void Join()
		{
			if (Worker.joinable())
				Worker.join();
		}

		// Asks for a synth with these presets. Only while nothing uses the synth Get returned: a load replaces it.
		void Request(const SoundFontPresets& presets)
		{
			{
				std::scoped_lock lock(Mutex);
				if (Started && Wanted.Covers(presets))
					return;
				Wanted.Add(presets);
				Started = true;
				Done.store(false);
				// A load in progress sees what is wanted now when it ends, and loads again
				if (Loading)
					return;
				Loading = true;
			}
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
			Load();
#else
			if (Worker.joinable())
				Worker.join();
			Worker = std::thread{&SoundFontLoader::LoadOnThread, this};
#endif
		}

		bool Ready() const
		{
			return Done.load();
		}

		// The synth, or null when no SoundFont loaded. Only once Ready
		tsf* Get() const
		{
			return Ready() ? Font : nullptr;
		}

	private:
		void Load()
		{
			while (true)
			{
				SoundFontPresets presets;
				{
					std::scoped_lock lock(Mutex);
					presets = Wanted;
				}
				auto font = LoadSoundFont(presets);

				std::scoped_lock lock(Mutex);
				if (!Wanted.Covers(presets) || !presets.Covers(Wanted))
				{
					// More was asked for meanwhile
					if (font)
						tsf_close(font);
					continue;
				}
				if (Font)
					tsf_close(Font);
				Font = font;
				Loading = false;
				Done.store(true);
				return;
			}
		}

		void LoadOnThread()
		{
			Load();
			// SDL keeps its error and environment per thread, and frees them only for threads it made
			SDL_CleanupTLS();
		}

		std::thread Worker;
		std::atomic<bool> Done{false};
		// Guards Wanted, Started and Loading, which the loading thread reads when it ends
		std::mutex Mutex;
		SoundFontPresets Wanted;
		bool Started = false;
		bool Loading = false;
		tsf* Font = nullptr;
	};

	SoundFontLoader SoundFont;

	// The presets a song plays: each melodic channel's programs, the SoundFont's first preset for a channel that
	// plays without choosing one, and the kits of the drums on channel 10, which starts with kit 0
	void AddPresets(const tml_message* song, SoundFontPresets& presets)
	{
		std::bitset<16> chose;
		for (auto message = song; message; message = message->next)
		{
			if (message->type != TML_PROGRAM_CHANGE && message->type != TML_NOTE_ON)
				continue;
			auto channel = static_cast<unsigned>(message->channel) % chose.size();
			if (channel == 9)
			{
				presets.DrumKits[0] = true;
				if (message->type == TML_PROGRAM_CHANGE)
					presets.DrumKits[static_cast<unsigned>(message->program) % presets.DrumKits.size()] = true;
			}
			else if (message->type == TML_PROGRAM_CHANGE)
			{
				chose[channel] = true;
				presets.Programs[static_cast<unsigned>(message->program) % presets.Programs.size()] = true;
			}
			else if (!chose[channel])
				presets.First = true;
		}
	}
}

void midi::music_play()
{
	if (!IsPlaying)
	{
		IsPlaying = true;
		play_track(NextTrack, true);
		NextTrack = MidiTracks::None;
	}
}

void midi::music_stop()
{
	if (IsPlaying)
	{
		IsPlaying = false;
		NextTrack = active_track;
		StopPlayback();
	}
}

void midi::StopPlayback()
{
	if (active_track != MidiTracks::None)
	{
		switch (Backend)
		{
		case MidiBackend::Native:
#ifdef SCP_NATIVE_MIDI
			NativeMidi_Stop();
#endif
			break;
		case MidiBackend::SoundFont:
			MIX_StopTrack(MusicTrack, 0);
			SetSynthSong(nullptr);
			break;
		case MidiBackend::Mixer:
			MIX_StopTrack(MusicTrack, 0);
			break;
		case MidiBackend::None:
			break;
		}
		active_track = MidiTracks::None;
	}
}

bool midi::StartPlayback(MidiSong* song)
{
	switch (Backend)
	{
	case MidiBackend::Native:
#ifdef SCP_NATIVE_MIDI
		NativeMidi_Start(song->Native, -1);
		return NativeMidi_Active();
#else
		return false;
#endif
	case MidiBackend::SoundFont:
		// While the SoundFont loads, the track counts as playing; music_update starts it
		if (!Synth)
			return true;
		SetSynthSong(song->Messages);
		return MIX_PlayTrack(MusicTrack, 0);
	case MidiBackend::Mixer:
		{
			if (!MIX_SetTrackAudio(MusicTrack, song->Audio))
				return false;

			auto options = SDL_CreateProperties();
			SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
			auto result = MIX_PlayTrack(MusicTrack, options);
			SDL_DestroyProperties(options);
			return result;
		}
	case MidiBackend::None:
	default:
		return false;
	}
}

int midi::music_init(MIX_Mixer* mixer, int volume)
{
	Mixer = mixer;
	Backend = MidiBackend::None;
	active_track = MidiTracks::None;
	NextTrack = MidiTracks::None;
	IsPlaying = false;
	track1 = nullptr;
	track2 = nullptr;
	track3 = nullptr;
	track4 = nullptr;
	track5 = nullptr;
	track6 = nullptr;
	track7 = nullptr;

	// Music needs an audio device, even with native MIDI, which plays outside of it.
	// Native MIDI first: on Windows and macOS it is the original's synth.
	if (Mixer && !pb::quickFlag)
	{
#ifdef SCP_NATIVE_MIDI
		if (NativeMidi_Init())
			Backend = MidiBackend::Native;
		else
		{
			printf("Could not initialize native MIDI, trying a SoundFont.\nSDL Error: %s\n", SDL_GetError());
			SDL_ClearError();
		}
#endif
		if (Backend == MidiBackend::None)
			StartSoundFontBackend();
		// Last, whatever MIDI decoder SDL_mixer has, if any
		if (Backend == MidiBackend::None)
		{
			MusicTrack = MIX_CreateTrack(Mixer);
			if (MusicTrack)
				Backend = MidiBackend::Mixer;
		}
	}
	SetVolume(volume);

	if (pb::BonusTable())
	{
		load_bonus_table_tracks();
	}
	else if (pb::FullTiltMode)
	{
		track1 = load_track("TABA1");
		track2 = load_track("TABA2");
		track3 = load_track("TABA3");

		// FT demo .006 has only one music track, but it is nearly 9 min. long
		if (!track1 && pb::FullTiltDemoMode)
			track1 = load_track("DEMO");
	}
	else
	{
		// 3DPB has only one music track. PINBALL2.MID is a bitmap font, like the PBMSG_FT resource in pinball.exe
		track1 = load_track("PINBALL");
	}

	if (Backend == MidiBackend::SoundFont)
	{
		SoundFontPresets presets;
		for (auto song : LoadedTracks)
			AddPresets(song->Messages, presets);
		SoundFont.Request(presets);
		// At once when the synth has them already
		music_update();
	}
	return track1 != nullptr;
}

void midi::StartSoundFontBackend()
{
	// Without a synth yet: the tracks load first, since they say which presets to load, and count as playing;
	// music_update starts the music, or falls back, when the SoundFont has loaded. Not after a load found none.
	if (SoundFont.Ready() && !SoundFont.Get())
		return;

	const SDL_AudioSpec synthSpec{SDL_AUDIO_F32, 2, SynthRate};
	SynthStream = SDL_CreateAudioStream(&synthSpec, nullptr);
	MusicTrack = MIX_CreateTrack(Mixer);
	if (SynthStream && MusicTrack && MIX_SetTrackAudioStream(MusicTrack, SynthStream))
	{
		SDL_SetAudioStreamGetCallback(SynthStream, RenderSynth, nullptr);
		Backend = MidiBackend::SoundFont;
	}
	else
		ShutdownBackend();
}

void midi::music_update()
{
	if (Backend != MidiBackend::SoundFont || Synth || !SoundFont.Ready())
		return;

	// The track asked for while the SoundFont loaded
	auto track = IsPlaying ? active_track : MidiTracks::None;
	active_track = MidiTracks::None;
	Synth = SoundFont.Get();
	if (!Synth)
	{
		// No SoundFont: whatever MIDI decoder SDL_mixer has, as music_init falls back
		ShutdownBackend();
		MusicTrack = MIX_CreateTrack(Mixer);
		if (MusicTrack)
		{
			Backend = MidiBackend::Mixer;
			for (auto song : LoadedTracks)
			{
				song->Audio =
					MIX_LoadAudio_IO(Mixer, SDL_IOFromConstMem(song->Data.data(), song->Data.size()), false, true);
				// As load_track says it when nothing can play a track
				if (!song->Audio)
					printf("Could not load a music track.\nSDL Error: %s\n", SDL_GetError());
			}
			SDL_ClearError();
		}
	}
	for (auto song : LoadedTracks)
		song->Data = {};

	SetVolume(Volume);
	if (track != MidiTracks::None)
		play_track(track, true);
}

void midi::music_shutdown()
{
	music_stop();

	for (auto song : LoadedTracks)
	{
#ifdef SCP_NATIVE_MIDI
		if (song->Native)
			NativeMidi_DestroySong(song->Native);
#endif
		tml_free(song->Messages);
		if (song->Audio)
			MIX_DestroyAudio(song->Audio);
		delete song;
	}
	active_track = MidiTracks::None;
	LoadedTracks.clear();
	ShutdownBackend();
	Mixer = nullptr;
}

void midi::WaitForSoundFont()
{
	SoundFont.Join();
}

void midi::ShutdownBackend()
{
#ifdef SCP_NATIVE_MIDI
	if (Backend == MidiBackend::Native)
		NativeMidi_Quit();
#endif
	if (MusicTrack)
		MIX_DestroyTrack(MusicTrack);
	MusicTrack = nullptr;
	if (SynthStream)
		SDL_DestroyAudioStream(SynthStream);
	SynthStream = nullptr;
	SynthSong = SynthNext = nullptr;
	// SoundFontLoader keeps the synth for the next music_init
	Synth = nullptr;
	Backend = MidiBackend::None;
}

void midi::SetVolume(int volume)
{
	Volume = volume;
	auto gain = static_cast<float>(volume) / options::MaxVolume;
	if (MusicTrack)
		MIX_SetTrackGain(MusicTrack, gain);
#ifdef SCP_NATIVE_MIDI
	if (Backend == MidiBackend::Native)
		NativeMidi_SetVolume(gain);
#endif
}

void midi::load_bonus_table_tracks()
{
	if (pb::Table == Tables::DragonsKeep)
	{
		// Its rules pick the track. DRAGON.EXE loads all but TABC3, which no rule plays, so track 3 stays empty
		track1 = load_track("TABC1");
		track2 = load_track("TABC2");
		track4 = load_track("TABC4");
		track5 = load_track("TABC5");
		track6 = load_track("TABC6");
		track7 = load_track("TABC7");
		return;
	}

	// PIRATES.EXE loads TABB1 to TABB6, plays TABB1 for any of them that is missing, and has a seventh track that is
	// TABB5
	track1 = load_track("TABB1");
	std::array tracks{&track2, &track3, &track4, &track5, &track6};
	for (auto index = 0u; index < tracks.size(); index++)
	{
		*tracks[index] = load_track(std::format("TABB{}", index + 2));
		if (!*tracks[index])
			*tracks[index] = track1;
	}
	track7 = track5;
}

MidiSong* midi::load_track(std::string fileName)
{
	if (Backend == MidiBackend::None)
		return nullptr;

	if (pb::FullTiltMode)
	{
		// FT sounds are in SOUND subfolder
		fileName.insert(0, 1, PathSeparator);
		fileName.insert(0, "SOUND");
	}

	auto data = load_track_sub(fileName, true);
	if (!data)
		data = load_track_sub(fileName, false);
	if (!data)
		return nullptr;

	auto song = new MidiSong();
	switch (Backend)
	{
	case MidiBackend::Native:
#ifdef SCP_NATIVE_MIDI
		song->Native = NativeMidi_LoadSong_IO(SDL_IOFromConstMem(data->data(), data->size()), true);
#endif
		break;
	case MidiBackend::SoundFont:
		song->Messages = tml_load_memory(data->data(), static_cast<int>(data->size()));
		if (song->Messages && !Synth)
			song->Data = *data;
		break;
	case MidiBackend::Mixer:
		// SDL_mixer keeps its own copy of the data
		song->Audio = MIX_LoadAudio_IO(Mixer, SDL_IOFromConstMem(data->data(), data->size()), false, true);
		break;
	case MidiBackend::None:
		break;
	}
	delete data;

	if (!song->Native && !song->Messages && !song->Audio)
	{
		printf("Could not load music track %s.\nSDL Error: %s\n", fileName.c_str(), SDL_GetError());
		SDL_ClearError();
		delete song;
		return nullptr;
	}

	LoadedTracks.push_back(song);
	return song;
}

std::vector<uint8_t>* midi::load_track_sub(std::string fileName, bool isMidi)
{
	// FT has music in two formats, depending on game version: MIDI in 16bit, MIDS in 32bit.
	// 3DPB music is MIDI only.
	fileName += isMidi ? ".MID" : ".MDS";
	for (int i = 0; i < 2; i++)
	{
		if (i == 1)
			std::transform(fileName.begin(), fileName.end(), fileName.begin(),
			               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		auto filePath = pb::make_path_name(fileName);
		auto midi = isMidi ? ReadFile(filePath) : MdsToMidi(filePath);
		if (midi)
		{
			// Dump converted MIDI file
			/*auto filePath = fileName + ".midi";
			FILE* fileHandle = fopenu(filePath.c_str(), "wb");
			fwrite(midi->data(), 1, midi->size(), fileHandle);
			fclose(fileHandle);*/
			return midi;
		}
	}

	return nullptr;
}

std::vector<uint8_t>* midi::ReadFile(const std::string& path)
{
	// SDL takes UTF-8 paths on every platform
	size_t size;
	auto contents = static_cast<uint8_t*>(SDL_LoadFile(path.c_str(), &size));
	if (!contents)
	{
		SDL_ClearError();
		return nullptr;
	}

	auto data = new std::vector<uint8_t>(contents, contents + size);
	SDL_free(contents);
	return data;
}

bool midi::play_track(MidiTracks track, bool replay)
{
	auto midi = TrackToMidi(track);
	if (!midi || (!replay && active_track == track))
		return false;

	StopPlayback();

	if (!IsPlaying)
	{
		NextTrack = track;
		return false;
	}

	if (!StartPlayback(midi))
	{
		active_track = MidiTracks::None;
		return false;
	}

	// On Windows, MIDI volume can only be set during playback.
	// And it changes application master volume for some reason.
	SetVolume(Volume);
	active_track = track;
	return true;
}

MidiTracks midi::get_active_track()
{
	if (!IsPlaying)
		return NextTrack;
	else
		return active_track;
}

MidiSong* midi::TrackToMidi(MidiTracks track)
{
	MidiSong* midi;
	switch (track)
	{
	default:
	case MidiTracks::None:
		midi = nullptr;
		break;
	case MidiTracks::Track1:
		midi = track1;
		break;
	case MidiTracks::Track2:
		midi = track2;
		break;
	case MidiTracks::Track3:
		midi = track3;
		break;
	case MidiTracks::Track4:
		midi = track4;
		break;
	case MidiTracks::Track5:
		midi = track5;
		break;
	case MidiTracks::Track6:
		midi = track6;
		break;
	case MidiTracks::Track7:
		midi = track7;
		break;
	}
	return midi;
}

/// <summary>
/// SDL_mixed does not support MIDS. To support FT music, a conversion to MIDI is required.
/// </summary>
/// <param name="file">Path to .MDS file</param>
/// <returns>Vector that contains MIDI file</returns>
std::vector<uint8_t>* midi::MdsToMidi(std::string file)
{
	auto fileHandle = fopenu(file.c_str(), "rb");
	if (!fileHandle)
		return nullptr;

	fseek(fileHandle, 0, SEEK_END);
	auto fileSize = static_cast<uint32_t>(ftell(fileHandle));
	auto buffer = new uint8_t[fileSize];
	auto fileBuf = reinterpret_cast<riff_header*>(buffer);
	fseek(fileHandle, 0, SEEK_SET);
	// A short read leaves a shorter file, which the checks below reject
	fileSize = static_cast<uint32_t>(fread(fileBuf, 1, fileSize, fileHandle));
	fclose(fileHandle);

	int returnCode = 0;
	std::vector<uint8_t>* midiOut = nullptr;
	do
	{
		if (fileSize < 12)
		{
			returnCode = 3;
			break;
		}
		if (fileBuf->Riff != FOURCC('R', 'I', 'F', 'F') ||
			fileBuf->Mids != FOURCC('M', 'I', 'D', 'S') ||
			fileBuf->Fmt != FOURCC('f', 'm', 't', ' '))
		{
			returnCode = 3;
			break;
		}
		if (fileBuf->FileSize > fileSize - 8)
		{
			returnCode = 3;
			break;
		}
		if (fileSize - 12 < 8)
		{
			returnCode = 3;
			break;
		}
		if (fileBuf->FmtSize < 12 || fileBuf->FmtSize > fileSize - 12)
		{
			returnCode = 3;
			break;
		}

		auto streamIdUsed = fileBuf->dwFlags == 0;
		auto dataChunk = reinterpret_cast<riff_data*>(reinterpret_cast<char*>(&fileBuf->dwTimeFormat) + fileBuf->
			FmtSize);
		if (dataChunk->Data != FOURCC('d', 'a', 't', 'a'))
		{
			returnCode = 3;
			break;
		}
		if (dataChunk->DataSize < 4)
		{
			returnCode = 3;
			break;
		}

		auto srcPtr = dataChunk->Blocks;
		std::vector<midi_event> midiEvents{};
		for (auto blockIndex = dataChunk->BlocksPerChunk; blockIndex; blockIndex--)
		{
			auto eventSizeInt = streamIdUsed ? 3 : 2;
			auto eventCount = srcPtr->CbBuffer / (4 * eventSizeInt);

			auto currentTicks = srcPtr->TkStart;
			auto srcPtr2 = reinterpret_cast<uint32_t*>(srcPtr->AData);
			for (auto i = 0u; i < eventCount; i++)
			{
				currentTicks += srcPtr2[0];
				auto event = streamIdUsed ? srcPtr2[2] : srcPtr2[1];
				midiEvents.push_back({currentTicks, event});
				srcPtr2 += eventSizeInt;
			}

			srcPtr = reinterpret_cast<riff_block*>(&srcPtr->AData[srcPtr->CbBuffer]);
		}

		// MIDS events can be out of order in the file
		std::sort(midiEvents.begin(), midiEvents.end(), [](const midi_event& lhs, const midi_event& rhs)
		{
			return lhs.iTicks < rhs.iTicks;
		});

		// MThd chunk
		std::vector<uint8_t>& midiBytes = *new std::vector<uint8_t>();
		midiOut = &midiBytes;
		midi_header header(SwapByteOrderShort(static_cast<uint16_t>(fileBuf->dwTimeFormat)));
		auto headerData = reinterpret_cast<const uint8_t*>(&header);
		midiBytes.insert(midiBytes.end(), headerData, headerData + sizeof header);

		// MTrk chunk
		midi_track track(7);
		auto trackData = reinterpret_cast<const uint8_t*>(&track);
		midiBytes.insert(midiBytes.end(), trackData, trackData + sizeof track);
		auto lengthPos = midiBytes.size() - 4;

		auto prevTime = 0u;
		for (const auto& event : midiEvents)
		{
			assertm(event.iTicks >= prevTime, "MIDS events: negative delta-time");
			uint32_t delta = event.iTicks - prevTime;
			prevTime = event.iTicks;

			// Delta time is in variable quantity, Big Endian
			uint32_t deltaVarLen;
			auto count = ToVariableLen(delta, deltaVarLen);
			auto deltaData = reinterpret_cast<const uint8_t*>(&deltaVarLen);
			midiBytes.insert(midiBytes.end(), deltaData, deltaData + count);

			switch (event.iEvent >> 24)
			{
			case 0:
				{
					// Type 0 - MIDI short message. 3 bytes: xx p1 p2 00, where xx - message, p* - parameters
					// Some of the messages have only one parameter
					auto msgMask = (event.iEvent) & 0xF0;
					auto shortMsg = (msgMask == 0xC0 || msgMask == 0xD0);
					auto eventData = reinterpret_cast<const uint8_t*>(&event.iEvent);
					midiBytes.insert(midiBytes.end(), eventData, eventData + (shortMsg ? 2 : 3));
					break;
				}
			case 1:
				{
					// Type 1 - tempo change, 3 bytes: xx xx xx 01
					// Meta message, set tempo, 3 bytes payload
					const uint8_t metaSetTempo[] = {0xFF, 0x51, 0x03};
					midiBytes.insert(midiBytes.end(), metaSetTempo, metaSetTempo + 3);

					auto eventBE = SwapByteOrderInt(event.iEvent);
					auto eventData = reinterpret_cast<const uint8_t*>(&eventBE) + 1;
					midiBytes.insert(midiBytes.end(), eventData, eventData + 3);
					break;
				}
			default:
				assertm(0, "MIDS events: uknown event");
				break;
			}
		}

		// Meta message, end of track, 0 bytes payload
		const uint8_t metaEndTrack[] = {0x00, 0xFF, 0x2f, 0x00};
		midiBytes.insert(midiBytes.end(), metaEndTrack, metaEndTrack + 4);

		// Set final MTrk size
		auto lengthBE = SwapByteOrderInt(static_cast<uint32_t>(midiBytes.size()) - sizeof header - sizeof track);
		auto lengthData = reinterpret_cast<const uint8_t*>(&lengthBE);
		std::copy_n(lengthData, 4, midiBytes.begin() + lengthPos);
	}
	while (false);

	delete[] buffer;
	if (returnCode && midiOut)
	{
		delete midiOut;
		midiOut = nullptr;
	}

	return midiOut;
}
