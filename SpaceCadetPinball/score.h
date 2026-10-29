#pragma once
#include "gdrv.h"

struct scoreStruct
{
	int Score;
	bool DirtyFlag;
	gdrv_bitmap8* BackgroundBmp;
	int OffsetX;
	int OffsetY;
	int Width;
	int Height;
	gdrv_bitmap8* CharBmp[10];
};

struct score_msg_font_type
{
	int GapWidth;
	int Height;
	gdrv_bitmap8* Chars[128]{};
};


class score
{
public:
	static score_msg_font_type* msg_fontp;
	// Full Tilt's second message font (CADET.EXE's "font2"), whose characters are pictures; the bonus tables' rules
	// show some texts in it
	static score_msg_font_type* msg_font2p;
	static int init();
	static scoreStruct* create(LPCSTR fieldName, gdrv_bitmap8* renderBgBmp);
	static scoreStruct* dup(scoreStruct* score, int scoreIndex);
	static void load_msg_font(LPCSTR lpName);
	static score_msg_font_type* load_msg_font_group(LPCSTR lpName);
	static void unload_msg_font();
	static void erase(scoreStruct* score, int blitFlag);
	static void set(scoreStruct* score, int value);
	static void update(scoreStruct* score);
	static void string_format(int score, char* str);
};
