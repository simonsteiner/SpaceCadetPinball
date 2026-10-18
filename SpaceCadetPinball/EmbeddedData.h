#pragma once
#include <cstdint>
#include <vector>


class EmbeddedData
{
public:
	static const char PB_MSGFT_bin_compressed_data_base85[5380 + 1];
	static const unsigned int SDL_GameControllerDB_compressed_data[63392 / 4];
	static const unsigned int SDL_GameControllerDB_compressed_size;

	// Return the decompressed data, or an empty vector if it is corrupt
	static std::vector<uint8_t> DecompressStb(const void* compressedData);
	static std::vector<uint8_t> DecompressBase85(const char* compressedDataBase85);
};
