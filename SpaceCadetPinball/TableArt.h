#pragma once

#include "gdrv.h"

class DatFile;
enum class Tables;
struct TableData;

// Art cut out of a table's own DAT file on the player's machine, for the table lists and Space Cadet 2026's score
// panel. It lives only in memory; nothing is written.
class TableArt
{
public:
	struct Image
	{
		int Width = 0, Height = 0;
		std::vector<ColorRgba> Pixels;

		bool Empty() const
		{
			return Pixels.empty();
		}
	};

	// A 120 by 120 picture for a table without one, each pixel the average of the area it covers. 3D Pinball's Space
	// Cadet gets the logo from its score panel; any other table its playfield, the "table" group's bitmap.
	static Image Picture(const TableData& data);
	// The art at the top of the table's score panel, for Space Cadet 2026's HUD, from the table's DAT file loaded;
	// empty for a table that has none known
	static Image PanelArt(Tables table, DatFile& dat);
	// MMPX, as Upscaled Art enlarges the table, by 2 or 4
	static Image Magnify(const Image& image, int factor);
	// A texture that scales smoothly, or null
	static SDL_Texture* Texture(SDL_Renderer* renderer, const Image& image);
};
