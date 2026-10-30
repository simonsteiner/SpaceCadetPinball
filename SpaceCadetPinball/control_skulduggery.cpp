#include "pch.h"
#include "control_skulduggery.h"

#include "control.h"
#include "midi.h"
#include "pb.h"
#include "TAnimatedBlocker.h"
#include "TDrain.h"
#include "TFlipper.h"
#include "TKickback.h"
#include "TKickout.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TLightRollover.h"
#include "TLightSweeps.h"
#include "TMagnet.h"
#include "TPinballTable.h"
#include "TPlunger.h"
#include "TPopupTarget.h"
#include "TSink.h"
#include "TSoloTarget.h"
#include "TSound.h"
#include "TTextBox.h"
#include "TTimer.h"
#include "translations.h"

// Skulduggery's rules, recovered from PIRATES.EXE one feature at a time (Phase F6). Ported so far: the serve, drain
// and game-over flow, with the ball save, the end-of-ball bonus, the launch's rolling skill shot and the music; the
// mode framework: the SKULDUGGERY drop targets, the modes' lights and pick, the captive ball's targets and a_spot102;
// and the modes Attack Ship, Tavern with the magnets and the mugs' sinks, and Treasure Hunt with the buried
// treasure's sinks and rollovers.
// Everything else is still a stand-in that keeps the table playable: kickouts and sinks give back the balls they
// catch, which only the rules would do otherwise, and a stuck ball is freed as Space Cadet's rules free one.
//
// PIRATES.EXE's rules run a state machine, as DRAGON.EXE's do, whose state each player keeps in info_text_box's
// message field: the engine's control::handler (0x423080) hands every event to one table-wide function (0x42d850),
// which passes it to the current state's function (0x42d900), and then to the component's own handler. States 1
// (game over), 2 (play), 3 (launch), 4 (Attack Ship), 7 (Tavern), 8 (Treasure Hunt), 16 (end of ball) and 17 (the
// game's last ball) are ported; the other modes (5 to 13) come with their features. Addresses below are PIRATES.EXE's.
// Its message numbers are CADET.EXE's, which differ from this port's above the light codes, so each call uses the
// port's MessageCode.
namespace
{
	// Codes PIRATES.EXE's rules functions send only among themselves, as DRAGON.EXE's numbers them
	constexpr auto SkipBonusText = MessageCode::DragonSkipBonusText; // 1, from a flipper key on the last ball
	constexpr auto EnterState = MessageCode::DragonEnterState;       // 0x4f
	constexpr auto ShowStateText = MessageCode::DragonShowStateText; // 0x50
	constexpr auto ModeEnd = MessageCode::DragonQuestEnd;            // 0x52: a mode is over, back to play
	constexpr auto BallDrained = MessageCode::DragonBallDrained;     // 0x53

	// The game's states, as PIRATES.EXE numbers them; 0 is the new game's until the first ball is served
	constexpr int NewGameState = 0;
	constexpr int GameOverState = 1;
	constexpr int PlayState = 2;
	constexpr int LaunchState = 3;
	constexpr int FirstModeState = 4;
	constexpr int AttackShipState = 4;
	constexpr int TavernState = 7;
	constexpr int TreasureHuntState = 8;
	constexpr int LastModeState = 11;
	constexpr int LavaBallState = 12;
	constexpr int JailbreakState = 13;
	constexpr int EndOfBallState = 16;
	constexpr int LastBallState = 17;

	TPinballTable* Table = nullptr;

	TDrain* drain;
	TPlunger* s_launcher1;
	TTextBox* info_text_box;
	TTimer* mode_timer;
	TLight* v_lite504;
	TLight* v_lite505;
	TLight* v_lite544;
	TLight* v_lite569;
	TLight* v_lite570;
	TLight* v_lite606;
	TLight* v_lite607;
	std::array<TLight*, 6> magnet_lights; // v_lite560 to v_lite565
	std::array<TLight*, 6> v_mlights;     // v_mlite44, v_mlite107, v_mlite106, v_mlite51, v_mlite110, v_mlite104
	TLightGroup* coin_light_group;
	TLightGroup* special_treasure_light_group;
	TLightGroup* fathom_light_group;
	std::array<TFlipper*, 4> flippers; // a_flip1 to a_flip4
	std::array<TMagnet*, 6> magnets;   // s_magnet1 to s_magnet6
	TAnimatedBlocker* a_cannon;
	TAnimatedBlocker* a_blocc;
	TAnimatedBlocker* a_bloce;
	TAnimatedBlocker* a_blocf;
	TAnimatedBlocker* a_blocg;
	TKickback* a_kick1;
	TKickback* a_kick2;
	TKickout* a_kout1;
	TKickout* a_kout2;
	TSink* a_sink9;
	TSink* a_sink12;
	TPinballComponent* s_oneway3;
	TPinballComponent* s_oneway12;
	TPinballComponent* s_bwal610;
	TSound* tabb42wave;
	TSound* tabb73wave;
	TSound* tabb74wave;
	TSound* tabb84wave;
	// The mode framework's: the SKULDUGGERY letters and the three banks of drop targets that spell them, the modes'
	// lights, the captive ball's targets and a_spot102, which picks the next mode
	std::array<TLight*, 11> letter_lights; // v_lite1 to v_lite11; v_lite1's message field keeps the letters spelled
	std::array<TLight*, 8> mode_lights;    // v_lite552 to v_lite559, in PickedModeStates' order
	std::array<TLight*, 4> captive_lights; // v_lite551, v_lite550, v_lite549 and v_lite548
	// a_spot11, a_spot8, a_spot9 and a_spot10; a_spot11's message field keeps those hit
	std::array<TSoloTarget*, 4> captive_targets;
	TSoloTarget* a_spot102;
	TLightGroup* map_piece_light_group;
	TSound* tabb02wave;
	TSound* tabb07wave;
	TSound* tabb19wave;
	TSound* tabb22wave;
	TSound* tabb23wave;
	TSound* tabb24wave;
	TSound* tabb29wave;
	// Shared by the modes: what they switch off as they start and give back as they end
	std::array<TLight*, 4> v_lite524s; // v_lite524 to v_lite527; v_lite524's message field keeps those lit
	TLight* v_lite520;
	TLight* v_lite521;
	TLightSweeps* light_sweeps;
	TSink* a_sink8;
	// Attack Ship's
	TLight* v_lite567;
	TPinballComponent* s_rexit8;
	TSound* tabb26wave;
	TSound* tabb40wave;
	TSound* tabb57wave;
	TSound* tabb58wave;
	TSound* tabb85wave;
	// Treasure Hunt's: the X lights and the sinks they mark, the orbits it takes over, the special treasures it finds
	// and the rollovers that bury treasure for Peg Leg's Loot
	TLight* v_lite519;
	TLightGroup* left_orbit_light_group;
	TLightGroup* right_orbit_light_group;
	TLightGroup* left_orbit_combo_light_group;
	TLightGroup* right_orbit_combo_light_group;
	TPinballComponent* s_trip3;
	TPinballComponent* s_trip8;
	std::array<TLight*, 6> treasure_lights; // v_lite571 to v_lite576, special_treasure_light_group's
	TSink* a_sink14;
	TSink* a_sink15;
	TSink* a_sink17;
	TPinballComponent* a_roll8;
	TPinballComponent* a_roll9;
	TSound* tabb11wave;
	TSound* tabb59wave;
	// Tavern's: its three mugs and their sinks, the magnets' keg and the awards the mugs give
	std::array<TLight*, 3> tavern_lights; // v_lite547, v_lite546, v_lite545; v_lite547's message field keeps those lit
	std::array<TSink*, 3> tavern_sinks;   // a_sink5, a_sink6, a_sink7
	std::array<TLight*, 3> v_lite530s;    // v_lite530 to v_lite532
	TSoloTarget* a_spot6;
	TLightGroup* keg_light_group;
	TSound* tabb00wave;
	TSound* tabb08wave;
	TSound* tabb65wave;
	TSound* tabb83wave;
	TSound* tabb87wave;

	// A bank of drop targets: its first target's message field keeps those down, and each target spells a letter
	struct DropBank
	{
		std::vector<TPopupTarget*> Targets;
		int FirstLetter;  // the letter its first target spells
		float BloccState; // where a full bank turns a_blocc and a_blocf
		float BlocfState;
		TSound* HitSound;
		TSound* FullSound;
	};
	// a_drop13 to a_drop15 (0x426a40), a_drop5 to a_drop9 (0x4264f0) and a_drop10 to a_drop12 (0x426eb0)
	std::array<DropBank, 3> drop_banks;

	template <typename T> void Find(T*& component, const char* name)
	{
		component = dynamic_cast<T*>(Table->find_component(name));
	}

	// info_text_box's message field, which the engine keeps per player on the bonus tables, as CADET.EXE does
	int& GameState()
	{
		return info_text_box->MessageField;
	}

	// drain's message field, the Ship's Hold: the bonus a lit v_lite570 carries to the next ball, also per player.
	// The drain's Reset, which a ball save and a drain during multiball send, clears it for every player, as Full
	// Tilt's TDrain does (0x418770 calls 0x413550 in CADET.EXE and PIRATES.EXE)
	int& ShipsHold()
	{
		return drain->MessageField;
	}

	// s_launcher1's message field, the skill shot's award showing (0 to 8)
	int& SkillShotAward()
	{
		return s_launcher1->MessageField;
	}

	int BallSaveStep;       // 0x435d90: 3 lit until the ball leaves the lane, 2 lit 10 s, 1 flashing 5 s, 0 off
	int DrainStep;          // 0x435d08: what the drain's timer does next
	bool LastBallRelease;   // 0x43aba8: the game's last ball drained, and the flippers are dead until the bonus ends
	bool ClearBuriedLights; // 0x43abb0: set when Peg Leg's Loot ends (#140); the next drain turns its lights off
	bool Launching;         // 0x435d30: set from the launch until play starts; the modes read it (#128)
	bool TallyQuiet;        // 0x435d34: count the bonus without showing it
	int TallyStep;          // 0x435d38
	int TallyTotal;         // 0x435d00
	int TargetHits;         // 0x435d48, Target Bonus: the popup targets hit this ball
	int BrigBalls;          // 0x435d3c: the balls the brig holds (#139), which the last ball releases
	int ModeFlags;          // 0x435cf8: the running mode's own count or flags; Attack Ship counts the ships captured
	int ModeAwardStep;      // 0x435d04: the next of a mode's awards
	// What Treasure Hunt's rollovers bury for Peg Leg's Loot to dig up (0x435d10 to 0x435d28)
	int BuryStep;
	int CoinsBuried;
	int TreasureBuried1;
	int TreasureBuried2;
	int BonusBuried;
	int JackpotBuried;
	bool ExtraBallBuried;

	void TableHook(MessageCode code, TPinballComponent* caller);
	void StateControl(int state, MessageCode code, const TPinballComponent* caller);

	void PlayTrack(MidiTracks track)
	{
		// PIRATES.EXE starts a track only if another one plays
		if (midi::get_active_track() != track)
			midi::play_track(track, true);
	}

