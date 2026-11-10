#include "pch.h"
#include "TTextBox.h"

#include "control.h"
#include "fullscrn.h"
#include "loader.h"
#include "pb.h"
#include "render.h"
#include "score.h"
#include "timer.h"


// Reads the box's position and size from attribute 1500 of the group for the current resolution; without them the
// box stays empty
TTextBox::TTextBox(TPinballTable* table, int groupIndex) : TPinballComponent(table, groupIndex, true)
{
	OffsetX = 0;
	OffsetY = 0;
	Width = 0;
	Height = 0;
	BgBmp = render::background_bitmap;
	Font = score::msg_fontp;
	CurrentMessage = nullptr;
	PreviousMessage = nullptr;
	Timer = 0;

	if (groupIndex > 0)
	{
		/*Full tilt: text box dimensions index is offset by resolution*/
		int arrLength;
		auto dimensions = loader::query_iattribute(groupIndex + fullscrn::GetResolution(), 1500, &arrLength);
		// A damaged table may lack them; the box then stays empty
		if (dimensions && arrLength >= 4)
		{
			OffsetX = dimensions[0];
			OffsetY = dimensions[1];
			Width = dimensions[2];
			Height = dimensions[3];
		}
	}
}

// Stops the timer and frees the queued messages
TTextBox::~TTextBox()
{
	if (Timer)
	{
		if (Timer != -1)
			timer::kill(Timer);
		Timer = 0;
	}
	while (CurrentMessage)
	{
		TTextBoxMessage* message = CurrentMessage;
		TTextBoxMessage* nextMessage = message->NextMessage;
		delete message;
		CurrentMessage = nextMessage;
	}
}

// Takes no messages of its own; on the bonus tables it keeps the message field per player
int TTextBox::Message(MessageCode code, float value)
{
	if (pb::BonusTable())
		KeepMessageFieldPerPlayer(code, value);
	return 0;
}

// Timer callback at the end of the current message's time: drops it, draws the next one and tells the rules
void TTextBox::TimerExpired(int timerId, void* caller)
{
	auto tb = static_cast<TTextBox*>(caller);
	TTextBoxMessage* message = tb->CurrentMessage;
	tb->Timer = 0;
	if (message)
	{
		TTextBoxMessage* nextMessage = message->NextMessage;
		delete message;
		tb->CurrentMessage = nextMessage;
		tb->Draw();
		control::handler(MessageCode::ControlTimerExpired, tb);
	}
}

// Erases the box, stops the timer and drops the queued messages, or only the leading low-priority ones if
// <lowPriorityOnly>; what is left, and an overlay, are drawn again
void TTextBox::Clear(bool lowPriorityOnly)
{
	gdrv_bitmap8* bmp = BgBmp;
	if (bmp)
		gdrv::copy_bitmap(
			render::vscreen,
			Width,
			Height,
			OffsetX,
			OffsetY,
			bmp,
			OffsetX,
			OffsetY);
	else
		gdrv::fill_bitmap(render::vscreen, Width, Height, OffsetX, OffsetY, 0);
	if (Timer)
	{
		if (Timer != -1)
			timer::kill(Timer);
		Timer = 0;
	}
	while (CurrentMessage && (!lowPriorityOnly || CurrentMessage->LowPriority))
	{
		auto message = CurrentMessage;
		CurrentMessage = message->NextMessage;
		delete message;
	}
	if (CurrentMessage || Overlay)
		Draw();
}

void TTextBox::DisplayOverlay(const char* text)
{
	Overlay = text;
	Repaint();
}

void TTextBox::EndOverlay()
{
	Overlay.reset();
	Repaint();
}

// Paints the box again without starting a second timer for the message that shows
void TTextBox::Repaint()
{
	// A running timer means the current message shows; drawing it again would set a second one
	if (Timer)
		Paint(true);
	else
		Draw();
}

