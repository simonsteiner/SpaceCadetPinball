#include "pch.h"
#include "TableArt.h"

#include "GroupData.h"
#include "partman.h"
#include "pb.h"
#include "Upscale.h"

namespace
{
	using Image = TableArt::Image;

	// An area of one of the table's bitmaps, read by palette index
	struct Area
	{
		const gdrv_bitmap8* Bmp;
		std::array<ColorRgba, 256> Palette;
		SDL_Rect Rect;

		uint8_t Index(int x, int y) const
		{
			// The DAT's bitmaps are stored bottom up
			auto row = reinterpret_cast<const uint8_t*>(Bmp->IndexedBmpPtr) +
					   static_cast<ptrdiff_t>(Bmp->IndexedStride) * (Bmp->Height - 1 - (Rect.y + y));
			return row[Rect.x + x];
		}

		// The colours the game sets from the palette, 10 to 245, opaque; the rest are Windows' own and black
		ColorRgba Color(uint8_t index) const
		{
			if (index < 10 || index >= 246)
				return ColorRgba::Black();
			auto color = Palette[index];
			return ColorRgba{color.GetRed(), color.GetGreen(), color.GetBlue(), 255};
		}

		Image Colors() const
		{
			Image image{Rect.w, Rect.h, std::vector<ColorRgba>(static_cast<size_t>(Rect.w) * Rect.h)};
			for (auto y = 0; y < Rect.h; y++)
				for (auto x = 0; x < Rect.w; x++)
					image.Pixels[static_cast<size_t>(y) * Rect.w + x] = Color(Index(x, y));
			return image;
		}

		// The average colour of the area from x0,y0 up to x1,y1, which are left out
		ColorRgba Average(int x0, int y0, int x1, int y1) const
		{
			unsigned red = 0;
			unsigned green = 0;
			unsigned blue = 0;
			unsigned count = 0;
			for (auto y = y0; y < y1; y++)
				for (auto x = x0; x < x1; x++)
				{
					auto color = Color(Index(x, y));
					red += color.GetRed();
					green += color.GetGreen();
					blue += color.GetBlue();
					count++;
				}
			return ColorRgba{static_cast<uint8_t>(red / count), static_cast<uint8_t>(green / count),
							 static_cast<uint8_t>(blue / count), 255};
		}
	};

	// The table's palette, copied out of the "background" group; none when the field is missing or too short
	std::optional<std::array<ColorRgba, 256>> ReadPalette(DatFile& dat)
	{
		std::array<ColorRgba, 256> palette{};
		auto groupIndex = dat.record_labeled("background");
		auto field = groupIndex < 0 ? nullptr : dat.field(groupIndex, FieldTypes::Palette);
		if (!field || dat.field_size(groupIndex, FieldTypes::Palette) < static_cast<int>(sizeof(palette)))
			return std::nullopt;
		std::memcpy(palette.data(), field, sizeof(palette));
		return palette;
	}

	// An area of a group's first bitmap, in the 600 by 416 frame, or all of it when the area is empty; none when the
	// bitmap is missing or doesn't cover the area
	std::optional<Area> Find(DatFile& dat, const char* group, SDL_Rect rect)
	{
		auto palette = ReadPalette(dat);
		auto groupIndex = dat.record_labeled(group);
		auto bmp = groupIndex < 0 ? nullptr : dat.Groups[groupIndex]->GetBitmap(0);
		if (!palette || !bmp || !bmp->IndexedBmpPtr || bmp->BitmapType == BitmapTypes::Spliced || bmp->Width <= 0 ||
			bmp->Height <= 0)
			return std::nullopt;
		if (rect.w <= 0 || rect.h <= 0)
			return Area{bmp, *palette, {0, 0, bmp->Width, bmp->Height}};
		rect.x -= bmp->XPosition;
		rect.y -= bmp->YPosition;
		if (rect.x < 0 || rect.y < 0 || rect.x + rect.w > bmp->Width || rect.y + rect.h > bmp->Height)
			return std::nullopt;
		return Area{bmp, *palette, rect};
	}

	// Whether a point of a mask, w by h, is set; false outside it
	bool At(const std::vector<uint8_t>& mask, int w, int h, int x, int y)
	{
		return x >= 0 && y >= 0 && x < w && y < h && mask[static_cast<size_t>(y) * w + x];
	}

	// How many of the 3 by 3 around a point of a mask are set
	int CountAround(const std::vector<uint8_t>& mask, int w, int h, int x, int y)
	{
		auto count = 0;
		for (auto dy = -1; dy <= 1; dy++)
			for (auto dx = -1; dx <= 1; dx++)
				if (At(mask, w, h, x + dx, y + dy))
					count++;
		return count;
	}

	// Whether any of the 3 by 3 around a point of a mask is set
	bool AnyAround(const std::vector<uint8_t>& mask, int w, int h, int x, int y)
	{
		for (auto dy = -1; dy <= 1; dy++)
			for (auto dx = -1; dx <= 1; dx++)
				if (At(mask, w, h, x + dx, y + dy))
					return true;
		return false;
	}

