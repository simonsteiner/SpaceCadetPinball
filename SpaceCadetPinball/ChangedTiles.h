#pragma once

// Finds what changed in a picture since the copy of it that was last uploaded, in square tiles, so only those parts go
// to the GPU again and only they are composed again (render, Compositor).
namespace ChangedTiles
{
	// In pixels. Small enough that a blinking lamp or a flipper costs a few tiles, large enough for few uploads.
	constexpr int Size = 64;

	// Compares the tiles that touch area, row of tiles by row of tiles, and calls changed with each run of changed
	// tiles, as a rectangle within the picture, after which the copy holds the picture there. The picture's rows are
	// stride pixels apart, the copy's width pixels.
	template <typename Pixel, typename Changed>
	void ForEachRun(const Pixel* picture, int stride, Pixel* copy, int width, int height, const SDL_Rect& area,
					Changed&& changed)
	{
		const auto left = std::max(area.x, 0) / Size * Size;
		const auto top = std::max(area.y, 0) / Size * Size;
		const auto right = std::min(area.x + area.w, width);
		const auto bottom = std::min(area.y + area.h, height);
		for (int tileY = top; tileY < bottom; tileY += Size)
		{
			const auto rows = std::min(Size, height - tileY);
			auto runStart = -1;
			// One step past the last tile, to end a run that reaches the right edge
			for (int tileX = left; tileX < right + Size; tileX += Size)
			{
				auto differs = false;
				const auto columns = tileX < right ? std::min(Size, width - tileX) : 0;
				for (int y = tileY; columns > 0 && y < tileY + rows; y++)
				{
					if (std::memcmp(&picture[static_cast<size_t>(y) * stride + tileX],
									&copy[static_cast<size_t>(y) * width + tileX], columns * sizeof(Pixel)) != 0)
					{
						differs = true;
						break;
					}
				}
				if (differs)
				{
					if (runStart < 0)
						runStart = tileX;
					continue;
				}
				if (runStart < 0)
					continue;

				const SDL_Rect run{runStart, tileY, std::min(tileX, width) - runStart, rows};
				for (int y = run.y; y < run.y + run.h; y++)
				{
					std::memcpy(&copy[static_cast<size_t>(y) * width + run.x],
								&picture[static_cast<size_t>(y) * stride + run.x], run.w * sizeof(Pixel));
				}
				changed(run);
				runStart = -1;
			}
		}
	}
} // namespace ChangedTiles
