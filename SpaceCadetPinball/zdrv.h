#pragma once
#include "gdrv.h"

struct zmap_header_type
{
	zmap_header_type(int width, int height, int stride);
	~zmap_header_type();
	int Width;
	int Height;
	int Stride;
	unsigned Resolution;
	uint16_t* ZPtr1;
	SDL_Texture* Texture;
	// 1 where a spliced bitmap has no pixel, which the game never paints (CADET.EXE paints only the runs); empty for
	// every map that wasn't spliced, such as all of 3D Pinball's
	std::vector<uint8_t> Holes;

private:
	static int pad(int width);
};

class zdrv
{
public:
	static void fill(zmap_header_type* zmap, int width, int height, int xOff, int yOff, uint16_t fillWord);
	static void paint(int width, int height, gdrv_bitmap8* dstBmp, int dstBmpXOff, int dstBmpYOff,
	                  zmap_header_type* dstZMap, int dstZMapXOff, int dstZMapYOff, gdrv_bitmap8* srcBmp, int srcBmpXOff,
	                  int srcBmpYOff, zmap_header_type* srcZMap, int srcZMapXOff, int srcZMapYOff);
	static void paint_flat(int width, int height, gdrv_bitmap8* dstBmp, int dstBmpXOff, int dstBmpYOff,
	                       zmap_header_type* zMap, int dstZMapXOff, int dstZMapYOff, gdrv_bitmap8* srcBmp,
	                       int srcBmpXOff, int srcBmpYOff, uint16_t depth);
	static void CreatePreview(zmap_header_type& zMap);
	static void FlipZMapHorizontally(zmap_header_type& zMap);

private:
	static void paint_with_holes(int width, int height, ColorRgba* dstPtr, int dstStride, uint16_t* dstPtrZ,
								 int dstZStride, const ColorRgba* srcPtr, int srcStride, const uint16_t* srcPtrZ,
								 const zmap_header_type* srcZMap, int srcZMapXOff, int srcZMapYOff);
};
