#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>

// Which presets of a General MIDI SoundFont the music needs
struct SoundFontPresets
{
	// The programs the melodic channels choose
	std::bitset<128> Programs;
	// The drum kits channel 10 chooses, and kit 0, which it starts with; none set when no drum plays
	std::bitset<128> DrumKits;
	// A melodic channel plays without choosing a program, so it plays the SoundFont's first preset
	bool First = false;
	// A channel chooses a bank, so its programs aren't the General MIDI ones: the music needs the whole SoundFont
	bool Whole = false;

	bool Covers(const SoundFontPresets& other) const
	{
		return Whole || (!other.Whole && (other.Programs & ~Programs).none() && (other.DrumKits & ~DrumKits).none() &&
						 (First || !other.First));
	}

	void Add(const SoundFontPresets& other)
	{
		Programs |= other.Programs;
		DrumKits |= other.DrumKits;
		First = First || other.First;
		Whole = Whole || other.Whole;
	}
};

// Leaves a SoundFont only the samples its wanted presets play, before TinySoundFont loads it. TinySoundFont decodes
// every compressed (SF3, Ogg Vorbis) sample when it loads the file, which for FluidR3Mono took 0.9 s and 298 MB on a
// PC and several seconds on a phone, while one table's music plays about a fifth of them.
namespace SoundFontFilter
{
	struct Result
	{
		// Compressed samples the file has, and how many of them stay
		int Samples = 0;
		int Kept = 0;
	};

	// Empties, in the file's bytes in memory, the header of every compressed sample that no wanted preset plays, so
	// TinySoundFont skips it: it treats a sample without a length as empty. A program is wanted in every bank that
	// has it, as TinySoundFont falls back from a bank without it to bank 0, and a drum kit in every drum bank. The
	// SoundFont's first preset is wanted too when a wanted program is missing, since a channel then keeps the preset
	// it had. The sample that follows a wanted one in the file stays too, because TinySoundFont reads a little past a
	// sample's end. Uncompressed samples aren't decoded and stay. A file this can't read, one without kit 0 of the
	// drum bank when drums play, and any file when the music chooses banks, stays as it is.
	Result KeepPresets(uint8_t* data, size_t size, const SoundFontPresets& presets);
} // namespace SoundFontFilter
