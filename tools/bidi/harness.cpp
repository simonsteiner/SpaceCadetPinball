// tools/check-bidi.sh's harness around DefinitiveText::Shape. Prints each case as its text and its shaped text, each
// ended by a NUL byte: every Hebrew and Arabic string in translations.cpp, the edition hint as winmain composes it,
// then the arguments.
#include "pch.h"
#include "DefinitiveText.h"
#include "translations.h"

#include <utility>

namespace
{
	void Print(const std::string& text)
	{
		auto shaped = DefinitiveText::Shape(text);
		fwrite(text.c_str(), 1, text.size() + 1, stdout);
		fwrite(shaped.c_str(), 1, shaped.size() + 1, stdout);
	}

	// Returns whether the UTF-8 text has a Hebrew or Arabic letter
	bool HasRightToLeft(const char* text)
	{
		// Every Hebrew and Arabic letter is two bytes in UTF-8, led by 0xD6 to 0xDB
		for (auto c = reinterpret_cast<const unsigned char*>(text); *c; c++)
		{
			if (*c >= 0xD6 && *c <= 0xDB)
				return true;
		}
		return false;
	}
} // namespace

// Prints the cases: the Hebrew and Arabic translations, the edition hint in both languages, then the arguments
int main(int argc, char** argv)
{
	using enum Msg;
	for (auto language : {"he", "ar"})
	{
		translations::SetCurrentLanguage(language);
		for (auto id = Min; id < Max; id = static_cast<Msg>(std::to_underlying(id) + 1))
		{
			// Strings the language lacks come back in English
			auto text = translations::GetTranslation(id);
			if (HasRightToLeft(text))
				Print(text);
		}

		// The edition hint names the key, or the menu item when Toggle Edition has no key (winmain::RenderEditionHint)
		auto menu = std::string{translations::GetTranslation(Menu1_Options)} + " > " +
					translations::GetTranslation(Menu1_ToggleEdition);
		for (auto& toggle : {menu, std::string{"Tab"}, std::string{"Ctrl+F2"}})
		{
			std::array<char, 256> formatted;
			snprintf(formatted.data(), formatted.size(), translations::GetTranslation(EditionHint), toggle.c_str());
			Print(formatted.data());
		}
	}
	for (int i = 1; i < argc; i++)
		Print(argv[i]);
	return 0;
}