	void AddJackpot(int score) // 0x423650
	{
		Table->JackpotScore = std::min(Table->JackpotScore + score, 10000000);
	}

	// 0x423350: the score without multiplier, bonus or first jackpot; the second jackpot still counts it
	int AddScoreUnmultiplied(TPinballComponent* /*scorer*/, int score)
	{
		auto bonus = Table->BonusScoreFlag;
		auto jackpot = Table->JackpotScoreFlag;
		auto multiplier = Table->ScoreMultiplier;
		Table->BonusScoreFlag = false;
		Table->JackpotScoreFlag = false;
		Table->ScoreMultiplier = 1;
		auto added = Table->AddScore(score);
		Table->BonusScoreFlag = bonus;
		Table->JackpotScoreFlag = jackpot;
		Table->ScoreMultiplier = multiplier;
		return added;
	}

	void SetState(int state)
	{
		GameState() = state;
		TableHook(EnterState, nullptr);
	}

	// A picture in the text box's second font, and what it shows in words for Space Cadet 2026's HUD
	void ShowPicture(const char* glyphs, float time, const char* words)
	{
		info_text_box->Display(glyphs, time, false, 1, words);
	}

	void ShowPicture(const char* glyphs, float time, Msg words)
	{
		ShowPicture(glyphs, time, pb::get_rc_string(words));
	}

	// 0x424c70: the magnets and their lights off
	void MagnetsOff()
	{
		using enum MessageCode;
		for (auto magnet : magnets)
			magnet->Message(TMagnetDisable, 0.0f);
		for (auto light : magnet_lights)
			light->Message(TLightResetAndTurnOff, 0.0f);
	}

	// a_bloce down, with v_lite504 off and v_lite505 on
	void BloceDown()
	{
		using enum MessageCode;
		a_bloce->Message(TAnimatedBlockerSetState, 0.0f);
		v_lite504->Message(TLightResetAndTurnOff, 0.0f);
		v_lite505->Message(TLightResetAndTurnOn, 0.0f);
	}

	// a_bloce up, with v_lite504 on and v_lite505 off
	void BloceUp()
	{
		using enum MessageCode;
		a_bloce->Message(TAnimatedBlockerSetState, 1.0f);
		v_lite504->Message(TLightResetAndTurnOn, 0.0f);
		v_lite505->Message(TLightResetAndTurnOff, 0.0f);
	}

	// 0x4232c0: the scorer's own score at <index>. PIRATES.EXE first tells the previous scorer's rules that it no
	// longer is (0x4d), which no ported handler takes yet, so the last scorer (0x435d44) isn't kept
	int ScoreBy(const TPinballComponent* scorer, int index)
	{
		return Table->AddScore(scorer->get_scoring(index));
	}

	// The bit fields PIRATES.EXE keeps in message fields
	unsigned Bit(int bit)
	{
		return 1u << static_cast<unsigned>(bit);
	}

	void SetBit(int& field, int bit)
	{
		field = static_cast<int>(static_cast<unsigned>(field) | Bit(bit));
	}

	bool AllSet(int field, unsigned mask)
	{
		return (static_cast<unsigned>(field) & mask) == mask;
	}

	// 0x423310: <score> for the table; PIRATES.EXE keeps its last scorer as ScoreBy does
	int AddScore(int score)
	{
		return Table->AddScore(score);
	}

	int& LettersSpelled()
	{
		return letter_lights[0]->MessageField;
	}

	// 0x423b70: with <flash>, the letters flash for 1 s and go out, and the word starts over; else they only go out
	void LettersOff(bool flash)
	{
		using enum MessageCode;
		if (flash)
			LettersSpelled() = 0;
		for (auto light : letter_lights)
			light->Message(flash ? TLightFlasherStartTimedThenStayOff : TLightResetAndTurnOff, flash ? 1.0f : 0.0f);
	}

	int& BankDown(const DropBank& bank)
	{
		return bank.Targets.front()->MessageField;
	}

	// 0x424f70, 0x424440 and 0x4252d0: the bank's targets come up again
	void ResetBank(const DropBank& bank)
	{
		BankDown(bank) = 0;
		for (auto target : bank.Targets)
			target->Message(MessageCode::TPopupTargetEnable, 0.0f);
	}

	// The mode each of mode_timer's picks starts, from 1 (0x43d0f0)
	constexpr std::array PickedModeStates{4, 8, 7, 5, 6, 9, 10, 11};
	constexpr int PegLegsLootPick = 7;

	// The modes ported so far; whatever would start another one lets the game go on in play
	constexpr std::array PortedModes{AttackShipState, TavernState, TreasureHuntState};

	bool ModePorted(int state)
	{
		return std::ranges::find(PortedModes, state) != PortedModes.end();
	}

	// mode_timer's message field keeps the picked mode, from 1, or 0 when none is picked
	int& PickedMode()
	{
		return mode_timer->MessageField;
	}

	// 0x425700: the picked mode's light goes out, and none is picked
	void ClearPickedMode()
	{
		if (auto picked = PickedMode(); picked >= 1 && picked <= static_cast<int>(mode_lights.size()))
			mode_lights[picked - 1]->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		PickedMode() = 0;
	}

	bool LightIdle(const TLight& light)
	{
		return !light.LightOnFlag && !light.ToggledOnFlag && !light.FlasherOnFlag;
	}

	// 0x425850: a random mode whose light is neither on nor flashing is picked, and its light flashes; once all the
	// map pieces are found, Peg Leg's Loot. Returns the pick's index in PickedModeStates
	int PickMode()
	{
		using enum MessageCode;
		int pick = PegLegsLootPick;
		if (map_piece_light_group->Message(TLightGroupGetOnCount, 0.0f) !=
			map_piece_light_group->Message(TLightGroupGetLightCount, 0.0f))
		{
			std::array<int, PegLegsLootPick> open{};
			int count = 0;
			for (int index = 0; index < PegLegsLootPick; index++)
			{
				if (LightIdle(*mode_lights[index]))
					open[count++] = index;
			}
			if (count)
			{
				pick = open[std::min(static_cast<int>(RandFloat() * count), count - 1)];
			}
			else
			{
				// None left: the one flashing stays picked
				for (int index = 0; index < static_cast<int>(mode_lights.size()); index++)
				{
					if (mode_lights[index]->FlasherOnFlag)
						return index;
				}
			}
		}
		// 0x4256d0
		ClearPickedMode();
		mode_lights[pick]->Message(TLightFlasherStartTimed, -1.0f);
		PickedMode() = pick + 1;
		return pick;
	}

	TLight* ModeLight(int state)
	{
		auto pick = std::ranges::find(PickedModeStates, state);
		return pick != PickedModeStates.end() ? mode_lights[pick - PickedModeStates.begin()] : nullptr;
	}

	// 0x4255a0: as a mode starts its light flashes, and as it ends the light stays on; play picks the next mode
	void ModeLights(int state, bool starting)
	{
		using enum MessageCode;
		ClearPickedMode();
		if (state == PlayState)
			PickMode();
		else if (auto light = ModeLight(state))
			light->Message(starting ? TLightFlasherStartTimed : TLightResetAndTurnOn, -1.0f);
	}

	// The picture a mode shows as it starts, in the text box's second font (0x423480 to 0x4235f0)
	struct ModePicture
	{
		int State;
		const char* Glyphs;
		float Time;
		Msg Words;
	};
	constexpr std::array ModePictures{
		ModePicture{AttackShipState, "H", 1.0f, Msg::Skulduggery_PictureAttackShip},
		ModePicture{TavernState, "333", 2.0f, Msg::Skulduggery_PictureTavern},
		ModePicture{TreasureHuntState, "I", 2.0f, Msg::Skulduggery_PictureTreasureHunt},
	};

	// 0x423610 and the mode starts (0x423480 to 0x4235f0): v_lite606 flashes while a mode runs (0x423400), the mode
	// shows its picture, and its state begins. A mode that isn't ported yet doesn't start
	bool StartMode(int state)
	{
		if (!ModePorted(state))
			return false;
		v_lite606->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		if (auto picture = std::ranges::find(ModePictures, state, &ModePicture::State); picture != ModePictures.end())
			ShowPicture(picture->Glyphs, picture->Time, picture->Words);
		SetState(state);
		return true;
	}

	// 0x425a50: the picked mode starts
	bool StartPickedMode()
	{
		auto picked = PickedMode();
		if (picked < 1 || picked > static_cast<int>(PickedModeStates.size()))
			return false;
		return StartMode(PickedModeStates[picked - 1]);
	}

	// 0x425de0: with all eleven letters spelled, the word starts over, the banks come up and the picked mode starts
	bool WordSpelled()
	{
		if (!AllSet(LettersSpelled(), 0x7ff))
			return false;
		Launching = true;
		LettersOff(true);
		for (const auto& bank : drop_banks)
			ResetBank(bank);
		// Not PIRATES.EXE's: a mode that isn't ported yet leaves the game in play, as if the launch were over
		if (!StartPickedMode())
			Launching = false;
		return true;
	}

	// 0x424700: the jackpot, with its picture for <time> plus 2 s; a negative <time> counts as positive and plays no
	// sound. Returns how long the picture shows
	float AwardJackpot(float time)
	{
		if (time < 0.0f)
			time = -time;
		else
			tabb85wave->Play(nullptr, "AwardJackpot");
		time += 2.0f;
		ShowPicture("G", time, Msg::Definitive_PictureJackpot);
		AddScoreUnmultiplied(nullptr, Table->JackpotScore);
		return time;
	}

	// The fathom lights' pictures in the second font (0x43aad8), by how many are lit: an anchor and the multiplier
	constexpr std::array FathomPictures{":", ":=<", ":><", ":?<", ":@<", ":A<", ":B<"};

	// 0x424350: the fathom lights flash and stay on, the multiplier is one more than those lit, and with a <time> of 0
	// or more its picture shows for <time> plus 2 s. The light group passes the flash to all of them, as CADET.EXE's
	// does, so this lights them all
	void LightFathoms(float time)
	{
		using enum MessageCode;
		fathom_light_group->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
		auto lit = fathom_light_group->Message(TLightGroupGetOnCount, 0.0f);
		if (time >= 0.0f)
		{
			auto index = std::clamp(lit, 0, static_cast<int>(FathomPictures.size()) - 1);
			std::array<char, 64> words{};
			snprintf(words.data(), words.size(), pb::get_rc_string(Msg::Skulduggery_PictureMultiplier),
					 static_cast<long>(index) + 1);
			ShowPicture(FathomPictures[index], time + 2.0f, words.data());
		}
		Table->ScoreMultiplier = lit + 1;
	}

	// 0x423870 and 0x425310 with 0: the lights v_lite524 to v_lite527 and the captive ball's go out as a mode starts
	void ModeLightsOff()
	{
		for (auto light : v_lite524s)
			light->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	}

	void CaptiveLightsOff()
	{
		for (auto light : captive_lights)
			light->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	}

	// 0x42aad0: what every mode does as it starts
	void ModeEnter()
	{
		ModeFlags = 0;
		ModeAwardStep = 0;
		ModeLightsOff();
		LettersOff(false);
		BloceUp();
		if (GameState() != LavaBallState)
			CaptiveLightsOff();
	}

