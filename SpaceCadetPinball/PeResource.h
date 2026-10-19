#pragma once

// Reads one resource out of a Windows executable (a PE file), such as the message font in the player's pinball.exe.
// The file is untrusted input: every offset is checked, and a truncated or foreign file just has no resource.
class PeResource
{
public:
	static constexpr uint16_t TypeRcData = 10; // RT_RCDATA

	// Return the data of the resource with the given integer type and string name, in its first language, or an
	// empty vector if the file can't be read, isn't a PE file or has no such resource. Names compare ignoring case.
	static std::vector<uint8_t> Load(const std::string& path, uint16_t type, const std::string& name);

	// The same lookup on a file already in memory.
	static std::vector<uint8_t> Find(const std::vector<uint8_t>& image, uint16_t type, const std::string& name);
};
