#include "pch.h"
#include "PeResource.h"

#include <cctype>

// The PE layout follows Microsoft's "PE Format" documentation: the DOS header points to the PE signature, the
// optional header's third data directory is the resource directory, and the section table maps its addresses
// (RVAs) to file offsets. The resource directory is a tree of type, name and language levels.

namespace
{
	// Far larger than the executables the game data came with (pinball.exe is 281 KB); a bigger file isn't read.
	constexpr long MaxFileSize = 64 * 1024 * 1024;
	constexpr uint32_t SubdirectoryFlag = 0x80000000u;

	class PeImage
	{
	public:
		explicit PeImage(const std::vector<uint8_t>& data) : Data(data)
		{
		}

		// Whether size bytes at offset lie inside the file
		bool Has(uint64_t offset, uint64_t size) const
		{
			return offset <= Data.size() && size <= Data.size() - offset;
		}

		uint32_t Byte(uint64_t offset) const
		{
			return Data[offset];
		}

		// Reads a little-endian 16-bit value at a file offset; false when it lies outside the file
		bool U16(uint64_t offset, uint16_t& value) const
		{
			if (!Has(offset, 2))
				return false;
			value = static_cast<uint16_t>(Byte(offset) | Byte(offset + 1) << 8u);
			return true;
		}

		// Reads a little-endian 32-bit value at a file offset; false when it lies outside the file
		bool U32(uint64_t offset, uint32_t& value) const
		{
			if (!Has(offset, 4))
				return false;
			value = Byte(offset) | Byte(offset + 1) << 8u | Byte(offset + 2) << 16u | Byte(offset + 3) << 24u;
			return true;
		}

		// Read the headers and the section table; false if this isn't a PE file
		bool ParseHeaders()
		{
			uint16_t dosMagic;
			uint32_t peOffset;
			if (!U16(0, dosMagic) || dosMagic != 0x5A4D || !U32(0x3C, peOffset))
				return false;
			if (uint32_t signature; !U32(peOffset, signature) || signature != 0x00004550)
				return false;

			uint16_t sectionCount;
			uint16_t optionalHeaderSize;
			uint16_t magic;
			uint64_t optionalHeader = uint64_t{peOffset} + 24;
			if (!U16(uint64_t{peOffset} + 6, sectionCount) || !U16(uint64_t{peOffset} + 20, optionalHeaderSize) ||
				!U16(optionalHeader, magic))
				return false;

			// PE32 or PE32+: where the data directory count sits differs
			uint64_t directoryCountOffset;
			if (magic == 0x10B)
				directoryCountOffset = 92;
			else if (magic == 0x20B)
				directoryCountOffset = 108;
			else
				return false;
			uint32_t directoryCount;
			constexpr uint32_t resourceDirectory = 2;
			uint64_t resourceEntry = directoryCountOffset + 4 + 8 * resourceDirectory;
			if (!U32(optionalHeader + directoryCountOffset, directoryCount) || directoryCount <= resourceDirectory ||
				resourceEntry + 8 > optionalHeaderSize)
				return false;
			if (!U32(optionalHeader + resourceEntry, ResourceRva) ||
				!U32(optionalHeader + resourceEntry + 4, ResourceSize))
				return false;

			uint64_t sectionTable = optionalHeader + optionalHeaderSize;
			for (uint16_t index = 0; index < sectionCount; index++)
			{
				uint64_t header = sectionTable + 40ULL * index;
				Section section{};
				if (!U32(header + 12, section.VirtualAddress) || !U32(header + 16, section.RawSize) ||
					!U32(header + 20, section.RawOffset))
					return false;
				Sections.push_back(section);
			}
			return true;
		}

		// Find the file offset of an RVA and how many bytes of its section follow it in the file
		bool MapRva(uint32_t rva, uint64_t& offset, uint64_t& available) const
		{
			for (const auto& section : Sections)
			{
				if (rva < section.VirtualAddress || rva - section.VirtualAddress >= section.RawSize)
					continue;
				uint32_t delta = rva - section.VirtualAddress;
				offset = uint64_t{section.RawOffset} + delta;
				if (offset > Data.size())
					return false;
				available = std::min<uint64_t>(section.RawSize - delta, Data.size() - offset);
				return true;
			}
			return false;
		}

