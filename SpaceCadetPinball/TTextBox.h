#pragma once
#include <optional>

#include "score.h"
#include "TPinballComponent.h"
#include "TTextBoxMessage.h"

class TTextBox :
	public TPinballComponent
{
public:
	int OffsetX;
	int OffsetY;
	int Width;
	int Height;
	int Timer;
	gdrv_bitmap8* BgBmp;
	score_msg_font_type* Font;
	TTextBoxMessage* CurrentMessage;
	TTextBoxMessage* PreviousMessage;
	// A text shown over the messages, which Dragon's Keep's quotes and credits use (DRAGON.EXE's 0x41e070): the
	// messages go on underneath and show again once it ends (0x41e0e0)
	std::optional<std::string> Overlay;

	TTextBox(TPinballTable* table, int groupIndex);
	~TTextBox() override;
	int Message(MessageCode code, float value) override;
	void Clear(bool lowPriorityOnly = false);
	void Display(const char* text, float time, bool lowPriority = false, int fontIndex = 0);
	void DisplayOverlay(const char* text);
	void EndOverlay();
	void DrawImGui();

private:
	struct LayoutResult
	{
		char *Start, *End;
		int Width;
	};

	static void TimerExpired(int timerId, void* caller);

	void Draw();
	// Paints the box: the overlay, else the current message if <display>
	void Paint(bool display);
	void Repaint();
	LayoutResult LayoutTextLine(char* textStart, const score_msg_font_type* font) const;
	const score_msg_font_type* MessageFont(const TTextBoxMessage* message) const;
	// The text is the last queued message's, in the same font, while a message shows
	bool IsRepeat(const char* text, int fontIndex) const;
};
