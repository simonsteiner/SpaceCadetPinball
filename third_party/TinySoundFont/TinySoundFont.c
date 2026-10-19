// The implementation of the single-header libraries, compiled once.
// stb_vorbis comes first, so that TinySoundFont also loads SF3 SoundFonts, whose samples are Ogg Vorbis.
#include "../stb/stb_vorbis.c"

#define TSF_IMPLEMENTATION
#include "tsf.h"

#define TML_IMPLEMENTATION
#include "tml.h"
