#include "pch.h"
#include "partman.h"

#include "gdrv.h"
#include "GroupData.h"
#include "zdrv.h"

short partman::_field_size[] =
{
	2, -1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0
};

DatFile* partman::load_records(LPCSTR lpFileName, bool fullTiltMode, bool msgFont)
{
	datFileHeader header{};

	auto fileHandle = fopenu(lpFileName, "rb");
	if (fileHandle == nullptr)
		return nullptr;

	if (fread(&header, 1, sizeof header, fileHandle) != sizeof header ||
		strcmp("PARTOUT(4.0)RESOURCE", header.FileSignature) != 0)
	{
		fclose(fileHandle);
		return nullptr;
	}

	auto datFile = new DatFile();
	if (!datFile)
	{
		fclose(fileHandle);
		return nullptr;
	}

	// Neither has to end in a zero inside its field
	datFile->AppName.assign(header.AppName, strnlen(header.AppName, sizeof header.AppName));
	datFile->Description.assign(header.Description, strnlen(header.Description, sizeof header.Description));

	if (header.Unknown)
	{
		auto unknownBuf = new char[header.Unknown];
		if (!unknownBuf)
		{
			fclose(fileHandle);
			delete datFile;
			return nullptr;
		}
		auto unknownRead = fread(unknownBuf, 1, header.Unknown, fileHandle) == header.Unknown;
		delete[] unknownBuf;
		if (!unknownRead)
		{
			fclose(fileHandle);
			delete datFile;
			return nullptr;
		}
	}

	datFile->Groups.reserve(header.NumberOfGroups);
	bool abort = false;
	for (auto groupIndex = 0; !abort && groupIndex < header.NumberOfGroups; ++groupIndex)
	{
		auto entryCount = LRead<uint8_t>(fileHandle, abort);
		auto groupData = new GroupData(groupIndex);
		groupData->ReserveEntries(entryCount);

		for (auto entryIndex = 0; entryIndex < entryCount; ++entryIndex)
		{
			auto entryData = ReadEntry(fileHandle, fullTiltMode);
			if (!entryData)
			{
				abort = true;
				break;
			}
			groupData->AddEntry(entryData);
		}

		datFile->Groups.push_back(groupData);
	}

	fclose(fileHandle);
	if (!abort && datFile->Groups.size() == header.NumberOfGroups)
	{
		datFile->Finalize(!fullTiltMode && msgFont);
		return datFile;
	}
	delete datFile;
	return nullptr;
}

EntryData* partman::ReadEntry(FILE* file, bool fullTiltMode)
{
	// A field type the format doesn't have, or a field too large to be one, means the file is damaged; the load stops
	// rather than size anything from it
	auto failed = false;
	auto typeIndex = LRead<uint8_t>(file, failed);
	if (failed || typeIndex >= std::size(_field_size))
		return nullptr;
	auto entryType = static_cast<FieldTypes>(typeIndex);
	int fixedSize = _field_size[typeIndex];
	size_t fieldSize = fixedSize >= 0 ? fixedSize : LRead<uint32_t>(file, failed);
	if (failed || !ValidFieldSize(entryType, fieldSize))
		return nullptr;

	auto entryData = new EntryData();
	entryData->EntryType = entryType;
	entryData->FieldSize = static_cast<int>(fieldSize);
	bool read;
	if (entryType == FieldTypes::Bitmap8bit)
		read = ReadBitmap(file, *entryData, fieldSize);
	else if (entryType == FieldTypes::Bitmap16bit)
		read = ReadZMap(file, *entryData, fieldSize, fullTiltMode);
	else
		read = ReadField(file, *entryData, fieldSize);
	if (!read)
	{
		// AddEntry would parse the partly read buffer
		delete entryData;
		return nullptr;
	}
	return entryData;
}

