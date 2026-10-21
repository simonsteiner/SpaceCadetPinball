#pragma once

#include <string>

// An ImGui colour from its components (ImGui's IM_COL32, with unsigned shifts)
constexpr ImU32 DefinitiveColor(ImU32 red, ImU32 green, ImU32 blue, ImU32 alpha = 0xFFU)
{
	return alpha << 24U | blue << 16U | green << 8U | red;
}

// Space Cadet 2026's visual language: colours from the table's own palette, its fonts, and the ImGui style and drawing
// pieces made from them. The plan has the mockups they come from (docs/plan/2026-10-20-space-cadet-2026.md, B1).
class DefinitiveTheme
{
public:
	// Deep space, the ground of every screen and panel, from the table's night sky
	static constexpr ImU32 Space950 = DefinitiveColor(0x05, 0x09, 0x18);
	static constexpr ImU32 Space900 = DefinitiveColor(0x0A, 0x12, 0x30);
	static constexpr ImU32 Space800 = DefinitiveColor(0x12, 0x1D, 0x4A);
	static constexpr ImU32 Space700 = DefinitiveColor(0x1B, 0x2B, 0x6B);
	static constexpr ImU32 Space600 = DefinitiveColor(0x2A, 0x3F, 0x91);
	// Chrome, the text and trim, from the rails: cool and slightly blue
	static constexpr ImU32 Chrome50 = DefinitiveColor(0xEE, 0xF2, 0xFA);
	static constexpr ImU32 Chrome200 = DefinitiveColor(0xC2, 0xCB, 0xDD);
	static constexpr ImU32 Chrome400 = DefinitiveColor(0x8A, 0x95, 0xB0);
	static constexpr ImU32 Chrome600 = DefinitiveColor(0x4E, 0x5A, 0x78);
	// The lamps and score digits: orange for focus and scores, red for the ball count and warnings
	static constexpr ImU32 LedOrange = DefinitiveColor(0xFF, 0x8C, 0x1A);
	static constexpr ImU32 LedRed = DefinitiveColor(0xFF, 0x3B, 0x24);

	// Type sizes in pixels for a window 720 pixels high at UI scale 1; screens scale them with the window
	static constexpr float DisplaySize = 60, TitleSize = 32, HeadingSize = 22, BodySize = 17, LabelSize = 14;
	static constexpr float ScoreSize = 44, CounterSize = 20;
	// Menu rows, high enough to be touch targets
	static constexpr float RowHeight = 48;
	static constexpr float Rounding = 6;

	enum class Lamp
	{
		Dark,
		Lit,
		Warning,
		Off,
	};

	// Adds the fonts to the atlas: Exo 2 with Noto for Greek, Hebrew and Arabic merged in, then the Font File option's
	// font if there is one, else a system font for Chinese, Japanese and Korean when one of those is the language.
	// Call it once per atlas, after the atlas has its default font, so Classic keeps that.
	static void AddFonts(ImFontAtlas* atlas, const std::string& fontFile);
	// Exo 2 Regular and SemiBold, and DSEG7 Classic Bold for numbers; null before AddFonts
	static ImFont* TextFont();
	static ImFont* HeadingFont();
	static ImFont* DigitsFont();

	// Turns ImGui's dark style into Space Cadet 2026's, at scale 1
	static void ApplyStyle(ImGuiStyle& style);

	// A lamp like the playfield's inserts, which lights on the row that has focus
	static void DrawLamp(ImDrawList* drawList, ImVec2 centre, float radius, Lamp state);
	// A panel of deep blue with a chrome rim, lit from above like the table's rails
	static void DrawPanel(ImDrawList* drawList, ImVec2 min, ImVec2 max);
	// Seven-segment digits with the unlit segments faintly behind them. A '!' is a blank digit place.
	static void DrawLed(ImDrawList* drawList, ImVec2 position, float size, const char* text, bool red = false);
	static ImVec2 LedSize(float size, const char* text);

private:
	static ImFont *Text, *Heading, *Digits;
};
