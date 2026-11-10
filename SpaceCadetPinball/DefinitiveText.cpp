#include "pch.h"
#include "DefinitiveText.h"

#include "translations.h"

#include <unordered_map>

namespace
{
	enum class Direction : char
	{
		Neutral,
		LeftToRight,
		RightToLeft,
		Number,
	};

	// An Arabic letter's isolated form in Arabic Presentation Forms-B; its final, initial and medial forms follow it
	// there. Dual-joining letters join on both sides, right-joining ones only to the letter before them.
	struct ArabicLetter
	{
		char32_t Isolated;
		bool DualJoining;
	};

	// U+0621 to U+064A, from Unicode's ArabicShaping.txt; zero for code points that aren't letters. Alef maksura is
	// right-joining here, because its initial and medial forms are only in Presentation Forms-A, which the font lacks.
	constexpr auto ArabicLetters = std::to_array<ArabicLetter>({
		{0xFE80, false}, {0xFE81, false}, {0xFE83, false}, {0xFE85, false}, {0xFE87, false}, {0xFE89, true},
		{0xFE8D, false}, {0xFE8F, true},  {0xFE93, false}, {0xFE95, true},  {0xFE99, true},  {0xFE9D, true},
		{0xFEA1, true},  {0xFEA5, true},  {0xFEA9, false}, {0xFEAB, false}, {0xFEAD, false}, {0xFEAF, false},
		{0xFEB1, true},  {0xFEB5, true},  {0xFEB9, true},  {0xFEBD, true},  {0xFEC1, true},  {0xFEC5, true},
		{0xFEC9, true},  {0xFECD, true},  {0, false},      {0, false},      {0, false},      {0, false},
		{0, false},      {0, false},      {0xFED1, true},  {0xFED5, true},  {0xFED9, true},  {0xFEDD, true},
		{0xFEE1, true},  {0xFEE5, true},  {0xFEE9, true},  {0xFEED, false}, {0xFEEF, false}, {0xFEF1, true},
	});
	constexpr char32_t Hamza = 0x0621;
	constexpr char32_t Yeh = 0x064A;
	constexpr char32_t Tatweel = 0x0640;
	constexpr char32_t Lam = 0x0644;

	// The table's entry for an Arabic letter, or null for any other code point
	const ArabicLetter* GetArabicLetter(char32_t c)
	{
		if (c < Hamza || c > Yeh)
			return nullptr;
		auto letter = &ArabicLetters[c - Hamza];
		return letter->Isolated ? letter : nullptr;
	}

	// Vowel marks sit on a letter without breaking its joins
	bool IsTransparent(char32_t c)
	{
		return (c >= 0x064B && c <= 0x065F) || c == 0x0670;
	}

	// Whether a character joins to the letter after it: tatweel and the dual-joining letters
	bool JoinsForward(char32_t c)
	{
		auto letter = GetArabicLetter(c);
		return c == Tatweel || (letter && letter->DualJoining);
	}

	// Whether a character joins to the letter before it: tatweel and every Arabic letter but hamza
	bool JoinsBackward(char32_t c)
	{
		return c == Tatweel || (GetArabicLetter(c) && c != Hamza);
	}

	// Lam followed by one of these alefs becomes a single ligature, isolated then final
	char32_t LamAlef(char32_t alef)
	{
		switch (alef)
		{
		case 0x0622:
			return 0xFEF5;
		case 0x0623:
			return 0xFEF7;
		case 0x0625:
			return 0xFEF9;
		case 0x0627:
			return 0xFEFB;
		default:
			return 0;
		}
	}

