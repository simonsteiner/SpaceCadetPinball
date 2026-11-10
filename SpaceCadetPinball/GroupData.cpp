#include "pch.h"

#include "GroupData.h"

#include "fullscrn.h"
#include "gdrv.h"
#include "partman.h"
#include "pb.h"
#include "PeResource.h"
#include "zdrv.h"


// Frees the buffer as what the entry's type says it holds: a bitmap, a depth map or a plain array
EntryData::~EntryData()
{
	if (Buffer)
	{
		if (EntryType == FieldTypes::Bitmap8bit)
		{
			delete reinterpret_cast<gdrv_bitmap8*>(Buffer);
		}
		else if (EntryType == FieldTypes::Bitmap16bit)
		{
			delete reinterpret_cast<zmap_header_type*>(Buffer);
		}
		else
			delete[] Buffer;
	}
}


GroupData::GroupData(int groupId)
{
	GroupId = groupId;
}

// Adds an entry to the group, which owns it from then on, and notes its name, bitmap or depth map. A spliced bitmap is
// replaced by the plain bitmap and the depth map it unpacks to.
void GroupData::AddEntry(EntryData* entry)
{
	auto addEntry = true;
	switch (entry->EntryType)
	{
	case FieldTypes::GroupName:
		GroupName = entry->Buffer;
		break;
	case FieldTypes::Bitmap8bit:
		{
			auto srcBmp = reinterpret_cast<gdrv_bitmap8*>(entry->Buffer);
			if (srcBmp->BitmapType == BitmapTypes::Spliced)
			{
				// Get rid of spliced bitmap early on, to simplify render pipeline
				auto bmp = new gdrv_bitmap8(srcBmp->Width, srcBmp->Height, true);
				auto zMap = new zmap_header_type(srcBmp->Width, srcBmp->Height, srcBmp->Width);
				// The runs fill the field after the bitmap's header
				SplitSplicedBitmap(*srcBmp, static_cast<size_t>(entry->FieldSize) - sizeof(dat8BitBmpHeader), *bmp,
								   *zMap);

				NeedsSort = true;
				addEntry = false;
				AddEntry(new EntryData(FieldTypes::Bitmap8bit, reinterpret_cast<char*>(bmp)));
				AddEntry(new EntryData(FieldTypes::Bitmap16bit, reinterpret_cast<char*>(zMap)));
				delete entry;
			}
			else
			{
				SetBitmap(srcBmp);
			}
			break;
		}
	case FieldTypes::Bitmap16bit:
		{
			SetZMap(reinterpret_cast<zmap_header_type*>(entry->Buffer));
			break;
		}
	default: break;
	}

	if (addEntry)
		Entries.push_back(entry);
}

// Sorts the entries by type again when unpacking a spliced bitmap has put them out of order
void GroupData::FinalizeGroup()
{
	if (NeedsSort)
	{
		// Entries within a group are sorted by EntryType, in ascending order.
		// Dat files follow this rule, zMaps inserted in the middle break it.
		NeedsSort = false;
		std::sort(Entries.begin(), Entries.end(), [](const EntryData* lhs, const EntryData* rhs)
		{
			return lhs->EntryType < rhs->EntryType;
		});
		Entries.shrink_to_fit();
	}
}

gdrv_bitmap8* GroupData::GetBitmap(int resolution) const
{
	return Bitmaps[resolution];
}

gdrv_bitmap8* GroupData::GetAltBitmap(int resolution) const
{
	return AltBitmaps[resolution];
}

zmap_header_type* GroupData::GetZMap(int resolution) const
{
	return ZMaps[resolution];
}

