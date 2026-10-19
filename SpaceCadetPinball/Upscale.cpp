#include "pch.h"
#include "Upscale.h"

#include "fullscrn.h"
#include "GroupData.h"
#include "pb.h"
#include "mmpx.h"

int Upscale::CurrentFactor = 0;
std::unordered_map<const gdrv_bitmap8*, std::unique_ptr<gdrv_bitmap8>> Upscale::Bitmaps{};
std::unordered_map<const zmap_header_type*, std::unique_ptr<zmap_header_type>> Upscale::ZMaps{};
std::unordered_map<const gdrv_bitmap8*, std::vector<std::string>> Upscale::SpriteNames{};

static_assert(sizeof(ColorRgba) == sizeof(uint32_t), "MMPX works on 32-bit ARGB pixels");

void Upscale::Init(int factor)
{
	if (factor != CurrentFactor)
		Clear();
	CurrentFactor = factor;
}

void Upscale::Clear()
{
	Bitmaps.clear();
	ZMaps.clear();
	SpriteNames.clear();
	CurrentFactor = 0;
}

std::vector<ColorRgba> Upscale::Magnify(const ColorRgba* pixels, int width, int height, int factor)
{
	// MMPX doubles the size; 4x is two passes
	std::vector<ColorRgba> src(pixels, pixels + width * height), dst;
	for (; factor > 1; factor /= 2, width *= 2, height *= 2)
	{
		dst.resize(static_cast<size_t>(width) * height * 4);
		mmpx_scale2x(reinterpret_cast<const uint32_t*>(src.data()), reinterpret_cast<uint32_t*>(dst.data()),
					 static_cast<uint32_t>(width), static_cast<uint32_t>(height));
		src.swap(dst);
	}
	return src;
}

gdrv_bitmap8* Upscale::Bitmap(gdrv_bitmap8* bmp)
{
	if (!bmp || !CurrentFactor)
		return bmp;
	auto found = Bitmaps.find(bmp);
	if (found != Bitmaps.end())
		return found->second.get();

	auto result = LoadOverride(*bmp);
	if (!result)
	{
		result = new gdrv_bitmap8(bmp->Width * CurrentFactor, bmp->Height * CurrentFactor, false);
		if (bmp->Width > 0 && bmp->Height > 0)
		{
			auto pixels = Magnify(bmp->BmpBufPtr1, bmp->Width, bmp->Height, CurrentFactor);
			std::copy(pixels.begin(), pixels.end(), result->BmpBufPtr1);
		}
	}
	result->XPosition = bmp->XPosition * CurrentFactor;
	result->YPosition = bmp->YPosition * CurrentFactor;
	Bitmaps[bmp].reset(result);
	return result;
}

zmap_header_type* Upscale::ZMap(zmap_header_type* zMap)
{
	if (!zMap || !CurrentFactor)
		return zMap;
	auto found = ZMaps.find(zMap);
	if (found != ZMaps.end())
		return found->second.get();

	// Nearest neighbour, so every depth step stays where the original has it
	auto factor = CurrentFactor;
	auto result = new zmap_header_type(zMap->Width * factor, zMap->Height * factor, zMap->Width * factor);
	for (int y = 0; y < result->Height; y++)
	{
		auto src = &zMap->ZPtr1[(y / factor) * zMap->Stride];
		auto dst = &result->ZPtr1[y * result->Stride];
		for (int x = 0; x < result->Width; x++)
			dst[x] = src[x / factor];
	}
	ZMaps[zMap].reset(result);
	return result;
}

void Upscale::Region(const gdrv_bitmap8& src, int x, int y, int width, int height, gdrv_bitmap8& dst)
{
	if (width <= 0 || height <= 0)
		return;
	std::vector<ColorRgba> pixels(static_cast<size_t>(width) * height);
	for (int row = 0; row < height; row++)
		std::copy_n(&src.BmpBufPtr1[(y + row) * src.Stride + x], width, &pixels[row * width]);

	auto factor = CurrentFactor;
	auto magnified = Magnify(pixels.data(), width, height, factor);
	for (int row = 0; row < height * factor; row++)
		std::copy_n(&magnified[row * width * factor], width * factor,
					&dst.BmpBufPtr1[(y * factor + row) * dst.Stride + x * factor]);
}

std::string Upscale::OverrideFolder()
{
	auto prefPath = SDL_GetPrefPath("", "SpaceCadetPinball");
	std::string folder = prefPath ? prefPath : "";
	SDL_free(prefPath);
	return folder + "sprites" + PathSeparator;
}

std::string Upscale::OriginalsFolder()
{
	return OverrideFolder() + "originals" + PathSeparator;
}

static std::string SpriteFileName(int groupId, std::string name, int resolution)
{
	for (auto& ch : name)
	{
		if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '-')
			ch = '_';
	}
	char prefix[16];
	snprintf(prefix, sizeof prefix, "%04d-", groupId);
	return prefix + name + "-r" + std::to_string(resolution) + ".png";
}

