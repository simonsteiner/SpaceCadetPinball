#pragma once
class TTextBoxMessage
{
public:
	TTextBoxMessage* NextMessage;
	char* Text;
	float Time;
	int EndTicks;
	bool LowPriority;
	// CADET.EXE's font index: 0 the box's own font, 1 the second font's pictures (score::msg_font2p)
	int FontIndex;

	TTextBoxMessage(const char* text, float time, bool lowPriority, int fontIndex = 0);
	~TTextBoxMessage();
	float TimeLeft() const;
	void Refresh(float time);
};
