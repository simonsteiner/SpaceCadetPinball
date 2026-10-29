#include "pch.h"
#include "DefinitiveStats.h"

#include "DefinitiveHud.h"
#include "options.h"
#include "TableView.h"
#include "translations.h"

#include <format>
#include <utility>

namespace
{
	using Stat = DefinitiveStats::Stat;

	constexpr const char* StatKeys[] = {"Games",      "Missions",   "Best Rank", "Balls Saved",  "Extra Balls",
										"Multiballs", "Best Score", "Quests",    "Dragons Slain"};
	static_assert(std::size(StatKeys) == static_cast<size_t>(Stat::Count));

	// The order is the list's on screen; keys are saved, so they stay as they are
	using enum DefinitiveStats::Offer;
	constexpr DefinitiveStats::Achievement AchievementList[] = {
		{"First Game", Msg::Definitive_AchFirstGame, Msg::Definitive_AchFirstGameText, Every},
		{"First Mission", Msg::Definitive_AchFirstMission, Msg::Definitive_AchFirstMissionText, SpaceCadet},
		{"Five Missions", Msg::Definitive_AchFiveMissions, Msg::Definitive_AchFiveMissionsText, SpaceCadet},
		{"Every Mission", Msg::Definitive_AchEveryMission, Msg::Definitive_AchEveryMissionText, SpaceCadet},
		{"First Quest", Msg::Definitive_AchFirstQuest, Msg::Definitive_AchFirstQuestText, DragonsKeep},
		{"Five Quests", Msg::Definitive_AchFiveQuests, Msg::Definitive_AchFiveQuestsText, DragonsKeep},
		{"Every Quest", Msg::Definitive_AchEveryQuest, Msg::Definitive_AchEveryQuestText, DragonsKeep},
		{"Dragon Slayer", Msg::Definitive_AchDragonSlayer, Msg::Definitive_AchDragonSlayerText, DragonsKeep},
		{"Ensign", Msg::Definitive_AchEnsign, Msg::Definitive_AchEnsignText, SpaceCadet},
		{"Captain", Msg::Definitive_AchCaptain, Msg::Definitive_AchCaptainText, SpaceCadet},
		{"Commodore", Msg::Definitive_AchCommodore, Msg::Definitive_AchCommodoreText, SpaceCadet},
		{"Fleet Admiral", Msg::Definitive_AchFleetAdmiral, Msg::Definitive_AchFleetAdmiralText, SpaceCadet},
		{"Ball Saved", Msg::Definitive_AchBallSaved, Msg::Definitive_AchBallSavedText, Every},
		{"Extra Ball", Msg::Definitive_AchExtraBall, Msg::Definitive_AchExtraBallText, Every},
		{"Multiball", Msg::Definitive_AchMultiball, Msg::Definitive_AchMultiballText, Every},
		{"Million", Msg::Definitive_AchMillion, Msg::Definitive_AchMillionText, Every},
		{"Ten Million", Msg::Definitive_AchTenMillion, Msg::Definitive_AchTenMillionText, Every},
		{"Hundred Million", Msg::Definitive_AchHundredMillion, Msg::Definitive_AchHundredMillionText, Every},
		{"Steady Hands", Msg::Definitive_AchSteadyHands, Msg::Definitive_AchSteadyHandsText, Every},
		{"Regular", Msg::Definitive_AchRegular, Msg::Definitive_AchRegularText, Every},
	};

	// The game being played: whether one is, and what counts per game, by player
	struct Game
	{
		bool Active;
		bool Tilted;
		std::array<int, 4> Missions;
		// Dragon's Keep's quests done, by player
		std::array<int, 4> Quests;
		// Each player's billions: the table counts them apart from the score, which drops by a billion as one passes
		std::array<long long, 4> Billions;
	};

	TableView Previous{};
	bool HavePrevious = false;
	Game Current{};
	// Unlocked achievements waiting for their notice, in the order unlocked
	std::vector<int> Waiting;

	int ActiveBalls(const TableView& view)
	{
		return static_cast<int>(
			std::count_if(view.Balls.begin(), view.Balls.end(), [](const auto& ball) { return ball.Active; }));
	}

	bool Playing(const TableView& view)
	{
		return !view.GameOver && !view.Demo && view.PlayerCount > 0;
	}

	int PlayerOf(const TableView& view)
	{
		return std::clamp(view.CurrentPlayer, 0, static_cast<int>(view.Scores.size()) - 1);
	}
} // namespace

std::string DefinitiveStats::Key(const char* name)
{
	// Per table, like the high scores
	return std::string{options::TableKeyPrefix()} + "Stats." + name;
}

std::string DefinitiveStats::QuestKey(int quest)
{
	return Key(std::format("Quest {}", quest).c_str());
}

int DefinitiveStats::Read(const std::string& key)
{
	// Reading a missing setting would save it, so a stat that never happened stays out of the file
	return options::HasSetting(key) ? options::get_int(key.c_str(), 0) : 0;
}