// Queues <text> to show for <time> seconds, or with -1 until another message replaces it, and draws it if nothing
// shows; a repeat of the last queued message only gets a new time
void TTextBox::Display(const char* text, float time, bool lowPriority, int fontIndex, const char* words)
{
	if (!text)
		return;

	// The bonus tables' text box (0x41de40 in PIRATES.EXE and DRAGON.EXE, the same in CADET.EXE) also clears its queue
	// for a message without a time, so a mode's help text replaces the picture shown before it; Pinball.exe's waits
	// for it, and Space Cadet keeps that with either game's data
	auto clearsQueue = pb::BonusTable() && time == -1.0f;
	if (IsRepeat(text, fontIndex) && !(clearsQueue && PreviousMessage->Time != -1.0f))
	{
		RefreshRepeat(time);
	}
	else
	{
		if (Timer == -1 || clearsQueue)
			Clear();

		auto message = new TTextBoxMessage(text, time, lowPriority, fontIndex, words);
		if (message->Text)
		{
			if (CurrentMessage)
				PreviousMessage->NextMessage = message;
			else
				CurrentMessage = message;
			PreviousMessage = message;
			if (Timer == 0)
				Draw();
		}
		else
		{
			delete message;
		}
	}
}

// Gives the repeated last message <time> and restarts its timer if it is the one showing
void TTextBox::RefreshRepeat(float time)
{
	PreviousMessage->Refresh(time);
	if (PreviousMessage != CurrentMessage)
		return;
	if (Timer && Timer != -1)
		timer::kill(Timer);
	if (time == -1.0f)
		Timer = -1;
	else
		Timer = timer::set(time, this, TimerExpired);
}

// Draws the overlay or the current message as ImGui text when there is no bitmap font for it; with a font the text
// is already in the frame
void TTextBox::DrawImGui()
{
	// Do nothing when using a font (the text will be rendered to VScreen in TTextBox::Draw)
	const char* text;
	if (Overlay)
	{
		if (Font)
			return;
		text = Overlay->c_str();
	}
	else
	{
		if (!CurrentMessage || CurrentMessage->FontIndex == 1 || MessageFont(CurrentMessage))
			return;
		text = CurrentMessage->Text;
	}

	char windowName[64];
	SDL_Rect rect;
	ImGuiWindowFlags window_flags =
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoInputs;

	rect.x = OffsetX;
	rect.y = OffsetY;
	rect.w = Width;
	rect.h = Height;

	rect = fullscrn::GetScreenRectFromPinballRect(rect);

	ImGui::SetNextWindowPos(ImVec2(static_cast<float>(rect.x), static_cast<float>(rect.y)));
	ImGui::SetNextWindowSize(ImVec2(static_cast<float>(rect.w), static_cast<float>(rect.h)));

	// Use the pointer to generate a window unique name per text box
	snprintf(windowName, sizeof(windowName), "TTextBox_%p", static_cast<void*>(this));
	if (ImGui::Begin(windowName, nullptr, window_flags))
	{
		// Sized with the table; FontScaleDpi would scale it a second time
		const auto& style = ImGui::GetStyle();
		ImGui::PushFont(nullptr, style.FontSizeBase * fullscrn::GetScreenToPinballRatio() / style.FontScaleDpi);

		// ToDo: centered text in FT
		ImGui::PushStyleColor(ImGuiCol_Text, pb::TextBoxColor);
		ImGui::TextWrapped("%s", text);
		ImGui::PopStyleColor();
		ImGui::PopFont();
	}
	ImGui::End();
}

// Skips to the first message still worth showing (an untimed one only if it is the last, a timed one unless it is
// over 2 s late), sets its timer and paints the box
void TTextBox::Draw()
{
	bool display = false;
	while (CurrentMessage)
	{
		if (CurrentMessage->Time == -1.0f)
		{
			if (!CurrentMessage->NextMessage)
			{
				Timer = -1;
				display = true;
				break;
			}
		}
		else if (CurrentMessage->TimeLeft() >= -2.0f)
		{
			Timer = timer::set(std::max(CurrentMessage->TimeLeft(), 0.25f), this, TimerExpired);
			display = true;
			break;
		}

		auto tmp = CurrentMessage;
		CurrentMessage = CurrentMessage->NextMessage;
		delete tmp;
	}

	Paint(display);
}

