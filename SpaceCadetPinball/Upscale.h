#pragma once
#include "gdrv.h"
#include "zdrv.h"

// Upscaled copies of the table's art, made on the player's machine from their own DAT file and never stored.
// Colours are magnified with MMPX, depth maps with nearest neighbour, so depth steps stay exact.
// A PNG in the override folder replaces the generated copy of its sprite.
class Upscale
{
public:
	static constexpr int MinFactor = 2, MaxFactor = 4;

	static void Init(int factor);
	static void Clear();
	static int Factor()
	{
		return CurrentFactor;
	}
	static gdrv_bitmap8* Bitmap(gdrv_bitmap8* bmp);
	static zmap_header_type* ZMap(zmap_header_type* zMap);
	// Magnifies a rectangle of src into dst, which is Factor() times larger, at the matching position
	static void Region(const gdrv_bitmap8& src, int x, int y, int width, int height, gdrv_bitmap8& dst);
	static std::string OverrideFolder();
	// Where ExportOriginals writes the original sprites
	static std::string OriginalsFolder();
	// Needs SDL 3.4 or newer, like the overrides; returns how many sprites were written
	static int ExportOriginals();

private:
	static int CurrentFactor;
	static std::unordered_map<const gdrv_bitmap8*, std::unique_ptr<gdrv_bitmap8>> Bitmaps;
	static std::unordered_map<const zmap_header_type*, std::unique_ptr<zmap_header_type>> ZMaps;
	// The file names an override of each sprite may have; ExportOriginals writes the first
	static std::unordered_map<const gdrv_bitmap8*, std::vector<std::string>> SpriteNames;

	static std::vector<ColorRgba> Magnify(const ColorRgba* pixels, int width, int height, int factor);
	static void FindSpriteNames();
	static gdrv_bitmap8* LoadOverride(const gdrv_bitmap8& bmp);
};