// Unpacks a spliced bitmap's runs of depth and colour index pairs into a bitmap and a depth map of its full size.
// Pixels no run covers keep index 0xff and depth 0xFFFF and are marked as holes in the depth map.
void GroupData::SplitSplicedBitmap(const gdrv_bitmap8& srcBmp, size_t srcSize, gdrv_bitmap8& bmp,
								   zmap_header_type& zMap)
{
	assertm(srcBmp.BitmapType == BitmapTypes::Spliced, "GroupData: wrong bitmap type");

	std::memset(bmp.IndexedBmpPtr, 0xff, bmp.Stride * bmp.Height);
	bmp.XPosition = srcBmp.XPosition;
	bmp.YPosition = srcBmp.YPosition;
	bmp.Resolution = srcBmp.Resolution;

	zdrv::fill(&zMap, zMap.Width, zMap.Height, 0, 0, 0xFFFF);
	zMap.Holes.assign(static_cast<size_t>(zMap.Stride) * zMap.Height, 1);
	zMap.Resolution = srcBmp.Resolution;

	auto tableWidth = fullscrn::resolution_array[srcBmp.Resolution].TableWidth;
	// One sprite of DRAGON.DAT, marked for 640 by 480, skips rows as if laid out for 800 by 600 and runs past its
	// end; its pixels outside the bitmap are dropped instead of written past it
	auto end = std::min(bmp.Stride * bmp.Height, zMap.Stride * zMap.Height);
	// The data packs 16-bit values between single bytes, so the values are read wherever they fall. Damaged data that
	// runs past the field's end stops the runs there, as their end marker does.
	auto src = srcBmp.IndexedBmpPtr;
	auto srcEnd = src + srcSize;
	auto ended = false;
	auto read16 = [&src, srcEnd, &ended]
	{
		uint16_t value = 0xffff;
		if (srcEnd - src < static_cast<ptrdiff_t>(sizeof value))
		{
			ended = true;
			return value;
		}
		std::memcpy(&value, src, sizeof value);
		src += sizeof value;
		return value;
	};
	// The step to the next run, or a negative one when the runs end
	auto nextStride = [&]
	{
		auto stride = static_cast<int16_t>(read16());
		if (ended)
			return int16_t{-1};
		// Stride is in terms of dst stride, hardcoded to match vScreen width in current resolution. Only damaged data
		// steps backwards; its runs stop there
		if (stride > bmp.Width)
			stride = static_cast<int16_t>(stride + bmp.Width - tableWidth);
		return stride;
	};
	// A run's pixel count, then each pixel's depth and colour index
	auto copyRun = [&](int& dstInd)
	{
		auto count = read16();
		while (count > 0 && !ended)
		{
			auto depth = read16();
			// The colour index after the depth has to be within the field too
			ended = ended || src >= srcEnd;
			if (ended)
				return;
			if (dstInd >= 0 && dstInd < end)
			{
				bmp.IndexedBmpPtr[dstInd] = *src;
				zMap.ZPtr1[dstInd] = depth;
				zMap.Holes[dstInd] = 0;
			}
			src++;
			dstInd++;
			count--;
		}
	};
	auto dstInd = 0;
	for (auto stride = nextStride(); stride >= 0; stride = nextStride())
	{
		dstInd += stride;
		copyRun(dstInd);
	}
}

// Keeps a bitmap as the group's first of its resolution, or as its alternative when there already is a first; a depth
// map of another size than the first bitmap is dropped
void GroupData::SetBitmap(gdrv_bitmap8* bmp)
{
	// Full Tilt's bonus tables give each ball frame a second bitmap, an alternative ball their rules can switch to.
	// The game draws the first unless told otherwise (CADET.EXE's TBall), so a later one doesn't replace it; the
	// second is kept apart, for a ball made while DRAGON.EXE's PB cheat is on.
	if (Bitmaps[bmp->Resolution])
	{
		if (!AltBitmaps[bmp->Resolution])
			AltBitmaps[bmp->Resolution] = bmp;
		return;
	}
	Bitmaps[bmp->Resolution] = bmp;

	// A zMap of another size would be read at the bitmap's; a damaged table's sprite goes without depth instead
	auto zMap = ZMaps[bmp->Resolution];
	if (zMap && (bmp->Width != zMap->Width || bmp->Height != zMap->Height))
		ZMaps[bmp->Resolution] = nullptr;
}

// Flips a depth map to match the bitmaps and keeps it as the group's depth map of its resolution, unless there already
// is one or its size differs from the bitmap's
void GroupData::SetZMap(zmap_header_type* zMap)
{
	// Flip zMap to match with flipped non-indexed bitmaps
	zdrv::FlipZMapHorizontally(*zMap);

	// The first one wins, as for bitmaps
	if (ZMaps[zMap->Resolution])
		return;
	// As in SetBitmap, a zMap of another size than the bitmap isn't kept
	if (auto bmp = Bitmaps[zMap->Resolution]; bmp && (bmp->Width != zMap->Width || bmp->Height != zMap->Height))
		return;
	ZMaps[zMap->Resolution] = zMap;
}


// Deletes the groups and their entries
DatFile::~DatFile()
{
	for (auto group : Groups)
	{
		if (!group)
			continue;

		for (const auto entry : group->GetEntries())
		{
			delete entry;
		}
		delete group;
	}
}

// Returns the buffer of the group's first entry of a type, or null when the group has none or the index is outside the
// file. Not for bitmaps and depth maps, which have their own getters.
char* DatFile::field(int groupIndex, FieldTypes targetEntryType)
{
	assertm(targetEntryType != FieldTypes::Bitmap8bit && targetEntryType != FieldTypes::Bitmap16bit,
	        "partman: Use specific get for bitmaps");

	auto group = Group(groupIndex);
	if (!group)
		return nullptr;
	for (const auto entry : group->GetEntries())
	{
		if (entry->EntryType == targetEntryType)
		{
			return entry->Buffer;
		}
		if (entry->EntryType > targetEntryType)
			break;
	}
	return nullptr;
}


