/*
    Copyright 2020 Morgan McGuire & Mara Gagiu.
    Available under the MIT license.
    https://casual-effects.com/research/McGuire2021PixelArt/index.html
*/

#pragma once

#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

// Magnifies srcWidth x srcHeight ARGB pixels to twice the size; dst holds 4 * srcWidth * srcHeight pixels
void mmpx_scale2x(const uint32_t* srcBuffer, uint32_t* dst, uint32_t srcWidth, uint32_t srcHeight);

#ifdef __cplusplus
}
#endif
