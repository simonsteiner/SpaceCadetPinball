#include "pch.h"
#include "DefinitiveStats.h"

#include "DefinitiveHud.h"
#include "options.h"
#include "TableView.h"
#include "translations.h"

#include <utility>

namespace
{
	using Stat = DefinitiveStats::Stat;

	constexpr const char* StatKeys[] = {"Games",       "Missions",   "Best Rank", "Balls Saved",
										"Extra Balls", "Multiballs", "Best Score"};
	static_assert(std::size(StatKeys) == static_cast<size_t>(Stat::Count));

	// The order is the list's on screen; keys are saved, so they stay as they are
	constexpr DefinitiveStats::Achievement AchievementList[] = {
		{"First Game", Msg::Definitive_AchFirstGame, Msg::Definitive_AchFirstGameText},
		{"First Mission", Msg::Definitive_AchFirstMission, Msg::Definitive_AchFirstMissionText},
		{"Five Missions", Msg::Definitive_AchFiveMissions, Msg::Definitive_AchFiveMissionsText},
		{"Every Mission", Msg::Definitive_AchEveryMission, Msg::Definitive_AchEveryMissionText},
		{"Ensign", Msg::Definitive_AchEnsign, Msg::Definitive_AchEnsignText},
		{"Captain", Msg::Definitive_AchCaptain, Msg::Definitive_AchCaptainText},
		{"Commodore", Msg::Definitive_AchCommodore, Msg::Definitive_AchCommodoreText},
		{"Fleet Admiral", Msg::Definitive_AchFleetAdmiral, Msg::Definitive_AchFleetAdmiralText},
		{"Ball Saved", Msg::Definitive_AchBallSaved, Msg::Definitive_AchBallSavedText},
		{"Extra Ball", Msg::Definitive_AchExtraBall, Msg::Definitive_AchExtraBallText},
		{"Multiball", Msg::Definitive_AchMultiball, Msg::Definitive_AchMultiballText},
		{"Million", Msg::Definitive_AchMillion, Msg::Definitive_AchMillionText},
		{"Ten Million", Msg::Definitive_AchTenMillion, Msg::Definitive_AchTenMillionText},
		{"Hundred Million", Msg::Definitive_AchHundredMillion, Msg::Definitive_AchHundredMillionText},
		{"Steady Hands", Msg::Definitive_AchSteadyHands, Msg::Definitive_AchSteadyHandsText},
		{"Regular", Msg::Definitive_AchRegular, Msg::Definitive_AchRegularText},
	};

	// The game being played: whether one is, and what counts per game, by player
	struct Game
	{
		bool Active;
		bool Tilted;
		std::array<int, 4> Missions;
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
	if (Read(name))
		return;
	SDL_Time now = 0;
	SDL_GetCurrentTime(&now);
	// Seconds fit an int until 2038; a clock before 1970 still counts as unlocked
	options::set_int(name.c_str(), std::max(1, static_cast<int>(SDL_NS_TO_SECONDS(now))));
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

	// A game starts when play starts, or when the scores drop because one was restarted; a demo game never counts
	auto playing = Playing(view);
	auto wasPlaying = Playing(Previous);
	auto restarted = false;
	for (size_t index = 0; index < view.Scores.size(); index++)
		restarted |= view.Scores[index] < Previous.Scores[index];
	if (playing && (!wasPlaying || restarted))
		Current = Game{true, false, {}};

	if (playing && Current.Active)
	{
		auto player = PlayerOf(Previous);
		// A mission ends finished when it lit lamps of the progress ring, or a new rank, which clears them; a mission
		// that runs out of fuel or ends with the ball does neither
		auto mission = DefinitiveHud::MissionOf(Previous.Mission);
		if (mission && !DefinitiveHud::MissionOf(view.Mission) &&
			(view.RankProgress > Previous.RankProgress || view.Rank > Previous.Rank))
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

		if (view.Rank > Previous.Rank && view.CurrentPlayer == Previous.CurrentPlayer)
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
		if (view.InfoMessages != Previous.InfoMessages &&
			(view.InfoText == translations::GetTranslation(Msg::STRING196) ||
			 view.InfoText == translations::GetTranslation(Msg::STRING197)))
		{
			Add(Key("Balls Saved"), 1);
			Unlock("Ball Saved");
		}

		if (view.ExtraBalls > Previous.ExtraBalls && view.CurrentPlayer == Previous.CurrentPlayer)
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
		auto score = view.Scores[PlayerOf(view)];
		constexpr std::pair<int, const char*> Scores[] = {
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
		for (auto index = 0; index < std::min(view.PlayerCount, static_cast<int>(view.Scores.size())); index++)
			Raise(Key("Best Score"), view.Scores[index]);
		Unlock("First Game");
		if (!Current.Tilted && view.MaxBallCount >= 3)
			Unlock("Steady Hands");
		if (Get(Stat::Games) >= 25)
			Unlock("Regular");
	}
	Previous = view;
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

std::span<const DefinitiveStats::Achievement> DefinitiveStats::Achievements()
{
	return AchievementList;
}

long long DefinitiveStats::UnlockedAt(size_t index)
{
	if (index >= std::size(AchievementList))
		return 0;
	return Read(Key((std::string{"Achievement."} + AchievementList[index].Key).c_str()));
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
	for (const auto& achievement : AchievementList)
		options::RemoveSetting(Key((std::string{"Achievement."} + achievement.Key).c_str()));
	Waiting.clear();
}