// Paints the box's background, then the overlay or, if <display>, the current message in its bitmap font, wrapped
// to the box and centered on Full Tilt's data
void TTextBox::Paint(bool display)
{
	if (auto bmp = BgBmp)
		gdrv::copy_bitmap(render::vscreen, Width, Height, OffsetX, OffsetY, bmp, OffsetX, OffsetY);
	else
		gdrv::fill_bitmap(render::vscreen, Width, Height, OffsetX, OffsetY, 0);

	if (display || Overlay)
	{
		auto font = Overlay ? Font : MessageFont(CurrentMessage);
		if (!font)
		{
			// Immediate mode drawing using system font is handled by TTextBox::DrawImGui
			return;
		}

		std::vector<LayoutResult> lines{};
		auto textHeight = 0;
		for (auto text = Overlay ? Overlay->data() : CurrentMessage->Text;
			 text[0] && textHeight + font->Height <= Height; textHeight += font->Height)
		{
			auto line = LayoutTextLine(text, font);
			if (line.Start == line.End)
				break;
			lines.push_back(line);
			text = line.End;
		}

		// Textboxes in FT display texts centered
		auto offY = OffsetY;
		if (pb::FullTiltMode)
			offY += (Height - textHeight) / 2;
		for (auto line : lines)
		{
			auto offX = OffsetX;
			if (pb::FullTiltMode)
				offX += (Width - line.Width) / 2;
			for (auto text = line.Start; text < line.End; text++)
			{
				auto charBmp = font->Chars[static_cast<unsigned char>(*text) % 128];
				if (charBmp)
				{
					auto height = charBmp->Height;
					auto width = charBmp->Width;
					if (render::background_bitmap)
						gdrv::copy_bitmap_w_transparency(render::vscreen, width, height, offX, offY, charBmp, 0,
						                                 0);
					else
						gdrv::copy_bitmap(render::vscreen, width, height, offX, offY, charBmp, 0, 0);
					offX += charBmp->Width + font->GapWidth;
				}
			}
			offY += font->Height;
		}
	}
}

// True when <text> in this font is the last queued message again, while a message shows
bool TTextBox::IsRepeat(const char* text, int fontIndex) const
{
	return CurrentMessage && !strcmp(text, PreviousMessage->Text) && PreviousMessage->FontIndex == fontIndex;
}

// The bitmap font a message is drawn in: the second font for a picture message, else the box's own; null when the
// game data has none
const score_msg_font_type* TTextBox::MessageFont(const TTextBoxMessage* message) const
{
	// A picture message without the second font shows nothing rather than its glyphs as text
	if (message->FontIndex == 1)
		return score::msg_font2p;
	return Font;
}

// Finds how much of <textStart> fits in one line of the box, breaking at a space where it can and at a newline;
// returns the line's start, where the next line starts, and its width in pixels
TTextBox::LayoutResult TTextBox::LayoutTextLine(char* textStart, const score_msg_font_type* font) const
{
	auto lineWidth = 0, wordWidth = 0;
	char *wordBoundary = nullptr, *textEnd;
	for (textEnd = textStart; ; ++textEnd)
	{
		auto maskedChar = textEnd[0] & 0x7F;
		if (!maskedChar || maskedChar == '\n')
			break;

		auto charBmp = font->Chars[maskedChar];
		if (!charBmp)
			continue;

		auto width = lineWidth + charBmp->Width + font->GapWidth;
		if (width > Width)
		{
			if (wordBoundary)
			{
				textEnd = wordBoundary;
				lineWidth = wordWidth;
			}
			break;
		}
		if (maskedChar == ' ')
		{
			wordBoundary = textEnd;
			wordWidth = width;
		}
		lineWidth = width;
	}

	while ((*textEnd & 0x7F) == ' ')
		++textEnd;
	if ((*textEnd & 0x7F) == '\n')
		++textEnd;
	return LayoutResult{textStart, textEnd, lineWidth};
}
