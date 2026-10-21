#pragma once

#include "DefinitiveTheme.h"

#include <initializer_list>

// The pieces Space Cadet 2026's screens are made of, on top of ImGui: a scale that follows the window, menu rows that
// light their lamp when they have focus, text in the visual language, and hints for the input device last used.
// Every row works with the keyboard and gamepad through ImGui's navigation, and with the mouse and touch.
class DefinitiveUi
{
public:
	enum class Device
	{
		Keyboard,
		Gamepad,
		Pointer,
	};

	// What a hint at the bottom of a screen stands for; it shows the key or button for it on the device last used
	enum class Action
	{
		Choose,
		Select,
		Back,
		Change,
		Group,
	};

	struct Hint
	{
		Action Does;
		const char* Label;
	};

	// ImGui flags ORed together as unsigned values, which bugprone-signed-bitwise accepts
	template <typename... Flags> static constexpr int Combine(Flags... flags)
	{
		return static_cast<int>((0U | ... | static_cast<unsigned>(flags)));
	}

	// Once a frame, before any screen draws
	static void NewFrame();
	// Sizes are given for a window 720 pixels high and multiplied by this: the window's height over 720, times the UI
	// Scale option, within limits
	static float Scale();
	static void SetScale(float scale);
	static Device LastDevice();

	// A font size for ImGui::PushFont that comes out at size * Scale() pixels, whatever ImGui's own scaling is
	static float FontSize(float size);
	static ImVec2 TextSize(ImFont* font, float size, const char* text);
	// Text in the order it is drawn in (DefinitiveText), right-aligned in width for right-to-left languages
	static void DrawText(ImDrawList* drawList, ImFont* font, float size, ImVec2 position, ImU32 color, const char* text,
						 float width = 0);

	// A menu row as wide as ImGui's item width, with a lamp that lights while the row has focus; true when chosen.
	// With an aside, a short text such as a key, at the row's end; trailing keeps room there for a screen's own
	// controls. A warning row lights its lamp red, for choices that end something.
	static bool MenuRow(const char* id, const char* label, bool warning = false, const char* aside = nullptr,
						bool enabled = true, float trailing = 0);
	// A second line under the next row's label, for a short explanation
	static void NextRowNote(const char* note);
	// A row with an on/off switch at its end; true when it should flip: chosen, or left or right toward the other state
	static bool ToggleRow(const char* id, const char* label, bool on, bool enabled = true);
	// A row with its choices side by side; returns the choice made, else current. Left and right step through them,
	// choosing the row steps on, and a tap picks one.
	static int ChoiceRow(const char* id, const char* label, std::initializer_list<const char*> choices, int current,
						 bool enabled = true);
	// A row with a slider; returns the new value. Left and right step, and a tap or a drag on the track sets it.
	static int SliderRow(const char* id, const char* label, int value, int min, int max, int step,
						 const char* format = "%d");
	// -1 or 1 when left or right was pressed this frame while the last row has focus, else 0
	static int RowStep();

	// The row the last MenuRow drew, and whether it has focus; for rows with their own controls on the right
	static ImRect LastRow();
	static bool LastRowFocused();
	// Gives the next row focus, as when a screen opens
	static void FocusNextRow();

	// A line of hints at a position, in key caps for the keyboard and button glyphs for a gamepad; nothing for a
	// mouse or touch
	static void DrawHints(ImDrawList* drawList, ImVec2 position, std::initializer_list<Hint> hints);

private:
	static float LayoutScale;
	static Device Last;
	static ImRect Row;
	static bool RowFocused, FocusNext;
	static const char* Note;
};
