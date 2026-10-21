#pragma once

// Space Cadet 2026's fonts from third_party/fonts, embedded as byte arrays that cmake/EmbedFonts.cmake generates at
// build time; third_party/README.md says where each comes from
#define DEFINITIVE_FONT(name)                                                                                          \
	extern const unsigned char DefinitiveFont_##name[];                                                                \
	extern const unsigned int DefinitiveFont_##name##_Size;
DEFINITIVE_FONT(DSEG7Classic_Bold)
DEFINITIVE_FONT(Exo2_Regular)
DEFINITIVE_FONT(Exo2_SemiBold)
DEFINITIVE_FONT(NotoSans_Greek)
DEFINITIVE_FONT(NotoSansArabic_Regular)
DEFINITIVE_FONT(NotoSansHebrew_Regular)
#undef DEFINITIVE_FONT
