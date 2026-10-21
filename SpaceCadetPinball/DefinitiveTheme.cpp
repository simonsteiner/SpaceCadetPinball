#include "pch.h"
#include "DefinitiveTheme.h"

#include "DefinitiveFontData.h"
#include "translations.h"

ImFont* DefinitiveTheme::Text = nullptr;
ImFont* DefinitiveTheme::Heading = nullptr;
ImFont* DefinitiveTheme::Digits = nullptr;

namespace
{
	struct SystemFont
	{
		std::string Path;
		// The face within a collection: Noto Sans CJK's has Japanese, Korean, Simplified and Traditional Chinese
		unsigned int Index;
	};

	// Fonts that come with each system or its usual packages, for the language chosen, best first
	std::vector<SystemFont> SystemCjkFonts(Lang language)
	{
		std::vector<SystemFont> fonts;
#if defined(_WIN32)
		auto windows = SDL_getenv("WINDIR");
		auto folder = std::string{windows ? windows : "C:\\Windows"} + "\\Fonts\\";
		// Arrays, not an initializer_list assigned in each case, whose array would end with the assignment
		static const std::vector<const char*> japanese{"YuGothM.ttc", "meiryo.ttc", "msgothic.ttc"},
			korean{"malgun.ttf", "gulim.ttc"}, simplified{"msyh.ttc", "simsun.ttc"},
			traditional{"msjh.ttc", "mingliu.ttc"};
		auto& names = language == Lang::Japanese            ? japanese
					  : language == Lang::Korean            ? korean
					  : language == Lang::SimplifiedChinese ? simplified
															: traditional;
		for (auto name : names)
			fonts.push_back({folder + name, 0});
#elif defined(__APPLE__)
		switch (language)
		{
		case Lang::Japanese:
			fonts.push_back({"/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc", 0});
			break;
		case Lang::Korean:
			fonts.push_back({"/System/Library/Fonts/AppleSDGothicNeo.ttc", 0});
			break;
		default:
			fonts.push_back({"/System/Library/Fonts/PingFang.ttc", 0});
			break;
		}
		fonts.push_back({"/System/Library/Fonts/Supplemental/Arial Unicode.ttf", 0});
#else
		auto noto = language == Lang::Japanese            ? 0u
					: language == Lang::Korean            ? 1u
					: language == Lang::SimplifiedChinese ? 2u
														  : 3u;
		for (auto path : {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
						  "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
						  "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc"})
			fonts.push_back({path, noto});
		fonts.push_back({"/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf", 0});
		fonts.push_back({"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", 0});
#endif
		return fonts;
	}

	// The fallback font's file, read once for both weights; the atlas reads it for as long as it lives
	std::vector<unsigned char> FallbackData;
	unsigned int FallbackIndex = 0;

	bool ReadFile(const std::string& path, std::vector<unsigned char>& data)
	{
		auto file = fopenu(path.c_str(), "rb");
		if (!file)
			return false;
		fseek(file, 0, SEEK_END);
		auto size = ftell(file);
		fseek(file, 0, SEEK_SET);
		data.resize(size > 0 ? static_cast<size_t>(size) : 0);
		auto read = !data.empty() && fread(data.data(), 1, data.size(), file) == data.size();
		fclose(file);
		if (!read)
			data.clear();
		return read;
	}

	// The Font File option's font, else a system font for Chinese, Japanese or Korean when one of those is the language
	void ReadFallback(const std::string& fontFile)
	{
		FallbackData.clear();
		FallbackIndex = 0;
		if (!fontFile.empty())
		{
			ReadFile(fontFile, FallbackData);
			return;
		}
		auto language = translations::GetCurrentLanguage()->Language;
		if (language != Lang::Japanese && language != Lang::Korean && language != Lang::SimplifiedChinese &&
			language != Lang::TraditionalChinese)
			return;
		for (auto& font : SystemCjkFonts(language))
		{
			if (ReadFile(font.Path, FallbackData))
			{
				FallbackIndex = font.Index;
				return;
			}
		}
	}