	// Each light of <lights> on if its bit of <field> is set, else off
	void RestoreLights(const std::array<TLight*, 4>& lights, int field)
	{
		using enum MessageCode;
		for (auto index = 0u; index < lights.size(); index++)
			lights[index]->Message(AllSet(field, 1u << index) ? TLightResetAndTurnOn : TLightResetAndTurnOff, 0.0f);
	}

	// 0x423c90: the letters spelled light again, and their targets go down unless their whole bank is spelled
	void RestoreLetters()
	{
		using enum MessageCode;
		for (const auto& bank : drop_banks)
		{
			BankDown(bank) = 0;
			auto count = static_cast<int>(bank.Targets.size());
			auto full = AllSet(LettersSpelled(), (Bit(count) - 1) << static_cast<unsigned>(bank.FirstLetter));
			for (auto index = 0; index < count; index++)
			{
				auto spelled = AllSet(LettersSpelled(), Bit(bank.FirstLetter + index));
				letter_lights[bank.FirstLetter + index]->Message(spelled ? TLightResetAndTurnOn : TLightResetAndTurnOff,
																 0.0f);
				if (spelled && !full)
				{
					bank.Targets[index]->Message(TPopupTargetDisable, 0.0f);
					SetBit(BankDown(bank), index);
				}
				else
				{
					bank.Targets[index]->Message(TPopupTargetEnable, 0.0f);
				}
			}
		}
	}

	// 0x42ab40: what every mode does as it ends: the lights it took give back what their fields keep (0x423900,
	// 0x423c90, 0x4253a0), and the banks come up
	void ModeLeave()
	{
		RestoreLights(v_lite524s, v_lite524s[0]->MessageField);
		RestoreLetters();
		RestoreLights(captive_lights, captive_targets[0]->MessageField);
		ResetBank(drop_banks[1]);
		ResetBank(drop_banks[0]);
		ResetBank(drop_banks[2]);
		v_lite520->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite521->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	}

	// A mode's text with the seconds mode_timer has left, if it runs
	void ShowTimeLeft(Msg text)
	{
		auto msLeft = mode_timer->Message(MessageCode::TTimerGetTimeLeft, 0.0f);
		if (!msLeft)
			return;
		std::array<char, 64> buffer{};
		snprintf(buffer.data(), buffer.size(), pb::get_rc_string(text), static_cast<long>(msLeft / 1000));
		info_text_box->Display(buffer.data(), 0.5f);
	}

	// The light sweep a mode starts with has ended: the ball held in a_sink8 comes out, unless the mode started from
	// the launch or the drop targets, and the mode's time starts
	void ModeSweepDone(float time)
	{
		if (!Launching)
			a_sink8->Message(MessageCode::TSinkResetTimer, 0.0f);
		mode_timer->Message(MessageCode::TTimerResetTimer, time);
		TableHook(ShowStateText, nullptr);
	}

	// 0x424d30: a random magnet whose light is neither on nor flashing, or any once none is, switches on, with its
	// light and the keg's picture for 2 s. Returns how long the picture shows
	float MagnetOn()
	{
		using enum MessageCode;
		std::array<int, 6> open{};
		int count = 0;
		for (int index = 0; index < static_cast<int>(magnet_lights.size()); index++)
		{
			if (LightIdle(*magnet_lights[index]))
				open[count++] = index;
		}
		if (!count)
		{
			for (; count < static_cast<int>(open.size()); count++)
				open[count] = count;
		}
		auto index = open[std::min(static_cast<int>(RandFloat() * count), count - 1)];
		magnets[index]->Message(TMagnetEnable, 0.0f);
		magnet_lights[index]->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
		ShowPicture("E", 2.0f, Msg::Skulduggery_PictureKeg);
		return 2.0f;
	}

	// The Tavern's half of 0x426a40 and 0x426eb0: each target down switches a magnet on, and a full bank awards the
	// jackpot after the keg (a_drop13 to a_drop15); a_drop10 to a_drop12 score as in play without the blockers
	void TavernBankHit(const DropBank& bank, const TPinballComponent* caller)
	{
		using enum MessageCode;
		auto full = AllSet(BankDown(bank), (1u << bank.Targets.size()) - 1);
		if (&bank == &drop_banks[2])
		{
			if (full)
			{
				ResetBank(bank);
				ScoreBy(caller, 1);
				bank.FullSound->Play(nullptr, "TavernBankHit");
			}
			else
			{
				bank.HitSound->Play(nullptr, "TavernBankHit");
				ScoreBy(caller, 0);
			}
			return;
		}
		if (full)
		{
			a_blocc->Message(TAnimatedBlockerSetState, 0.0f);
			BloceUp();
			a_blocf->Message(TAnimatedBlockerSetState, 0.0f);
			a_blocg->Message(TAnimatedBlockerSetState, 0.0f);
			ResetBank(bank);
			AwardJackpot(MagnetOn());
			AddScore(50000);
		}
		else
		{
			MagnetOn();
			tabb87wave->Play(nullptr, "TavernBankHit");
			AddScore(25000);
		}
	}

	// 0x426a40, 0x4264f0 and 0x426eb0: a full bank turns the blockers, comes up again and scores more. In play, the
	// last letter of the word starts the picked mode instead
	void BankHit(const DropBank& bank, const TPinballComponent* caller)
	{
		using enum MessageCode;
		if (GameState() == PlayState && WordSpelled())
			return;
		if (GameState() == AttackShipState && &bank == &drop_banks[1])
		{
			// Attack Ship's ship (0x4264f0): a full bank sinks it for the jackpot, and "Ship Sunk" follows the picture
			if (AllSet(BankDown(bank), (1u << bank.Targets.size()) - 1))
			{
				ResetBank(bank);
				auto time = AwardJackpot(0.0f);
				fathom_light_group->Message(TLightGroupRestartNotifyTimer, time);
				AddScore(25000);
			}
			else
			{
				tabb22wave->Play(nullptr, "BankHit");
				AddScore(10000);
			}
			return;
		}
		if (GameState() == TavernState && &bank != &drop_banks[1])
		{
			TavernBankHit(bank, caller);
			return;
		}
		if (AllSet(BankDown(bank), (1u << bank.Targets.size()) - 1))
		{
			a_blocc->Message(TAnimatedBlockerSetState, bank.BloccState);
			BloceUp();
			a_blocf->Message(TAnimatedBlockerSetState, bank.BlocfState);
			a_blocg->Message(TAnimatedBlockerSetState, 0.0f);
			ResetBank(bank);
			ScoreBy(caller, 1);
			bank.FullSound->Play(nullptr, "BankHit");
		}
		else
		{
			ScoreBy(caller, 0);
			bank.HitSound->Play(nullptr, "BankHit");
		}
	}

	void ShowBonus(Msg text, int score, float time)
	{
		std::array<char, 64> buffer{};
		snprintf(buffer.data(), buffer.size(), pb::get_rc_string(text), static_cast<long>(score));
		info_text_box->Display(buffer.data(), time);
	}

	// State 1, 0x42bfd0
	void GameOverStateControl(MessageCode code)
	{
		using enum MessageCode;
		if (code != EnterState)
			return;
		Table->Message(GameOver, 0.0f);
		PlayTrack(MidiTracks::Track7);
		if (pb::chk_highscore())
		{
			tabb73wave->Play(nullptr, "GameOverStateControl");
			Table->LightGroup->Message(TLightFlasherStartTimedThenStayOff, 3.0f);
			info_text_box->Display(pb::get_rc_string(Msg::Skulduggery_HighScore), -1.0f);
		}
	}

	// State 2, 0x42ab90
	void PlayStateControl(MessageCode code)
	{
		if (code == EnterState)
		{
			Launching = false;
			PlayTrack(MidiTracks::Track1);
		}
		else if (code == ShowStateText)
		{
			info_text_box->Display(pb::get_rc_string(Msg::Skulduggery_SpellSkulduggery), -1.0f);
		}
	}

	// The rolling skill shot's pictures in the second font (0x43aa58): seven symbols that turn one step every 0.2 s
	constexpr std::array<const char*, 9> SkillShotPictures{
		"$&(),.2", "\"$&'*,.", "0\"$%(*,", "20\"#&(*", ".20!$&(", ",.2/\"$&", "*,.10\"$", "(*,-20\"", "&(*+.20",
	};

