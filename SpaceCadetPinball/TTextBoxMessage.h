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
	// What a picture message shows, in words, for Space Cadet 2026's HUD, which draws no pictures
	std::string Words;

	TTextBoxMessage(const char* text, float time, bool lowPriority, int fontIndex = 0, const char* words = nullptr);
	~TTextBoxMessage();
	float TimeLeft() const;
	void Refresh(float time);
};