	// Each pixel set where most of the 3 by 3 around it are, which drops single pixels
	std::vector<uint8_t> Majority(const std::vector<uint8_t>& mask, int w, int h)
	{
		std::vector<uint8_t> result(mask.size());
		for (auto y = 0; y < h; y++)
			for (auto x = 0; x < w; x++)
				result[static_cast<size_t>(y) * w + x] = CountAround(mask, w, h, x, y) >= 5;
		return result;
	}

	// The sum of the values up to r away from x,y, across or down, inside the w by h
	float BoxSum(const std::vector<float>& values, int w, int h, int x, int y, int r, bool vertical)
	{
		auto sum = 0.0f;
		for (auto d = -r; d <= r; d++)
		{
			auto sx = vertical ? x : x + d;
			auto sy = vertical ? y + d : y;
			if (sx >= 0 && sy >= 0 && sx < w && sy < h)
				sum += values[static_cast<size_t>(sy) * w + sx];
		}
		return sum;
	}

	// The average of the 2r+1 values around each value, across or down
	std::vector<float> BoxBlurPass(const std::vector<float>& values, int w, int h, int r, bool vertical)
	{
		std::vector<float> pass(values.size());
		for (auto y = 0; y < h; y++)
			for (auto x = 0; x < w; x++)
				pass[static_cast<size_t>(y) * w + x] =
					BoxSum(values, w, h, x, y, r, vertical) / static_cast<float>(2 * r + 1);
		return pass;
	}

	// The average of the 2r+1 square around each value, in both directions in turn
	std::vector<float> BoxBlur(const std::vector<float>& values, int w, int h, int r)
	{
		return BoxBlurPass(BoxBlurPass(values, w, h, r, false), w, h, r, true);
	}

	// Mixes a colour over another by a weight from 0 to 1
	ColorRgba Mix(ColorRgba under, ColorRgba over, float weight)
	{
		auto channel = [weight](uint8_t a, uint8_t b)
		{ return static_cast<uint8_t>(std::lround(static_cast<float>(a) + (static_cast<float>(b) - a) * weight)); };
		return ColorRgba{channel(under.GetRed(), over.GetRed()), channel(under.GetGreen(), over.GetGreen()),
						 channel(under.GetBlue(), over.GetBlue()), 255};
	}

	// The key of Dragon's Keep's wordmark, by palette entry, so it is the same on every machine: the gold letters and
	// their dark red shadow are the wordmark's; the yellow-green texture behind them, the blue windows and near black
	// are not
	std::vector<uint8_t> KeyWordmark(const Area& word)
	{
		std::array<bool, 256> wordmark{};
		for (auto index = 10; index < 246; index++)
		{
			auto color = word.Color(static_cast<uint8_t>(index));
			auto red = static_cast<float>(color.GetRed());
			auto green = static_cast<float>(color.GetGreen());
			auto blue = static_cast<float>(color.GetBlue());
			auto gold = red > 110 && blue < red * 0.55f && green < red * 0.85f;
			auto shadow = red > 60 && green < red * 0.55f && blue < red * 0.55f;
			wordmark[static_cast<size_t>(index)] = gold || shadow;
		}
		auto w = word.Rect.w;
		auto h = word.Rect.h;
		std::vector<uint8_t> keyed(static_cast<size_t>(w) * h);
		for (auto y = 0; y < h; y++)
			for (auto x = 0; x < w; x++)
				keyed[static_cast<size_t>(y) * w + x] = wordmark[word.Index(x, y)];
		return Majority(keyed, w, h);
	}

	// The margin around the wordmark that its shadow spreads into
	constexpr int ShadowMargin = 4;

	// The wordmark's shadow: its key, w by h, widened by a pixel and blurred, in the margin around it
	std::vector<float> WordmarkShadow(const std::vector<uint8_t>& keyed, int w, int h)
	{
		auto sw = w + ShadowMargin * 2;
		auto sh = h + ShadowMargin * 2;
		std::vector<float> shadow(static_cast<size_t>(sw) * sh);
		for (auto y = 0; y < sh; y++)
			for (auto x = 0; x < sw; x++)
				shadow[static_cast<size_t>(y) * sw + x] =
					AnyAround(keyed, w, h, x - ShadowMargin, y - ShadowMargin) ? 1.0f : 0.0f;
		return BoxBlur(BoxBlur(shadow, sw, sh, 2), sw, sh, 1);
	}

	// How much of the wordmark shows at a point of its key: all of it on the key, half on its edge, which softens the
	// edge by a pixel
	float WordmarkWeight(const std::vector<uint8_t>& keyed, int w, int h, int x, int y)
	{
		if (At(keyed, w, h, x, y))
			return 1.0f;
		if (AnyAround(keyed, w, h, x, y))
			return 0.5f;
		return 0.0f;
	}