	// State 3, 0x42abf0: the ball was served. The text box turns the skill shot's award every 0.2 s, and the lane's
	// exit (s_oneway3) awards the one showing. The awards (coins, a mode, a random award) come with their features;
	// until then the launch ends in play
	void LaunchStateControl(MessageCode code, const TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code == EnterState)
		{
			Launching = true;
			TargetHits = 0;
			a_blocc->Message(TAnimatedBlockerSetState, -1.0f);
			BloceDown();
			a_blocf->Message(TAnimatedBlockerSetState, 0.0f);
			a_blocg->Message(TAnimatedBlockerSetState, -1.0f);
		}
		else if (code == ShowStateText)
		{
			auto& award = SkillShotAward();
			award = award + 1 == static_cast<int>(SkillShotPictures.size()) ? 0 : award + 1;
			ShowPicture(SkillShotPictures[award], 0.2f, Msg::Skulduggery_PictureSkillShot);
		}
		else if (code == ControlCollision && caller == s_oneway3)
		{
			info_text_box->Clear();
			// 0x423460: v_lite606 off, then play
			v_lite606->Message(TLightResetAndTurnOff, 0.0f);
			SetState(PlayState);
		}
	}

	// State 4, 0x42ad70: Attack Ship. The ship's bank (a_drop5 to a_drop9) sinks it for the jackpot, and the wire
	// ramp's exit (s_rexit8) captures it, which lights the fathoms and raises the multiplier. It runs for 29.38 s after
	// its light sweep
	void AttackShipStateControl(MessageCode code, const TPinballComponent* caller)
	{
		using enum MessageCode;
		switch (code)
		{
		case ControlTimerExpired:
			if (caller == mode_timer)
				TableHook(ModeEnd, nullptr);
			else if (caller == light_sweeps)
				ModeSweepDone(29.38f);
			break;
		case ControlNotifyTimerExpired:
			// The jackpot's picture is over
			if (caller == fathom_light_group)
			{
				info_text_box->Display(pb::get_rc_string(Msg::Skulduggery_ShipSunk), 2.0f);
				tabb26wave->Play(nullptr, "AttackShipStateControl");
			}
			break;
		case EnterState:
			ModeEnter();
			ModeFlags = 1;
			light_sweeps->Message(TLightSweepsStart, 0.0f);
			ResetBank(drop_banks[1]);
			a_blocc->Message(TAnimatedBlockerSetState, -1.0f);
			BloceUp();
			a_blocf->Message(TAnimatedBlockerSetState, 0.0f);
			a_blocg->Message(TAnimatedBlockerSetState, 0.0f);
			AddScore(200000);
			info_text_box->Display(pb::get_rc_string(Msg::Skulduggery_AttackShipHelp), -1.0f);
			for (auto index = 3; index < 8; index++)
				letter_lights[index]->Message(TLightFlasherStartTimed, -1.0f);
			v_lite567->Message(TLightFlasherStartTimed, -1.0f);
			PlayTrack(MidiTracks::Track2);
			tabb58wave->Play(nullptr, "AttackShipStateControl");
			break;
		case ShowStateText:
			ShowTimeLeft(Msg::Skulduggery_AttackShip);
			break;
		case ModeEnd:
			v_lite567->Message(TLightResetAndTurnOff, 0.0f);
			ResetBank(drop_banks[1]);
			ModeLeave();
			v_lite606->MessageField++;
			BloceDown();
			break;
		case BallDrained:
			mode_timer->Message(TTimerResetTimer, -1.0f);
			TableHook(ModeEnd, nullptr);
			break;
		default:
			break;
		}
	}

	// 0x424500: the X lights out
	void TreasureXsOff()
	{
		for (auto light : v_mlights)
			light->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	}

	// 0x424580: the next X flashes, with the lights of the sink it marks; Returns the light sweep that points to it
	int NextTreasureX()
	{
		using enum MessageCode;
		a_sink9->ActiveFlag = 1;
		a_sink12->ActiveFlag = 1;
		switch (ModeAwardStep++)
		{
		case 0:
			v_mlights[4]->Message(TLightFlasherStartTimed, -1.0f);
			left_orbit_light_group->Message(TLightFlasherStartTimed, -1.0f);
			return 5;
		case 1:
			v_mlights[1]->Message(TLightFlasherStartTimed, -1.0f);
			v_lite524s[1]->Message(TLightFlasherStartTimed, -1.0f);
			v_lite520->Message(TLightFlasherStartTimed, -1.0f);
			return 2;
		case 2:
			v_mlights[5]->Message(TLightFlasherStartTimed, -1.0f);
			right_orbit_light_group->Message(TLightFlasherStartTimed, -1.0f);
			return 6;
		case 3:
			v_mlights[2]->Message(TLightFlasherStartTimed, -1.0f);
			v_lite524s[2]->Message(TLightFlasherStartTimed, -1.0f);
			v_lite521->Message(TLightFlasherStartTimed, -1.0f);
			return 3;
		default:
			ModeAwardStep = 0;
			v_mlights[0]->Message(TLightFlasherStartTimed, -1.0f);
			v_lite524s[0]->Message(TLightFlasherStartTimed, -1.0f);
			v_lite519->Message(TLightFlasherStartTimed, -1.0f);
			return 1;
		}
	}

	// 0x424780: special treasure <index>, its light and its score, with its name for <time> plus 2 s. The random
	// award's flag that shortens it to 1.625 s (0x43abac) comes with that award
	void AwardSpecialTreasure(int index)
	{
		using enum Msg;
		struct Treasure
		{
			Msg Name;
			int Score;
		};
		static constexpr std::array treasures{
			Treasure{Skulduggery_GoldIngots, 100000},       Treasure{Skulduggery_SilverChalice, 250000},
			Treasure{Skulduggery_SignetRing, 500000},       Treasure{Skulduggery_FrantzRuby, 750000},
			Treasure{Skulduggery_ShapirosSceptre, 1000000}, Treasure{Skulduggery_CrownOfSandige, 1500000},
		};
		special_treasure_light_group->MessageField++;
		index = std::clamp(index, 0, static_cast<int>(treasures.size()) - 1);
		treasure_lights[index]->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		AddScore(treasures[index].Score);
		info_text_box->Display(pb::get_rc_string(treasures[index].Name), 2.0f);
		tabb84wave->Play(nullptr, "AwardSpecialTreasure");
	}

	// 0x4236a0: the ball a sink caught comes out of a_sink8 in play, else out of a_sink14, a_sink15 or a_sink17 at
	// random
	void SinkKick()
	{
		if (GameState() == PlayState)
		{
			a_sink8->Message(MessageCode::TSinkResetTimer, -1.0f);
			return;
		}
		static const std::array sinks{&a_sink17, &a_sink15, &a_sink14};
		auto index = std::min(static_cast<int>(RandFloat() * 3.0f), 2);
		(*sinks[index])->Message(MessageCode::TSinkResetTimer, -1.0f);
	}

	// Each lit light of <group> goes out, and then, as CADET.EXE's 0x31 does, its first <count> plus one light up;
	// this port's TLightGroupToggleSplitIndex skips a <count> of all the lights, which CADET.EXE lights in full
	void LightFirst(TLightGroup* group, int count)
	{
		using enum MessageCode;
		group->Message(TLightTurnOff, 0.0f);
		auto lights = group->Message(TLightGroupGetLightCount, 0.0f);
		if (count >= lights)
			group->Message(TLightResetAndTurnOn, 0.0f);
		else
			group->Message(TLightGroupToggleSplitIndex, static_cast<float>(count));
	}

	// State 8, 0x42b090: Treasure Hunt. An X flashes by one of five sinks (NextTreasureX); the ball in that sink finds
	// a special treasure, or the jackpot at a_sink17, and the next X flashes. It runs for 30.68 s after its light sweep
	void TreasureHuntStateControl(MessageCode code, const TPinballComponent* caller)
	{
		using enum MessageCode;
		switch (code)
		{
		case ControlTimerExpired:
			if (caller == mode_timer)
				TableHook(ModeEnd, nullptr);
			else if (caller == light_sweeps)
				ModeSweepDone(30.68f);
			break;
		case ControlCollision:
			if (dynamic_cast<const TLightRollover*>(caller))
				AddJackpot(20000);
			break;
		case EnterState:
			ModeEnter();
			a_blocc->Message(TAnimatedBlockerSetState, -1.0f);
			a_blocf->Message(TAnimatedBlockerSetState, -1.0f);
			a_blocg->Message(TAnimatedBlockerSetState, -1.0f);
			AddScore(300000);
			TreasureXsOff();
			s_trip3->ActiveFlag = 0;
			s_trip8->ActiveFlag = 0;
			// The orbits' lights wait for the mode's end
			s_trip3->MessageField = left_orbit_light_group->Message(TLightGroupGetOnCount, 0.0f);
			s_trip8->MessageField = right_orbit_light_group->Message(TLightGroupGetOnCount, 0.0f);
			left_orbit_light_group->Message(TLightResetAndTurnOff, 0.0f);
			right_orbit_light_group->Message(TLightResetAndTurnOff, 0.0f);
			left_orbit_combo_light_group->Message(TLightResetAndTurnOff, 0.0f);
			right_orbit_combo_light_group->Message(TLightResetAndTurnOff, 0.0f);
			v_lite520->Message(TLightResetAndTurnOff, 0.0f);
			v_lite521->Message(TLightResetAndTurnOff, 0.0f);
			light_sweeps->Message(TLightSweepsStart, static_cast<float>(NextTreasureX()));
			PlayTrack(MidiTracks::Track3);
			tabb59wave->Play(nullptr, "TreasureHuntStateControl");
			info_text_box->Display(pb::get_rc_string(Msg::Skulduggery_TreasureHuntHelp), -1.0f);
			break;
		case ShowStateText:
			ShowTimeLeft(Msg::Skulduggery_TreasureHunt);
			break;
		case ModeEnd:
			TreasureXsOff();
			a_sink9->ActiveFlag = 0;
			a_sink12->ActiveFlag = 0;
			ModeLeave();
			v_lite606->MessageField++;
			BloceDown();
			v_lite519->Message(TLightResetAndTurnOff, 0.0f);
			s_trip3->ActiveFlag = 1;
			s_trip8->ActiveFlag = 1;
			LightFirst(left_orbit_light_group, s_trip3->MessageField);
			LightFirst(right_orbit_light_group, s_trip8->MessageField);
			break;
		case BallDrained:
			mode_timer->Message(TTimerResetTimer, -1.0f);
			TableHook(ModeEnd, nullptr);
			break;
		default:
			break;
		}
	}

	// 0x423aa0: a map piece, with its picture, and One Million; the last one picks Peg Leg's Loot in play
	void AwardMapPiece()
	{
		using enum MessageCode;
		map_piece_light_group->Message(TLightGroupResetAndTurnOn, 1.0f);
		v_lite607->MessageField++;
		ShowPicture("C", 2.0f, Msg::Skulduggery_PictureMapPiece);
		info_text_box->Display(pb::get_rc_string(Msg::Skulduggery_OneMillion), 4.0f);
		AddScore(1000000);
		tabb83wave->Play(nullptr, "AwardMapPiece");
		if (map_piece_light_group->Message(TLightGroupGetOnCount, 0.0f) ==
				map_piece_light_group->Message(TLightGroupGetLightCount, 0.0f) &&
			GameState() == PlayState)
			PickMode();
	}

	// 0x423720: the next coin lights, and the coins lit score (0x43d0c0), with the coins' picture and the score
	void AwardCoins()
	{
		static constexpr std::array scores{0, 50000, 100000, 200000, 400000, 800000, 1600000};
		coin_light_group->Message(MessageCode::TLightGroupResetAndTurnOn, 1.0f);
		auto lit = coin_light_group->Message(MessageCode::TLightGroupGetOnCount, 1.0f);
		auto score = AddScore(scores[std::clamp(lit, 0, static_cast<int>(scores.size()) - 1)]);
		ShowPicture("D", 2.0f, Msg::Skulduggery_PictureCoins);
		ShowBonus(Msg::Skulduggery_Score, score, 4.0f);
		tabb84wave->Play(nullptr, "AwardCoins");
	}

	// 0x4239d0: an extra ball
	void AwardExtraBall()
	{
		tabb00wave->Play(nullptr, "AwardExtraBall");
		Table->ExtraBalls = 1;
		info_text_box->Display(pb::get_rc_string(Msg::Skulduggery_ExtraBall), 2.0f);
	}

	// 0x424fb0: a map piece, a special treasure, coins or an extra ball, at random
	void TavernAward()
	{
		switch (std::min(static_cast<int>(RandFloat() * 4.0f), 3))
		{
		case 0:
			AwardMapPiece();
			break;
		case 1:
			AwardSpecialTreasure(special_treasure_light_group->MessageField);
			break;
		case 2:
			AwardCoins();
			break;
		default:
			AwardExtraBall();
			break;
		}
	}

	// 0x425d50 with 0: v_lite530 to v_lite532 flash out, and a_spot6's count starts over
	void CrewLightsOff()
	{
		for (auto light : v_lite530s)
			light->Message(MessageCode::TLightResetAndTurnOff, 1.0f);
		a_spot6->MessageField = 0;
	}

	// State 7, 0x42b420: Tavern. The mugs (v_lite547, v_lite546, v_lite545) flash one at a time at random, and the
	// ball in a mug's sink while its light flashes gives a random award (TavernAward). The drop targets switch the
	// magnets on. It runs for 27.4 s after its light sweep
	void TavernStateControl(MessageCode code, const TPinballComponent* caller)
	{
		using enum MessageCode;
		switch (code)
		{
		case ControlTimerExpired:
			if (caller == mode_timer)
			{
				for (auto light : tavern_lights)
					light->Message(TLightResetAndTurnOff, 0.0f);
				TableHook(ModeEnd, nullptr);
			}
			else if (caller == light_sweeps)
			{
				ModeSweepDone(27.4f);
				TavernStateControl(ControlTimerExpired, tavern_lights[0]);
			}
			else if (std::ranges::find(tavern_lights, caller) != tavern_lights.end())
			{
				// The next mug flashes for 0.1 to 0.25 s
				auto light = tavern_lights[std::min(static_cast<int>(RandFloat() * 3.0f), 2)];
				auto steps = std::min(static_cast<int>(RandFloat() * 150.0f), 149);
				light->Message(TLightFlasherStartTimed, static_cast<float>(steps + 100) / 1000.0f);
			}
			break;
		case EnterState:
			ModeEnter();
			CrewLightsOff();
			AddScore(200000);
			light_sweeps->Message(TLightSweepsStart, 7.0f);
			PlayTrack(MidiTracks::Track4);
			tabb65wave->Play(nullptr, "TavernStateControl");
			ResetBank(drop_banks[0]);
			for (auto light : tavern_lights)
				light->Message(TLightResetAndTurnOff, 0.0f);
			a_spot6->MessageField = 0;
			for (auto light : v_lite530s)
				light->Message(TLightResetAndTurnOff, 0.0f);
			info_text_box->Display(pb::get_rc_string(Msg::Skulduggery_TavernHelp), -1.0f);
			for (auto index = 0; index < 3; index++)
				letter_lights[index]->Message(TLightFlasherStartTimed, -1.0f);
			break;
		case ShowStateText:
			ShowTimeLeft(Msg::Skulduggery_Tavern);
			break;
		case ModeEnd:
			CrewLightsOff();
			ResetBank(drop_banks[0]);
			MagnetsOff();
			ModeLeave();
			v_lite606->MessageField++;
			BloceDown();
			break;
		case BallDrained:
			mode_timer->Message(TTimerResetTimer, -1.0f);
			TableHook(ModeEnd, nullptr);
			break;
		default:
			break;
		}
	}

	// State 16, 0x42b820: count the bonus, one text at a time, then serve the next ball or end the game
	void EndOfBallStateControl(MessageCode code, const TPinballComponent* caller)
	{
		using enum MessageCode;
		switch (code)
		{
		case SkipBonusText: // count the rest without showing it
			TallyQuiet = true;
			info_text_box->Clear();
			break;
		case ControlTimerExpired:
			// mode_timer: the bonus took too long; count the rest at once. PIRATES.EXE's step 8 is past them all
			if (!caller || caller != mode_timer)
				return;
			LastBallRelease = false;
			TallyStep = 8;
			break;
		case EnterState:
			TallyQuiet = false;
			TallyStep = 0;
			TallyTotal = 0;
			mode_timer->Message(TTimerResetTimer, 30.0f);
			if (Table->TiltLockFlag && !LastBallRelease)
				TallyStep = 6;
			info_text_box->Clear();
			tabb42wave->Play(nullptr, "EndOfBallStateControl");
			break;
		case ShowStateText:
			break;
		case BallDrained:
			if (TallyStep < 6)
				return;
			break;
		default:
			return;
		}

		// Each bonus shows for 2 s; PIRATES.EXE shortens them to 0.25 s while a flag of the random award is set
		// (0x43abac), which comes with it
		constexpr float textTime = 2.0f;
		while (true)
		{
			auto showBonus = [](Msg text, int score)
			{
				TallyTotal += score;
				if (TallyQuiet)
					return false;
				tabb84wave->Play(nullptr, "EndOfBallStateControl");
				ShowBonus(text, score, textTime);
				return true;
			};
			switch (TallyStep++)
			{
			case 0:
				if (TargetHits && showBonus(Msg::Skulduggery_TargetBonus, TargetHits * 10000))
					return;
				break;
			case 1:
				// v_lite606's message field counts the modes played
				if (v_lite606->MessageField && showBonus(Msg::Skulduggery_ModeBonus, v_lite606->MessageField * 200000))
					return;
				break;
			case 2:
				// v_lite607's counts the map pieces
				if (v_lite607->MessageField &&
					showBonus(Msg::Skulduggery_MapPieceBonus, v_lite607->MessageField * 250000))
					return;
				break;
			case 3:
				if (auto treasures = special_treasure_light_group->MessageField;
					treasures && showBonus(Msg::Skulduggery_SpecialTreasures, treasures * 100000))
					return;
				break;
			case 4:
				if (ShipsHold() && showBonus(Msg::Skulduggery_ShipsHold, ShipsHold()))
					return;
				break;
			case 5:
				// A lit v_lite570 keeps this ball's bonus for the next one
				if (v_lite570->light_on())
				{
					v_lite570->Message(TLightResetAndTurnOff, 0.0f);
					ShipsHold() = TallyTotal;
				}
				else
				{
					ShipsHold() = 0;
				}
				if (TallyTotal)
				{
					AddScoreUnmultiplied(nullptr, TallyTotal);
					if (!TallyQuiet)
					{
						tabb74wave->Play(nullptr, "EndOfBallStateControl");
						ShowBonus(Msg::Skulduggery_TotalBonus, TallyTotal, textTime / 2.0f + textTime);
						return;
					}
				}
				break;
			default:
				// Wait for the brig's balls the last ball released
				if (LastBallRelease && Table->MultiballCount)
					return;
				LastBallRelease = false;
				mode_timer->Message(TTimerResetTimer, -1.0f);
				if (Table->ExtraBalls)
				{
					Table->ExtraBalls--;
					static constexpr std::array shootAgain{Msg::STRING198, Msg::STRING199, Msg::STRING200,
														   Msg::STRING201};
					info_text_box->Display(pb::get_rc_string(shootAgain[std::min(Table->CurrentPlayer, 3)]), -1.0f);
					Table->Message(ClearTiltLock, 0.0f);
					s_launcher1->Message(PlungerStartFeedTimer, 0.96f);
				}
				else
				{
					Table->ChangeBallCount(Table->BallCount - 1);
					if (Table->CurrentPlayer + 1 == Table->PlayerCount && !Table->BallCount)
					{
						Table->LightGroup->Message(TLightResetAndTurnOff, 0.0f);
						SetState(GameOverState);
						Table->LightGroup->Message(TLightFtResetOverride, 0.0f);
						return;
					}
					Table->Message(ClearTiltLock, 0.0f);
					drain->Message(SetTiltLock, 0.0f);
					Table->Message(SwitchToNextPlayer, 0.0f);
					s_launcher1->Message(PlungerStartFeedTimer, 0.96f);
				}
				SetState(LaunchState);
				Table->LightGroup->Message(TLightFtResetOverride, 0.0f);
				return;
			}
		}
	}

	// State 17, 0x42d730: the game's last ball drained; the brig lets its balls go, and the flippers stay dead until
	// the bonus is counted
	void LastBallStateControl(MessageCode code)
	{
		using enum MessageCode;
		if (code == EnterState)
		{
			s_bwal610->ActiveFlag = 0;
			a_kick2->ActiveFlag = 0;
			a_kick1->Message(TKickbackEnable, 0.0f);
			s_oneway12->ActiveFlag = 0;
			for (auto magnet : magnets)
				magnet->Message(TMagnetDisable, 0.0f);
			a_kout1->Message(TKickoutDisable, 0.0f);
			a_kout2->Message(TKickoutDisable, 0.0f);
			if (BrigBalls > 0)
			{
				Table->MultiballCount += BrigBalls;
				BrigBalls = 0;
			}
		}
		else if (code == BallDrained && Table->MultiballCount == 1)
		{
			a_kick1->Message(TKickbackDisable, 0.0f);
		}
	}

	// 0x42d900
	void StateControl(int state, MessageCode code, const TPinballComponent* caller)
	{
		// Every state clears the mode picked, play picks one, and a mode's light flashes while it runs. As a mode
		// ends its light stays on, it adds 750,000 to the jackpot, and the next mode is picked
		if (code == EnterState)
		{
			ModeLights(state, true);
		}
		else if (code == ModeEnd)
		{
			if (state >= FirstModeState && state <= LastModeState)
				AddJackpot(750000);
			ModeLights(state, false);
			PickMode();
		}

		switch (state)
		{
		case NewGameState:
			break;
		case GameOverState:
			GameOverStateControl(code);
			break;
		case PlayState:
			PlayStateControl(code);
			break;
		case LaunchState:
			LaunchStateControl(code, caller);
			break;
		case EndOfBallState:
			EndOfBallStateControl(code, caller);
			break;
		case LastBallState:
			LastBallStateControl(code);
			break;
		case AttackShipState:
			AttackShipStateControl(code, caller);
			break;
		case TavernState:
			TavernStateControl(code, caller);
			break;
		case TreasureHuntState:
			TreasureHuntStateControl(code, caller);
			break;
		default:
			// The other modes' states (up to 13) aren't ported yet, and StartMode doesn't enter them; PIRATES.EXE goes
			// back to play from any state it doesn't know (0x42d830)
			if (state < FirstModeState || state > JailbreakState)
				SetState(PlayState);
			break;
		}
	}

	// 0x42d850: every component's events reach it before the component's own handler
	void TableHook(MessageCode code, TPinballComponent* caller)
	{
		if (!info_text_box)
			return;
		while (true)
		{
			// Popup targets count for the Target Bonus; they, spot targets and sinks add to the jackpot
			if (dynamic_cast<TPopupTarget*>(caller))
			{
				TargetHits++;
				AddJackpot(20000);
			}
			else if (dynamic_cast<TSink*>(caller))
			{
				AddJackpot(40000);
			}
			else if (dynamic_cast<TSoloTarget*>(caller))
			{
				AddJackpot(20000);
			}
			if (code == MessageCode::ControlTimerExpired && caller == info_text_box && !info_text_box->CurrentMessage)
				code = ShowStateText;
			StateControl(GameState(), code, caller);
			if (code != ModeEnd)
				return;
			code = EnterState;
			caller = nullptr;
			GameState() = PlayState;
		}
	}

	// drain, 0x426110
	void DrainControl(MessageCode code, TPinballComponent*)
	{
		using enum MessageCode;
		if (code == ControlTimerExpired)
		{
			if (LastBallRelease)
				return;
			if (DrainStep == 2)
				s_launcher1->Message(PlungerStartFeedTimer, 0.96f);
			else if (DrainStep == 3)
				s_launcher1->Message(PlungerRelaunchBall, 0.96f);
			return;
		}
		if (code != ControlCollision)
			return;

		DrainStep = 1;
		if (Table->TiltLockFlag)
		{
			v_lite569->Message(TLightResetAndTurnOff, 0.0f);
			BallSaveStep = 0;
		}
		if (v_lite569->light_on())
		{
			// Ball save: the drain's own timer serves the same ball again. Its Reset clears every player's Ship's Hold,
			// as PIRATES.EXE's 0x402 does
			info_text_box->Display(pb::get_rc_string(Msg::Skulduggery_ShootAgain), 2.0f);
			DrainStep = 3;
			drain->Message(Reset, 0.0f);
			Table->BallInDrainFlag = 1; // PIRATES.EXE's table field 0x6a, as DRAGON.EXE's
			DrainControl(ControlTimerExpired, drain);
			return;
		}

		TableHook(BallDrained, drain);
		if (!Table->MultiballCount && !Table->ExtraBalls && Table->BallCount == 1 &&
			Table->CurrentPlayer + 1 == Table->PlayerCount && !LastBallRelease)
		{
			// The game's last ball: the brig's balls come out, with the flippers dead
			LastBallRelease = Table->TiltLockFlag != -1;
			Table->TiltLockFlag = 1;
			for (auto flipper : flippers)
				flipper->Message(SetTiltLock, 0.0f);
			SetState(LastBallState);
		}
		if (Table->MultiballCount && !LastBallRelease)
		{
			DrainStep = 0;
			drain->Message(Reset, 0.0f);
			return;
		}
		if (ClearBuriedLights)
		{
			ClearBuriedLights = false;
			coin_light_group->Message(TLightResetAndTurnOff, 0.0f);
			special_treasure_light_group->Message(TLightResetAndTurnOff, 0.0f);
		}
		if (GameState() == EndOfBallState || GameState() == GameOverState)
			return;
		SetState(EndOfBallState);
	}

	// s_launcher1, 0x425e70
	void LauncherControl(MessageCode code, TPinballComponent*)
	{
		using enum MessageCode;
		switch (code)
		{
		case PlungerInputReleased:
			// The cannon fires
			if (!Table->TiltLockFlag)
			{
				a_cannon->Message(TAnimatedBlockerSetState, 3.0f);
				a_cannon->Message(TAnimatedBlockerAnimateTo, 0.0f);
			}
			break;
		case PlungerFeedBall:
			TableHook(ShowStateText, nullptr);
			// The ball save's light as its step has it
			switch (BallSaveStep)
			{
			case 0:
				v_lite569->Message(TLightResetAndTurnOff, 0.0f);
				break;
			case 1:
				if (!v_lite569->FlasherOnFlag)
					v_lite569->Message(TLightFlasherStartTimed, 5.0f);
				break;
			case 2:
				if (!v_lite569->ToggledOnFlag)
					v_lite569->Message(TLightTurnOnTimed, 10.0f);
				break;
			default:
				BallSaveStep = 3;
				if (!v_lite569->LightOnFlag)
					v_lite569->Message(TLightResetAndTurnOn, 0.0f);
				break;
			}
			break;
		case PlungerStartFeedTimer:
			// A new ball, unless the ball save serves the same one again
			if (!BallSaveStep)
			{
				SetState(LaunchState);
				MagnetsOff();
				BloceDown();
			}
			v_lite569->Message(TLightResetAndTurnOn, 0.0f);
			BallSaveStep = 3;
			v_lite544->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			a_kick2->ActiveFlag = 1;
			s_bwal610->ActiveFlag = 1;
			fathom_light_group->Message(TLightResetAndTurnOff, 0.0f);
			Table->ScoreMultiplier = 1;
			a_kick1->Message(TKickbackDisable, 0.0f);
			BloceDown();
			break;
		default:
			break;
		}
	}

	// v_lite569, the ball save's light, 0x426080
	void BallSaveControl(MessageCode code, TPinballComponent*)
	{
		using enum MessageCode;
		if (code != ControlTimerExpired)
			return;
		switch (BallSaveStep)
		{
		case 3:
			BallSaveStep = 2;
			v_lite569->Message(TLightTurnOnTimed, 10.0f);
			break;
		case 2:
			BallSaveStep = 1;
			v_lite569->Message(TLightFlasherStartTimed, 5.0f);
			break;
		case 1:
			BallSaveStep = 0;
			v_lite569->Message(TLightResetAndTurnOff, 0.0f);
			break;
		default:
			break;
		}
	}

	// s_oneway3, the lane's exit, 0x42a3b0: the ball save starts counting once the ball is out
	void LaneExitControl(MessageCode code, TPinballComponent* caller)
	{
		if (code == MessageCode::ControlCollision && BallSaveStep == 3)
			BallSaveControl(MessageCode::ControlTimerExpired, caller);
	}

	// Stand-ins until the features that own them are ported
	void StandInControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		if (dynamic_cast<TMagnet*>(caller))
			return;
		if (auto kickout = dynamic_cast<TKickout*>(caller))
			kickout->Message(TKickoutRestartTimer, kickout->TimerTime1);
		else if (auto sink = dynamic_cast<TSink*>(caller))
			sink->Message(TSinkResetTimer, sink->TimerTime);
	}

	// The drop targets, 0x4268b0 to 0x427300: each one down counts for its bank, and in play and Lava-Ball it spells
	// its letter
	void DropTargetControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		for (const auto& bank : drop_banks)
		{
			auto target = std::ranges::find(bank.Targets, caller);
			if (target == bank.Targets.end())
				continue;
			auto index = static_cast<int>(target - bank.Targets.begin());
			SetBit(BankDown(bank), index);
			if (GameState() == PlayState || GameState() == LavaBallState)
			{
				auto letter = bank.FirstLetter + index;
				SetBit(LettersSpelled(), letter);
				letter_lights[letter]->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			}
			BankHit(bank, caller);
			return;
		}
	}

	// a_spot11, a_spot8, a_spot9 and a_spot10, the captive ball's targets, 0x427580 to 0x4277f0: in play each one
	// lights its light, and all four score more and start Lava-Ball (0x427540). Sword Fight's use of them comes with it
	void CaptiveTargetControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		auto target = std::ranges::find(captive_targets, caller);
		if (target == captive_targets.end())
			return;
		tabb23wave->Play(nullptr, "CaptiveTargetControl");
		if (GameState() != PlayState)
		{
			ScoreBy(caller, 0);
			return;
		}
		auto index = static_cast<int>(target - captive_targets.begin());
		auto& hit = captive_targets[0]->MessageField;
		SetBit(hit, index);
		captive_lights[index]->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
		if (!AllSet(hit, 0xf))
		{
			ScoreBy(caller, 0);
			return;
		}
		hit = 0;
		ScoreBy(caller, 1);
		StartMode(LavaBallState);
	}

	// a_spot102, 0x4273b0: in play it picks the next mode and names it; during a mode it adds 10 s to its time
	void ModeTargetControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		if (auto state = GameState(); state == PlayState)
		{
			using enum Msg;
			static constexpr std::array selected{
				Skulduggery_AttackModeSelected,     Skulduggery_TreasureHuntModeSelected,
				Skulduggery_TavernModeSelected,     Skulduggery_BermudaModeSelected,
				Skulduggery_StormModeSelected,      Skulduggery_MutinyModeSelected,
				Skulduggery_SwordfightModeSelected, Skulduggery_PeglegsLootModeSelected,
			};
			info_text_box->Display(pb::get_rc_string(selected[PickMode()]), 2.0f);
		}
		else if (state >= FirstModeState && state <= LastModeState)
		{
			if (auto msLeft = mode_timer->Message(TTimerGetTimeLeft, 0.0f); msLeft > 0)
				mode_timer->Message(TTimerResetTimer, static_cast<float>(msLeft) / 1000.0f + 10.0f);
		}
		ScoreBy(caller, 0);
		v_lite607->Message(TLightFlasherStartTimed, 1.0f);
	}

	// s_rexit8, the wire ramp's exit, 0x429140: its score, shown; in Attack Ship it captures the ship
	void WireRampExitControl(MessageCode code, TPinballComponent* caller)
	{
		if (code != MessageCode::ControlRampExitToLayer2)
			return;
		if (GameState() != AttackShipState)
		{
			ShowBonus(Msg::Skulduggery_Score, ScoreBy(caller, 0), 2.0f);
			tabb40wave->Play(nullptr, "WireRampExitControl");
			return;
		}
		ResetBank(drop_banks[1]);
		ModeFlags++;
		info_text_box->Display(pb::get_rc_string(Msg::Skulduggery_ShipCaptured), 2.0f);
		LightFathoms(2.0f);
		tabb57wave->Play(nullptr, "WireRampExitControl");
	}

	// A sink an X marks in Treasure Hunt: its X, the orbit or the light that goes out with the X, and the light that
	// flashes as the ball leaves. The components are found at MakeLinks, so the entries point to where they're kept
	struct TreasureSink
	{
		TSink* const* Sink;
		TLight* const* X;
		TLightGroup* const* Orbit;
		TLight* const* Marker;
		TLight* const* Flash;
	};
	// a_sink9 (0x428700), a_sink12 (0x428800), a_sink14 (0x428120), a_sink15 (0x427e50) and a_sink17 (0x427b90)
	const std::array TreasureSinks{
		TreasureSink{&a_sink9, &v_mlights[4], &left_orbit_light_group, nullptr, nullptr},
		TreasureSink{&a_sink12, &v_mlights[5], &right_orbit_light_group, nullptr, nullptr},
		TreasureSink{&a_sink14, &v_mlights[2], nullptr, &v_lite524s[2], &v_lite521},
		TreasureSink{&a_sink15, &v_mlights[1], nullptr, &v_lite524s[1], &v_lite520},
		TreasureSink{&a_sink17, &v_mlights[0], nullptr, &v_lite524s[0], &v_lite519},
	};

	// The sinks' Treasure Hunt part. In other states they still give their balls back, until their features come
	void TreasureSinkControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		if (GameState() != TreasureHuntState)
		{
			StandInControl(code, caller);
			return;
		}
		auto sink = std::ranges::find(TreasureSinks, caller, [](const TreasureSink& entry) { return *entry.Sink; });
		if (auto x = *sink->X; !x->FlasherOnFlag)
		{
			ShowBonus(Msg::Skulduggery_SecretHideaway, ScoreBy(caller, 0), 2.0f);
			tabb11wave->Play(nullptr, "TreasureSinkControl");
			SinkKick();
		}
		else
		{
			x->Message(TLightResetAndTurnOn, 0.0f);
			if (sink->Orbit)
				(*sink->Orbit)->Message(TLightResetAndTurnOff, 0.0f);
			if (sink->Marker)
				(*sink->Marker)->Message(TLightResetAndTurnOff, 0.0f);
			if (sink->Flash)
				(*sink->Flash)->Message(TLightResetAndTurnOff, 0.0f);
			if (caller == a_sink17)
				AwardJackpot(0.0f);
			else
				AwardSpecialTreasure(special_treasure_light_group->MessageField);
			NextTreasureX();
			a_sink8->Message(TSinkResetTimer, -1.0f);
		}
		if (sink->Flash)
			(*sink->Flash)->Message(TLightFlasherStartTimed, 2.0f);
	}

	// 0x423830 and 0x424950: what is buried, with its name for 2 s
	void ShowBuried(Msg text)
	{
		info_text_box->Display(pb::get_rc_string(text), 2.0f);
	}

	// 0x424950: the next special treasure not found is buried in <slot>, and its light flashes
	void BuryTreasure(int& slot)
	{
		using enum Msg;
		static constexpr std::array buried{Skulduggery_GoldIngotsBuried,      Skulduggery_SilverChaliceBuried,
										   Skulduggery_SignetRingBuried,      Skulduggery_FrantzRubyBuried,
										   Skulduggery_ShapirosSceptreBuried, Skulduggery_CrownOfSandigeBuried};
		slot = special_treasure_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
		auto index = std::clamp(slot, 0, static_cast<int>(buried.size()) - 1);
		treasure_lights[index]->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		ShowBuried(buried[index]);
		tabb84wave->Play(nullptr, "BuryTreasure");
	}

	// 0x424aa0: the bonus buried, from the special treasures, map pieces, modes and targets, times the fathoms lit
	int BuriedBonus()
	{
		auto points = (special_treasure_light_group->MessageField * 2 + v_lite607->MessageField) * 25 +
					  v_lite606->MessageField * 20 + TargetHits;
		return fathom_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f) * points * 10000;
	}

	// a_roll8 and a_roll9, 0x429c30 and 0x429ac0: in Treasure Hunt each one buries the next treasure for Peg Leg's
	// Loot (0x424b00): coins three times, two special treasures, the bonus, the jackpot and an extra ball, then coins.
	// Their other states come with the WINDS lanes
	void BuryRolloverControl(MessageCode code, TPinballComponent*)
	{
		if (code != MessageCode::ControlCollision || GameState() != TreasureHuntState)
			return;
		using enum Msg;
		switch (BuryStep++)
		{
		case 3:
			BuryTreasure(TreasureBuried1);
			break;
		case 4:
			BuryTreasure(TreasureBuried2);
			break;
		case 5:
			ShowBuried(Skulduggery_BonusBuried);
			BonusBuried = BuriedBonus();
			break;
		case 6:
			ShowBuried(Skulduggery_JackpotBuried);
			JackpotBuried = Table->JackpotScore;
			break;
		case 7:
			ShowBuried(Skulduggery_ExtraBallBuried);
			ExtraBallBuried = true;
			break;
		default:
			ShowBuried(Skulduggery_CoinsBuried);
			CoinsBuried++;
			break;
		}
	}

	// a_sink8, 0x42a3d0: a ball in it comes straight out again
	void HideawayControl(MessageCode code, TPinballComponent* caller)
	{
		if (code == MessageCode::ControlCollision)
			caller->Message(MessageCode::TSinkResetTimer, 0.0f);
	}

	// 0x428900: the mugs' picture, their score shown, and the ball out of a_sink8; all three mugs score more and flash
	// out
	void TavernMugs(const TPinballComponent* caller)
	{
		using enum MessageCode;
		static constexpr std::array pictures{"333", "433", "343", "443", "334", "434", "344", "444"};
		auto& lit = tavern_lights[0]->MessageField;
		ShowPicture(pictures[static_cast<unsigned>(lit) & 7u], 2.0f, Msg::Skulduggery_PictureTavern);
		auto all = AllSet(lit, 7);
		if (all)
		{
			for (auto light : tavern_lights)
				light->Message(TLightFlasherStartTimedThenStayOff, 1.0f);
			lit = 0;
		}
		auto score = ScoreBy(caller, all ? 1 : 0);
		tabb08wave->Play(nullptr, "TavernMugs");
		ShowBonus(Msg::Skulduggery_Score, score, 4.0f);
		a_blocc->Message(TAnimatedBlockerSetState, -1.0f);
		a_blocf->Message(TAnimatedBlockerSetState, -1.0f);
		a_blocg->Message(TAnimatedBlockerSetState, -1.0f);
		a_sink8->Message(TSinkResetTimer, -1.0f);
	}

	// a_sink5, a_sink6 and a_sink7, the mugs' sinks, 0x428a20 to 0x428b00: each lights its mug; in Tavern, a flashing
	// mug gives a random award. The ball comes out of a_sink8
	void TavernSinkControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		auto index = static_cast<int>(std::ranges::find(tavern_sinks, caller) - tavern_sinks.begin());
		auto light = tavern_lights[index];
		if (GameState() != TavernState)
		{
			SetBit(tavern_lights[0]->MessageField, index);
			light->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			TavernMugs(caller);
			return;
		}
		if (light->FlasherOnFlag)
			TavernAward();
		else
			tabb08wave->Play(nullptr, "TavernSinkControl");
		a_sink8->Message(TSinkResetTimer, -1.0f);
	}

	// s_magnet1 to s_magnet6, 0x42a470 to 0x42a740: a ball through a magnet switches it off and scores 100,000
	void MagnetControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		auto index = std::ranges::find(magnets, caller) - magnets.begin();
		tabb87wave->Play(nullptr, "MagnetControl");
		magnet_lights[index]->Message(TLightResetAndTurnOff, 2.0f);
		caller->Message(TMagnetDisable, 0.0f);
		ShowBonus(Msg::Skulduggery_Score, AddScore(100000), 2.0f);
	}

	void NoControl(MessageCode, TPinballComponent*)
	{
		// Components whose own handler isn't ported yet; WithHook still passes their events to the table
	}

	// PIRATES.EXE's control::handler (0x423080) calls the table-wide one, then the component's handler
	template <auto& Handler> void WithHook(MessageCode code, TPinballComponent* caller)
	{
		TableHook(code, caller);
		Handler(code, caller);
	}

	// The scores PIRATES.EXE's binding table (0x43c490) gives its components, for the handlers that score by them
	const std::map<std::string_view, std::vector<int>> ComponentScores{
		{"a_bump1", {1500, 3000, 4500, 6000, 7500, 9000, 10500}},
		{"a_bump2", {1500, 3000, 4500, 6000, 7500, 9000, 10500}},
		{"a_bump3", {1500, 3000, 4500, 6000, 7500, 9000, 10500}},
		{"a_rebo1", {350}},
		{"a_rebo2", {350}},
		{"a_drop5", {3000, 25000}},
		{"a_drop6", {3000, 25000}},
		{"a_drop7", {3000, 25000}},
		{"a_drop8", {3000, 25000}},
		{"a_drop9", {3000, 25000}},
		{"a_drop13", {4000, 30000}},
		{"a_drop14", {4000, 30000}},
		{"a_drop15", {4000, 30000}},
		{"a_drop10", {5000, 35000}},
		{"a_drop11", {5000, 35000}},
		{"a_drop12", {5000, 35000}},
		{"a_spot102", {50000}},
		{"a_spot11", {1000, 15000}},
		{"a_spot8", {1000, 15000}},
		{"a_spot9", {1000, 15000}},
		{"a_spot10", {1000, 15000}},
		{"a_spot4", {5000, 20000}},
		{"a_spot5", {5000, 20000}},
		{"a_spot6", {5000}},
		{"a_sink17", {40000}},
		{"a_sink15", {40000}},
		{"a_sink14", {40000}},
		{"a_sink4", {100000}},
		{"a_sink9", {40000}},
		{"a_sink12", {40000}},
		{"a_sink5", {75000, 200000}},
		{"a_sink6", {75000, 200000}},
		{"a_sink7", {75000, 200000}},
		{"a_ramp8", {25000, 50000, 75000, 150000, 175000, 200000, 275000, 300000, 325000, 425000, 450000, 475000}},
		{"a_ramp5", {25000, 50000, 75000, 150000, 175000, 200000, 275000, 300000, 325000, 425000, 450000, 475000}},
		{"a_ramp13", {50000}},
		{"s_rexit8", {100000}},
		{"s_trip3", {25000, 50000, 75000, 150000, 175000, 200000, 275000, 300000, 325000, 425000, 450000, 475000}},
		{"s_trip8", {25000, 50000, 75000, 150000, 175000, 200000, 275000, 300000, 325000, 425000, 450000, 475000}},
		{"s_trip11", {25000}},
		{"a_roll3", {25000}},
		{"a_roll5", {10000}},
		{"a_roll1", {10000}},
		{"a_roll2", {25000, 50000, 75000}},
		{"a_roll9", {25000, 50000, 75000}},
		{"a_roll8", {25000, 50000, 75000}},
		{"a_roll6", {25000, 50000, 75000}},
		{"a_roll12", {1200, 20000}},
		{"a_roll11", {1200, 20000}},
		{"a_roll10", {1200, 20000}},
		{"a_kick2", {5000}},
	};

	// deque keeps each link where it is as more are added, since the components point to them
	std::deque<component_control> Links;

	void Link(TPinballComponent& component, void (&handler)(MessageCode, TPinballComponent*))
	{
		static const std::vector<int> none;
		auto scores = component.GroupName ? ComponentScores.find(component.GroupName) : ComponentScores.end();
		auto& list = scores != ComponentScores.end() ? scores->second : none;
		component.Control = &Links.emplace_back(handler, static_cast<unsigned>(list.size()), list.data());
	}

	// The table's own events (0x4230c0)
	void TableControl(MessageCode code)
	{
		using enum MessageCode;
		if (!Table || !info_text_box)
			return;
		switch (code)
		{
		case SetTiltLock:
			SetState(LastBallState);
			break;
		case NewGame:
			// PIRATES.EXE also resets the modes' and the skill shot's state here (0x435d10 to 0x435d28), ported with
			// them
			Table->JackpotScore = 1000000;
			BallSaveStep = 0;
			BuryStep = 0;
			CoinsBuried = 1;
			TreasureBuried1 = -1;
			TreasureBuried2 = -1;
			BonusBuried = 0;
			JackpotBuried = 0;
			ExtraBallBuried = false;
			s_oneway12->ActiveFlag = 0;
			LastBallRelease = false;
			ClearBuriedLights = false;
			for (auto light : v_mlights)
				light->Message(TLightResetAndTurnOff, 0.0f);
			a_sink9->ActiveFlag = 0;
			a_sink12->ActiveFlag = 0;
			PlayTrack(MidiTracks::Track1);
			BrigBalls = 0;
			a_kick2->ActiveFlag = 0;
			s_bwal610->ActiveFlag = 0;
			a_kick1->Message(TKickbackDisable, 0.0f);
			a_kout1->Message(TKickoutDisable, 0.0f);
			a_kout2->Message(TKickoutDisable, 0.0f);
			// 0x423440: v_lite606 off and the launch, then the new game's state until the first ball
			v_lite606->Message(TLightResetAndTurnOff, 0.0f);
			SetState(LaunchState);
			GameState() = NewGameState;
			break;
		default:
			break;
		}
	}

	bool IsDropTarget(const TPinballComponent* component)
	{
		return std::ranges::any_of(drop_banks, [component](const DropBank& bank)
								   { return std::ranges::find(bank.Targets, component) != bank.Targets.end(); });
	}

	void MakeLinks(TPinballTable* table)
	{
		Table = table;
		Links.clear();
		Find(drain, "drain");
		Find(s_launcher1, "s_launcher1");
		Find(info_text_box, "info_text_box");
		Find(mode_timer, "mode_timer");
		Find(v_lite504, "v_lite504");
		Find(v_lite505, "v_lite505");
		Find(v_lite544, "v_lite544");
		Find(v_lite569, "v_lite569");
		Find(v_lite570, "v_lite570");
		Find(v_lite606, "v_lite606");
		Find(v_lite607, "v_lite607");
		for (auto index = 0u; index < magnet_lights.size(); index++)
			Find(magnet_lights[index], std::format("v_lite{}", 560 + index).c_str());
		constexpr std::array mlights{"v_mlite44", "v_mlite107", "v_mlite106", "v_mlite51", "v_mlite110", "v_mlite104"};
		for (auto index = 0u; index < v_mlights.size(); index++)
			Find(v_mlights[index], mlights[index]);
		Find(coin_light_group, "coin_light_group");
		Find(special_treasure_light_group, "special_treasure_light_group");
		Find(fathom_light_group, "fathom_light_group");
		for (auto index = 0u; index < flippers.size(); index++)
			Find(flippers[index], std::format("a_flip{}", index + 1).c_str());
		for (auto index = 0u; index < magnets.size(); index++)
			Find(magnets[index], std::format("s_magnet{}", index + 1).c_str());
		Find(a_cannon, "a_cannon");
		Find(a_blocc, "a_blocc");
		Find(a_bloce, "a_bloce");
		Find(a_blocf, "a_blocf");
		Find(a_blocg, "a_blocg");
		Find(a_kick1, "a_kick1");
		Find(a_kick2, "a_kick2");
		Find(a_kout1, "a_kout1");
		Find(a_kout2, "a_kout2");
		Find(a_sink9, "a_sink9");
		Find(a_sink12, "a_sink12");
		Find(s_oneway3, "s_oneway3");
		Find(s_oneway12, "s_oneway12");
		Find(s_bwal610, "s_bwal610");
		Find(tabb42wave, "tabb42wave");
		Find(tabb73wave, "tabb73wave");
		Find(tabb74wave, "tabb74wave");
		Find(tabb84wave, "tabb84wave");
		for (auto index = 0u; index < letter_lights.size(); index++)
			Find(letter_lights[index], std::format("v_lite{}", index + 1).c_str());
		for (auto index = 0u; index < mode_lights.size(); index++)
			Find(mode_lights[index], std::format("v_lite{}", 552 + index).c_str());
		for (auto index = 0u; index < captive_lights.size(); index++)
			Find(captive_lights[index], std::format("v_lite{}", 551 - index).c_str());
		constexpr std::array captives{"a_spot11", "a_spot8", "a_spot9", "a_spot10"};
		for (auto index = 0u; index < captive_targets.size(); index++)
			Find(captive_targets[index], captives[index]);
		Find(a_spot102, "a_spot102");
		Find(map_piece_light_group, "map_piece_light_group");
		Find(tabb02wave, "tabb02wave");
		Find(tabb07wave, "tabb07wave");
		Find(tabb19wave, "tabb19wave");
		Find(tabb22wave, "tabb22wave");
		Find(tabb23wave, "tabb23wave");
		Find(tabb24wave, "tabb24wave");
		Find(tabb29wave, "tabb29wave");
		for (auto index = 0u; index < v_lite524s.size(); index++)
			Find(v_lite524s[index], std::format("v_lite{}", 524 + index).c_str());
		Find(v_lite520, "v_lite520");
		Find(v_lite521, "v_lite521");
		Find(light_sweeps, "light_sweeps");
		Find(a_sink8, "a_sink8");
		Find(v_lite567, "v_lite567");
		Find(s_rexit8, "s_rexit8");
		Find(tabb26wave, "tabb26wave");
		Find(tabb40wave, "tabb40wave");
		Find(tabb57wave, "tabb57wave");
		Find(tabb58wave, "tabb58wave");
		Find(tabb85wave, "tabb85wave");
		Find(v_lite519, "v_lite519");
		Find(left_orbit_light_group, "left_orbit_light_group");
		Find(right_orbit_light_group, "right_orbit_light_group");
		Find(left_orbit_combo_light_group, "left_orbit_combo_light_group");
		Find(right_orbit_combo_light_group, "right_orbit_combo_light_group");
		Find(s_trip3, "s_trip3");
		Find(s_trip8, "s_trip8");
		for (auto index = 0u; index < treasure_lights.size(); index++)
			Find(treasure_lights[index], std::format("v_lite{}", 571 + index).c_str());
		Find(a_sink14, "a_sink14");
		Find(a_sink15, "a_sink15");
		Find(a_sink17, "a_sink17");
		Find(a_roll8, "a_roll8");
		Find(a_roll9, "a_roll9");
		Find(tabb11wave, "tabb11wave");
		Find(tabb59wave, "tabb59wave");
		for (auto index = 0u; index < tavern_lights.size(); index++)
		{
			Find(tavern_lights[index], std::format("v_lite{}", 547 - index).c_str());
			Find(tavern_sinks[index], std::format("a_sink{}", 5 + index).c_str());
			Find(v_lite530s[index], std::format("v_lite{}", 530 + index).c_str());
		}
		Find(a_spot6, "a_spot6");
		Find(keg_light_group, "keg_light_group");
		Find(tabb00wave, "tabb00wave");
		Find(tabb08wave, "tabb08wave");
		Find(tabb65wave, "tabb65wave");
		Find(tabb83wave, "tabb83wave");
		Find(tabb87wave, "tabb87wave");
		auto bank = [](int first, int last, int letter, float blocc, float blocf, TSound* hit, TSound* full)
		{
			DropBank result{{}, letter, blocc, blocf, hit, full};
			for (auto index = first; index <= last; index++)
			{
				TPopupTarget* target;
				Find(target, std::format("a_drop{}", index).c_str());
				result.Targets.push_back(target);
			}
			return result;
		};
		drop_banks = {bank(13, 15, 0, 0.0f, 0.0f, tabb02wave, tabb29wave),
					  bank(5, 9, 3, 0.0f, -1.0f, tabb22wave, tabb24wave),
					  bank(10, 12, 8, -1.0f, 0.0f, tabb07wave, tabb19wave)};

		std::vector<const void*> all{drain,
									 s_launcher1,
									 info_text_box,
									 mode_timer,
									 v_lite504,
									 v_lite505,
									 v_lite544,
									 v_lite569,
									 v_lite570,
									 v_lite606,
									 v_lite607,
									 coin_light_group,
									 special_treasure_light_group,
									 fathom_light_group,
									 a_cannon,
									 a_blocc,
									 a_bloce,
									 a_blocf,
									 a_blocg,
									 a_kick1,
									 a_kick2,
									 a_kout1,
									 a_kout2,
									 a_sink9,
									 a_sink12,
									 s_oneway3,
									 s_oneway12,
									 s_bwal610,
									 tabb42wave,
									 tabb73wave,
									 tabb74wave,
									 tabb84wave};
		all.insert(all.end(), magnet_lights.begin(), magnet_lights.end());
		all.insert(all.end(), v_mlights.begin(), v_mlights.end());
		all.insert(all.end(), flippers.begin(), flippers.end());
		all.insert(all.end(), magnets.begin(), magnets.end());
		all.insert(all.end(), letter_lights.begin(), letter_lights.end());
		all.insert(all.end(), mode_lights.begin(), mode_lights.end());
		all.insert(all.end(), captive_lights.begin(), captive_lights.end());
		all.insert(all.end(), captive_targets.begin(), captive_targets.end());
		all.insert(all.end(), {a_spot102, map_piece_light_group, tabb02wave, tabb07wave, tabb19wave, tabb22wave,
							   tabb23wave, tabb24wave, tabb29wave});
		all.insert(all.end(), v_lite524s.begin(), v_lite524s.end());
		all.insert(all.end(), {v_lite520, v_lite521, light_sweeps, a_sink8, v_lite567, s_rexit8, tabb26wave, tabb40wave,
							   tabb57wave, tabb58wave, tabb85wave});
		all.insert(all.end(), treasure_lights.begin(), treasure_lights.end());
		all.insert(all.end(), {v_lite519, left_orbit_light_group, right_orbit_light_group, left_orbit_combo_light_group,
							   right_orbit_combo_light_group, s_trip3, s_trip8, a_sink14, a_sink15, a_sink17, a_roll8,
							   a_roll9, tabb11wave, tabb59wave});
		all.insert(all.end(), tavern_lights.begin(), tavern_lights.end());
		all.insert(all.end(), tavern_sinks.begin(), tavern_sinks.end());
		all.insert(all.end(), v_lite530s.begin(), v_lite530s.end());
		all.insert(all.end(), {a_spot6, keg_light_group, tabb00wave, tabb08wave, tabb65wave, tabb83wave, tabb87wave});
		for (const auto& dropBank : drop_banks)
			all.insert(all.end(), dropBank.Targets.begin(), dropBank.Targets.end());
		if (std::ranges::find(all, nullptr) != all.end())
		{
			// Not Skulduggery's DAT as PIRATES.EXE knows it; leave the table without rules
			info_text_box = nullptr;
			return;
		}

		for (auto component : table->ComponentList)
		{
			if (!component)
				continue;
			if (component == drain)
				Link(*component, WithHook<DrainControl>);
			else if (component == s_launcher1)
				Link(*component, WithHook<LauncherControl>);
			else if (component == v_lite569)
				Link(*component, WithHook<BallSaveControl>);
			else if (component == s_oneway3)
				Link(*component, WithHook<LaneExitControl>);
			else if (IsDropTarget(component))
				Link(*component, WithHook<DropTargetControl>);
			else if (std::ranges::find(captive_targets, component) != captive_targets.end())
				Link(*component, WithHook<CaptiveTargetControl>);
			else if (component == a_spot102)
				Link(*component, WithHook<ModeTargetControl>);
			else if (component == s_rexit8)
				Link(*component, WithHook<WireRampExitControl>);
			else if (std::ranges::find(TreasureSinks, component,
									   [](const TreasureSink& entry) { return *entry.Sink; }) != TreasureSinks.end())
				Link(*component, WithHook<TreasureSinkControl>);
			else if (component == a_roll8 || component == a_roll9)
				Link(*component, WithHook<BuryRolloverControl>);
			else if (component == a_sink8)
				Link(*component, WithHook<HideawayControl>);
			else if (std::ranges::find(tavern_sinks, component) != tavern_sinks.end())
				Link(*component, WithHook<TavernSinkControl>);
			else if (std::ranges::find(magnets, component) != magnets.end())
				Link(*component, WithHook<MagnetControl>);
			else if (dynamic_cast<TKickout*>(component) || dynamic_cast<TSink*>(component))
				Link(*component, WithHook<StandInControl>);
			else
				Link(*component, WithHook<NoControl>);
		}
	}

	void UnstuckBall(TBall& ball, int)
	{
		TableRules::UnstuckBonusTableBall(*Table, ball);
	}

	void ClearLinks()
	{
		Table = nullptr;
		info_text_box = nullptr;
		Links.clear();
	}
} // namespace

const TableRules control_skulduggery::Rules{
	MakeLinks,
	ClearLinks,
	TableControl,
	UnstuckBall,
	[](int) { /* the cheats come with their own pull request */ },
	[] { /* PIRATES.EXE has no rank to bump */ },
	[] { /* no credits to end without the cheats */ },
	[](TableView&) { /* the HUD's fields come with the close-out */ },
	[](bool) { return false; },
};
