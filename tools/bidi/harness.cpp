// tools/check-bidi.sh's harness around DefinitiveText::Shape. Prints each case as its text and its shaped text, each
// ended by a NUL byte: every Hebrew and Arabic string in translations.cpp, the edition hint as winmain composes it,
// then the arguments.
#include "pch.h"
#include "DefinitiveText.h"
#include "translations.h"

namespace
{
	void Print(const std::string& text)
	{
		auto shaped = DefinitiveText::Shape(text);
		fwrite(text.c_str(), 1, text.size() + 1, stdout);
		fwrite(shaped.c_str(), 1, shaped.size() + 1, stdout);
	}

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

int main(int argc, char** argv)
{
	for (auto language : {"he", "ar"})
	{
		translations::SetCurrentLanguage(language);
		for (auto id = Msg::Min; id < Msg::Max; id = static_cast<Msg>(static_cast<int>(id) + 1))
		{
			// Strings the language lacks come back in English
			auto text = translations::GetTranslation(id);
			if (HasRightToLeft(text))
				Print(text);
		}

		// The edition hint names the key, or the menu item when Toggle Edition has no key (winmain::RenderEditionHint)
		auto menu = std::string{translations::GetTranslation(Msg::Menu1_Options)} + " > " +
					translations::GetTranslation(Msg::Menu1_ToggleEdition);
		for (auto& toggle : {menu, std::string{"Tab"}, std::string{"Ctrl+F2"}})
		{
			char formatted[256];
			snprintf(formatted, sizeof formatted, translations::GetTranslation(Msg::EditionHint), toggle.c_str());
			Print(formatted);
		}
	}
	for (int i = 1; i < argc; i++)
		Print(argv[i]);
	return 0;
}
