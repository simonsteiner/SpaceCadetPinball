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
#elif defined(__ANDROID__)
#include <filesystem>

// A test APK's assets hold the data in bundled/ and its list in bundled.txt: the data's hash on the first line, then
// one file per line, relative to bundled/ (tools/make-test-build.sh android). The first start copies the files into
// prefPath, the app's private storage, in a folder named after the hash, since SDL reads assets only through SDL_IO
// and the game opens its files with fopen. An APK without the list has no data of its own.
std::string BundledData::Folder(const std::string& prefPath)
{
	size_t size = 0;
	auto list = static_cast<char*>(SDL_LoadFile("bundled.txt", &size));
	if (!list)
	{
		SDL_ClearError();
		return {};
	}
	std::vector<std::string> lines;
	for (std::string text{list, size}; !text.empty();)
	{
		auto end = text.find('\n');
		auto line = text.substr(0, end);
		if (!line.empty())
			lines.push_back(line);
		text = end == std::string::npos ? std::string{} : text.substr(end + 1);
	}
	SDL_free(list);
	if (lines.size() < 2 || prefPath.empty())
		return {};

	auto folder = prefPath + "bundled-" + lines[0];
	std::error_code error;
	if (!std::filesystem::is_directory(folder, error))
	{
		// Copied next to it first, so a start that ends halfway leaves no folder that looks complete
		auto partial = folder + ".part";
		printf("Copying the bundled game data into %s\n", folder.c_str());
		std::filesystem::remove_all(partial, error);
		for (size_t index = 1; index < lines.size(); index++)
		{
			const auto& name = lines[index];
			if (name.starts_with('/') || name.find("..") != std::string::npos)
				return {};
			auto data = SDL_LoadFile(("bundled/" + name).c_str(), &size);
			auto path = partial + PathSeparator + name;
			std::filesystem::create_directories(std::filesystem::path{path}.parent_path(), error);
			auto file = data ? fopenu(path.c_str(), "wb") : nullptr;
			auto written = file ? fwrite(data, 1, size, file) : 0;
			auto closed = file && fclose(file) == 0;
			SDL_free(data);
			if (!closed || written != size)
			{
				printf("Could not copy the bundled game data: %s\n", name.c_str());
				return {};
			}
		}
		std::filesystem::rename(partial, folder, error);
		if (error)
		{
			printf("Could not copy the bundled game data: %s\n", error.message().c_str());
			return {};
		}
	}
	printf("Bundled game data: %s\n", folder.c_str());
	return folder + PathSeparator;
}
#elif defined(SCP_BUNDLED_DATA_PRELOADED)
std::string BundledData::Folder(const std::string& prefPath)
{
	// The web page loads SpaceCadetPinball.data there before the game starts (CMakeLists.txt)
	printf("Bundled game data: /bundled\n");
	return "/bundled/";
}
#else
std::string BundledData::Folder(const std::string& prefPath)
{
	return {};
}
#endif
