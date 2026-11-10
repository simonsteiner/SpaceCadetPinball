#include "pch.h"
#include "SoundFontFilter.h"

namespace
{
	// A chunk's bytes within the file; no data when the file doesn't have it
	struct Chunk
	{
		uint8_t* Data = nullptr;
		size_t Size = 0;

		size_t Count(size_t recordSize) const
		{
			return Size / recordSize;
		}
	};

	// The records of the SoundFont's "hydra", by their sizes in the file (SoundFont 2.04, section 7)
	constexpr size_t PresetSize = 38, BagSize = 4, GeneratorSize = 4, InstrumentSize = 22, SampleSize = 46;
	constexpr uint16_t GeneratorInstrument = 41, GeneratorSampleId = 53;
	constexpr uint16_t DrumBank = 128;
	// SF3's flag on a sample's type for Ogg Vorbis data
	constexpr unsigned SampleCompressed = 0x10;
	// A right (2) or left (4) sample, whose link names the other
	constexpr unsigned SampleStereoHalf = 6;
	constexpr size_t SampleStart = 20, SampleLink = 42, SampleType = 44;

	uint16_t Read16(const uint8_t* at)
	{
		return static_cast<uint16_t>(static_cast<unsigned>(at[0]) | static_cast<unsigned>(at[1]) << 8u);
	}

	uint32_t Read32(const uint8_t* at)
	{
		return static_cast<uint32_t>(at[0]) | static_cast<uint32_t>(at[1]) << 8u | static_cast<uint32_t>(at[2]) << 16u |
			   static_cast<uint32_t>(at[3]) << 24u;
	}

	bool Named(const uint8_t* at, const char* name)
	{
		return std::memcmp(at, name, 4) == 0;
	}

	// The chunk of that name among the chunks in data, and for a LIST its type
	Chunk Find(uint8_t* data, size_t size, const char* name, const char* listType = nullptr)
	{
		for (size_t offset = 0; offset + 8 <= size;)
		{
			auto chunkSize = static_cast<size_t>(Read32(data + offset + 4));
			auto start = offset + 8;
			if (chunkSize > size - start)
				break;
			if (Named(data + offset, name) && (!listType || (chunkSize >= 4 && Named(data + start, listType))))
				return listType ? Chunk{data + start + 4, chunkSize - 4} : Chunk{data + start, chunkSize};
			// Chunks are padded to an even length
			offset = start + chunkSize + (chunkSize & 1u);
		}
		return {};
	}

	// The generator values of one kind in the zones of bags [firstBag, lastBag)
	template <typename Use>
	void ForEachGenerator(const Chunk& bags, const Chunk& generators, size_t firstBag, size_t lastBag, uint16_t kind,
						  Use&& use)
	{
		// Each bag ends where the next starts, so the last record only ends the one before
		for (auto bag = firstBag; bag < lastBag && bag + 1 < bags.Count(BagSize); bag++)
		{
			size_t first = Read16(bags.Data + bag * BagSize);
			size_t last = Read16(bags.Data + (bag + 1) * BagSize);
			for (auto index = first; index < last && index < generators.Count(GeneratorSize); index++)
			{
				auto generator = generators.Data + index * GeneratorSize;
				if (Read16(generator) == kind)
					use(Read16(generator + 2));
			}
		}
	}
} // namespace