void DefinitiveStats::Add(const std::string& key, int amount)
{
	options::set_int(key.c_str(), Read(key) + amount);
}

void DefinitiveStats::Raise(const std::string& key, int value)
{
	if (value > Read(key))
		options::set_int(key.c_str(), value);
}

void DefinitiveStats::Unlock(const char* key)
{
	auto name = Key((std::string{"Achievement."} + key).c_str());
	if (options::HasSetting(name))
		return;
	SDL_Time now = 0;
	SDL_GetCurrentTime(&now);
	// Seconds as text, which outlast 2038; a clock before 1970 still counts as unlocked
	options::SetSetting(name, std::to_string(std::max<long long>(1, SDL_NS_TO_SECONDS(now))));
	// The notice shows only in Space Cadet 2026, so one unlocked in Classic doesn't wait for a later switch
	if (options::GetEdition() != Editions::Definitive)
		return;
	for (size_t index = 0; index < std::size(AchievementList); index++)
	{
		if (std::strcmp(AchievementList[index].Key, key) == 0)
			Waiting.push_back(static_cast<int>(index));
	}
}

void DefinitiveStats::Update(const TableView& view)
{
	if (!HavePrevious)
	{
		Previous = view;
		HavePrevious = true;
		return;
	}

	// A game starts when play starts, or when a new game sets every score back to 0; a demo game never counts, nor
	// does one a cheat was used in, as the high scores don't take it
	auto playing = Playing(view);
	auto wasPlaying = Playing(Previous);
	auto zeroed = std::ranges::all_of(view.Scores, [](int score) { return score == 0; });
	auto restarted = zeroed && std::ranges::any_of(Previous.Scores, [](int score) { return score != 0; });
	if (playing && (!wasPlaying || restarted))
		Current = Game{true, false, {}, {}, {}};
	if (view.CheatsUsed)
		Current.Active = false;

	if (playing && Current.Active)
	{
		// A score that drops by most of a billion has passed one
		for (size_t index = 0; index < view.Scores.size(); index++)
		{
			if (Previous.Scores[index] - view.Scores[index] > 900'000'000)
				Current.Billions[index]++;
		}

		auto player = PlayerOf(Previous);
		auto samePlayer = view.CurrentPlayer == Previous.CurrentPlayer;
		// Dragon's Keep's view has no mission (-1) or rank (0), so Space Cadet's counting below finds none there
		if (view.Kind == TableView::TableKind::DragonsKeep)
			UpdateDragon(view, player, samePlayer);
		// A mission ends finished when it lit lamps of the progress ring, or a new rank, which clears them; a mission
		// that runs out of fuel or ends with the ball does neither. Time Warp's way back (state 24) ends through the
		// kickout instead, which costs a rank. On a drain the next player's lamps come in, so only the same player's
		// count.
		auto mission = DefinitiveHud::MissionOf(Previous.Mission);
		auto finished = view.RankProgress > Previous.RankProgress || view.Rank > Previous.Rank ||
						(Previous.Mission == 24 && view.Rank < Previous.Rank);
		if (mission && !DefinitiveHud::MissionOf(view.Mission) && samePlayer && finished)
		{
			Add(Key("Missions"), 1);
			Add(Key(("Mission " + std::to_string(mission)).c_str()), 1);
			Current.Missions[player]++;
			Unlock("First Mission");
			if (Current.Missions[player] >= 5)
				Unlock("Five Missions");
			auto every = true;
			for (auto each = 2; each <= 18; each++)
				every &= MissionsFinished(each) > 0;
			if (every)
				Unlock("Every Mission");
		}

		if (view.Rank > Previous.Rank && samePlayer)
		{
			Raise(Key("Best Rank"), view.Rank);
			// The ranks are Cadet 1 to Fleet Admiral 9
			constexpr std::pair<int, const char*> Ranks[] = {
				{2, "Ensign"}, {4, "Captain"}, {7, "Commodore"}, {9, "Fleet Admiral"}};
			for (auto [rank, key] : Ranks)
			{
				if (view.Rank >= rank)
					Unlock(key);
			}
		}

		// The drain sends the ball back with Replay Ball or Re-Deploy in the info box
		if (view.Kind != TableView::TableKind::DragonsKeep && view.InfoMessages != Previous.InfoMessages &&
			(view.InfoText == translations::GetTranslation(Msg::STRING196) ||
			 view.InfoText == translations::GetTranslation(Msg::STRING197)))
		{
			Add(Key("Balls Saved"), 1);
			Unlock("Ball Saved");
		}

		if (view.ExtraBalls > Previous.ExtraBalls && samePlayer)
		{
			Add(Key("Extra Balls"), 1);
			Unlock("Extra Ball");
		}

		if (ActiveBalls(view) >= 2 && ActiveBalls(Previous) < 2)
		{
			Add(Key("Multiballs"), 1);
			Unlock("Multiball");
		}

		Current.Tilted |= view.Tilted;
		auto current = PlayerOf(view);
		auto score = Current.Billions[current] * 1'000'000'000 + view.Scores[current];
		constexpr std::pair<long long, const char*> Scores[] = {
			{1'000'000, "Million"}, {10'000'000, "Ten Million"}, {100'000'000, "Hundred Million"}};
		for (auto [threshold, key] : Scores)
		{
			if (score >= threshold)
				Unlock(key);
		}
	}

	// The game ends when the table turns Game Over
	if (Current.Active && wasPlaying && view.GameOver && !view.Demo)
	{
		Current.Active = false;
		Add(Key("Games"), 1);
		// Settings hold an int, which stops past two billion
		for (auto index = 0; index < std::min(view.PlayerCount, static_cast<int>(view.Scores.size())); index++)
			Raise(Key("Best Score"),
				  static_cast<int>(std::min<long long>(Current.Billions[index] * 1'000'000'000 + view.Scores[index],
													   std::numeric_limits<int>::max())));
		Unlock("First Game");
		if (!Current.Tilted)
			Unlock("Steady Hands");
		if (Get(Stat::Games) >= 25)
			Unlock("Regular");
	}
	Previous = view;
}

