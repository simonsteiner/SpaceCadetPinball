#include "pch.h"
#include "partman.h"

#include "gdrv.h"
#include "GroupData.h"
#include "zdrv.h"

short partman::_field_size[] =
{
	2, -1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0
};

DatFile* partman::load_records(LPCSTR lpFileName, bool fullTiltMode)
{
	datFileHeader header{};
	dat8BitBmpHeader bmpHeader{};
	dat16BitBmpHeader zMapHeader{};

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
			// A field type the format doesn't have, or a field too large to be one, means the file is damaged; the
			// load stops rather than size anything from it
			auto typeIndex = LRead<uint8_t>(fileHandle, abort);
			if (abort || typeIndex >= std::size(_field_size))
			{
				abort = true;
				break;
			}
			auto entryData = new EntryData();
			auto entryType = static_cast<FieldTypes>(typeIndex);
			entryData->EntryType = entryType;

			int fixedSize = _field_size[typeIndex];
			size_t fieldSize = fixedSize >= 0 ? fixedSize : LRead<uint32_t>(fileHandle, abort);
			entryData->FieldSize = static_cast<int>(fieldSize);
			if (abort || !ValidFieldSize(entryType, fieldSize))
			{
				abort = true;
				delete entryData;
				break;
			}

			if (entryType == FieldTypes::Bitmap8bit)
			{
				Read(fileHandle, &bmpHeader, sizeof(dat8BitBmpHeader), abort);
				if (abort || !ValidBitmapHeader(bmpHeader, fieldSize))
				{
					// The header is partly read or doesn't fit its field; don't size a bitmap from it
					abort = true;
					delete entryData;
					break;
				}

				auto bmp = new gdrv_bitmap8(bmpHeader);
				entryData->Buffer = reinterpret_cast<char*>(bmp);
				Read(fileHandle, bmp->IndexedBmpPtr, bmpHeader.Size, abort);
			}
			else if (entryType == FieldTypes::Bitmap16bit)
			{
				/*Full tilt has extra byte(@0:resolution) in zMap*/
				auto zMapResolution = 0u;
				if (fullTiltMode)
				{
					zMapResolution = LRead<uint8_t>(fileHandle, abort);
					fieldSize--;

					// -1 means universal resolution, maybe. FT demo .006 is the only known user.	
					if (zMapResolution == 0xff)
						zMapResolution = 0;
				}

				Read(fileHandle, &zMapHeader, sizeof(dat16BitBmpHeader), abort);
				// A group keeps a zMap for each of the three resolutions
				if (abort || zMapResolution > 2 || fieldSize < sizeof(dat16BitBmpHeader) || zMapHeader.Width < 0 ||
					zMapHeader.Height < 0 || zMapHeader.Stride < zMapHeader.Width)
				{
					// The header is partly read or doesn't fit its field; don't size a zMap from it
					abort = true;
					delete entryData;
					break;
				}
				auto length = fieldSize - sizeof(dat16BitBmpHeader);

				zmap_header_type* zMap;
				if (zMapHeader.Stride * zMapHeader.Height * 2u == length)
				{
					zMap = new zmap_header_type(zMapHeader.Width, zMapHeader.Height, zMapHeader.Stride);
					zMap->Resolution = zMapResolution;
					Read(fileHandle, zMap->ZPtr1, length, abort);
				}
				else
				{
					// 3DPB .dat has zeroed zMap headers, in groups 497 and 498, skip them.
					fseek(fileHandle, static_cast<int>(length), SEEK_CUR);
					zMap = new zmap_header_type(0, 0, 0);
				}
				entryData->Buffer = reinterpret_cast<char*>(zMap);
			}
			else
			{
				// Zeros after the field: the components and the loader read some arrays at fixed places, which a
				// damaged field can be too short for
				constexpr size_t slack = 64;
				auto entryBuffer = new char[fieldSize + slack]{};
				entryData->Buffer = entryBuffer;
				if (!entryBuffer)
				{
					abort = true;
					break;
				}
				Read(fileHandle, entryBuffer, fieldSize, abort);
				// Names and strings are read as C strings
				if ((entryType == FieldTypes::GroupName || entryType == FieldTypes::String) && !abort &&
					entryBuffer[fieldSize - 1] != 0)
					abort = true;
			}

			if (abort)
			{
				// AddEntry would parse the partly read buffer
				delete entryData;
				break;
			}
			groupData->AddEntry(entryData);
		}

		datFile->Groups.push_back(groupData);
	}

	fclose(fileHandle);
	if (!abort && datFile->Groups.size() == header.NumberOfGroups)
	{
		datFile->Finalize();
		return datFile;
	}
	delete datFile;
	return nullptr;
}

bool partman::ValidFieldSize(FieldTypes type, size_t size)
{
	// No field of a real DAT comes near this; a larger size is damage, not data
	constexpr size_t largest = 16 * 1024 * 1024;
	if (size > largest)
		return false;
	switch (type)
	{
	case FieldTypes::Bitmap8bit:
		return size >= sizeof(dat8BitBmpHeader);
	case FieldTypes::Bitmap16bit:
		// Full Tilt!'s zMaps have a byte for their resolution before the header
		return size >= sizeof(dat16BitBmpHeader);
	case FieldTypes::GroupName:
	case FieldTypes::String:
		return size >= 1;
	case FieldTypes::Palette:
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
	// A spliced bitmap is packed runs, which GroupData reads within its size; any other holds every row, padded to 4
	if (header.IsFlagSet(bmp8Flags::Spliced))
		return true;
	auto stride = (header.Width + 3) / 4 * 4;
	return header.Size == stride * header.Height;
}
