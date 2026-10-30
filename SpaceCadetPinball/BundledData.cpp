#include "pch.h"
#include "BundledData.h"

#if defined(SCP_BUNDLED_DATA_HASH) && defined(_WIN32)
#include <filesystem>
#include <span>

#include <windows.h>

namespace
{
	std::filesystem::path Utf8Path(const std::string& text)
	{
		return std::u8string{text.begin(), text.end()};
	}

	// The tar archive the build put into the executable as the resource BUNDLEDDATA (CMakeLists.txt)
	std::span<const char> Archive()
	{
		// RT_RCDATA, spelt out because windows.h picks its A or W form from UNICODE
		auto resource = FindResourceA(nullptr, "BUNDLEDDATA", MAKEINTRESOURCEA(10));
		auto loaded = resource ? LoadResource(nullptr, resource) : nullptr;
		auto data = loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr;
		if (!data)
			return {};
		return {data, SizeofResource(nullptr, resource)};
	}

	// Writes the archive's files under folder. The build packs it with `cmake -E tar --format=gnutar`, from plain
	// files with short relative names, so anything else means the archive isn't the one it made.
	bool Unpack(std::span<const char> archive, const std::string& folder)
	{
		constexpr size_t Block = 512;
		size_t offset = 0;
		while (offset + Block <= archive.size())
		{
			auto header = archive.data() + offset;
			// The archive ends in empty blocks
			if (header[0] == '\0')
				return true;
			std::string name{header, strnlen(header, 100)};
			auto size = std::strtoull(std::string{header + 124, 12}.c_str(), nullptr, 8);
			auto type = header[156];
			offset += Block;
			if (size > archive.size() - offset || name.starts_with('/') || name.find("..") != std::string::npos)
				return false;
			if (type == '0' || type == '\0')
			{
				auto path = folder + name;
				std::error_code error;
				std::filesystem::create_directories(Utf8Path(path).parent_path(), error);
				auto file = fopenu(path.c_str(), "wb");
				if (!file)
					return false;
				auto written = fwrite(archive.data() + offset, 1, size, file);
				if (fclose(file) != 0 || written != size)
					return false;
			}
			else if (type != '5')
				return false;
			offset += (size + Block - 1) / Block * Block;
		}
		return false;
	}
} // namespace

std::string BundledData::Folder(const std::string& prefPath)
{
	if (prefPath.empty())
		return {};
	// Named after the data, not the build, so builds with the same data share one copy
	auto folder = prefPath + "bundled-" SCP_BUNDLED_DATA_HASH;
	std::error_code error;
	if (!std::filesystem::is_directory(Utf8Path(folder), error))
	{
		// Unpacked next to it first, so a start that ends halfway leaves no folder that looks complete
		auto partial = folder + ".part";
		printf("Unpacking the bundled game data into %s\n", folder.c_str());
		std::filesystem::remove_all(Utf8Path(partial), error);
		if (!Unpack(Archive(), partial + PathSeparator))
		{
			printf("Could not unpack the bundled game data\n");
			return {};
		}
		std::filesystem::rename(Utf8Path(partial), Utf8Path(folder), error);
		if (error)
		{
			printf("Could not unpack the bundled game data: %s\n", error.message().c_str());
			return {};
		}
	}
	printf("Bundled game data: %s\n", folder.c_str());
	return folder + PathSeparator;
}
#else
std::string BundledData::Folder(const std::string& prefPath)
{
	return {};
}
#endif
