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
	constexpr ArabicLetter ArabicLetters[] = {
		{0xFE80, false}, {0xFE81, false}, {0xFE83, false}, {0xFE85, false}, {0xFE87, false}, {0xFE89, true},
		{0xFE8D, false}, {0xFE8F, true},  {0xFE93, false}, {0xFE95, true},  {0xFE99, true},  {0xFE9D, true},
		{0xFEA1, true},  {0xFEA5, true},  {0xFEA9, false}, {0xFEAB, false}, {0xFEAD, false}, {0xFEAF, false},
		{0xFEB1, true},  {0xFEB5, true},  {0xFEB9, true},  {0xFEBD, true},  {0xFEC1, true},  {0xFEC5, true},
		{0xFEC9, true},  {0xFECD, true},  {0, false},      {0, false},      {0, false},      {0, false},
		{0, false},      {0, false},      {0xFED1, true},  {0xFED5, true},  {0xFED9, true},  {0xFEDD, true},
		{0xFEE1, true},  {0xFEE5, true},  {0xFEE9, true},  {0xFEED, false}, {0xFEEF, false}, {0xFEF1, true},
	};
	constexpr char32_t Hamza = 0x0621, Yeh = 0x064A, Tatweel = 0x0640, Lam = 0x0644;

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

	bool JoinsForward(char32_t c)
	{
		auto letter = GetArabicLetter(c);
		return c == Tatweel || (letter && letter->DualJoining);
	}

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
			auto form = joinsBefore && joinsAfter ? 3 : joinsAfter ? 2 : joinsBefore ? 1 : 0;
			joined += static_cast<char32_t>(letter->Isolated + form);
		}
		return joined;
	}

	Direction GetDirection(char32_t c)
	{
		if ((c >= '0' && c <= '9') || (c >= 0x0660 && c <= 0x0669) || (c >= 0x06F0 && c <= 0x06F9))
			return Direction::Number;
		if ((c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF) || c == 0x200F)
			return Direction::RightToLeft;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == 0x200E ||
			(c >= 0xC0 && c != 0xD7 && c != 0xF7 && (c < 0x2000 || c > 0x2BFF) && (c < 0x3000 || c > 0x303F)))
			return Direction::LeftToRight;
		return Direction::Neutral;
	}

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
		std::vector<Direction> directions(line.size());
		for (size_t i = 0; i < line.size(); i++)
		{
			directions[i] = GetDirection(line[i]);
			// A number's separators and signs read with it, as in 1,000 or 50%
			if (directions[i] == Direction::Neutral && i > 0 && directions[i - 1] == Direction::Number &&
				(i + 1 < line.size() && GetDirection(line[i + 1]) == Direction::Number
					 ? std::u32string_view{U",.:/"}.find(line[i]) != std::u32string_view::npos
					 : line[i] == '%'))
				directions[i] = Direction::Number;
		}

		// A number after left-to-right text, with only neutrals and numbers in between, reads as part of it, as in
		// "Space Cadet 2026" (Unicode rule W7)
		auto strong = Direction::RightToLeft;
		for (auto& direction : directions)
		{
			if (direction == Direction::LeftToRight || direction == Direction::RightToLeft)
				strong = direction;
			else if (direction == Direction::Number && strong == Direction::LeftToRight)
				direction = Direction::LeftToRight;
		}

		// Left to right means a level of 2 and right to left 1. Neutrals between two left-to-right letters or numbers
		// that follow them go left to right, all others right to left: next to a neutral, any other number counts as
		// right to left.
		std::vector<int> levels(line.size(), 1);
		for (size_t i = 0; i < line.size();)
		{
			if (directions[i] != Direction::Neutral)
			{
				levels[i] = directions[i] == Direction::RightToLeft ? 1 : 2;
				i++;
				continue;
			}
			auto end = i;
			while (end < line.size() && directions[end] == Direction::Neutral)
				end++;
			auto before = i > 0 && directions[i - 1] == Direction::LeftToRight;
			auto after = end < line.size() && directions[end] == Direction::LeftToRight;
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

bool DefinitiveText::RightToLeft()
{
	auto language = translations::GetCurrentLanguage()->Language;
	return language == Lang::Arabic || language == Lang::Hebrew;
}

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

const char* DefinitiveText::Visual(const char* text)
{
	// Menus draw the same strings every frame; numbers that change would fill the cache, so it starts over now and then
	static std::unordered_map<std::string, std::string> cache;
	if (cache.size() > 512)
		cache.clear();
	auto found = cache.find(text);
	if (found == cache.end())
		found = cache.emplace(text, Shape(text)).first;
	return found->second.c_str();
}