// As field, for the entry that follows skipFirstN others of the same type
char* DatFile::field_nth(int groupIndex, FieldTypes targetEntryType, int skipFirstN)
{
	assertm(targetEntryType != FieldTypes::Bitmap8bit && targetEntryType != FieldTypes::Bitmap16bit,
	        "partman: Use specific get for bitmaps");

	auto group = Group(groupIndex);
	if (!group)
		return nullptr;
	auto skipCount = 0;
	for (const auto entry : group->GetEntries())
	{
		if (entry->EntryType > targetEntryType)
			break;
		if (entry->EntryType == targetEntryType)
			if (skipCount++ == skipFirstN)
				return entry->Buffer;
	}
	return nullptr;
}

// Returns the size in bytes of the entry field_nth finds, or 0 when there is none
int DatFile::field_size_nth(int groupIndex, FieldTypes targetEntryType, int skipFirstN)
{
	auto group = Group(groupIndex);
	if (!group)
		return 0;
	auto skipCount = 0;
	for (const auto entry : group->GetEntries())
	{
		if (entry->EntryType > targetEntryType)
			return 0;
		if (entry->EntryType == targetEntryType)
			if (skipCount++ == skipFirstN)
				return entry->FieldSize;
	}
	return 0;
}

int DatFile::field_size(int groupIndex, FieldTypes targetEntryType)
{
	return field_size_nth(groupIndex, targetEntryType, 0);
}

// The group at an index, or null when the index is outside the file
GroupData* DatFile::Group(int groupIndex) const
{
	// Group indices also come from the table's own data, which a damaged file can point anywhere
	return groupIndex >= 0 && groupIndex < static_cast<int>(Groups.size()) ? Groups[groupIndex] : nullptr;
}

// Returns the index of the group with exactly this name, the last such group in the file, or -1
int DatFile::record_labeled(LPCSTR targetGroupName)
{
	auto targetLength = strlen(targetGroupName);
	for (int groupIndex = static_cast<int>(Groups.size()) - 1; groupIndex >= 0; --groupIndex)
	{
		auto groupName = field(groupIndex, FieldTypes::GroupName);
		if (!groupName)
			continue;

		auto index = 0u;
		for (; index < targetLength; index++)
			if (targetGroupName[index] != groupName[index])
				break;
		if (index == targetLength && !targetGroupName[index] && !groupName[index])
			return groupIndex;
	}

	return -1;
}

// Returns the first entry of a type in the group with this name, or null when the group or the entry is missing
char* DatFile::field_labeled(LPCSTR lpString, FieldTypes fieldType)
{
	auto groupIndex = record_labeled(lpString);
	return groupIndex < 0 ? nullptr : field(groupIndex, fieldType);
}

// The group's bitmap for the current resolution, or null when it has none or the index is outside the file
gdrv_bitmap8* DatFile::GetBitmap(int groupIndex)
{
	auto group = Group(groupIndex);
	return group ? group->GetBitmap(fullscrn::GetResolution()) : nullptr;
}

// The group's second bitmap for the current resolution, or null
gdrv_bitmap8* DatFile::GetAltBitmap(int groupIndex)
{
	auto group = Group(groupIndex);
	return group ? group->GetAltBitmap(fullscrn::GetResolution()) : nullptr;
}

// The group's depth map for the current resolution, or null
zmap_header_type* DatFile::GetZMap(int groupIndex)
{
	auto group = Group(groupIndex);
	return group ? group->GetZMap(fullscrn::GetResolution()) : nullptr;
}

// The 3D Pinball message font isn't in PINBALL.DAT but in pinball.exe, as the PBMSG_FT resource. Look for the
// executable next to the DAT in the usual letter cases and take the font from it. Without it, text boxes draw their
// text with ImGui.
std::vector<uint8_t> DatFile::LoadMsgFontFromExe()
{
	for (const char* exeName : {"PINBALL.EXE", "pinball.exe", "Pinball.exe"})
	{
		auto exePath = pb::make_path_name(exeName);
		auto exeFile = fopenu(exePath.c_str(), "rb");
		if (!exeFile)
			continue;
		fclose(exeFile);

		auto font = PeResource::Load(exePath, PeResource::TypeRcData, "PBMSG_FT");
		if (IsValidMsgFont(font))
		{
			printf("Loading message font from: %s\n", exePath.c_str());
			return font;
		}
		printf("No message font in %s, text boxes use the ImGui font\n", exePath.c_str());
		return {};
	}
	printf("No pinball.exe next to the game data for the message font, text boxes use the ImGui font\n");
	return {};
}