	// Replaces each Arabic letter by its isolated, final, initial or medial form, by whether the nearest letters on
	// each side join to it, and lam with a following alef by their ligature; other characters stay as they are
	std::u32string JoinArabic(const std::u32string& text)
	{
		// The nearest letter on each side, skipping vowel marks
		auto neighbour = [&text](size_t index, int step) -> char32_t
		{
			for (auto i = static_cast<ptrdiff_t>(index) + step; i >= 0 && i < static_cast<ptrdiff_t>(text.size());
				 i += step)
			{
				if (!IsTransparent(text[i]))
					return text[i];
			}
			return 0;
		};

		std::u32string joined;
		joined.reserve(text.size());
		for (size_t i = 0; i < text.size(); i++)
		{
			auto c = text[i];
			auto letter = GetArabicLetter(c);
			if (!letter)
			{
				joined += c;
				continue;
			}

			auto joinsBefore = JoinsBackward(c) && JoinsForward(neighbour(i, -1));
			if (c == Lam)
			{
				auto next = neighbour(i, 1);
				if (auto ligature = LamAlef(next))
				{
					joined += static_cast<char32_t>(ligature + (joinsBefore ? 1 : 0));
					// Skips to the alef, keeping any vowel marks in between
					while (text[++i] != next)
						joined += text[i];
					continue;
				}
			}
			auto joinsAfter = letter->DualJoining && JoinsBackward(neighbour(i, 1));
			// Isolated, final, initial, medial
			auto form = (joinsAfter ? 2 : 0) + (joinsBefore ? 1 : 0);
			joined += static_cast<char32_t>(letter->Isolated + form);
		}
		return joined;
	}

	// The direction a character reads in: Western and Arabic digits as numbers, Hebrew and Arabic with their
	// presentation forms and the right-to-left mark right to left, Latin and most other letters left to right, the rest
	// neutral
	Direction GetDirection(char32_t c)
	{
		using enum Direction;
		if ((c >= '0' && c <= '9') || (c >= 0x0660 && c <= 0x0669) || (c >= 0x06F0 && c <= 0x06F9))
			return Number;
		if ((c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF) || c == 0x200F)
			return RightToLeft;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == 0x200E ||
			(c >= 0xC0 && c != 0xD7 && c != 0xF7 && (c < 0x2000 || c > 0x2BFF) && (c < 0x3000 || c > 0x303F)))
			return LeftToRight;
		return Neutral;
	}

	// The opening bracket or angle quote for a closing one and the other way round; any other character unchanged
	char32_t Mirror(char32_t c)
	{
		switch (c)
		{
		case '(':
			return ')';
		case ')':
			return '(';
		case '[':
			return ']';
		case ']':
			return '[';
		case '{':
			return '}';
		case '}':
			return '{';
		case '<':
			return '>';
		case '>':
			return '<';
		case 0xAB:
			return 0xBB;
		case 0xBB:
			return 0xAB;
		default:
			return c;
		}
	}

	// A simplified Unicode bidi algorithm for one right-to-left line: left-to-right words and numbers keep their order,
	// the rest is read right to left, and brackets in right-to-left text are mirrored.
	std::u32string ReorderLine(const std::u32string& line)
	{
		using enum Direction;
		std::vector<Direction> directions(line.size());
		for (size_t i = 0; i < line.size(); i++)
		{
			directions[i] = GetDirection(line[i]);
			// A number's separators and signs read with it, as in 1,000 or 50%
			if (directions[i] == Neutral && i > 0 && directions[i - 1] == Number &&
				(i + 1 < line.size() && GetDirection(line[i + 1]) == Number
					 ? std::u32string_view{U",.:/"}.contains(line[i])
					 : line[i] == '%'))
				directions[i] = Number;
		}

		// A number after left-to-right text, with only neutrals and numbers in between, reads as part of it, as in
		// "Space Cadet 2026" (Unicode rule W7)
		auto strong = RightToLeft;
		for (auto& direction : directions)
		{
			if (direction == LeftToRight || direction == RightToLeft)
				strong = direction;
			else if (direction == Number && strong == LeftToRight)
				direction = LeftToRight;
		}

		// Left to right means a level of 2 and right to left 1. Neutrals between two left-to-right letters or numbers
		// that follow them go left to right, all others right to left: next to a neutral, any other number counts as
		// right to left.
		std::vector<int> levels(line.size(), 1);
		for (size_t i = 0; i < line.size();)
		{
			if (directions[i] != Neutral)
			{
				levels[i] = directions[i] == RightToLeft ? 1 : 2;
				i++;
				continue;
			}
			auto end = i;
			while (end < line.size() && directions[end] == Neutral)
				end++;
			auto before = i > 0 && directions[i - 1] == LeftToRight;
			auto after = end < line.size() && directions[end] == LeftToRight;
			for (; i < end; i++)
				levels[i] = before && after ? 2 : 1;
		}

		// Reverses the left-to-right runs, then the whole line, so they come back in their own order
		std::u32string visual = line;
		for (size_t i = 0; i < visual.size();)
		{
			auto end = i;
			while (end < visual.size() && levels[end] == 2)
				end++;
			std::reverse(visual.begin() + static_cast<ptrdiff_t>(i), visual.begin() + static_cast<ptrdiff_t>(end));
			i = end == i ? i + 1 : end;
		}
		std::reverse(visual.begin(), visual.end());
		std::reverse(levels.begin(), levels.end());
		std::u32string drawn;
		drawn.reserve(visual.size());
		for (size_t i = 0; i < visual.size(); i++)
		{
			// Direction marks have done their job and have nothing to draw
			if (visual[i] == 0x200E || visual[i] == 0x200F)
				continue;
			drawn += levels[i] == 1 ? Mirror(visual[i]) : visual[i];
		}
		return drawn;
	}

	// The code points of UTF-8 text
	std::u32string Decode(std::string_view text)
	{
		std::u32string decoded;
		decoded.reserve(text.size());
		auto end = text.data() + text.size();
		for (auto c = text.data(); c < end;)
		{
			unsigned int codePoint;
			c += ImTextCharFromUtf8(&codePoint, c, end);
			decoded += static_cast<char32_t>(codePoint);
		}
		return decoded;
	}

	// The code points as UTF-8 text
	std::string Encode(const std::u32string& text)
	{
		std::string encoded;
		encoded.reserve(text.size() * 2);
		char buffer[5];
		for (auto c : text)
			encoded.append(buffer, ImTextCharToUtf8(buffer, static_cast<unsigned int>(c)));
		return encoded;
	}
} // namespace

