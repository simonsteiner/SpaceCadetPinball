#pragma once
#include <cstdint>
#include <vector>


class EmbeddedData
{
public:
	static const unsigned int SDL_GameControllerDB_compressed_data[63392 / 4];
	static const unsigned int SDL_GameControllerDB_compressed_size;

	// Return the decompressed data, or an empty vector if it is corrupt or truncated
	static std::vector<uint8_t> DecompressStb(const void* compressedData, size_t compressedSize);
};
