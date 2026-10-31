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
		const ColorRgba* Palette;
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
	};

	// An area of a group's first bitmap, in the 600 by 416 frame, or all of it when the area is empty; none when the
	// bitmap is missing or doesn't cover the area
	std::optional<Area> Find(DatFile& dat, const char* group, SDL_Rect rect)
	{
		auto palette = reinterpret_cast<ColorRgba*>(dat.field_labeled("background", FieldTypes::Palette));
		auto groupIndex = dat.record_labeled(group);
		auto bmp = groupIndex < 0 ? nullptr : dat.Groups[groupIndex]->GetBitmap(0);
		if (!palette || !bmp || !bmp->IndexedBmpPtr || bmp->BitmapType == BitmapTypes::Spliced || bmp->Width <= 0 ||
			bmp->Height <= 0)
			return std::nullopt;
		if (rect.w <= 0 || rect.h <= 0)
			return Area{bmp, palette, {0, 0, bmp->Width, bmp->Height}};
		rect.x -= bmp->XPosition;
		rect.y -= bmp->YPosition;
		if (rect.x < 0 || rect.y < 0 || rect.x + rect.w > bmp->Width || rect.y + rect.h > bmp->Height)
			return std::nullopt;
		return Area{bmp, palette, rect};
	}

	// Whether a point of a mask, w by h, is set; false outside it
	bool At(const std::vector<uint8_t>& mask, int w, int h, int x, int y)
	{
		return x >= 0 && y >= 0 && x < w && y < h && mask[static_cast<size_t>(y) * w + x];
	}

	// Each pixel set where most of the 3 by 3 around it are, which drops single pixels
	std::vector<uint8_t> Majority(const std::vector<uint8_t>& mask, int w, int h)
	{
		std::vector<uint8_t> result(mask.size());
		for (auto y = 0; y < h; y++)
			for (auto x = 0; x < w; x++)
			{
				auto count = 0;
				for (auto dy = -1; dy <= 1; dy++)
					for (auto dx = -1; dx <= 1; dx++)
						count += At(mask, w, h, x + dx, y + dy);
				result[static_cast<size_t>(y) * w + x] = count >= 5;
			}
		return result;
	}

	// The average of the 2r+1 square around each value, in both directions in turn
	std::vector<float> BoxBlur(std::vector<float> values, int w, int h, int r)
	{
		std::vector<float> pass(values.size());
		for (auto vertical = 0; vertical < 2; vertical++)
		{
			for (auto y = 0; y < h; y++)
				for (auto x = 0; x < w; x++)
				{
					auto sum = 0.0f;
					for (auto d = -r; d <= r; d++)
					{
						auto sx = vertical ? x : x + d, sy = vertical ? y + d : y;
						if (sx >= 0 && sy >= 0 && sx < w && sy < h)
							sum += values[static_cast<size_t>(sy) * w + sx];
					}
					pass[static_cast<size_t>(y) * w + x] = sum / static_cast<float>(2 * r + 1);
				}
			values.swap(pass);
		}
		return values;
	}

	// Mixes a colour over another by a weight from 0 to 1
	ColorRgba Mix(ColorRgba under, ColorRgba over, float weight)
	{
		auto channel = [weight](uint8_t a, uint8_t b)
		{ return static_cast<uint8_t>(std::lround(static_cast<float>(a) + (static_cast<float>(b) - a) * weight)); };
		return ColorRgba{channel(under.GetRed(), over.GetRed()), channel(under.GetGreen(), over.GetGreen()),
						 channel(under.GetBlue(), over.GetBlue()), 255};
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

		// The key, by palette entry, so it is the same on every machine: the gold letters and their dark red shadow
		// are the wordmark's; the yellow-green texture behind them, the blue windows and near black are not
		std::array<bool, 256> wordmark{};
		for (auto index = 10; index < 246; index++)
		{
			auto color = word->Color(static_cast<uint8_t>(index));
			auto red = static_cast<float>(color.GetRed()), green = static_cast<float>(color.GetGreen()),
				 blue = static_cast<float>(color.GetBlue());
			auto gold = red > 110 && blue < red * 0.55f && green < red * 0.85f;
			auto shadow = red > 60 && green < red * 0.55f && blue < red * 0.55f;
			wordmark[static_cast<size_t>(index)] = gold || shadow;
		}
		auto w = word->Rect.w, h = word->Rect.h;
		std::vector<uint8_t> keyed(static_cast<size_t>(w) * h);
		for (auto y = 0; y < h; y++)
			for (auto x = 0; x < w; x++)
				keyed[static_cast<size_t>(y) * w + x] = wordmark[word->Index(x, y)];
		keyed = Majority(keyed, w, h);

		// The shadow: the mask widened by a pixel and blurred, in a margin around the wordmark
		constexpr int margin = 4;
		auto sw = w + margin * 2, sh = h + margin * 2;
		std::vector<float> shadow(static_cast<size_t>(sw) * sh);
		for (auto y = 0; y < sh; y++)
			for (auto x = 0; x < sw; x++)
			{
				auto set = false;
				for (auto dy = -1; dy <= 1 && !set; dy++)
					for (auto dx = -1; dx <= 1 && !set; dx++)
						set = At(keyed, w, h, x - margin + dx, y - margin + dy);
				shadow[static_cast<size_t>(y) * sw + x] = set ? 1.0f : 0.0f;
			}
		shadow = BoxBlur(BoxBlur(std::move(shadow), sw, sh, 2), sw, sh, 1);

		// Centred at the bottom of the art, the shadow black at 75% under it, and the mask's edge softened by a pixel
		auto image = art->Colors();
		auto left = (image.Width - w) / 2, top = image.Height - h;
		for (auto y = 0; y < sh; y++)
			for (auto x = 0; x < sw; x++)
			{
				auto ax = left - margin + x, ay = top - margin + y;
				if (ax < 0 || ay < 0 || ax >= image.Width || ay >= image.Height)
					continue;
				auto& pixel = image.Pixels[static_cast<size_t>(ay) * image.Width + ax];
				pixel = Mix(pixel, ColorRgba::Black(), shadow[static_cast<size_t>(y) * sw + x] * 0.75f);
				auto wx = x - margin, wy = y - margin;
				if (wx < 0 || wy < 0 || wx >= w || wy >= h)
					continue;
				auto edge = false;
				for (auto dy = -1; dy <= 1 && !edge; dy++)
					for (auto dx = -1; dx <= 1 && !edge; dx++)
						edge = At(keyed, w, h, wx + dx, wy + dy);
				auto weight = At(keyed, w, h, wx, wy) ? 1.0f : edge ? 0.5f : 0.0f;
				pixel = Mix(pixel, word->Color(word->Index(wx, wy)), weight);
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
	auto areaW = area->Rect.w, areaH = area->Rect.h;
	auto scale = std::min(static_cast<float>(size) / areaW, static_cast<float>(size) / areaH);
	auto width = std::max(1, static_cast<int>(areaW * scale));
	auto height = std::max(1, static_cast<int>(areaH * scale));
	auto left = (size - width) / 2, top = (size - height) / 2;
	Image picture{size, size, std::vector<ColorRgba>(size * size, ColorRgba::Black())};
	for (auto y = 0; y < height; y++)
	{
		auto srcY0 = y * areaH / height, srcY1 = std::max(srcY0 + 1, (y + 1) * areaH / height);
		for (auto x = 0; x < width; x++)
		{
			auto srcX0 = x * areaW / width, srcX1 = std::max(srcX0 + 1, (x + 1) * areaW / width);
			unsigned red = 0, green = 0, blue = 0, count = 0;
			for (auto srcY = srcY0; srcY < srcY1; srcY++)
				for (auto srcX = srcX0; srcX < srcX1; srcX++)
				{
					auto color = area->Color(area->Index(srcX, srcY));
					red += color.GetRed();
					green += color.GetGreen();
					blue += color.GetBlue();
					count++;
				}
			picture.Pixels[(top + y) * size + left + x] =
				ColorRgba{static_cast<uint8_t>(red / count), static_cast<uint8_t>(green / count),
						  static_cast<uint8_t>(blue / count), 255};
		}
	}
	return picture;
}

Image TableArt::PanelArt(Tables table, DatFile& dat)
{
	// The art at the top of each panel, in the 600 by 416 frame. The panel's bitmap starts at 386,12; the areas keep
	// inside its rim: Full Tilt!'s textured edge two pixels wide, Skulduggery's gold line
	std::optional<Area> area;
	switch (table)
	{
	case Tables::SpaceCadet3DPB:
		// The logo, as the table's picture has it
		area = Find(dat, "background", {394, 20, 185, 126});
		break;
	case Tables::SpaceCadetFullTilt:
		// The "SPACE CADET" arc and the ship
		area = Find(dat, "background", {388, 14, 199, 152});
		break;
	case Tables::Skulduggery:
		// The title and the two pirates, above the skull and the treasure and the gold line of the digit box
		area = Find(dat, "background", {387, 13, 206, 131});
		break;
	case Tables::DragonsKeep:
		return DragonsKeepArt(dat);
	case Tables::FullTiltDemo:
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
