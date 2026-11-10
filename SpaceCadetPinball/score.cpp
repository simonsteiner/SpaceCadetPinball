#include "pch.h"
#include "score.h"

#include "fullscrn.h"
#include "loader.h"
#include "GroupData.h"
#include "pb.h"
#include "render.h"


score_msg_font_type* score::msg_fontp;
score_msg_font_type* score::msg_font2p;

int score::init()
{
	return 1;
}

// Makes a score field from the record named fieldName, offset by the resolution, whose short array holds the group
// of the first of ten digit bitmaps and the box's offset and size; null if the array is missing
scoreStruct* score::create(LPCSTR fieldName, gdrv_bitmap8* renderBgBmp)
{
	auto score = new scoreStruct();
	if (!score)
		return nullptr;
	score->Score = -9999;
	score->BackgroundBmp = renderBgBmp;

	/*Full tilt: score box dimensions index is offset by resolution*/
	auto dimensionsId = pb::record_table->record_labeled(fieldName) + fullscrn::GetResolution();
	auto dimensions = reinterpret_cast<int16_t*>(loader::loader_table->field(dimensionsId,
	                                                                         FieldTypes::ShortArray));
	if (!dimensions)
	{
		delete score;
		return nullptr;
	}
	int groupIndex = *dimensions++;
	score->OffsetX = *dimensions++;
	score->OffsetY = *dimensions++;
	score->Width = *dimensions++;
	score->Height = *dimensions;

	for (int index = 0; index < 10; index++)
	{
		score->CharBmp[index] = loader::loader_table->GetBitmap(groupIndex);
		++groupIndex;
	}
	return score;
}

scoreStruct* score::dup(scoreStruct* score, int scoreIndex)
{
	return new scoreStruct(*score);
}

void score::load_msg_font(LPCSTR lpName)
{
	msg_fontp = load_msg_font_group(lpName);
}

// Loads a message font from the group named lpName: the gap width for the current resolution, and the bitmaps of the
// groups that follow as the characters from 32 up to the first group without one. Null if there is no such group
score_msg_font_type* score::load_msg_font_group(LPCSTR lpName)
{
	if (!pb::record_table)
		return nullptr;

	int groupIndex = pb::record_table->record_labeled(lpName);
	if (groupIndex < 0)
		return nullptr;

	auto font = new score_msg_font_type();

	// FT font has multiple resolutions
	auto gapArray = reinterpret_cast<int16_t*>(pb::record_table->field(groupIndex, FieldTypes::ShortArray));
	if (gapArray)
		font->GapWidth = gapArray[fullscrn::GetResolution()];
	else
		font->GapWidth = 0;

	for (auto charIndex = 32; charIndex < 128; charIndex++, ++groupIndex)
	{
		auto bmp = pb::record_table->GetBitmap(groupIndex);
		if (!bmp)
			break;
		if (!font->Height)
			font->Height = bmp->Height;
		font->Chars[charIndex] = bmp;
	}
	return font;
}


void score::unload_msg_font()
{
	delete msg_fontp;
	msg_fontp = nullptr;
	delete msg_font2p;
	msg_font2p = nullptr;
}

// Clears the score's box in the frame, with its background bitmap if it has one, else with colour 0
void score::erase(scoreStruct* score, int blitFlag)
{
	if (score)
	{
		if (score->BackgroundBmp)
			gdrv::copy_bitmap(
				render::vscreen,
				score->Width,
				score->Height,
				score->OffsetX,
				score->OffsetY,
				score->BackgroundBmp,
				score->OffsetX,
				score->OffsetY);
		else
			gdrv::fill_bitmap(render::vscreen, score->Width, score->Height, score->OffsetX, score->OffsetY, 0);
	}
}

// Sets the score's value and marks it for redrawing; a null score is ignored
void score::set(scoreStruct* score, int value)
{
	if (score)
	{
		score->Score = value;
		score->DirtyFlag = true;
	}
}


// Redraws a score marked dirty: clears its box and draws the digits right-aligned in it. A negative score leaves the
// box empty, and one above 1000000000 is not redrawn at all
void score::update(scoreStruct* score)
{
	char scoreBuf[12]{};
	if (score && score->DirtyFlag && score->Score <= 1000000000)
	{
		score->DirtyFlag = false;
		int x = score->Width + score->OffsetX;
		int y = score->OffsetY;
		erase(score, 0);
		if (score->Score >= 0)
		{
			snprintf(scoreBuf, sizeof scoreBuf, "%d", score->Score);
			for (ptrdiff_t index = strlen(scoreBuf) - 1; index >= 0; index--)
			{
				unsigned char curChar = scoreBuf[index];
				curChar -= '0';
				gdrv_bitmap8* bmp = score->CharBmp[curChar % 10u];
				x -= bmp->Width;
				int height = bmp->Height;
				int width = bmp->Width;
				if (render::background_bitmap)
					gdrv::copy_bitmap_w_transparency(render::vscreen, width, height, x, y, bmp, 0, 0);
				else
					gdrv::copy_bitmap(render::vscreen, width, height, x, y, bmp, 0, 0);
			}
		}
	}
}

// Writes the score with thousands separators into str, which must hold 36 characters; -999 gives an empty string
void score::string_format(int score, char* str)
{
	if (score == -999)
	{
		*str = 0;
	}
	else
	{
		int scoreMillions = score % 1000000000 / 1000000;
		if (score / 1000000000 <= 0)
		{
			if (scoreMillions <= 0)
			{
				if (score % 1000000 / 1000 <= 0)
					snprintf(str, 36, "%d", score);
				else
					snprintf(str, 36, "%d,%03d", score % 1000000 / 1000, score % 1000);
			}
			else
			{
				snprintf(str, 36, "%d,%03d,%03d", scoreMillions, score % 1000000 / 1000, score % 1000);
			}
		}
		else
		{
			snprintf(
				str,
				36,
				"%d,%03d,%03d,%03d",
				score / 1000000000,
				scoreMillions,
				score % 1000000 / 1000,
				score % 1000);
		}
	}
}