		// The data of the resource with this type ID and name, in the first language its directory lists; empty
		// when there is none or it lies outside the file. Needs ParseHeaders to have succeeded
		std::vector<uint8_t> FindResource(uint16_t type, const std::string& name)
		{
			uint64_t available;
			if (ResourceRva == 0 || !MapRva(ResourceRva, ResourceBase, available))
				return {};
			ResourceLength = std::min<uint64_t>(ResourceSize, available);

			// Type and name levels lead to subdirectories, the language level to a data entry
			auto isType = [type](uint32_t id) { return id == type; };
			auto isName = [this, &name](uint32_t id) { return NameEquals(id, name); };
			auto anyLanguage = [](uint32_t) { return true; };
			uint32_t nameDirectory;
			uint32_t languageDirectory;
			uint32_t dataEntry;
			if (!FindEntry(0, isType, true, nameDirectory) ||
				!FindEntry(nameDirectory, isName, true, languageDirectory) ||
				!FindEntry(languageDirectory, anyLanguage, false, dataEntry))
				return {};

			uint32_t dataRva;
			uint32_t dataSize;
			if (!InResource(dataEntry, 16) || !U32(ResourceBase + dataEntry, dataRva) ||
				!U32(ResourceBase + dataEntry + 4, dataSize))
				return {};
			uint64_t dataOffset;
			if (!MapRva(dataRva, dataOffset, available) || dataSize > available)
				return {};
			auto begin = Data.begin() + static_cast<std::ptrdiff_t>(dataOffset);
			return {begin, begin + static_cast<std::ptrdiff_t>(dataSize)};
		}

	private:
		struct Section
		{
			uint32_t VirtualAddress;
			uint32_t RawSize;
			uint32_t RawOffset;
		};

		const std::vector<uint8_t>& Data;
		std::vector<Section> Sections;
		uint32_t ResourceRva{};
		uint32_t ResourceSize{};
		uint64_t ResourceBase{};
		uint64_t ResourceLength{};

		// Resource offsets are relative to the start of the resource directory and must stay inside it
		bool InResource(uint64_t offset, uint64_t size) const
		{
			return offset <= ResourceLength && size <= ResourceLength - offset;
		}

		// Find the first entry of a directory whose name or ID matches, and return where it points to
		template <typename Matches>
		bool FindEntry(uint32_t directory, Matches matches, bool wantSubdirectory, uint32_t& target) const
		{
			uint16_t namedCount;
			uint16_t idCount;
			if (!InResource(directory, 16) || !U16(ResourceBase + directory + 12, namedCount) ||
				!U16(ResourceBase + directory + 14, idCount))
				return false;
			for (uint32_t index = 0; index < uint32_t{namedCount} + idCount; index++)
			{
				uint64_t entry = uint64_t{directory} + 16 + 8ULL * index;
				uint32_t id;
				uint32_t pointer;
				if (!InResource(entry, 8) || !U32(ResourceBase + entry, id) || !U32(ResourceBase + entry + 4, pointer))
					return false;
				if (!matches(id))
					continue;
				if (((pointer & SubdirectoryFlag) != 0) != wantSubdirectory)
					return false;
				target = pointer & ~SubdirectoryFlag;
				return true;
			}
			return false;
		}

		// An entry with a string name has the flag set and points to a length-prefixed UTF-16 string
		bool NameEquals(uint32_t id, const std::string& name) const
		{
			if ((id & SubdirectoryFlag) == 0)
				return false;
			uint32_t offset = id & ~SubdirectoryFlag;
			uint16_t length;
			if (!InResource(offset, 2) || !U16(ResourceBase + offset, length) || length != name.size() ||
				!InResource(uint64_t{offset} + 2, 2ULL * length))
				return false;
			for (uint16_t index = 0; index < length; index++)
			{
				uint16_t c;
				if (!U16(ResourceBase + offset + 2 + 2ULL * index, c) || c >= 128 ||
					std::toupper(c) != std::toupper(static_cast<unsigned char>(name[index])))
					return false;
			}
			return true;
		}
	};
} // namespace

// Reads the file, unless it is larger than MaxFileSize, and looks the resource up in it; empty when it can't be read
std::vector<uint8_t> PeResource::Load(const std::string& path, uint16_t type, const std::string& name)
{
	auto file = fopenu(path.c_str(), "rb");
	if (!file)
		return {};

	std::vector<uint8_t> image;
	if (fseek(file, 0, SEEK_END) == 0)
	{
		auto size = ftell(file);
		if (size > 0 && size <= MaxFileSize && fseek(file, 0, SEEK_SET) == 0)
		{
			image.resize(static_cast<size_t>(size));
			if (fread(image.data(), 1, image.size(), file) != image.size())
				image.clear();
		}
	}
	fclose(file);
	return Find(image, type, name);
}

// Parses the image as a PE file and returns the resource's data; empty when it isn't one or has no such resource
std::vector<uint8_t> PeResource::Find(const std::vector<uint8_t>& image, uint16_t type, const std::string& name)
{
	PeImage pe(image);
	if (!pe.ParseHeaders())
		return {};
	return pe.FindResource(type, name);
}