// Check that the font holds all the characters AddMsgFont reads, so a foreign resource can't make it overrun
bool DatFile::IsValidMsgFont(const std::vector<uint8_t>& data)
{
	constexpr auto headerSize = offsetof(MsgFont, Data);
	if (data.size() < headerSize)
		return false;
	auto font = reinterpret_cast<const MsgFont*>(data.data());
	if (font->Height <= 0)
		return false;
	size_t offset = headerSize;
	for (auto charInd = 32; charInd < 128; charInd++)
	{
		if (offset >= data.size())
			return false;
		uint8_t width = data[offset];
		size_t charSize = 1 + static_cast<size_t>(width) * static_cast<size_t>(font->Height);
		if (width != font->CharWidths[charInd] || charSize > data.size() - offset)
			return false;
		offset += charSize;
	}
	return true;
}

// Adds the message font from pinball.exe when asked to, then puts each group's entries in order
void DatFile::Finalize(bool addMsgFont)
{
	if (addMsgFont)
	{
		[[maybe_unused]] int groupIndex = record_labeled("pbmsg_ft");
		assertm(groupIndex < 0, "DatFile: pbmsg_ft is already in .dat");

		// Load 3DPB font into dat to simplify pipeline
		auto rcData = LoadMsgFontFromExe();
		if (!rcData.empty())
			AddMsgFont(reinterpret_cast<MsgFont*>(rcData.data()), "pbmsg_ft");

		// PINBALL2.MID is an alternative font provided in 3DPB data
		// Scaled down because it is too large for top text box
		/*auto file = pb::make_path_name("PINBALL2.MID");
		auto fileHandle = fopenu(file.c_str(), "rb");
		fseek(fileHandle, 0, SEEK_END);
		auto fileSize = static_cast<uint32_t>(ftell(fileHandle));
		auto rcData = reinterpret_cast<MsgFont*>(new uint8_t[fileSize]);
		fseek(fileHandle, 0, SEEK_SET);
		fread(rcData, 1, fileSize, fileHandle);
		fclose(fileHandle);
		auto groupId = Groups.back()->GroupId + 1u;
		AddMsgFont(rcData, "pbmsg_ft");
		delete[] rcData;
		for (auto i = groupId; i < Groups.size(); i++)
			Groups[i]->GetBitmap(0)->ScaleIndexed(0.84f, 0.84f);*/
	}

	for (auto group : Groups)
	{
		group->FinalizeGroup();
	}
}

// Appends a group for each of the font's characters 32 to 127, holding its bitmap with the rows in reverse order; the
// first of them also gets the font's name and its gap width
void DatFile::AddMsgFont(MsgFont* font, const std::string& fontName)
{
	auto groupId = Groups.back()->GroupId + 1;
	auto ptrToData = reinterpret_cast<char*>(font->Data);
	for (auto charInd = 32; charInd < 128; charInd++, groupId++)
	{
		auto curChar = reinterpret_cast<MsgFontChar*>(ptrToData);
		assertm(curChar->Width == font->CharWidths[charInd], "Score: mismatched font width");
		ptrToData += curChar->Width * font->Height + 1;

		auto bmp = new gdrv_bitmap8(curChar->Width, font->Height, true);
		auto srcPtr = curChar->Data;
		auto dstPtr = &bmp->IndexedBmpPtr[bmp->Stride * (bmp->Height - 1)];
		for (auto y = 0; y < font->Height; ++y)
		{
			memcpy(dstPtr, srcPtr, curChar->Width);
			srcPtr += curChar->Width;
			dstPtr -= bmp->Stride;
		}

		auto group = new GroupData(groupId);
		group->AddEntry(new EntryData(FieldTypes::Bitmap8bit, reinterpret_cast<char*>(bmp)));
		if (charInd == 32)
		{
			// First font group holds font name and gap width
			auto groupName = new char[fontName.length() + 1];
			strcpy(groupName, fontName.c_str());
			group->AddEntry(new EntryData(FieldTypes::GroupName, groupName));

			auto gaps = new char[2];
			*reinterpret_cast<int16_t*>(gaps) = font->GapWidth;
			group->AddEntry(new EntryData(FieldTypes::ShortArray, gaps));
		}
		else
		{
			auto groupName = new char[30];
			snprintf(groupName, 30, "char %d='%c'", charInd, charInd);
			group->AddEntry(new EntryData(FieldTypes::GroupName, groupName));
		}

		Groups.push_back(group);
	}
}