	// A font's line height (hhea ascender to descender) in ems, or 0 if the file can't be read. ImGui sizes a font by
	// its line height, so fonts with taller lines come out smaller at the same size.
	float LineHeightInEms(const unsigned char* data, size_t size)
	{
		auto read16 = [&](size_t offset) -> unsigned
		{ return offset + 2 <= size ? static_cast<unsigned>(data[offset]) << 8U | data[offset + 1] : 0U; };
		auto read32 = [&](size_t offset) -> size_t
		{ return offset + 4 <= size ? static_cast<size_t>(read16(offset)) << 16U | read16(offset + 2) : 0U; };
		size_t head = 0, hhea = 0;
		auto tables = static_cast<size_t>(read16(4));
		for (size_t i = 0; i < tables; i++)
		{
			auto entry = 12 + i * 16;
			if (entry + 16 > size)
				return 0;
			if (!memcmp(data + entry, "head", 4))
				head = read32(entry + 8);
			else if (!memcmp(data + entry, "hhea", 4))
				hhea = read32(entry + 8);
		}
		auto unitsPerEm = head ? read16(head + 18) : 0;
		if (!unitsPerEm || !hhea)
			return 0;
		auto ascender = static_cast<int16_t>(read16(hhea + 4)), descender = static_cast<int16_t>(read16(hhea + 6));
		return static_cast<float>(ascender - descender) / static_cast<float>(unitsPerEm);
	}

	ImFont* AddEmbedded(ImFontAtlas* atlas, const char* name, const unsigned char* data, unsigned int size, bool merge)
	{
		// A merged font is scaled to the main one's em, so every script has letters of the same size
		static float mainLineHeight = 0;
		auto lineHeight = LineHeightInEms(data, size);
		if (!merge)
			mainLineHeight = lineHeight;
		ImFontConfig config{};
		if (merge && mainLineHeight > 0 && lineHeight > 0)
			config.ExtraSizeScale = lineHeight / mainLineHeight;
		// The arrays are the program's; the atlas only reads them
		config.FontDataOwnedByAtlas = false;
		config.MergeMode = merge;
		// ImGui's own font is 13 pixels, and table text boxes drawn with ImGui size themselves from the base size
		config.SizePixels = 13;
		ImFormatString(config.Name, IM_COUNTOF(config.Name), "%s", name);
		return atlas->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), static_cast<int>(size), 0, &config);
	}

	ImFont* AddTextFont(ImFontAtlas* atlas, const char* name, const unsigned char* data, unsigned int size)
	{
		auto font = AddEmbedded(atlas, name, data, size, false);
		AddEmbedded(atlas, "Noto Sans Greek", DefinitiveFont_NotoSans_Greek, DefinitiveFont_NotoSans_Greek_Size, true);
		AddEmbedded(atlas, "Noto Sans Hebrew", DefinitiveFont_NotoSansHebrew_Regular,
					DefinitiveFont_NotoSansHebrew_Regular_Size, true);
		AddEmbedded(atlas, "Noto Sans Arabic", DefinitiveFont_NotoSansArabic_Regular,
					DefinitiveFont_NotoSansArabic_Regular_Size, true);

		// Last, so it only fills in what the embedded fonts lack
		if (!FallbackData.empty())
		{
			ImFontConfig config{};
			config.FontDataOwnedByAtlas = false;
			config.MergeMode = true;
			config.SizePixels = 13;
			config.FontNo = FallbackIndex;
			atlas->AddFontFromMemoryTTF(FallbackData.data(), static_cast<int>(FallbackData.size()), 0, &config);
		}
		return font;
	}

	ImU32 WithAlpha(ImU32 color, float alpha)
	{
		return (color & 0x00FFFFFFU) | static_cast<ImU32>(std::lround(alpha * 255.0f)) << 24U;
	}

	std::string Ghost(const char* text)
	{
		std::string ghost{text};
		for (auto& c : ghost)
		{
			if ((c >= '0' && c <= '9') || c == '!')
				c = '8';
		}
		return ghost;
	}
} // namespace