bool partman::ReadBitmap(FILE* file, EntryData& entry, size_t fieldSize)
{
	// Not value-initialized: Flags has no zero enumerator, and the read fills every byte or fails
	dat8BitBmpHeader header;
	auto failed = false;
	Read(file, &header, sizeof header, failed);
	// The header is partly read or doesn't fit its field; don't size a bitmap from it
	if (failed || !ValidBitmapHeader(header, fieldSize))
		return false;

	auto bmp = new gdrv_bitmap8(header);
	// EntryData::Buffer holds every field type as char*; GroupData::AddEntry and ~EntryData cast it back by
	// EntryType, and its bytes are never read through it
	entry.Buffer = reinterpret_cast<char*>(bmp);
	Read(file, bmp->IndexedBmpPtr, header.Size, failed);
	return !failed;
}

bool partman::ReadZMap(FILE* file, EntryData& entry, size_t fieldSize, bool fullTiltMode)
{
	/*Full tilt has extra byte(@0:resolution) in zMap*/
	auto failed = false;
	auto resolution = 0u;
	if (fullTiltMode)
	{
		resolution = LRead<uint8_t>(file, failed);
		fieldSize--;

		// -1 means universal resolution, maybe. FT demo .006 is the only known user.
		if (resolution == 0xff)
			resolution = 0;
	}

	dat16BitBmpHeader header{};
	Read(file, &header, sizeof header, failed);
	// A group keeps a zMap for each of the three resolutions. A header partly read or not fitting its field doesn't
	// size a zMap.
	if (failed || resolution > 2 || fieldSize < sizeof header || header.Width < 0 || header.Height < 0 ||
		header.Stride < header.Width)
		return false;
	auto length = fieldSize - sizeof header;

	zmap_header_type* zMap;
	if (header.Stride * header.Height * 2u == length)
	{
		zMap = new zmap_header_type(header.Width, header.Height, header.Stride);
		zMap->Resolution = resolution;
		Read(file, zMap->ZPtr1, length, failed);
	}
	else
	{
		// 3DPB .dat has zeroed zMap headers, in groups 497 and 498, skip them.
		fseek(file, static_cast<int>(length), SEEK_CUR);
		zMap = new zmap_header_type(0, 0, 0);
	}
	// Stored as char* like the bitmap in ReadBitmap, and cast back by EntryType
	entry.Buffer = reinterpret_cast<char*>(zMap);
	return !failed;
}

bool partman::ReadField(FILE* file, EntryData& entry, size_t fieldSize)
{
	// Zeros after the field: the components and the loader read some arrays at fixed places, which a damaged field
	// can be too short for
	constexpr size_t slack = 64;
	auto buffer = new char[fieldSize + slack]{};
	entry.Buffer = buffer;
	auto failed = false;
	Read(file, buffer, fieldSize, failed);
	// Names and strings are read as C strings
	auto cString = entry.EntryType == FieldTypes::GroupName || entry.EntryType == FieldTypes::String;
	return !failed && !(cString && buffer[fieldSize - 1] != 0);
}

bool partman::ValidFieldSize(FieldTypes type, size_t size)
{
	using enum FieldTypes;
	if (size > LargestField)
		return false;
	switch (type)
	{
	case Bitmap8bit:
		return size >= sizeof(dat8BitBmpHeader);
	case Bitmap16bit:
		// Full Tilt!'s zMaps have a byte for their resolution before the header
		return size >= sizeof(dat16BitBmpHeader);
	case GroupName:
	case String:
		return size >= 1;
	case Palette:
		// 256 colours, the game reading entries 10 to 245
		return size >= 256 * sizeof(uint32_t);
	default:
		return true;
	}
}

bool partman::ValidBitmapHeader(const dat8BitBmpHeader& header, size_t fieldSize)
{
	// A group keeps a bitmap for each of the three resolutions
	if (header.Resolution > 2 || header.Width < 0 || header.Height < 0 || header.Size < 0 ||
		static_cast<size_t>(header.Size) + sizeof(dat8BitBmpHeader) != fieldSize)
		return false;
	// A spliced bitmap is packed runs, which GroupData reads within its size and unpacks to its full size, so that
	// size gets the same bound as a field; any other bitmap holds every row, padded to 4
	if (header.IsFlagSet(bmp8Flags::Spliced))
		return static_cast<size_t>(header.Width) * static_cast<size_t>(header.Height) <= LargestField;
	auto stride = (header.Width + 3) / 4 * 4;
	return header.Size == stride * header.Height;
}
