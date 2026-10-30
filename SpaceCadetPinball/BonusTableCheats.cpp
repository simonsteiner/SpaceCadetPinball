#include "pch.h"
#include "BonusTableCheats.h"

#include "pb.h"
#include "TPinballTable.h"
#include "TTextBox.h"
#include "timer.h"

namespace
{
	// The credits a minute without a key shows, one text up to each '~'
	constexpr const char* CreditsText =
		"Full Tilt! was created by Cinematronics~for Maxis.~Cinematronics Team:~"
		"Programming\nMichael Sandige\nJohn Taylor~Art\nJohn Frantz Jr.\nRyan Medeiros~Design\nKevin Gliner~"
		"Sound Effects\nMatt Ridgeway~Donald S. Griffin~Design Consultant\nMark Sprenger~Music\nMatt Ridgeway~"
		"Producer\nKevin Gliner~Voices\nMike McGeary\nWilliam Rice~Grand Poobah\nDavid Stafford~"
		"Special Thanks\nPaula Sandige\nAlex St. John~Brad Silverberg\nJeff Camp~Danny Thorpe\nGreg Hospelhorn~"
		"Maxis Team:~Producer\nJohn Csicsery~Product Manager\nLarry Lee~Lead Tester\nMichael Gilmartin~"
		"QA Manager\nAlan Barton~Additional Testing\nJoe Longworth\nScott Shicoff~Owen Nelson\nJohn \"Jussi\" Ylinen~"
		"John Landes\nMarc Meyer~Cathy Castro\nKeith Meyer~Additional Art\nOcean Quigley~"
		"Rick Macaraeg\nCharlie Aquilina~Art Director\nSharon Barr~Install Program\nKevin O'Hare~Intro Music~"
		"Brian Conrad~John Csicsery~Special Thanks\nSam Poole\nJoe Scirica~Jeff Braun\nBob Derber\nAshley Csicsery~"
		"Tom Forge\nWill \"Burr\" Wright~";

	constexpr std::string_view QuoteWord = "QUOTE";
} // namespace

BonusTableCheats::BonusTableCheats(std::initializer_list<std::string_view> words, const char* quotes,
								   bool endQuotesOnce, bool (*fire)(int cheat)) :
	Words(words), Quotes(quotes), EndQuotesOnce(endQuotesOnce), Fire(fire)
{
}

void BonusTableCheats::Key(int key, TPinballTable& table, TTextBox& textBox)
{
	// The tables get Windows' key codes, whose letters are capitals and which SDL's keys without a character don't
	// match
	if (static_cast<unsigned>(key) & SDLK_SCANCODE_MASK)
		key = 0;
	else if (key >= 'a' && key <= 'z')
		key -= 'a' - 'A';

	if (QuotesShown)
	{
		if (EndQuotesOnce)
			QuotesShown = false;
		EndScroll(textBox);
	}

	// A key that isn't the next of the cheat being typed ends it, except a cheat's last key out of turn, which the
	// tables then ignore
	auto ended = true;
	if (KeyFits(key))
	{
		CheatKey = key;
		ended = false;
	}
	else if (auto fired = KeyFires(key); fired < 0)
	{
		ended = false;
	}
	else if (fired > 0 && Words[fired - 1] == QuoteWord)
	{
		QuotesShown = true;
		ScrollTextBox = &textBox;
		Scroll(true);
	}
	else if (fired > 0)
	{
		ended = Fire(fired);
	}
	if (ended)
		CheatKey = 0;
	if (!CheatKey)
		CheatTyped = 0;

	HiddenTest(key, table);

	// The idle credits come this way, as Space Cadet's rules take them
	Typed.push_back(static_cast<char>(key));
	if (Typed.size() > 7)
		Typed.erase(0, 1);
	if (Typed == "CREDITS")
	{
		QuotesShown = false;
		ScrollTextBox = &textBox;
		Scroll(true);
		pb::CreditsActive = true;
	}
}