void DefinitiveTheme::AddFonts(ImFontAtlas* atlas, const std::string& fontFile)
{
	ReadFallback(fontFile);
	Text = AddTextFont(atlas, "Exo 2", DefinitiveFont_Exo2_Regular, DefinitiveFont_Exo2_Regular_Size);
	Heading = AddTextFont(atlas, "Exo 2 SemiBold", DefinitiveFont_Exo2_SemiBold, DefinitiveFont_Exo2_SemiBold_Size);
	Digits = AddEmbedded(atlas, "DSEG7 Classic Bold", DefinitiveFont_DSEG7Classic_Bold,
						 DefinitiveFont_DSEG7Classic_Bold_Size, false);
}

ImFont* DefinitiveTheme::TextFont()
{
	return Text;
}

ImFont* DefinitiveTheme::HeadingFont()
{
	return Heading;
}

ImFont* DefinitiveTheme::DigitsFont()
{
	return Digits;
}

void DefinitiveTheme::ApplyStyle(ImGuiStyle& style)
{
	auto color = [](ImU32 value, float alpha = 1.0f)
	{ return ImGui::ColorConvertU32ToFloat4(WithAlpha(value, alpha)); };
	auto& colors = style.Colors;
	colors[ImGuiCol_Text] = color(Chrome50);
	colors[ImGuiCol_TextDisabled] = color(Chrome400);
	colors[ImGuiCol_WindowBg] = color(Space900, 0.96f);
	colors[ImGuiCol_ChildBg] = color(Space900, 0);
	colors[ImGuiCol_PopupBg] = color(Space900, 0.98f);
	colors[ImGuiCol_Border] = color(Chrome400, 0.35f);
	colors[ImGuiCol_FrameBg] = color(Space950);
	colors[ImGuiCol_FrameBgHovered] = color(Space800);
	colors[ImGuiCol_FrameBgActive] = color(Space700);
	colors[ImGuiCol_TitleBg] = color(Space900);
	colors[ImGuiCol_TitleBgActive] = color(Space800);
	colors[ImGuiCol_TitleBgCollapsed] = color(Space900, 0.75f);
	colors[ImGuiCol_MenuBarBg] = color(Space900);
	colors[ImGuiCol_ScrollbarBg] = color(Space950, 0.6f);
	colors[ImGuiCol_ScrollbarGrab] = color(Chrome600);
	colors[ImGuiCol_ScrollbarGrabHovered] = color(Chrome400);
	colors[ImGuiCol_ScrollbarGrabActive] = color(Chrome200);
	colors[ImGuiCol_CheckMark] = color(LedOrange);
	colors[ImGuiCol_SliderGrab] = color(Chrome200);
	colors[ImGuiCol_SliderGrabActive] = color(LedOrange);
	colors[ImGuiCol_Button] = color(Space800);
	colors[ImGuiCol_ButtonHovered] = color(Space700);
	colors[ImGuiCol_ButtonActive] = color(Space600);
	colors[ImGuiCol_Header] = color(Space700);
	colors[ImGuiCol_HeaderHovered] = color(Space700);
	colors[ImGuiCol_HeaderActive] = color(Space600);
	colors[ImGuiCol_Separator] = color(Chrome600);
	colors[ImGuiCol_SeparatorHovered] = color(Chrome400);
	colors[ImGuiCol_SeparatorActive] = color(LedOrange);
	colors[ImGuiCol_ResizeGrip] = color(Chrome600, 0.4f);
	colors[ImGuiCol_ResizeGripHovered] = color(Chrome400, 0.6f);
	colors[ImGuiCol_ResizeGripActive] = color(LedOrange);
	colors[ImGuiCol_Tab] = color(Space800);
	colors[ImGuiCol_TabHovered] = color(Space700);
	colors[ImGuiCol_TabSelected] = color(Space600);
	colors[ImGuiCol_TabSelectedOverline] = color(LedOrange);
	colors[ImGuiCol_TabDimmed] = color(Space900);
	colors[ImGuiCol_TabDimmedSelected] = color(Space800);
	colors[ImGuiCol_TableHeaderBg] = color(Space800);
	colors[ImGuiCol_TableBorderStrong] = color(Chrome600);
	colors[ImGuiCol_TableBorderLight] = color(Space700);
	colors[ImGuiCol_TextSelectedBg] = color(LedOrange, 0.35f);
	colors[ImGuiCol_NavCursor] = color(LedOrange);
	colors[ImGuiCol_ModalWindowDimBg] = color(Space950, 0.6f);
	colors[ImGuiCol_PlotLines] = color(Chrome200);
	colors[ImGuiCol_PlotLinesHovered] = color(LedOrange);
	colors[ImGuiCol_PlotHistogram] = color(LedOrange);
	colors[ImGuiCol_PlotHistogramHovered] = color(LedRed);

	style.WindowRounding = Rounding;
	style.ChildRounding = Rounding;
	style.PopupRounding = Rounding;
	style.FrameRounding = 4;
	style.GrabRounding = 4;
	style.TabRounding = 4;
	style.ScrollbarRounding = Rounding;
	style.WindowBorderSize = 1;
	style.PopupBorderSize = 1;
}