// Whether the language chosen is Arabic or Hebrew
bool DefinitiveText::RightToLeft()
{
	auto language = translations::GetCurrentLanguage()->Language;
	return language == Lang::Arabic || language == Lang::Hebrew;
}

// The text in drawing order, as Visual gives it but without the cache: Arabic letters joined, then each line
// reordered on its own; text with nothing right to left comes back as it is
std::string DefinitiveText::Shape(std::string_view text)
{
	auto decoded = Decode(text);
	if (std::none_of(decoded.begin(), decoded.end(),
					 [](char32_t c) { return GetDirection(c) == Direction::RightToLeft; }))
		return std::string{text};

	auto joined = JoinArabic(decoded);
	std::u32string visual;
	for (size_t start = 0; start <= joined.size();)
	{
		auto end = std::min(joined.find(U'\n', start), joined.size());
		visual += ReorderLine(joined.substr(start, end - start));
		if (end < joined.size())
			visual += U'\n';
		start = end + 1;
	}
	return Encode(visual);
}

// The text shaped for drawing, from a cache that is emptied once it holds more than 512 texts; the pointer is good
// until the next call
const char* DefinitiveText::Visual(const char* text)
{
	// Menus draw the same strings every frame; numbers that change would fill the cache, so it starts over now and then
	static std::unordered_map<std::string, std::string> cache;
	if (cache.size() > 512)
		cache.clear();
	auto found = cache.find(text);
	if (found == cache.end())
		found = cache.try_emplace(text, Shape(text)).first;
	return found->second.c_str();
}
