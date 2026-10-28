#include "pch.h"
#include "zdrv.h"
#include "winmain.h"


zmap_header_type::zmap_header_type(int width, int height, int stride)
{
	Resolution = 0;
	Width = width;
	Height = height;
	Stride = stride >= 0 ? stride : pad(width);
	Texture = nullptr;
	ZPtr1 = new unsigned short[Stride * Height];
}

zmap_header_type::~zmap_header_type()
{
	delete[] ZPtr1;
	if (Texture)
		SDL_DestroyTexture(Texture);
}

int zmap_header_type::pad(int width)
{
	int result = width;
	if (width & 3)
		result = width - (width & 3) + 4;
	return result;
}


void zdrv::fill(zmap_header_type* zmap, int width, int height, int xOff, int yOff, uint16_t fillWord)
{
	auto dstPtr = &zmap->ZPtr1[zmap->Stride * yOff + xOff];
	for (int y = height; y > 0; --y)
	{
		for (int x = width; x > 0; --x)
		{
			*dstPtr++ = fillWord;
		}
		dstPtr += zmap->Stride - width;
	}
}


void zdrv::paint(int width, int height, gdrv_bitmap8* dstBmp, int dstBmpXOff, int dstBmpYOff, zmap_header_type* dstZMap,
                 int dstZMapXOff, int dstZMapYOff, gdrv_bitmap8* srcBmp, int srcBmpXOff, int srcBmpYOff,
                 zmap_header_type* srcZMap, int srcZMapXOff, int srcZMapYOff)
{
	assertm(srcBmp->BitmapType != BitmapTypes::Spliced, "Wrong bmp type");

	auto srcPtr = &srcBmp->BmpBufPtr1[srcBmp->Stride * srcBmpYOff + srcBmpXOff];
	auto dstPtr = &dstBmp->BmpBufPtr1[dstBmp->Stride * dstBmpYOff + dstBmpXOff];
	auto srcPtrZ = &srcZMap->ZPtr1[srcZMap->Stride * srcZMapYOff + srcZMapXOff];
	auto dstPtrZ = &dstZMap->ZPtr1[dstZMap->Stride * dstZMapYOff + dstZMapXOff];

	if (!srcZMap->Holes.empty())
	{
		paint_with_holes(width, height, dstPtr, dstBmp->Stride, dstPtrZ, dstZMap->Stride, srcPtr, srcBmp->Stride,
						 srcPtrZ, srcZMap, srcZMapXOff, srcZMapYOff);
		return;
	}

	for (int y = height; y > 0; y--)
	{
		for (int x = width; x > 0; --x)
		{
			if (*dstPtrZ >= *srcPtrZ)
			{
				*dstPtr = *srcPtr;
				*dstPtrZ = *srcPtrZ;
			}
			++srcPtr;
			++dstPtr;
			++srcPtrZ;
			++dstPtrZ;
		}

		srcPtr += srcBmp->Stride - width;
		dstPtr += dstBmp->Stride - width;
		srcPtrZ += srcZMap->Stride - width;
		dstPtrZ += dstZMap->Stride - width;
	}
}

void zdrv::paint_with_holes(int width, int height, ColorRgba* dstPtr, int dstStride, uint16_t* dstPtrZ, int dstZStride,
							const ColorRgba* srcPtr, int srcStride, const uint16_t* srcPtrZ,
							const zmap_header_type* srcZMap, int srcZMapXOff, int srcZMapYOff)
{
	// paint, skipping the pixels a spliced bitmap leaves out
	auto holes = &srcZMap->Holes[srcZMap->Stride * srcZMapYOff + srcZMapXOff];
	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			if (!holes[x] && dstPtrZ[x] >= srcPtrZ[x])
			{
				dstPtr[x] = srcPtr[x];
				dstPtrZ[x] = srcPtrZ[x];
			}
		}
		dstPtr += dstStride;
		dstPtrZ += dstZStride;
		srcPtr += srcStride;
		srcPtrZ += srcZMap->Stride;
		holes += srcZMap->Stride;
	}
}

void zdrv::paint_flat(int width, int height, gdrv_bitmap8* dstBmp, int dstBmpXOff, int dstBmpYOff,
                      zmap_header_type* zMap, int dstZMapXOff, int dstZMapYOff, gdrv_bitmap8* srcBmp, int srcBmpXOff,
                      int srcBmpYOff, uint16_t depth)
{
	assertm(srcBmp->BitmapType != BitmapTypes::Spliced, "Wrong bmp type");

	auto dstPtr = &dstBmp->BmpBufPtr1[dstBmp->Stride * dstBmpYOff + dstBmpXOff];
	auto srcPtr = &srcBmp->BmpBufPtr1[srcBmp->Stride * srcBmpYOff + srcBmpXOff];
	auto zPtr = &zMap->ZPtr1[zMap->Stride * dstZMapYOff + dstZMapXOff];

	for (int y = height; y > 0; y--)
	{
		for (int x = width; x > 0; --x)
		{
			if ((*srcPtr).Color && *zPtr > depth)
			{
				*dstPtr = *srcPtr;
			}
			++srcPtr;
			++dstPtr;
			++zPtr;
		}

		srcPtr += srcBmp->Stride - width;
		dstPtr += dstBmp->Stride - width;
		zPtr += zMap->Stride - width;
	}
}

void zdrv::CreatePreview(zmap_header_type& zMap)
{
	if (zMap.Texture)
		return;

	auto tmpBuff = new ColorRgba[zMap.Width * zMap.Height];

	auto dst = tmpBuff;
	auto src = zMap.ZPtr1;
	for (auto y = 0; y < zMap.Height; y++)
	{
		for (auto x = 0; x < zMap.Width; x++)
		{
			auto depth = static_cast<uint8_t>((0xffff - *src++) / 0xff);
			*dst++ = ColorRgba{ depth, depth, depth, 0xff };
		}
		src += zMap.Stride - zMap.Width;
	}

	auto texture = SDL_CreateTexture
	(
		winmain::Renderer,
		SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STATIC,
		zMap.Width, zMap.Height
	);
	SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
	SDL_UpdateTexture(texture, nullptr, tmpBuff, zMap.Width * 4);
	zMap.Texture = texture;
	delete[] tmpBuff;
}

void zdrv::FlipZMapHorizontally(zmap_header_type& zMap)
{
	// Flip in-place, iterate over Height/2 lines
	auto dst = zMap.ZPtr1;
	auto src = zMap.ZPtr1 + zMap.Stride * (zMap.Height - 1);
	for (auto y = zMap.Height - 1; y >= zMap.Height / 2; y--)
	{
		for (auto x = 0; x < zMap.Width; x++)
		{
			std::swap(*dst++, *src++);
		}
		dst += zMap.Stride - zMap.Width;
		src -= zMap.Stride + zMap.Width;
	}

	if (!zMap.Holes.empty())
	{
		auto holes = zMap.Holes.data();
		for (auto y = 0; y < zMap.Height / 2; y++)
			std::swap_ranges(holes + zMap.Stride * y, holes + zMap.Stride * y + zMap.Width,
							 holes + zMap.Stride * (zMap.Height - 1 - y));
	}
}