void DefinitiveTheme::DrawLamp(ImDrawList* drawList, ImVec2 centre, float radius, Lamp state)
{
	if (state == Lamp::Off)
	{
		drawList->AddCircle(centre, radius - 0.5f, Chrome600, 0, 1);
		return;
	}
	auto color = state == Lamp::Warning ? LedRed : LedOrange;
	if (state == Lamp::Dark)
	{
		drawList->AddCircleFilled(centre, radius, WithAlpha(color, 0.15f));
		drawList->AddCircle(centre, radius - 0.5f, WithAlpha(color, 0.35f), 0, 1);
		return;
	}
	// A soft glow around a lit core, with a hot spot towards the light
	drawList->AddCircleFilled(centre, radius * 2.4f, WithAlpha(color, 0.08f));
	drawList->AddCircleFilled(centre, radius * 1.6f, WithAlpha(color, 0.22f));
	drawList->AddCircleFilled(centre, radius, color);
	drawList->AddCircleFilled(ImVec2{centre.x - radius * 0.25f, centre.y - radius * 0.3f}, radius * 0.45f,
							  DefinitiveColor(0xFF, 0xE2, 0xB8, 0xD0));
}

void DefinitiveTheme::DrawPanel(ImDrawList* drawList, ImVec2 min, ImVec2 max)
{
	drawList->AddRectFilled(min, max, WithAlpha(Space900, 0.96f), Rounding);
	drawList->AddRect(min, max, WithAlpha(Chrome400, 0.35f), Rounding);
	// The light catches the top edge, and the bottom one falls into shadow
	drawList->AddLine(ImVec2{min.x + Rounding, min.y + 0.5f}, ImVec2{max.x - Rounding, min.y + 0.5f},
					  WithAlpha(Chrome50, 0.55f));
	drawList->AddLine(ImVec2{min.x + Rounding, max.y - 0.5f}, ImVec2{max.x - Rounding, max.y - 0.5f},
					  WithAlpha(Space950, 0.9f));
}

void DefinitiveTheme::DrawLed(ImDrawList* drawList, ImVec2 position, float size, const char* text, bool red)
{
	auto color = red ? LedRed : LedOrange;
	auto ghost = Ghost(text);
	drawList->AddText(Digits, size, position, WithAlpha(color, 0.1f), ghost.c_str());
	drawList->AddText(Digits, size, position, color, text);
}

ImVec2 DefinitiveTheme::LedSize(float size, const char* text)
{
	auto ghost = Ghost(text);
	return Digits->CalcTextSizeA(size, FLT_MAX, 0, ghost.c_str());
}