void Upscale::FindSpriteNames()
{
	// A sprite's file name is its group's number and name and the table resolution, such as 0108-ball-r0.png.
	// A component with several bitmaps keeps them in the groups after its own, which have no name; those take the
	// component's number and name with the bitmap's index, so the ball's sizes are 0108-ball-r0.png, then
	// 0108-ball_1-r0.png to 0108-ball_6-r0.png. Their own number with an empty name, such as 0109--r0.png, is
	// what earlier versions looked for, so it still works.
	if (!SpriteNames.empty() || !pb::record_table)
		return;
	auto resolution = fullscrn::GetResolution();
	const GroupData* owner = nullptr;
	int stateIndex = 0;
	for (auto group : pb::record_table->Groups)
	{
		auto& entries = group->GetEntries();
		auto kind = !entries.empty() && entries[0]->EntryType == FieldTypes::ShortValue
						? *reinterpret_cast<const int16_t*>(entries[0]->Buffer)
						: 0;
		if (kind == 200)
		{
			owner = group->GroupName.empty() ? nullptr : group;
			stateIndex = 0;
		}
		else if (kind == 201 && owner)
			stateIndex++;
		else
			owner = nullptr;

		auto bmp = group->GetBitmap(resolution);
		if (!bmp)
			continue;
		auto& names = SpriteNames[bmp];
		if (kind == 201 && owner && group->GroupName.empty())
			names.push_back(
				SpriteFileName(owner->GroupId, owner->GroupName + "_" + std::to_string(stateIndex), resolution));
		names.push_back(SpriteFileName(group->GroupId, group->GroupName, resolution));
	}
}

gdrv_bitmap8* Upscale::LoadOverride(const gdrv_bitmap8& bmp)
{
#if SDL_VERSION_ATLEAST(3, 4, 0)
	FindSpriteNames();
	auto names = SpriteNames.find(&bmp);
	if (names == SpriteNames.end())
		return nullptr;

	std::string path;
	SDL_IOStream* io = nullptr;
	for (auto& name : names->second)
	{
		path = OverrideFolder() + name;
		io = SDL_IOFromFile(path.c_str(), "rb");
		if (io)
			break;
	}
	if (!io)
	{
		SDL_ClearError();
		return nullptr;
	}
	auto surface = SDL_LoadPNG_IO(io, true);
	auto converted = surface ? SDL_ConvertSurface(surface, SDL_PIXELFORMAT_BGRA32) : nullptr;
	SDL_DestroySurface(surface);
	if (!converted)
	{
		printf("Could not load sprite %s: %s\n", path.c_str(), SDL_GetError());
		SDL_ClearError();
		return nullptr;
	}

	gdrv_bitmap8* result = nullptr;
	if (converted->w == bmp.Width * CurrentFactor && converted->h == bmp.Height * CurrentFactor)
	{
		result = new gdrv_bitmap8(converted->w, converted->h, false);
		for (int y = 0; y < converted->h; y++)
		{
			auto row = reinterpret_cast<const ColorRgba*>(static_cast<const uint8_t*>(converted->pixels) +
														  static_cast<size_t>(y) * converted->pitch);
			// The frame has no blending and paint_flat skips only colour 0, so a pixel is either transparent or
			// opaque: mostly transparent becomes 0, whatever colour it hides, the rest is drawn solid
			auto dst = &result->BmpBufPtr1[y * result->Stride];
			for (int x = 0; x < converted->w; x++)
			{
				dst[x] = row[x];
				if (row[x].GetAlpha() < 128)
					dst[x] = ColorRgba{0};
				else
					dst[x].SetAlpha(255);
			}
		}
	}
	else
	{
		printf("Ignoring sprite %s: it is %dx%d, but at %dx it must be %dx%d\n", path.c_str(), converted->w,
			   converted->h, CurrentFactor, bmp.Width * CurrentFactor, bmp.Height * CurrentFactor);
	}
	SDL_DestroySurface(converted);
	return result;
#else
	return nullptr;
#endif
}

int Upscale::ExportOriginals()
{
	// The original sprites, for upscaling elsewhere; they come from the player's own DAT and stay on their machine
#if SDL_VERSION_ATLEAST(3, 4, 0)
	FindSpriteNames();
	if (!pb::record_table)
		return 0;
	auto folder = OriginalsFolder();
	if (!SDL_CreateDirectory(folder.c_str()))
	{
		printf("Could not create %s: %s\n", folder.c_str(), SDL_GetError());
		SDL_ClearError();
		return 0;
	}

	int count = 0;
	auto resolution = fullscrn::GetResolution();
	for (auto group : pb::record_table->Groups)
	{
		auto bmp = group->GetBitmap(resolution);
		auto names = SpriteNames.find(bmp);
		if (!bmp || names == SpriteNames.end() || bmp->Width <= 0 || bmp->Height <= 0)
			continue;
		auto surface = SDL_CreateSurfaceFrom(bmp->Width, bmp->Height, SDL_PIXELFORMAT_BGRA32, bmp->BmpBufPtr1,
											 bmp->Stride * static_cast<int>(sizeof(ColorRgba)));
		if (surface && SDL_SavePNG(surface, (folder + names->second.front()).c_str()))
			count++;
		SDL_DestroySurface(surface);
	}
	SDL_ClearError();
	printf("Exported %d sprites to %s\n", count, folder.c_str());
	return count;
#else
	return 0;
#endif
}
