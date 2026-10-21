#pragma once

#include <string>
#include <string_view>

// Hebrew and Arabic for ImGui, which draws each character left to right in the order a string stores them and does
// no shaping. Space Cadet 2026 turns such text into the order and letter forms it must be drawn in; Classic draws it
// as stored.
class DefinitiveText
{
public:
	// Whether the language chosen is written right to left
	static bool RightToLeft();
	// The text in drawing order, each line on its own: right-to-left runs reversed and the line read right to left,
	// Arabic letters replaced by their joined forms (Arabic Presentation Forms-B) and direction marks dropped. Text
	// with nothing right to left comes back as it is. Format the text first: reordering moves format specifiers. The
	// result lives in a cache until the next call, so use it before calling again.
	static const char* Visual(const char* text);
	// What Visual does, without the cache
	static std::string Shape(std::string_view text);
};