// Whether <key> is the next of a cheat's keys, but its last; a first key also picks the cheat
bool BonusTableCheats::KeyFits(int key)
{
	for (size_t index = 0; index < Words.size(); index++)
	{
		auto word = Words[index];
		if (key == word.front())
		{
			if (CheatKey)
				return false;
			CheatTyped = static_cast<int>(index) + 1;
			return true;
		}
	}
	for (auto word : Words)
	{
		for (size_t index = 1; index + 1 < word.size(); index++)
		{
			if (key == word[index] && CheatKey == word[index - 1])
				return true;
		}
	}
	return false;
}

// For a cheat's last key: the cheat it fires after the key before it, or -1 out of turn; 0 for any other key
int BonusTableCheats::KeyFires(int key) const
{
	auto last = false;
	for (auto word : Words)
	{
		if (key != word.back())
			continue;
		if (CheatKey == word[word.size() - 2])
			return CheatTyped;
		last = true;
	}
	return last ? -1 : 0;
}

void BonusTableCheats::HiddenTest(int key, TPinballTable& table)
{
	auto& step = HiddenTestStep;
	switch (key)
	{
	case 'H':
		step = step == 0 ? 1 : 0;
		break;
	case 'I':
		step = step == 1 ? 2 : 0;
		break;
	case 'D':
		step = step == 2 || step == 3 ? step + 1 : 0;
		break;
	case 'E':
		step = step == 4 || step == 8 ? step + 1 : 0;
		break;
	case 'N':
		step = step == 5 ? 6 : 0;
		break;
	case '\t':
		// The tables take no space in place of the Tab
		step = step == 6 ? 7 : 0;
		break;
	case 'S':
		step = step == 9 ? 10 : 0;
		break;
	case 'T':
		step = step == 7 || step == 10 ? step + 1 : 0;
		if (step == 11)
		{
			pb::cheat_mode = true;
			table.CheatsUsed = 1;
		}
		break;
	default:
		step = 0;
		break;
	}
}

// 0x422a80 in DRAGON.EXE, 0x422a90 in PIRATES.EXE: the scroller shows the next of the quotes or the credits over
// info_text_box for 2 s; after the last and an empty one, or with <restart>, it starts over
void BonusTableCheats::Scroll(bool restart)
{
	if (ScrollTimer)
		timer::kill(ScrollTimer);
	ScrollTimer = 0;
	if (restart || !ScrollNext)
		ScrollNext = QuotesShown ? Quotes : CreditsText;

	std::string_view rest{ScrollNext};
	auto text = rest.substr(0, rest.find('~'));
	ScrollNext = text.size() < rest.size() ? ScrollNext + text.size() + 1 : nullptr;
	ScrollTextBox->DisplayOverlay(std::string{text}.c_str());
	ScrollTimer = timer::set(2.0f, this, ScrollTimerExpired);
}

void BonusTableCheats::ScrollTimerExpired(int, void* caller)
{
	auto cheats = static_cast<BonusTableCheats*>(caller);
	cheats->ScrollTimer = 0;
	cheats->Scroll(false);
}

void BonusTableCheats::EndScroll(TTextBox& textBox)
{
	if (ScrollTimer)
		timer::kill(ScrollTimer);
	ScrollTimer = 0;
	textBox.EndOverlay();
}

void BonusTableCheats::ToggleAltBall()
{
	AltBall = !AltBall;
}

bool BonusTableCheats::AltBallFrame(bool available)
{
	if (!available)
		AltBall = false;
	return AltBall;
}

void BonusTableCheats::Clear()
{
	CheatKey = 0;
	CheatTyped = 0;
	HiddenTestStep = 0;
	AltBall = false;
	QuotesShown = false;
	ScrollTimer = 0;
	ScrollNext = nullptr;
	Typed.clear();
	ScrollTextBox = nullptr;
}