void DefinitiveStats::UpdateDragon(const TableView& view, int player, bool samePlayer)
{
	// The rules count a quest done as it ends; the one that ended ran, or waited to be taken up again, before. On a
	// drain the next player's count comes in, so only the same player's counts.
	const auto& dragon = view.Dragon;
	const auto& before = Previous.Dragon;
	if (dragon.QuestsDone > before.QuestsDone && samePlayer)
	{
		Add(Key("Quests"), 1);
		auto quest = before.State == TableView::DragonsKeep::RestartQuestState ? before.RestartQuest : before.State;
		if (quest >= TableView::DragonsKeep::FirstQuestState && quest <= TableView::DragonsKeep::LastQuestState)
			Add(QuestKey(quest), 1);
		Current.Quests[player]++;
		Unlock("First Quest");
		if (Current.Quests[player] >= 5)
			Unlock("Five Quests");
		auto every = true;
		for (auto each = TableView::DragonsKeep::FirstQuestState; each <= TableView::DragonsKeep::LastQuestState;
			 each++)
			every &= QuestsDone(each) > 0;
		if (every)
			Unlock("Every Quest");
	}

	// Slay Dragon's last hit takes its hits left to 0, and the Super Jackpot follows while it still runs
	if (before.State == 9 && before.HitsLeft > 0 && dragon.State == 9 && dragon.HitsLeft <= 0)
	{
		Add(Key("Dragons Slain"), 1);
		Unlock("Dragon Slayer");
	}

	// The ball save shows no text, so the rules count it for the view
	if (dragon.BallSaves > before.BallSaves)
	{
		Add(Key("Balls Saved"), 1);
		Unlock("Ball Saved");
	}
}

void DefinitiveStats::Reset()
{
	Previous = TableView{};
	HavePrevious = false;
	Current = Game{};
}

int DefinitiveStats::Get(Stat stat)
{
	return Read(Key(StatKeys[std::to_underlying(stat)]));
}

int DefinitiveStats::MissionsFinished(int mission)
{
	return Read(Key(("Mission " + std::to_string(mission)).c_str()));
}

int DefinitiveStats::QuestsDone(int quest)
{
	return Read(QuestKey(quest));
}

std::span<const DefinitiveStats::Achievement> DefinitiveStats::Achievements()
{
	return AchievementList;
}

bool DefinitiveStats::Offered(const Achievement& achievement, const TableView& view)
{
	switch (achievement.Tables)
	{
	case Offer::SpaceCadet:
		return view.Kind == TableView::TableKind::SpaceCadet;
	case Offer::DragonsKeep:
		return view.Kind == TableView::TableKind::DragonsKeep;
	default:
		return true;
	}
}

long long DefinitiveStats::UnlockedAt(size_t index)
{
	if (index >= std::size(AchievementList))
		return 0;
	auto key = Key((std::string{"Achievement."} + AchievementList[index].Key).c_str());
	return options::HasSetting(key) ? std::strtoll(options::GetSetting(key, "0").c_str(), nullptr, 10) : 0;
}

int DefinitiveStats::Unannounced()
{
	return Waiting.empty() ? -1 : Waiting.front();
}

void DefinitiveStats::Announced()
{
	if (!Waiting.empty())
		Waiting.erase(Waiting.begin());
}

void DefinitiveStats::Clear()
{
	for (auto key : StatKeys)
		options::RemoveSetting(Key(key));
	for (auto mission = 2; mission <= 18; mission++)
		options::RemoveSetting(Key(("Mission " + std::to_string(mission)).c_str()));
	for (auto quest = TableView::DragonsKeep::FirstQuestState; quest <= TableView::DragonsKeep::LastQuestState; quest++)
		options::RemoveSetting(QuestKey(quest));
	for (const auto& achievement : AchievementList)
		options::RemoveSetting(Key((std::string{"Achievement."} + achievement.Key).c_str()));
	Waiting.clear();
}