// Walks the SoundFont's preset, instrument and sample lists to find the samples the wanted presets play, and zeroes
// the start, end and loop points of the other compressed ones; the result is empty when the file stays as it is
SoundFontFilter::Result SoundFontFilter::KeepPresets(uint8_t* data, size_t size, const SoundFontPresets& presets)
{
	if (!data || size < 12 || !Named(data, "RIFF") || !Named(data + 8, "sfbk"))
		return {};
	auto hydra = Find(data + 12, size - 12, "LIST", "pdta");
	if (!hydra.Data)
		return {};
	auto presetHeaders = Find(hydra.Data, hydra.Size, "phdr");
	auto presetBags = Find(hydra.Data, hydra.Size, "pbag");
	auto presetGenerators = Find(hydra.Data, hydra.Size, "pgen");
	auto instruments = Find(hydra.Data, hydra.Size, "inst");
	auto instrumentBags = Find(hydra.Data, hydra.Size, "ibag");
	auto instrumentGenerators = Find(hydra.Data, hydra.Size, "igen");
	auto samples = Find(hydra.Data, hydra.Size, "shdr");
	if (!presetHeaders.Data || !presetBags.Data || !presetGenerators.Data || !instruments.Data ||
		!instrumentBags.Data || !instrumentGenerators.Data || !samples.Data)
		return {};

	// Each list ends with a record that only marks the end
	auto sampleCount = samples.Count(SampleSize);
	sampleCount = sampleCount > 0 ? sampleCount - 1 : 0;
	std::vector<bool> keep(sampleCount, false);
	std::vector<bool> instrumentWanted(instruments.Count(InstrumentSize), false);
	auto presetCount = presetHeaders.Count(PresetSize);
	presetCount = presetCount > 0 ? presetCount - 1 : 0;
	// TinySoundFont's first preset is the one with the lowest bank, then program
	size_t firstPreset = 0;
	auto firstKey = std::numeric_limits<uint32_t>::max();
	std::bitset<128> programsFound;
	auto firstKitFound = false;
	for (size_t preset = 0; preset < presetCount; preset++)
	{
		auto header = presetHeaders.Data + preset * PresetSize;
		auto program = Read16(header + 20);
		auto bank = Read16(header + 22);
		if (auto key = static_cast<uint32_t>(bank) << 16u | static_cast<uint32_t>(program); key < firstKey)
		{
			firstKey = key;
			firstPreset = preset;
		}
		if (program >= presets.Programs.size())
			continue;
		// TinySoundFont looks for a program in the channel's bank, which stays 0 here, and nowhere else
		if (bank == 0 && presets.Programs[program])
			programsFound[program] = true;
		firstKitFound = firstKitFound || (bank == DrumBank && program == 0);
	}
	// The drums start on kit 0 of the drum bank and fall back to it from a kit the SoundFont doesn't have. Without
	// it TinySoundFont plays them with another preset; nothing is taken out then. Nor when the music chooses banks.
	if (presets.Whole || (presets.DrumKits.any() && !firstKitFound))
		return {};
	auto wantFirst = presets.First || programsFound != presets.Programs;

	for (size_t preset = 0; preset < presetCount; preset++)
	{
		auto header = presetHeaders.Data + preset * PresetSize;
		auto program = Read16(header + 20);
		auto bank = Read16(header + 22);
		auto wanted = program < presets.Programs.size() &&
					  (bank >= DrumBank ? presets.DrumKits[program] : presets.Programs[program]);
		if (!wanted && !(wantFirst && preset == firstPreset))
			continue;
		ForEachGenerator(presetBags, presetGenerators, Read16(header + 24), Read16(header + PresetSize + 24),
						 GeneratorInstrument,
						 [&instrumentWanted](uint16_t instrument)
						 {
							 if (instrument < instrumentWanted.size())
								 instrumentWanted[instrument] = true;
						 });
	}
	for (size_t instrument = 0; instrument + 1 < instruments.Count(InstrumentSize); instrument++)
	{
		if (!instrumentWanted[instrument])
			continue;
		auto record = instruments.Data + instrument * InstrumentSize;
		ForEachGenerator(instrumentBags, instrumentGenerators, Read16(record + 20),
						 Read16(record + InstrumentSize + 20), GeneratorSampleId,
						 [&keep](uint16_t sample)
						 {
							 if (sample < keep.size())
								 keep[sample] = true;
						 });
	}
	// The other half of a stereo pair stays with its half
	for (size_t sample = 0; sample < sampleCount; sample++)
	{
		auto record = samples.Data + sample * SampleSize;
		auto link = Read16(record + SampleLink);
		if (keep[sample] && (static_cast<unsigned>(Read16(record + SampleType)) & SampleStereoHalf) &&
			link < sampleCount)
			keep[link] = true;
	}
	const auto played = keep;

	// TinySoundFont's interpolation reads a little past a sample's end, into the sample that follows it in the file.
	// With that one kept too, the music is what the whole SoundFont plays; without it, a few notes end differently.
	for (auto sample = sampleCount; sample-- > 1;)
	{
		if (played[sample - 1])
			keep[sample] = true;
	}

	Result result{};
	for (size_t sample = 0; sample < sampleCount; sample++)
	{
		auto record = samples.Data + sample * SampleSize;
		if (!(static_cast<unsigned>(Read16(record + SampleType)) & SampleCompressed))
			continue;
		result.Samples++;
		if (keep[sample])
			result.Kept++;
		else
			// Its start, end and loop points
			std::memset(record + SampleStart, 0, 16);
	}
	return result;
}
