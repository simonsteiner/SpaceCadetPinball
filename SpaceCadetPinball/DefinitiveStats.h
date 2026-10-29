#pragma once

#include <span>
#include <string>

struct TableView;
enum class Msg : int;

// Space Cadet 2026's stats and achievements, worked out from the table view: games played, missions finished, ranks
// reached, balls saved and the like; on Dragon's Keep, quests done and dragons slain in place of missions and ranks.
// They are kept per table in the settings file, next to the high scores, and never leave the player's machine.
class DefinitiveStats
{
public:
	enum class Stat
	{
		Games,
		Missions,
		BestRank,
		BallsSaved,
		ExtraBalls,
		Multiballs,
		BestScore,
		// Dragon's Keep's
		Quests,
		DragonsSlain,
		Count,
	};

	// The tables an achievement is offered on
	enum class Offer
	{
		Every,
		SpaceCadet,
		DragonsKeep,
	};

	struct Achievement
	{
		// The settings key, which never changes once released
		const char* Key;
		Msg Name;
		Msg Description;
		Offer Tables;
	};

	// After every update of a game in play, in either edition; never in replay playback. A demo game doesn't count.
	static void Update(const TableView& view);
	// Forgets the game in progress, when the table goes away
	static void Reset();

	static int Get(Stat stat);
	// How often a mission from 2 for Target Practice to 18 for Maelstrom was finished (DefinitiveHud::MissionOf)
	static int MissionsFinished(int mission);
	// How often a Dragon's Keep quest was done, by its state, 4 for Dragon Hoard to 8 for Wizard's Fury
	static int QuestsDone(int quest);
	static std::span<const Achievement> Achievements();
	// Whether the table in the view offers an achievement
	static bool Offered(const Achievement& achievement, const TableView& view);
	// When an achievement was unlocked, in seconds since 1970, or 0 while it is locked
	static long long UnlockedAt(size_t index);
	// The next achievement unlocked that no notice has shown yet, else -1; Announced takes it off the list
	static int Unannounced();
	static void Announced();
	// Clears the stats and achievements of the table loaded
	static void Clear();

private:
	static std::string Key(const char* name);
	static int Read(const std::string& key);
	static void Add(const std::string& key, int amount);
	static void Raise(const std::string& key, int value);
	static void Unlock(const char* key);
	// Dragon's Keep's quests done, dragons slain and balls saved, in place of Space Cadet's missions, ranks and balls
	// saved, which its view doesn't have
	static void UpdateDragon(const TableView& view, int player, bool samePlayer);
};