	// Dragon's Keep's panel art has no name in it, unlike the other tables': the "Dragon's Keep" wordmark from the
	// bottom of the panel is keyed onto the bottom of the dragon's head and neck, over a soft dark shadow
	Image DragonsKeepArt(DatFile& dat)
	{
		// The dragon's head and neck and the knight, inside the panel's gold line and the dark one within it
		auto art = Find(dat, "background", {388, 30, 205, 158});
		auto word = Find(dat, "background", {392, 351, 193, 59});
		if (!art || !word)
			return {};
		auto w = word->Rect.w;
		auto h = word->Rect.h;
		auto keyed = KeyWordmark(*word);
		auto shadow = WordmarkShadow(keyed, w, h);
		auto sw = w + ShadowMargin * 2;
		auto sh = h + ShadowMargin * 2;

		// Centred at the bottom of the art, the shadow black at 75% under it
		auto image = art->Colors();
		auto left = (image.Width - w) / 2;
		auto top = image.Height - h;
		for (auto y = 0; y < sh; y++)
			for (auto x = 0; x < sw; x++)
			{
				auto ax = left - ShadowMargin + x;
				auto ay = top - ShadowMargin + y;
				if (ax < 0 || ay < 0 || ax >= image.Width || ay >= image.Height)
					continue;
				auto& pixel = image.Pixels[static_cast<size_t>(ay) * image.Width + ax];
				pixel = Mix(pixel, ColorRgba::Black(), shadow[static_cast<size_t>(y) * sw + x] * 0.75f);
				auto wx = x - ShadowMargin;
				auto wy = y - ShadowMargin;
				if (wx < 0 || wy < 0 || wx >= w || wy >= h)
					continue;
				pixel = Mix(pixel, word->Color(word->Index(wx, wy)), WordmarkWeight(keyed, w, h, wx, wy));
			}
		return image;
	}
} // namespace

Image TableArt::Picture(const TableData& data)
{
	constexpr int size = 120;
	const std::unique_ptr<DatFile> dat{
		partman::load_records((data.Folder + data.DatFileName).c_str(), data.Table != Tables::SpaceCadet3DPB, false)};
	if (!dat)
		return {};
	// The logo's box is at 394,20 to 578,183 in the 600 by 416 frame; its bottom rows hold the ball's label and empty
	// digit box, which the picture leaves out
	auto logo = data.Table == Tables::SpaceCadet3DPB;
	auto area = logo ? Find(*dat, "background", {394, 20, 185, 126}) : Find(*dat, "table", {});
	if (!area)
		return {};

	// Fit the area into the square, centred on black
	auto areaW = area->Rect.w;
	auto areaH = area->Rect.h;
	auto scale = std::min(static_cast<float>(size) / areaW, static_cast<float>(size) / areaH);
	auto width = std::max(1, static_cast<int>(areaW * scale));
	auto height = std::max(1, static_cast<int>(areaH * scale));
	auto left = (size - width) / 2;
	auto top = (size - height) / 2;
	Image picture{size, size, std::vector<ColorRgba>(size * size, ColorRgba::Black())};
	for (auto y = 0; y < height; y++)
	{
		auto srcY0 = y * areaH / height;
		auto srcY1 = std::max(srcY0 + 1, (y + 1) * areaH / height);
		for (auto x = 0; x < width; x++)
		{
			auto srcX0 = x * areaW / width;
			auto srcX1 = std::max(srcX0 + 1, (x + 1) * areaW / width);
			picture.Pixels[(top + y) * size + left + x] = area->Average(srcX0, srcY0, srcX1, srcY1);
		}
	}
	return picture;
}

Image TableArt::PanelArt(Tables table, DatFile& dat)
{
	// The art at the top of each panel, in the 600 by 416 frame. The panel's bitmap starts at 386,12; the areas keep
	// inside its rim: Full Tilt!'s textured edge two pixels wide, Skulduggery's gold line
	std::optional<Area> area;
	using enum Tables;
	switch (table)
	{
	case SpaceCadet3DPB:
		// The logo, as the table's picture has it
		area = Find(dat, "background", {394, 20, 185, 126});
		break;
	case SpaceCadetFullTilt:
		// The "SPACE CADET" arc and the ship
		area = Find(dat, "background", {388, 14, 199, 152});
		break;
	case Skulduggery:
		// The title and the two pirates, above the skull and the treasure and the gold line of the digit box
		area = Find(dat, "background", {387, 13, 206, 131});
		break;
	case DragonsKeep:
		return DragonsKeepArt(dat);
	case FullTiltDemo:
		// Not known to match CADET.DAT's panel
		break;
	}
	return area ? area->Colors() : Image{};
}

Image TableArt::Magnify(const Image& image, int factor)
{
	if (image.Empty())
		return {};
	return {image.Width * factor, image.Height * factor,
			Upscale::Magnify(image.Pixels.data(), image.Width, image.Height, factor)};
}

SDL_Texture* TableArt::Texture(SDL_Renderer* renderer, const Image& image)
{
	if (image.Empty())
		return nullptr;
	auto texture =
		SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STATIC, image.Width, image.Height);
	if (!texture)
		return nullptr;
	SDL_UpdateTexture(texture, nullptr, image.Pixels.data(), image.Width * static_cast<int>(sizeof(ColorRgba)));
	SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
	return texture;
}
