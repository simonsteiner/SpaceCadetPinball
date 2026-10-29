#include "pch.h"
#include "control_dragon.h"

#include "control.h"
#include "midi.h"
#include "pb.h"
#include "TAnimatedBlocker.h"
#include "TBall.h"
#include "TBumper.h"
#include "TDiverter.h"
#include "TDrain.h"
#include "TFlagSpinner.h"
#include "TFlipper.h"
#include "THole.h"
#include "TKickback.h"
#include "TKickout.h"
#include "TLightBargraph.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TLightSweeps.h"
#include "TPinballTable.h"
#include "TPlunger.h"
#include "TPopupTarget.h"
#include "TRamp.h"
#include "TRollover.h"
#include "TSink.h"
#include "TSoloTarget.h"
#include "TSound.h"
#include "TTextBox.h"
#include "TTimer.h"
#include "TWall.h"
#include "translations.h"

// Dragon's Keep's rules, recovered from DRAGON.EXE (Phase F): the serve, drain and game-over flow, with the ball
// save, the end-of-ball bonus and the music; the quest framework and the five quests (Dragon Hoard, Fire Lizards,
// Rescue Maiden, Dragon Pass, Wizard's Fury); the shop; the three multiball modes and Slay Dragon; and the
// components' own scoring. A stuck ball is freed as Space Cadet's rules free one, and DRAGON.EXE's cheats (0x4282c0)
// aren't ported.
//
// DRAGON.EXE's rules run a state machine whose state each player keeps in info_text_box's message field: the
// component handlers and the engine report to one table-wide function (0x42c020), which hands every event to the
// current state's function (0x42c120). States 0 (game over), 1 (new game), 2 (play), 3 (restart quest), 4 to 8 (the
// quests), 10 (launch), 11 (end of ball), 12 (the shop), 13 to 15 (the modes) and 9 (Slay Dragon) are ported, so all
// of them. Addresses below are DRAGON.EXE's. Its message numbers differ from
// this port's above the light codes, so each call uses the port's MessageCode: CADET.EXE's light groups number theirs 4
// above this port's, its targets 4 above, its kickouts, sinks and timers 9 or 10 above; its animated blockers and
// diverters keep CADET.EXE's.
namespace
{
	// Codes DRAGON.EXE's rules functions send only among themselves; SkipBonusText comes from a rule not ported yet
	constexpr auto EnterState = MessageCode::DragonEnterState;
	constexpr auto ShowStateText = MessageCode::DragonShowStateText;
	constexpr auto BallDrained = MessageCode::DragonBallDrained;
	constexpr auto SkipBonusText = MessageCode::DragonSkipBonusText;
	constexpr auto QuestResume = MessageCode::DragonQuestResume; // 0x51: back from Restart Quest
	constexpr auto QuestEnd = MessageCode::DragonQuestEnd;       // 0x52: the quest is over, done or not
	// CADET.EXE's temporary override of a light (0x19) and its end (0x1a), which a light group passes to its lights
	constexpr auto OverrideOff = MessageCode::TLightFtTmpOverrideOff;
	constexpr auto OverrideReset = MessageCode::TLightFtResetOverride;

	// The game's states, as DRAGON.EXE numbers them
	constexpr int GameOverState = 0;
	constexpr int NewGameState = 1;
	constexpr int PlayState = 2;
	constexpr int RestartQuestState = 3;
	constexpr int FireLizardsState = 5;
	constexpr int DragonHoardState = 4;
	constexpr int RescueMaidenState = 6;
	constexpr int DragonPassState = 7;
	constexpr int WizardsFuryState = 8;
	constexpr int SlayDragonState = 9;
	constexpr int LaunchState = 10;
	constexpr int EndOfBallState = 11;
	constexpr int ShopState = 12;
	constexpr int TowerBarrageState = 13;
	constexpr int GaggleBallState = 14;
	constexpr int DragonFlurryState = 15;

	TPinballTable* Table = nullptr;

	TDrain* drain;
	TPlunger* plunger;
	TTextBox* info_text_box;
	TTimer* mode_timer;
	TLight* v_lite28;
	TLight* v_lite31;
	TLight* v_lite37;
	TLight* v_lite38;
	TLight* v_lite39;
	TLight* v_lite40;
	TLight* v_lite48;
	TLight* v_lite49;
	TLight* v_lite50;
	TLight* v_lite113;
	TLight* v_lite114;
	TLight* v_lite115;
	TLightGroup* dragon_egg_light_group;
	TFlipper* a_flip1;
	TFlipper* a_flip2;
	TFlipper* a_flip3;
	TSink* a_sink1;
	TSink* a_sink4;
	TSink* a_sink16;
	TSink* a_sink17;
	TSink* a_sink18;
	TSound* tabc06wave;
	TSound* tabc13wave;
	TSound* tabc44wave;
	TSound* tabc56wave;
	TPinballComponent* s_oneway2;
	// The quest framework's and Fire Lizards' components, and those the serve resets
	THole* s_hole118;
	TKickout* a_kout2;
	TKickout* a_kout3;
	TKickback* a_kick1;
	TSink* a_sink2;
	TSink* a_sink9;
	TLightSweeps* light_sweeps;
	TAnimatedBlocker* a_rocka;
	TAnimatedBlocker* a_rockb;
	TAnimatedBlocker* a_sword;
	TAnimatedBlocker* a_jaw;
	TAnimatedBlocker* a_center_post;
	TAnimatedBlocker* a_bloce;
	TAnimatedBlocker* a_blocf;
	std::array<TDiverter*, 8> diverters; // a_dvtraa, a_dvtrab, a_dvtrba, ... a_dvtrdb
	TSoloTarget* a_spot19;
	TSoloTarget* a_spot26;
	TSoloTarget* a_spot28;
	TPinballComponent* a_bump1;
	TPinballComponent* a_ramp107;
	TPinballComponent* s_bwal170;
	TLightGroup* outlane_kicker_light_group;
	TLightGroup* gargoyle_spot_light_group;
	TLightGroup* spell_spot_light_group;
	TLightGroup* dragon_letter_light_group;
	TLightGroup* dragon_fire_light_group;
	TLightGroup* dragon_mouth_light_group;
	std::array<TLight*, 5> quest_lights;  // v_lite71 to v_lite75, for states 4 to 8 (0x43bb58)
	std::array<TLight*, 6> lizard_lights; // v_lite22 to v_lite27, which together start Dragon Flurry
	TLight* v_lite16;
	TLight* v_lite17;
	TLight* v_lite18;
	TLight* v_lite29;
	TLight* v_lite30;
	TLight* v_lite32;
	TLight* v_lite33;
	TLight* v_lite34;
	TLight* v_lite35;
	TLight* v_lite36;
	TLight* v_lite45;
	TLight* v_lite46;
	TLight* v_lite47;
	TLight* v_lite63;
	TLight* v_lite64;
	TLight* v_lite65;
	TLight* v_lite66;
	TLight* v_lite67;
	TLight* v_lite68;
	TLight* v_lite69;
	TLight* v_lite70;
	TLight* v_lite76;
	TLight* v_lite91;
	TLight* v_lite119;
	TLight* v_lite125;
	TLight* v_lite126;
	TSound* tabc16wave;
	TSound* tabc32wave;
	TSound* tabc34wave;
	TSound* tabc43wave;
	TSound* tabc48wave;
	// Wizard's Fury's, the spells' and the mystery award's
	TLightGroup* well_light_group;
	TSound* tabc21wave;
	TSound* tabc24wave;
	TSound* tabc35wave;
	TSound* tabc41wave;
	TSound* tabc55wave;
	// Dragon Pass's: the gargoyle targets and their lights, the armor kickout and the dragon eggs
	TKickout* a_kout1;
	TSoloTarget* a_spot8;
	TSoloTarget* a_spot10;
	TSoloTarget* a_spot12;
	TSoloTarget* a_spot14;
	TLight* v_lite11;
	TLight* v_lite12;
	TLight* v_lite13;
	TLight* v_lite14;
	TLight* v_lite55;
	TLight* v_lite56;
	TSound* tabc08wave;
	TSound* tabc27wave;
	TSound* tabc40wave;
	TSound* tabc42wave;
	TSound* tabc53wave;
	// Rescue Maiden's: the tower's windows and the prison tripwires
	TLightGroup* prison_window_light_group;
	TLightGroup* watch_window_light_group;
	TLightGroup* gate_window_light_group;
	TLight* v_lite57;
	TPinballComponent* s_trip5;
	TPinballComponent* s_trip6;
	TSound* tabc04wave;
	TSound* tabc50wave;
	TSound* tabc51wave;
	// Dragon Hoard's: the gems' lights, which the gems light up, and what gives each gem
	TLightGroup* gem_light_group;
	TLight* v_lite88; // its message field keeps the gems stolen, one bit each
	TLight* v_lite89;
	TLight* v_lite90;
	TLight* v_lite92;
	TLight* v_lite93;
	TLight* v_lite94;
	TLight* v_lite95;
	TLight* v_lite117;
	TPinballComponent* s_oneway5;
	TPinballComponent* a_accl1;
	TPinballComponent* a_ramp6;
	TPinballComponent* a_ramp103;
	std::array<TSoloTarget*, 5> diamond_targets; // a_spot4 to a_spot7 and a_spot22
	std::array<TSoloTarget*, 3> pearl_targets;   // a_spot1 to a_spot3
	TSound* tabc46wave;
	TSound* tabc47wave;
	// The scoring that belongs to no quest: bumpers, rebounds, rollovers, the mystery, tower lock, sword, quest and
	// lane targets, the ramp exits, the kickback and the armor and spell lights' timers
	TPinballComponent* a_bump3;
	TPinballComponent* a_bump4;
	TPinballComponent* a_spot27;
	TPinballComponent* a_spot30;
	TPinballComponent* a_spot31;
	TPinballComponent* a_roll1;
	TPinballComponent* a_roll2;
	TPinballComponent* a_roll5;
	TPinballComponent* a_roll6;
	TPinballComponent* a_roll7;
	TPinballComponent* s_rexit18;
	TPinballComponent* s_rexit19;
	TPinballComponent* v_wlite96;
	TPinballComponent* v_wlite97;
	TPinballComponent* v_wlite98;
	TPinballComponent* v_wlite99;
	TPinballComponent* a_rebo1;
	TPinballComponent* a_rebo2;
	TPinballComponent* a_spot1;
	TPinballComponent* a_spot2;
	TPinballComponent* a_spot3;
	TPinballComponent* a_spot4;
	TPinballComponent* a_spot5;
	TPinballComponent* a_spot6;
	TPinballComponent* a_spot7;
	TPinballComponent* a_spot22;
	TLight* v_lite3;
	TLight* v_lite4;
	TLight* v_lite5;
	TLight* v_lite6;
	TLight* v_lite7;
	TLight* v_lite8;
	TLight* v_lite9;
	TLight* v_lite10;
	TLight* v_lite19;
	TLight* v_lite20;
	TLight* v_lite21;
	TLight* v_lite120;
	TLight* v_lite121;
	TSound* tabc03wave;
	TSound* tabc05wave;
	TSound* tabc07wave;
	TSound* tabc22wave;
	TSound* tabc39wave;
	TSound* tabc96wave;
	// The tower's other tripwires, Fire Ridge's and the ramps' sounds
	TPinballComponent* s_trip1;
	TPinballComponent* s_trip2;
	TPinballComponent* s_trip3;
	TPinballComponent* s_trip4;
	TPinballComponent* s_trip8;
	TPinballComponent* s_trip9;
	TSound* tabc09wave;
	TSound* tabc18wave;
	TSound* tabc20wave;
	// The tower lock's other sinks
	TSink* a_sink10;
	TSink* a_sink13;
	TSink* a_sink15;
	TSound* tabc10wave;
	TSound* tabc33wave;
	TLight* v_lite78;
	TLight* v_lite77;
	TLight* v_lite79;
	TSound* tabc61wave;
	// Slay Dragon's
	TSink* a_sink19;
	std::array<TLight*, 8> fire_lights; // v_lite113 to v_lite115 and v_lite58 to v_lite62 (0x43bb78)
	TSound* tabc57wave;
	TSound* tabc58wave;
	bool SlayLaunchPending; // 0x434d04: Slay Dragon's light show runs, so the lane exit doesn't start its time
	bool RampCrossed;       // 0x434d18: the ball is on a_ramp107, the dragon's ramp, so the time waits for it
	int SlaySeconds;        // 0x434cf8: Slay Dragon's time
	int FireDirection;      // 0x439b0c: which way the fire lights run, 1 up, -1 down
	int ShopItem;        // 0x434d1c: what the shop offers, its price in dragon eggs; 0 for nothing
	int ShopSpell;       // 0x434d28: the spell it offers
	int MaidenLockLight; // 0x434d14: how v_lite48 was when Rescue Maiden started, 0 off, 1 on, 2 flashing

	template <typename T> void Find(T*& component, const char* name)
	{
		component = dynamic_cast<T*>(Table->find_component(name));
	}

	// info_text_box's message field, which the engine keeps per player on the bonus tables, as CADET.EXE does
	int& GameState()
	{
		return info_text_box->MessageField;
	}

	// drain's message field, the bonus a lit Bonus Hold (v_lite31) carries to the next ball, also per player
	int& BonusHold()
	{
		return drain->MessageField;
	}

	int BallSaveStep;              // 0x434d2c: 3 lit until the ball leaves the lane, 2 lit 10 s, 1 flashing 5 s, 0 off
	int DrainStep;                 // 0x43bb6c: what the drain's timer does next
	int LastBallRelease;           // 0x439b1c: the last ball released the tower lock's balls, 2 if it was tilted
	bool BallDrainedFlag;          // 0x439b14
	// 0x439b10: set from the launch until play starts, and while a ball waits for a quest to start; play keeps the
	// launch's text, and KickOrShop lets the ball out of s_hole118 instead of the given sink
	bool BallHeld;
	bool SlayDragonFlag;           // 0x439b18, set by Slay Dragon
	bool TallyQuiet;               // 0x434d0c: count the bonus without showing it
	int TallyStep;                 // 0x434d10
	int TallyTotal;                // 0x434d00
	int TargetHits;                // 0x434d08, Target Bonus
	int SpellCount;                // 0x434d24, Spell Bonus: the spells cast this ball
	TPinballComponent* LastScorer; // 0x434d20

	void TableHook(MessageCode code, TPinballComponent* caller);

	void PlayTrack(MidiTracks track)
	{
		// DRAGON.EXE starts a track only if another one plays
		if (midi::get_active_track() != track)
			midi::play_track(track, true);
	}

	void AddJackpot(int score) // 0x4238a0
	{
		Table->JackpotScore = std::min(Table->JackpotScore + score, 10000000);
	}

	// 0x4230f0; DRAGON.EXE first tells the previous scorer's rules that it no longer is (0x4d), which no ported
	// handler takes yet
	int AddScore(TPinballComponent* scorer, int score)
	{
		LastScorer = scorer;
		return Table->AddScore(score);
	}

	// 0x4230a0: the scorer's own score at <index>
	int ScoreBy(TPinballComponent* scorer, int index)
	{
		return AddScore(scorer, scorer->get_scoring(index));
	}

	// 0x423130: the score without multiplier, bonus or first jackpot, as Space Cadet's SpecialAddScore; the second
	// jackpot still counts it
	int AddScoreUnmultiplied(TPinballComponent* scorer, int score)
	{
		LastScorer = scorer;
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

	void StateControl(int state, MessageCode code, TPinballComponent* caller);

	// The tower lock's lights (0x423b80): their override ends at the next LockLights
	void LockReset()
	{
		for (auto light : {v_lite48, v_lite49, v_lite50})
			light->Message(OverrideOff, 0.0f);
		a_sink18->ActiveFlag = 0;
		a_sink17->ActiveFlag = 0;
		for (auto light : {v_lite48, v_lite49, v_lite50})
			light->MessageField = 0;
	}

	// 0x423be0: the first flashing lock light opens its sink; with all three lit, Tower Barrage starts
	void LockLights()
	{
		for (auto light : {v_lite48, v_lite49, v_lite50})
			light->Message(OverrideReset, 0.0f);
		// DRAGON.EXE reads the lights' flashing flag (field 0x46)
		int open = -1;
		if (v_lite48->FlasherOnFlag)
			open = 0;
		else if (v_lite49->FlasherOnFlag)
			open = 1;
		else if (v_lite50->FlasherOnFlag)
			open = 2;
		a_sink18->ActiveFlag = open == 0;
		a_sink17->ActiveFlag = open == 1;
		v_lite48->MessageField = open == 0;
		v_lite49->MessageField = open == 1;
		v_lite50->MessageField = open == 2;
		if (open < 0 && v_lite48->light_on() && v_lite49->light_on() && v_lite50->light_on())
			SetState(TowerBarrageState);
	}

	// 0x428580: a quest's lights give way to its own
	void QuestEndReset()
	{
		LockReset();
		dragon_letter_light_group->Message(OverrideOff, 0.0f);
	}

	// 0x4285a0: a quest ends; the lights it took over come back
	void QuestLeave()
	{
		info_text_box->Clear();
		v_lite63->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite64->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		dragon_letter_light_group->Message(OverrideReset, 0.0f);
		LockLights();
	}

	// 0x4238d0: the sword, the targets a_spot19, a_spot26 and a_spot28 and the lights v_lite16 to v_lite18 for
	// <level> 0 to 3, or all down for -1
	void SetLevelLights(int level)
	{
		using enum MessageCode;
		constexpr float Forever = -1.0f;
		switch (level)
		{
		case 0:
		case 1:
			tabc13wave->Play(nullptr, "SetLevelLights");
			a_sword->Message(TAnimatedBlockerAnimateTo, 0.0f);
			a_spot19->Message(TSoloTargetDisable, 0.0f);
			a_spot26->Message(TSoloTargetDisable, 0.0f);
			a_spot28->Message(TSoloTargetEnable, 0.0f);
			if (level == 0)
			{
				v_lite16->Message(TLightFlasherStartTimed, Forever);
				v_lite17->Message(TLightFlasherStartTimed, Forever);
			}
			else
			{
				v_lite16->Message(TLightResetAndTurnOn, 0.0f);
				v_lite17->Message(TLightResetAndTurnOn, 0.0f);
			}
			v_lite18->Message(TLightFlasherStartTimed, Forever);
			break;
		case 2:
			tabc13wave->Play(nullptr, "SetLevelLights");
			a_sword->Message(TAnimatedBlockerAnimateTo, 1.0f);
			a_spot19->Message(TSoloTargetDisable, 0.0f);
			a_spot26->Message(TSoloTargetEnable, 0.0f);
			a_spot28->Message(TSoloTargetDisable, 0.0f);
			v_lite16->Message(TLightResetAndTurnOn, 0.0f);
			v_lite17->Message(TLightFlasherStartTimed, Forever);
			v_lite18->Message(TLightResetAndTurnOff, 0.0f);
			break;
		case 3:
			a_sword->Message(TAnimatedBlockerAnimateOn, 0.0f);
			a_spot19->Message(TSoloTargetEnable, 0.0f);
			a_spot26->Message(TSoloTargetDisable, 0.0f);
			a_spot28->Message(TSoloTargetDisable, 0.0f);
			v_lite16->Message(TLightFlasherStartTimed, Forever);
			v_lite17->Message(TLightResetAndTurnOff, 0.0f);
			v_lite18->Message(TLightResetAndTurnOff, 0.0f);
			break;
		case -1:
			a_sword->Message(TAnimatedBlockerAnimateTo, 0.0f);
			for (auto target : {a_spot19, a_spot26, a_spot28})
				target->Message(TSoloTargetDisable, 0.0f);
			for (auto light : {v_lite16, v_lite17, v_lite18})
				light->Message(TLightResetAndTurnOff, Forever);
			break;
		default:
			break;
		}
	}

	// 0x4241c0: let the ball out of <sink>, or out of s_hole118 if a ball waits there
	void KickOrShop(TSink* sink, float time)
	{
		if (BallHeld)
		{
			s_hole118->MessageField = 2;
			s_hole118->Message(MessageCode::THoleRelease, 0.0f);
			return;
		}
		sink->Message(MessageCode::TSinkResetTimer, time);
	}

	// Each quest's state, by its index in quest_lights; a_rockb's message field keeps the picked one's index and
	// a_kout2's its state, 0 when none is picked
	int QuestState(int index)
	{
		return index + 4;
	}

	TLight* PickedQuestLight()
	{
		auto index = a_rockb->MessageField;
		return index >= 0 && index < static_cast<int>(quest_lights.size()) ? quest_lights[index] : nullptr;
	}

	void ClearPickedQuest() // 0x423e50
	{
		if (!a_kout2->MessageField)
			return;
		if (auto light = PickedQuestLight())
			light->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		a_kout2->MessageField = 0;
	}

	// 0x423e90: a random quest whose light is off flashes; with none left, Slay Dragon starts. Returns the state
	int PickQuest()
	{
		if (BallDrainedFlag)
			return -1;
		ClearPickedQuest();
		std::array<int, 5> open{};
		int count = 0;
		for (int index = 0; index < static_cast<int>(quest_lights.size()); index++)
		{
			if (!quest_lights[index]->light_on())
				open[count++] = index;
		}
		if (!count)
		{
			SetLevelLights(-1);
			v_lite76->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
			SetState(SlayDragonState);
			return SlayDragonState;
		}
		auto index = open[std::min(static_cast<int>(RandFloat() * count), count - 1)];
		a_rockb->MessageField = index;
		a_kout2->MessageField = QuestState(index);
		quest_lights[index]->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		return a_kout2->MessageField;
	}

	// 0x424000: the quest is over and its light stays on; the next one is picked
	void QuestDone()
	{
		if (a_kout2->MessageField)
		{
			AddJackpot(500000);
			v_lite28->MessageField++;
			if (auto light = PickedQuestLight())
				light->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
			a_kout2->MessageField = 0;
		}
		PickQuest();
	}

	bool QuestPorted(int state)
	{
		return state >= DragonHoardState && state <= WizardsFuryState;
	}

	// 0x424210: the picked quest starts. A quest whose state isn't ported yet doesn't, and its light keeps flashing
	bool StartQuest()
	{
		auto state = a_kout2->MessageField;
		if (!state || !QuestPorted(state))
			return false;
		GameState() = state;
		v_lite46->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		a_rockb->Message(MessageCode::TAnimatedBlockerAnimateOn, 2.0f);
		TableHook(EnterState, nullptr);
		return true;
	}

	// A picture in the text box's second font, whose characters DRAGON.EXE's texts name (0x439a58 on)
	void ShowPicture(const std::string& glyphs, float time)
	{
		info_text_box->Display(glyphs.c_str(), time, false, 1);
	}

	// 0x424490: the jackpot, and its picture with <show>
	void AwardJackpot(bool show = true)
	{
		tabc43wave->Play(nullptr, "AwardJackpot");
		AddScore(nullptr, Table->JackpotScore);
		if (show)
			ShowPicture("\"", 2.0f);
	}

	// 0x4231e0: an extra ball, with its text for <textTime> seconds
	void AwardExtraBall(float textTime)
	{
		Table->ExtraBalls = 1;
		tabc21wave->Play(nullptr, "AwardExtraBall");
		if (textTime >= 0.0078125f)
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_ExtraBall), textTime);
	}

	// 0x423240: full armor, the kickback and the outlane kickers lit, with its picture and sound
	void AwardMaxArmor()
	{
		a_kick1->ActiveFlag = 1;
		v_lite37->MessageField = 0;
		v_lite37->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		outlane_kicker_light_group->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		ShowPicture("+", 2.0f);
		tabc41wave->Play(nullptr, "AwardMaxArmor");
	}

	void CenterPostControl(MessageCode code, TPinballComponent*);
	void BloceControl(MessageCode code, TPinballComponent*);

	// 0x4234b0: one armor piece whose light (v_lite37 to v_lite39) is off, at random: the kickback (25,000), the
	// center post (50,000) or a_bloce (100,000), each with its picture
	void AwardArmor()
	{
		std::array<int, 3> open{};
		int count = 0;
		for (auto [piece, light] : {std::pair{0, v_lite37}, {1, v_lite38}, {2, v_lite39}})
		{
			if (!light->light_on())
				open[count++] = piece;
		}
		if (!count)
			return;
		switch (open[std::min(static_cast<int>(RandFloat() * count), count - 1)])
		{
		case 0:
			AwardMaxArmor();
			AddScore(nullptr, 25000);
			break;
		case 1:
			tabc40wave->Play(nullptr, "AwardArmor");
			CenterPostControl(MessageCode::TAnimatedBlockerAnimateOn, a_center_post);
			ShowPicture("*", 2.0f);
			tabc40wave->Play(nullptr, "AwardArmor");
			AddScore(nullptr, 50000);
			break;
		default:
			tabc42wave->Play(nullptr, "AwardArmor");
			BloceControl(MessageCode::TAnimatedBlockerAnimateOn, a_bloce);
			ShowPicture(")", 2.0f);
			tabc42wave->Play(nullptr, "AwardArmor");
			AddScore(nullptr, 100000);
			break;
		}
	}

	// 0x4232b0: <count> more dragon eggs, a picture of the eggs lit, then 100,000 for each
	void AwardDragonEggs(int count)
	{
		for (; count > 0; count--)
			dragon_egg_light_group->Message(MessageCode::TLightGroupResetAndTurnOn, 1.0f);
		tabc27wave->Play(nullptr, "AwardDragonEggs");
		auto eggs = dragon_egg_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
		ShowPicture(std::string(std::clamp(eggs, 0, 8), '#'), 2.0f);
		AddScore(nullptr, eggs * 100000);
	}

	// 0x423600: a random spell whose light is off (v_lite28, v_lite29, v_lite31, v_lite32), or -1
	int PickSpell()
	{
		std::array<int, 4> open{};
		int count = 0;
		for (auto [spell, light] : {std::pair{0, v_lite28}, {1, v_lite29}, {2, v_lite31}, {3, v_lite32}})
		{
			if (!light->light_on())
				open[count++] = spell;
		}
		if (!count)
			return -1;
		return open[std::min(static_cast<int>(RandFloat() * count), count - 1)];
	}

	void BlocfControl(MessageCode code, TPinballComponent*);

	// 0x4236b0: a spell, counted for the Spell Bonus, with its picture
	void CastSpell(int spell)
	{
		using enum MessageCode;
		SpellCount++;
		switch (spell)
		{
		case 0:
			v_lite28->Message(TLightTurnOnTimed, 30.0f);
			break;
		case 1:
			v_lite29->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			v_lite30->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			a_kout3->ActiveFlag = 1;
			break;
		case 2:
			v_lite31->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			break;
		default:
			BlocfControl(TAnimatedBlockerAnimateOff, a_blocf);
			break;
		}
		AddScore(nullptr, 120000);
		tabc35wave->Play(nullptr, "CastSpell");
		ShowPicture("$", 2.0f);
	}

	void CastRandomSpell() // 0x4237f0
	{
		auto spell = PickSpell();
		if (spell != -1)
			CastSpell(spell);
	}

	// 0x4244e0: all six fire lizard lights start Dragon Flurry
	bool DragonFlurryReady()
	{
		for (auto light : lizard_lights)
		{
			if (!light->light_on())
				return false;
		}
		SetState(DragonFlurryState);
		return true;
	}

	// 0x424050: the dragon's mouth closes; with <keepOverride>, its lights' override ends
	void SlayDragonReset(bool keepOverride)
	{
		using enum MessageCode;
		SlayDragonFlag = false;
		auto code = keepOverride ? OverrideReset : OverrideOff;
		v_lite119->Message(code, 0.0f);
		dragon_mouth_light_group->Message(code, 0.0f);
		dragon_fire_light_group->Message(code, 0.0f);
		dragon_mouth_light_group->Message(TLightResetAndTurnOff, 0.0f);
		dragon_fire_light_group->Message(TLightResetAndTurnOff, 0.0f);
		dragon_fire_light_group->Message(TLightGroupRestartNotifyTimer, -1.0f);
		for (auto component :
			 std::initializer_list<TPinballComponent*>{v_lite113, v_lite114, v_lite115, s_bwal170, a_ramp107})
			component->ActiveFlag = 0;
		a_jaw->Message(TAnimatedBlockerAnimateTo, 0.0f);
	}

	// a_center_post (0x425910) and a_bloce (0x425a10): up for InitialDuration with <light> on, then
	// ExtendedDuration flashing; the rules raise them with AnimateOn and drop them with AnimateOff
	void ArmorBlockerControl(MessageCode code, TAnimatedBlocker* blocker, TLight* light)
	{
		using enum MessageCode;
		switch (code)
		{
		case TAnimatedBlockerAnimateOff:
			blocker->MessageField = 0;
			blocker->Message(TAnimatedBlockerAnimateOff, 0.0f);
			light->Message(TLightResetAndTurnOff, 0.0f);
			break;
		case TAnimatedBlockerAnimateOn:
			blocker->MessageField = 1;
			blocker->Message(TAnimatedBlockerAnimateOn, 0.0f);
			blocker->Message(TAnimatedBlockerRestartTimeout, static_cast<float>(blocker->InitialDuration));
			light->Message(TLightFlasherStartTimedThenStayOn, 2.0f);
			break;
		case ControlTimerExpired:
			if (blocker->MessageField == 1)
			{
				blocker->MessageField = 2;
				auto time = static_cast<float>(blocker->ExtendedDuration);
				blocker->Message(TAnimatedBlockerRestartTimeout, time);
				light->Message(TLightFlasherStartTimedThenStayOff, time);
				break;
			}
			blocker->MessageField = 0;
			blocker->Message(TAnimatedBlockerAnimateOff, 0.0f);
			break;
		default:
			break;
		}
	}

	void CenterPostControl(MessageCode code, TPinballComponent*)
	{
		ArmorBlockerControl(code, a_center_post, v_lite38);
	}

	void BloceControl(MessageCode code, TPinballComponent*)
	{
		ArmorBlockerControl(code, a_bloce, v_lite39);
	}

	// a_blocf, 0x425b10: the other way round, down for InitialDuration with v_lite32 on, then up again
	void BlocfControl(MessageCode code, TPinballComponent*)
	{
		using enum MessageCode;
		switch (code)
		{
		case TAnimatedBlockerAnimateOff:
			a_blocf->MessageField = 1;
			a_blocf->Message(TAnimatedBlockerAnimateOff, 0.0f);
			a_blocf->Message(TAnimatedBlockerRestartTimeout, static_cast<float>(a_blocf->InitialDuration));
			v_lite32->Message(TLightTurnOnTimed, static_cast<float>(a_blocf->InitialDuration));
			break;
		case TAnimatedBlockerAnimateOn:
			a_blocf->Message(TAnimatedBlockerRestartTimeout, -1.0f);
			v_lite32->Message(TLightResetAndTurnOff, 0.0f);
			a_blocf->MessageField = 0;
			a_blocf->Message(TAnimatedBlockerAnimateOn, 0.0f);
			break;
		case ControlTimerExpired:
			if (a_blocf->MessageField == 1)
			{
				a_blocf->MessageField = 2;
				auto time = static_cast<float>(a_blocf->ExtendedDuration);
				a_blocf->Message(TAnimatedBlockerRestartTimeout, time);
				v_lite32->Message(TLightFlasherStartTimed, time);
				break;
			}
			a_blocf->MessageField = 0;
			a_blocf->Message(TAnimatedBlockerAnimateOn, 0.0f);
			break;
		default:
			break;
		}
	}

	// 0x423780: the serve's and the tilt's resets
	void TiltResets()
	{
		using enum MessageCode;
		for (auto light : {v_lite28, v_lite29, v_lite30})
			light->Message(TLightResetAndTurnOff, 0.0f);
		a_kout3->Message(Reset, 0.0f);
		v_lite31->Message(TLightResetAndTurnOff, 0.0f);
		BlocfControl(TAnimatedBlockerAnimateOn, a_blocf);
	}

	void ShowBonus(Msg text, int score)
	{
		std::array<char, 64> buffer{};
		snprintf(buffer.data(), buffer.size(), pb::get_rc_string(text), static_cast<long>(score));
		info_text_box->Display(buffer.data(), 2.0f);
	}

	// State 0, 0x42bf60
	void GameOverStateControl(MessageCode code, TPinballComponent*)
	{
		if (code != EnterState)
			return;
		PlayTrack(MidiTracks::Track1);
		Table->Message(MessageCode::GameOver, 0.0f);
		if (pb::chk_highscore())
		{
			tabc56wave->Play(nullptr, "GameOverStateControl");
			Table->LightGroup->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 3.0f);
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_HighScore), -1.0f);
		}
	}

	// State 2, 0x428800
	void PlayStateControl(MessageCode code, TPinballComponent*)
	{
		if (code == EnterState)
		{
			if (!BallHeld)
				info_text_box->Clear();
			BallHeld = false;
			PlayTrack(MidiTracks::Track7);
		}
		else if (code == ShowStateText)
		{
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_HitFireLizards), -1.0f);
		}
	}

	// The launch ends at the first object of these types the ball hits (DRAGON.EXE: 1000, 1005, 1006, 1012, 1014,
	// 1015, 1017, 1018, 1019, 1020, 1021, 1024, 1030); TLightRollover and TTripwire are TRollovers. The types are
	// what each class reports, so 1010's walls count as 1000 and 1029's kickouts as 1012, as in DRAGON.EXE
	bool EndsLaunch(TPinballComponent* caller)
	{
		return dynamic_cast<TWall*>(caller) || dynamic_cast<TBumper*>(caller) || dynamic_cast<TPopupTarget*>(caller) ||
			   dynamic_cast<TKickout*>(caller) || dynamic_cast<TKickback*>(caller) ||
			   dynamic_cast<TRollover*>(caller) || dynamic_cast<TSink*>(caller) ||
			   dynamic_cast<TFlagSpinner*>(caller) || dynamic_cast<TSoloTarget*>(caller) ||
			   dynamic_cast<TRamp*>(caller) || dynamic_cast<TLightBargraph*>(caller);
	}

	// State 10, 0x4285f0: the ball was served; the first thing it hits ends the launch, with a skill shot
	void LaunchStateControl(MessageCode code, TPinballComponent* caller)
	{
		if (code == EnterState || code == ShowStateText)
		{
			if (code == EnterState)
				BallHeld = true;
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_LaunchBall), -1.0f);
			return;
		}
		if (code == MessageCode::ControlBallCaptured && caller == s_hole118)
		{
			// A stand-in: DRAGON.EXE always has a quest to start here, but one not ported yet doesn't start
			if (!StartQuest())
				KickOrShop(a_sink1, 0.0f);
			return;
		}
		if (code != MessageCode::ControlCollision || !caller)
			return;

		if (caller != drain)
		{
			if (!EndsLaunch(caller))
				return;
			if (dynamic_cast<TSoloTarget*>(caller))
			{
				// a_spot4 to a_spot7 select a quest and a_spot22 a mode, without a skill shot; ported with them
				const std::string_view name = caller->GroupName;
				if (name == "a_spot4" || name == "a_spot5" || name == "a_spot6" || name == "a_spot7" ||
					name == "a_spot22")
				{
					SetState(PlayState);
					return;
				}
			}
			if (!Table->TiltLockFlag)
				AddScore(nullptr, 15000);
		}
		SetState(PlayState);
		TableHook(ShowStateText, nullptr);
	}

	// The quest's text with the seconds mode_timer has left, if it runs
	void ShowTimeLeft(Msg text)
	{
		auto msLeft = mode_timer->Message(MessageCode::TTimerGetTimeLeft, 0.0f);
		if (!msLeft)
			return;
		std::array<char, 64> buffer{};
		snprintf(buffer.data(), buffer.size(), pb::get_rc_string(text), static_cast<long>(msLeft / 1000));
		info_text_box->Display(buffer.data(), 0.5f);
	}

	// State 3, 0x428870: the quest's time ran out; a fire lizard sink within 10 s takes it up again (QuestResume),
	// else it ends. mode_timer's message field keeps the quest's state
	void RestartQuestStateControl(MessageCode code, const TPinballComponent* caller)
	{
		using enum MessageCode;
		switch (code)
		{
		case ControlTimerExpired:
			if (caller != mode_timer)
				return;
			v_lite63->Message(TLightResetAndTurnOff, 0.0f);
			v_lite64->Message(TLightResetAndTurnOff, 0.0f);
			StateControl(mode_timer->MessageField, QuestEnd, nullptr);
			mode_timer->MessageField = -1;
			if (GameState() == RestartQuestState)
				SetState(PlayState);
			break;
		case EnterState:
			v_lite63->Message(TLightFlasherStartTimed, -1.0f);
			v_lite64->Message(TLightFlasherStartTimed, -1.0f);
			mode_timer->Message(TTimerResetTimer, 10.0f);
			[[fallthrough]];
		case ShowStateText:
			ShowTimeLeft(Msg::Dragon_RestartQuest);
			break;
		case BallDrained:
			StateControl(mode_timer->MessageField, QuestEnd, nullptr);
			mode_timer->Message(TTimerResetTimer, -1.0f);
			mode_timer->MessageField = -1;
			GameState() = PlayState;
			break;
		default:
			break;
		}
	}

	// State 5, 0x4291c0: Fire Lizards. Each fire lizard sink slays a lizard (FireLizardSlain) until the time runs out
	void FireLizardsStateControl(MessageCode code, const TPinballComponent* caller)
	{
		using enum MessageCode;
		switch (code)
		{
		case ControlTimerExpired:
			if (caller == mode_timer)
			{
				mode_timer->MessageField = GameState();
				for (auto light : {v_lite66, v_lite65, v_lite91})
					light->Message(OverrideOff, 0.0f);
				SetState(RestartQuestState);
			}
			else if (caller == light_sweeps)
			{
				// The quest's light show is over: the ball comes out and the time starts
				QuestEndReset();
				KickOrShop(a_sink1, 0.0f);
				mode_timer->Message(TTimerResetTimer, 31.69f);
				TableHook(ShowStateText, nullptr);
			}
			break;
		case EnterState:
			light_sweeps->Message(TLightSweepsStart, 1.0f);
			v_lite91->MessageField = 0;
			a_sink2->MessageField = 0;
			AddScore(nullptr, 100000);
			info_text_box->Clear();
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_PutBallInFireLizardSinks), -1.0f);
			v_lite63->Message(TLightFlasherStartTimed, -1.0f);
			v_lite64->Message(TLightFlasherStartTimed, -1.0f);
			tabc48wave->Play(nullptr, "FireLizardsStateControl");
			PlayTrack(MidiTracks::Track1);
			break;
		case QuestResume:
			v_lite63->Message(TLightFlasherStartTimed, -1.0f);
			v_lite64->Message(TLightFlasherStartTimed, -1.0f);
			tabc48wave->Play(nullptr, "FireLizardsStateControl");
			for (auto light : {v_lite66, v_lite65, v_lite91})
				light->Message(OverrideReset, 0.0f);
			mode_timer->Message(TTimerResetTimer, 15.0f);
			if (info_text_box->CurrentMessage)
				break;
			[[fallthrough]];
		case ShowStateText:
			ShowTimeLeft(Msg::Dragon_FireLizards);
			break;
		case BallDrained:
			GameState() = PlayState;
			mode_timer->Message(TTimerResetTimer, -1.0f);
			[[fallthrough]];
		case QuestEnd:
			QuestLeave();
			v_lite63->Message(TLightResetAndTurnOff, 0.0f);
			v_lite64->Message(TLightResetAndTurnOff, 0.0f);
			for (auto light : {v_lite66, v_lite65, v_lite91})
			{
				light->Message(TLightResetAndTurnOff, 0.0f);
				light->Message(OverrideReset, 0.0f);
			}
			QuestDone();
			break;
		default:
			break;
		}
	}

	// 0x428a20: one more gem; with all nine, an extra ball
	void StealGem(int gem)
	{
		v_lite88->MessageField |= gem;
		if (v_lite88->MessageField == 0x1ff)
		{
			gem_light_group->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
			AwardExtraBall(2.0f);
			return;
		}
		tabc47wave->Play(nullptr, "StealGem");
	}

	// State 4, 0x428a80: Dragon Hoard. Nine shots each steal a gem, with its light and its text
	void DragonHoardStateControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		auto steal = [caller](TLight* light, int score, Msg text, int gem)
		{
			if (light)
				light->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			auto added = AddScore(caller, score);
			std::array<char, 64> buffer{};
			snprintf(buffer.data(), buffer.size(), pb::get_rc_string(text), static_cast<long>(added));
			info_text_box->Display(buffer.data(), 2.0f);
			StealGem(gem);
		};
		switch (code)
		{
		case ControlTimerExpired:
			if (caller == mode_timer)
			{
				mode_timer->MessageField = GameState();
				gem_light_group->Message(OverrideOff, 0.0f);
				SetState(RestartQuestState);
			}
			else if (caller == light_sweeps)
			{
				QuestEndReset();
				KickOrShop(a_sink1, 0.0f);
				mode_timer->Message(TTimerResetTimer, 27.73f);
				TableHook(ShowStateText, nullptr);
			}
			break;
		case ControlCollision:
			if (dynamic_cast<TBumper*>(caller))
				steal(v_lite88, 10000, Msg::Dragon_Gold, 1);
			else if (caller == s_oneway5)
			{
				v_lite95->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
				AwardJackpot();
				StealGem(0x40);
			}
			else if (caller == a_sink1 || caller == a_sink2)
				steal(v_lite91, 100000, Msg::Dragon_Ruby, 0x20);
			else if (std::ranges::find(diamond_targets, caller) != diamond_targets.end())
				steal(v_lite89, 50000, Msg::Dragon_Diamond, 2);
			else if (std::ranges::find(pearl_targets, caller) != pearl_targets.end())
				steal(v_lite90, 60000, Msg::Dragon_Pearl, 4);
			else if (caller == a_spot10 || caller == a_spot8 || caller == a_spot14 || caller == a_spot12)
				steal(v_lite93, 70000, Msg::Dragon_Sapphire, 0x10);
			else if (caller == a_accl1)
				steal(v_lite117, 100000, Msg::Dragon_Opal, 0x100);
			break;
		case ControlRampExitToLayer2: // CADET.EXE's 0x56, the ramps' second wall
			if (caller == a_ramp103)
				steal(v_lite94, 250000, Msg::Dragon_Amethyst, 0x80);
			else if (caller == a_ramp6)
				steal(v_lite92, 200000, Msg::Dragon_Emerald, 8);
			break;
		case EnterState:
			light_sweeps->Message(TLightSweepsStart, 0.0f);
			gem_light_group->Message(TLightFlasherStartTimed, -1.0f);
			diverters[2]->Message(TDiverterEnable, 0.0f);
			diverters[3]->Message(TDiverterDisable, 0.0f);
			diverters[4]->Message(TDiverterEnable, 0.0f);
			diverters[5]->Message(TDiverterDisable, 0.0f);
			AddScore(nullptr, 100000);
			info_text_box->Clear();
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_StealGems), -1.0f);
			v_lite88->MessageField = 0;
			PlayTrack(MidiTracks::Track2);
			tabc46wave->Play(nullptr, "DragonHoardStateControl");
			break;
		case QuestResume:
			tabc46wave->Play(nullptr, "DragonHoardStateControl");
			gem_light_group->Message(OverrideReset, 0.0f);
			mode_timer->Message(TTimerResetTimer, 15.0f);
			if (info_text_box->CurrentMessage)
				break;
			[[fallthrough]];
		case ShowStateText:
			ShowTimeLeft(Msg::Dragon_DragonHoard);
			break;
		case BallDrained:
			GameState() = PlayState;
			mode_timer->Message(TTimerResetTimer, -1.0f);
			[[fallthrough]];
		case QuestEnd:
			QuestLeave();
			gem_light_group->Message(TLightResetAndTurnOff, 0.0f);
			gem_light_group->Message(OverrideReset, 0.0f);
			QuestDone();
			break;
		default:
			break;
		}
	}

	// The light at <index> of a light group, or nullptr
	TLight* GroupLight(TLightGroup* group, int index)
	{
		return index >= 0 && index < static_cast<int>(group->List.size()) ? group->List[index] : nullptr;
	}

	// State 6, 0x429510: Rescue Maiden. The prison tripwires light the tower's windows; with all four lit, the ball
	// in the prison tower (a_sink18) rescues the maiden. A highlight runs round the windows every 0.2 s
	void RescueMaidenStateControl(MessageCode code, const TPinballComponent* caller)
	{
		using enum MessageCode;
		auto windows = prison_window_light_group;
		switch (code)
		{
		case ControlTimerExpired:
			if (caller == mode_timer)
			{
				windows->Message(OverrideOff, 0.0f);
				windows->Message(TLightGroupRestartNotifyTimer, -1.0f);
				mode_timer->MessageField = GameState();
				SetState(RestartQuestState);
			}
			else if (caller == light_sweeps)
			{
				QuestEndReset();
				v_lite48->Message(TLightFlasherStartTimed, -1.0f);
				v_lite48->Message(OverrideReset, 0.0f);
				watch_window_light_group->Message(TLightResetAndTurnOff, 0.0f);
				gate_window_light_group->Message(TLightResetAndTurnOff, 0.0f);
				windows->Message(TLightResetAndTurnOff, 0.0f);
				windows->Message(TLightGroupRestartNotifyTimer, 0.2f);
				KickOrShop(a_sink18, 0.0f);
				mode_timer->Message(TTimerResetTimer, 30.45f);
				TableHook(ShowStateText, nullptr);
			}
			return;
		case ControlNotifyTimerExpired:
			if (caller != windows)
				return;
			{
				windows->Message(TLightGroupRestartNotifyTimer, 0.2f);
				auto index = windows->MessageField;
				if (auto light = GroupLight(windows, index))
					light->Message(OverrideReset, 0.0f);
				index = index < 3 ? index + 1 : 0;
				windows->MessageField = index;
				if (auto light = GroupLight(windows, index))
					light->Message(light->light_on() ? OverrideOff : MessageCode::TLightFtTmpOverrideOn, 0.0f);
			}
			return;
		case ControlCollision:
			if (caller != a_sink18 || windows->Message(TLightGroupGetOnCount, 0.0f) != 4)
				return;
			mode_timer->Message(TTimerResetTimer, -1.0f);
			windows->Message(TLightGroupRestartNotifyTimer, -1.0f);
			break;
		case EnterState:
			if (v_lite48->FlasherOnFlag)
				MaidenLockLight = 2;
			else if (v_lite48->LightOnFlag || v_lite48->ToggledOnFlag)
				MaidenLockLight = 1;
			else
				MaidenLockLight = 0;
			light_sweeps->Message(TLightSweepsStart, 2.0f);
			v_lite47->Message(TLightResetAndTurnOn, 0.0f);
			diverters[0]->Message(TDiverterDisable, 0.0f);
			diverters[1]->Message(TDiverterEnable, 0.0f);
			diverters[2]->Message(TDiverterEnable, 0.0f);
			diverters[3]->Message(TDiverterDisable, 0.0f);
			diverters[4]->Message(TDiverterDisable, 0.0f);
			diverters[5]->Message(TDiverterEnable, 0.0f);
			v_lite125->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			v_lite126->Message(TLightResetAndTurnOff, 0.0f);
			diverters[6]->Message(TDiverterDisable, 0.0f);
			diverters[7]->Message(TDiverterEnable, 0.0f);
			AddScore(nullptr, 100000);
			info_text_box->Clear();
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_PutBallInPrisonTower), -1.0f);
			tabc50wave->Play(nullptr, "RescueMaidenStateControl");
			PlayTrack(MidiTracks::Track4);
			v_lite48->Message(TLightFlasherStartTimed, -1.0f);
			return;
		case QuestResume:
			tabc50wave->Play(nullptr, "RescueMaidenStateControl");
			windows->Message(OverrideReset, 0.0f);
			mode_timer->Message(TTimerResetTimer, 15.0f);
			windows->Message(TLightGroupRestartNotifyTimer, 0.2f);
			if (info_text_box->CurrentMessage)
				return;
			[[fallthrough]];
		case ShowStateText:
			ShowTimeLeft(Msg::Dragon_RescueMaiden);
			return;
		case BallDrained:
			GameState() = PlayState;
			mode_timer->Message(TTimerResetTimer, -1.0f);
			break;
		case QuestEnd:
			break;
		default:
			return;
		}

		// The quest ends, rescued (ControlCollision), timed out (QuestEnd) or drained; v_lite48 is as it was
		switch (MaidenLockLight)
		{
		case 0:
			v_lite48->Message(TLightResetAndTurnOff, -1.0f);
			break;
		case 1:
			v_lite48->Message(TLightResetAndTurnOn, -1.0f);
			break;
		default:
			v_lite48->Message(TLightTurnOn, 0.0f);
			v_lite48->Message(TLightFlasherStartTimed, -1.0f);
			break;
		}
		QuestLeave();
		windows->Message(TLightResetAndTurnOff, 0.0f);
		windows->Message(OverrideReset, 0.0f);
		if (code == ControlCollision)
			SetState(PlayState);
		QuestDone();
		auto keep = [] { return GameState() == RescueMaidenState || GameState() == TowerBarrageState; };
		if (!keep())
		{
			v_lite47->Message(TLightResetAndTurnOff, 0.0f);
			diverters[0]->Message(TDiverterEnable, 0.0f);
			diverters[1]->Message(TDiverterDisable, 0.0f);
		}
		if (!keep())
		{
			v_lite125->Message(TLightResetAndTurnOff, 0.0f);
			v_lite126->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			diverters[6]->Message(TDiverterEnable, 0.0f);
			diverters[7]->Message(TDiverterDisable, 0.0f);
		}
	}

	void RaiseWeapon();

	// State 12, 0x42ac80: the shop. The ball waits in s_hole118 while the dragon eggs buy what their number offers,
	// or the best that is still of use: 1 to 3 the picked quest, 4 a spell, 5 a weapon, 6 armor, 7 the extra ball
	// light, 8 an extra ball. The left flipper (a_flip1) buys it and spends that many eggs; any other declines
	void ShopStateControl(MessageCode code, const TPinballComponent* caller)
	{
		using enum MessageCode;
		auto eggs = dragon_egg_light_group;
		if (code == TFlipperExtend)
		{
			GameState() = PlayState;
			info_text_box->Clear();
			if (caller == a_flip1)
			{
				switch (ShopItem)
				{
				case 1:
				case 2:
				case 3:
					BallHeld = true;
					if (StartQuest())
					{
						eggs->Message(TLightGroupResetAndTurnOff, 1.0f);
						return;
					}
					BallHeld = false;
					break;
				case 4:
					CastSpell(ShopSpell);
					break;
				case 5:
					RaiseWeapon();
					break;
				case 6:
					AwardArmor();
					break;
				case 7:
					tabc35wave->Play(nullptr, "ShopStateControl");
					v_lite57->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
					break;
				default:
					AwardExtraBall(2.0f);
					break;
				}
				for (; ShopItem > 0; ShopItem--)
					eggs->Message(TLightGroupResetAndTurnOff, 1.0f);
				s_hole118->MessageField = 1;
			}
			else
			{
				AddScore(nullptr, 25000);
				s_hole118->MessageField = 0;
			}
			s_hole118->Message(THoleRelease, 0.0f);
			BallHeld = true;
			TableHook(EnterState, nullptr);
			return;
		}

		if (code == EnterState)
		{
			s_hole118->MessageField = 0;
			info_text_box->Clear();
			ShopItem = eggs->Message(TLightGroupGetOnCount, 0.0f);
			auto allLit = [](std::initializer_list<TLight*> lights)
			{ return std::ranges::all_of(lights, [](const TLight* light) { return light->light_on(); }); };
			// Each item that is of no use any more gives way to the one below it
			if (ShopItem == 8 && Table->ExtraBalls)
				ShopItem = 7;
			if (ShopItem == 7 && v_lite57->light_on())
				ShopItem = 6;
			if (ShopItem == 6 && allLit({v_lite37, v_lite38, v_lite39}))
				ShopItem = 5;
			if (ShopItem == 5 && allLit({v_lite33, v_lite34, v_lite35, v_lite36}))
				ShopItem = 4;
			if (ShopItem == 4)
			{
				ShopSpell = PickSpell();
				if (ShopSpell == -1)
					ShopItem = 1;
			}
			if (ShopItem >= 1 && ShopItem <= 3)
				ShopItem = 1;
			else if (ShopItem < 1 || ShopItem > 8)
				ShopItem = 0;
		}
		else if (code != ShowStateText)
			return;

		using enum Msg;
		std::optional<Msg> text;
		switch (ShopItem)
		{
		case 0:
			tabc32wave->Play(nullptr, "ShopStateControl");
			AddScore(nullptr, 25000);
			s_hole118->MessageField = 0;
			info_text_box->Display(pb::get_rc_string(Dragon_NoDragonEggs), 2.0f);
			s_hole118->Message(THoleRelease, 2.0f);
			BallHeld = true;
			SetState(PlayState);
			return;
		case 1:
			{
				static constexpr std::array quests{Dragon_BuyDragonHoard, Dragon_BuyFireLizards, Dragon_BuyRescueMaiden,
												   Dragon_BuyDragonPass, Dragon_BuyWizardsFury};
				if (auto state = a_kout2->MessageField; state >= DragonHoardState && state <= WizardsFuryState)
					text = quests[state - DragonHoardState];
				break;
			}
		case 4:
			text = Dragon_PurchaseSpell;
			break;
		case 5:
			text = Dragon_PurchaseWeapon;
			break;
		case 6:
			text = Dragon_PurchaseArmor;
			break;
		case 7:
			text = Dragon_PurchaseExtraBallLight;
			break;
		default:
			text = Dragon_PurchaseExtraBall;
			break;
		}
		if (text)
			info_text_box->Display(pb::get_rc_string(*text), -1.0f);
		// The shop sounds even with no offer to show
		if (code == EnterState)
			tabc33wave->Play(nullptr, "ShopStateControl");
	}

	// State 13, 0x42b0e0: Tower Barrage, a multiball. With all three locks lit, their three balls come out one second
	// apart, and the tower's windows flash; it ends when a drain leaves one ball
	void TowerBarrageStateControl(MessageCode code, TPinballComponent*)
	{
		using enum MessageCode;
		auto windows = {prison_window_light_group, watch_window_light_group, gate_window_light_group};
		switch (code)
		{
		case EnterState:
			QuestEndReset();
			v_lite47->Message(TLightResetAndTurnOn, 0.0f);
			diverters[0]->Message(TDiverterDisable, 0.0f);
			diverters[1]->Message(TDiverterEnable, 0.0f);
			diverters[2]->Message(TDiverterEnable, 0.0f);
			diverters[3]->Message(TDiverterDisable, 0.0f);
			diverters[4]->Message(TDiverterEnable, 0.0f);
			diverters[5]->Message(TDiverterDisable, 0.0f);
			v_lite125->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			v_lite126->Message(TLightResetAndTurnOff, 0.0f);
			diverters[6]->Message(TDiverterDisable, 0.0f);
			diverters[7]->Message(TDiverterEnable, 0.0f);
			for (auto group : windows)
				group->Message(TLightFlasherStartTimed, -1.0f);
			Table->MultiballCount += 3;
			for (const auto& [sink, light, time] :
				 {std::tuple{a_sink18, v_lite48, 1.0f}, {a_sink17, v_lite49, 2.0f}, {a_sink16, v_lite50, 3.0f}})
			{
				sink->Message(TSinkResetTimer, time);
				light->Message(TLightFlasherStartTimedThenStayOff, time);
			}
			v_lite78->Message(TLightFlasherStartTimed, -1.0f);
			info_text_box->Clear();
			[[fallthrough]];
		case ShowStateText:
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_TowerBarrage), -1.0f);
			break;
		case BallDrained:
			if (Table->MultiballCount != 1)
				break;
			QuestLeave();
			for (auto group : windows)
				group->Message(TLightResetAndTurnOff, 0.0f);
			v_lite78->Message(TLightResetAndTurnOn, 0.0f);
			SetState(PlayState);
			if (GameState() != RescueMaidenState && GameState() != TowerBarrageState)
			{
				v_lite47->Message(TLightResetAndTurnOff, 0.0f);
				diverters[0]->Message(TDiverterEnable, 0.0f);
				diverters[1]->Message(TDiverterDisable, 0.0f);
				v_lite125->Message(TLightResetAndTurnOff, 0.0f);
				v_lite126->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
				diverters[6]->Message(TDiverterEnable, 0.0f);
				diverters[7]->Message(TDiverterDisable, 0.0f);
			}
			break;
		default:
			break;
		}
	}

	// State 14, 0x42b3c0: Gargoyle Gaggle-Ball, a two-ball multiball the gargoyle kickout (a_kout2) starts in play:
	// a second ball comes out of a_sink1 and the kickout lets its own go
	void GaggleBallStateControl(MessageCode code, TPinballComponent*)
	{
		using enum MessageCode;
		switch (code)
		{
		case EnterState:
			QuestEndReset();
			Table->MultiballCount++;
			a_sink1->Message(TSinkResetTimer, -1.0f);
			if (!BallHeld)
				a_kout2->Message(TKickoutRestartTimer, -1.0f);
			v_lite66->Message(TLightFlasherStartTimed, 1.0f);
			v_lite65->Message(TLightFlasherStartTimed, 1.0f);
			AddScore(nullptr, 100000);
			v_lite77->Message(TLightFlasherStartTimedThenStayOn, -1.0f);
			info_text_box->Clear();
			tabc48wave->Play(nullptr, "GaggleBallStateControl");
			PlayTrack(MidiTracks::Track1);
			[[fallthrough]];
		case ShowStateText:
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_GargoyleGaggleBall), -1.0f);
			break;
		case BallDrained:
			if (Table->MultiballCount != 1)
				break;
			QuestLeave();
			v_lite77->Message(TLightResetAndTurnOn, 0.0f);
			SetState(PlayState);
			v_lite63->Message(TLightResetAndTurnOff, 0.0f);
			v_lite64->Message(TLightResetAndTurnOff, 0.0f);
			break;
		default:
			break;
		}
	}

	// State 15, 0x42b510: Dragon Flurry, a five-ball multiball once all six fire lizard lights (v_lite22 to
	// v_lite27) are lit: balls come out of both fire lizard sinks, the well and the first lock, and a new one is
	// served if the ball that started it is held
	void DragonFlurryStateControl(MessageCode code, TPinballComponent*)
	{
		using enum MessageCode;
		switch (code)
		{
		case EnterState:
			QuestEndReset();
			Table->MultiballCount += 4;
			if (BallHeld)
			{
				Table->MultiballCount--;
				plunger->Message(PlungerStartFeedTimer, 0.96f);
			}
			a_sink1->Message(TSinkResetTimer, -1.0f);
			v_lite66->Message(TLightFlasherStartTimed, 1.0f);
			a_sink2->Message(TSinkResetTimer, -1.0f);
			v_lite65->Message(TLightFlasherStartTimed, 1.0f);
			a_sink4->Message(TSinkResetTimer, 1.0f);
			well_light_group->Message(TLightFlasherStartTimed, 1.0f);
			a_sink18->Message(TSinkResetTimer, 1.0f);
			v_lite48->Message(TLightFlasherStartTimedThenStayOff, 1.0f);
			dragon_letter_light_group->Message(OverrideReset, 0.0f);
			for (auto light : lizard_lights)
				light->Message(TLightTurnOn, 0.0f);
			for (auto light : lizard_lights)
				light->Message(TLightFlasherStartTimed, -1.0f);
			v_lite79->Message(TLightFlasherStartTimedThenStayOn, -1.0f);
			info_text_box->Clear();
			tabc61wave->Play(nullptr, "DragonFlurryStateControl");
			[[fallthrough]];
		case ShowStateText:
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_DragonFlurry), -1.0f);
			break;
		case BallDrained:
			if (Table->MultiballCount != 1)
				break;
			QuestLeave();
			v_lite79->Message(TLightResetAndTurnOn, 0.0f);
			for (auto light : lizard_lights)
				light->Message(TLightResetAndTurnOff, 0.0f);
			SetState(PlayState);
			break;
		default:
			break;
		}
	}

	void AddSecondJackpot(int score) // 0x423870
	{
		Table->SecondJackpotScore = std::min(Table->SecondJackpotScore + score, 20000000);
	}

	// 0x424130: the dragon's mouth opens: its lights come on and its jaw opens, which then lets the ball onto its
	// ramp (ControlAnimationDone in Slay Dragon)
	void OpenDragonMouth()
	{
		SlayDragonFlag = true;
		for (auto light : {v_lite113, v_lite114, v_lite115})
			light->ActiveFlag = 1;
		v_lite119->Message(MessageCode::TLightFtTmpOverrideOn, 0.0f);
		dragon_mouth_light_group->Message(OverrideReset, 0.0f);
		dragon_fire_light_group->Message(OverrideReset, 0.0f);
		FireDirection = 1;
		v_lite113->MessageField = -1;
		a_jaw->Message(MessageCode::TAnimatedBlockerAnimateTo, 7.0f);
	}

	// State 9, 0x42a150: Slay Dragon, once all five quests are done or from the sword (a_spot28). Its time is 30 s
	// and 5 more for each armor piece and spell lit; the hits it needs, in v_lite114's message field, are 8 less
	// the weapons and two spells lit. Each ball in the dragon's mouth (a_sink19) is a hit; the last one counts down
	// to the Super Jackpot. Bumpers, kickouts, fire lizard sinks and targets add to the second jackpot meanwhile
	void SlayDragonStateControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		switch (code)
		{
		case EnterState:
			{
				SlayLaunchPending = true;
				light_sweeps->Message(TLightSweepsStart, 5.0f);
				a_sink18->ActiveFlag = 0;
				a_sink17->ActiveFlag = 0;
				RampCrossed = false;
				SlaySeconds = 30;
				if (v_lite37->light_on())
				{
					SlaySeconds = 35;
					AwardMaxArmor();
					tabc24wave->Play(nullptr, "SlayDragonStateControl");
				}
				if (v_lite38->light_on())
				{
					SlaySeconds += 5;
					CenterPostControl(TAnimatedBlockerAnimateOn, a_center_post);
				}
				if (v_lite39->light_on())
				{
					SlaySeconds += 5;
					BloceControl(TAnimatedBlockerAnimateOn, a_bloce);
				}
				for (auto light : {v_lite31, v_lite32})
				{
					if (light->light_on())
						SlaySeconds += 5;
				}
				v_lite114->MessageField = 8;
				for (auto light : {v_lite33, v_lite34, v_lite35, v_lite36, v_lite28, v_lite29})
				{
					if (light->light_on())
						v_lite114->MessageField--;
				}
				auto keep = GameState() == RescueMaidenState || GameState() == TowerBarrageState;
				if (!keep)
				{
					v_lite47->Message(TLightResetAndTurnOff, 0.0f);
					diverters[0]->Message(TDiverterEnable, 0.0f);
					diverters[1]->Message(TDiverterDisable, 0.0f);
				}
				diverters[2]->Message(TDiverterDisable, 0.0f);
				diverters[3]->Message(TDiverterEnable, 0.0f);
				diverters[4]->Message(TDiverterDisable, 0.0f);
				diverters[5]->Message(TDiverterEnable, 0.0f);
				if (!keep)
				{
					v_lite125->Message(TLightResetAndTurnOff, 0.0f);
					v_lite126->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
					diverters[6]->Message(TDiverterEnable, 0.0f);
					diverters[7]->Message(TDiverterDisable, 0.0f);
				}
				AddScore(nullptr, 500000);
				info_text_box->Display(pb::get_rc_string(Msg::Dragon_PutBallInDragonsMouth), -1.0f);
				v_lite29->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
				v_lite30->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
				a_kout3->ActiveFlag = 1;
				tabc57wave->Play(nullptr, "SlayDragonStateControl");
				return;
			}
		case QuestResume:
			tabc57wave->Play(nullptr, "SlayDragonStateControl");
			mode_timer->Message(TTimerResetTimer, static_cast<float>(SlaySeconds) / 2.0f);
			v_lite29->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			v_lite30->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			a_kout3->ActiveFlag = 1;
			OpenDragonMouth();
			if (info_text_box->CurrentMessage)
				return;
			[[fallthrough]];
		case ShowStateText:
			ShowTimeLeft(Msg::Dragon_SlayDragon);
			return;
		case SetTiltLock:
			TiltResets();
			[[fallthrough]];
		case BallDrained:
			GameState() = PlayState;
			mode_timer->Message(TTimerResetTimer, -1.0f);
			[[fallthrough]];
		case QuestEnd:
			SetLevelLights(3);
			for (auto light : quest_lights)
				light->Message(TLightResetAndTurnOff, 0.0f);
			QuestLeave();
			SlayDragonReset(true);
			for (auto light : {v_lite77, v_lite78, v_lite79})
				light->Message(TLightResetAndTurnOff, 0.0f);
			v_lite76->Message(TLightResetAndTurnOn, 0.0f);
			QuestDone();
			return;
		case ControlRampLayerChanged:
			if (caller == a_ramp107)
				RampCrossed = true;
			return;
		case ControlRampExitToLayer1:
			if (caller == a_ramp107)
				RampCrossed = false;
			return;
		case ControlNotifyTimerExpired:
			if (caller == dragon_fire_light_group)
			{
				// The fire runs up the eight lights and back down
				auto& index = v_lite113->MessageField;
				if (FireDirection < 1)
				{
					if (--index == -1)
						FireDirection = 1;
					else
						fire_lights[index]->Message(TLightResetAndTurnOff, 0.0f);
				}
				else
				{
					++index;
					if (index == 0)
						tabc58wave->Play(nullptr, "SlayDragonStateControl");
					if (index >= 0 && index <= 7)
						fire_lights[index]->Message(TLightResetAndTurnOn, 0.0f);
					else if (index == 8)
						FireDirection = -1;
				}
				dragon_fire_light_group->Message(TLightGroupRestartNotifyTimer,
												 index > 5 && index < 9 ? 0.012f : 0.123f);
			}
			else if (caller == dragon_mouth_light_group)
			{
				// The last hit's count down to the Super Jackpot
				auto step = dragon_mouth_light_group->MessageField++;
				if (step < 0)
					return;
				if (step < 9)
				{
					// 0x439ac8: eight pictures, then a blank one
					constexpr std::string_view countdown = ",-./0123";
					ShowPicture(step < 8 ? std::string(1, countdown[step]) : std::string(), -1.0f);
					dragon_mouth_light_group->Message(TLightGroupRestartNotifyTimer, 0.4f);
					return;
				}
				if (step != 9)
					return;
				Table->LightGroup->Message(OverrideReset, 0.0f);
				auto added = AddScoreUnmultiplied(caller, Table->SecondJackpotScore);
				std::array<char, 64> buffer{};
				snprintf(buffer.data(), buffer.size(), pb::get_rc_string(Msg::Dragon_SuperJackpot),
						 static_cast<long>(added));
				info_text_box->Display(buffer.data(), -1.0f);
				AddScore(caller, 1500000);
				AwardJackpot(false);
				for (auto light : {v_lite33, v_lite34, v_lite35, v_lite36})
					light->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
				Table->ScoreMultiplier = 5;
				AwardMaxArmor();
				CenterPostControl(TAnimatedBlockerAnimateOn, a_center_post);
				BloceControl(TAnimatedBlockerAnimateOn, a_bloce);
				v_lite28->Message(TLightTurnOnTimed, 30.0f);
				v_lite29->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
				v_lite30->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
				a_kout3->ActiveFlag = 1;
				v_lite31->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
				BlocfControl(TAnimatedBlockerAnimateOff, a_blocf);
				SlayDragonStateControl(QuestEnd, nullptr);
				a_sink16->Message(TSinkResetTimer, -1.0f);
				dragon_letter_light_group->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
				DragonFlurryReady();
			}
			return;
		case ControlCollision:
			if (dynamic_cast<TBumper*>(caller))
				AddSecondJackpot(50000);
			else if (dynamic_cast<TKickout*>(caller))
				AddSecondJackpot(750000);
			else if (caller == s_oneway2)
			{
				// The ball left the lane after the light show: the time starts
				if (!SlayLaunchPending && BallSaveStep == 3)
				{
					mode_timer->Message(TTimerResetTimer, static_cast<float>(SlaySeconds));
					ShowTimeLeft(Msg::Dragon_SlayDragon);
				}
			}
			else if (caller == a_sink19)
			{
				if (v_lite114->MessageField == 1)
				{
					Table->LightGroup->Message(MessageCode::TLightFtTmpOverrideOn, 0.0f);
					SlayDragonReset(false);
					mode_timer->Message(TTimerResetTimer, -1.0f);
					dragon_mouth_light_group->MessageField = 0;
					dragon_mouth_light_group->Message(TLightGroupRestartNotifyTimer, 0.0001f);
					info_text_box->Clear();
				}
				RampCrossed = false;
			}
			else if (caller == a_sink1 || caller == a_sink2)
				AddSecondJackpot(500000);
			else if (dynamic_cast<TSoloTarget*>(caller))
				AddSecondJackpot(100000);
			return;
		case ControlTimerExpired:
			if (caller == mode_timer)
			{
				// Time's up, unless the ball is on the dragon's ramp
				if (!RampCrossed)
				{
					mode_timer->MessageField = GameState();
					SlayDragonReset(false);
					SetState(RestartQuestState);
				}
				else
					mode_timer->Message(TTimerResetTimer, 0.05f);
			}
			else if (caller == light_sweeps)
			{
				SlayLaunchPending = false;
				QuestEndReset();
				OpenDragonMouth();
				if (BallSaveStep != 3)
				{
					mode_timer->Message(TTimerResetTimer, static_cast<float>(SlaySeconds));
					TableHook(ShowStateText, nullptr);
				}
			}
			return;
		case ControlAnimationDone:
			// The jaw is open: its ramp and the wall behind it come in, and the fire runs
			if (SlayDragonFlag && caller == a_jaw)
			{
				a_ramp107->ActiveFlag = 1;
				s_bwal170->ActiveFlag = 1;
				dragon_fire_light_group->Message(TLightGroupRestartNotifyTimer, 0.123f);
			}
			return;
		default:
			return;
		}
	}

	// a_sink19, the dragon's mouth, 0x427f10: in Slay Dragon each ball is a hit and 15 s more; it comes out of a
	// random sink and scores by the fire lights lit. Elsewhere the ball goes to a_sink1
	void DragonMouthControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		if (GameState() != SlayDragonState)
		{
			a_sink1->Message(TSinkResetTimer, 0.0f);
			return;
		}
		if (--v_lite114->MessageField == 0)
		{
			tabc56wave->Play(nullptr, "DragonMouthControl");
			return;
		}
		auto msLeft = mode_timer->Message(TTimerGetTimeLeft, 0.0f);
		auto secondsLeft = static_cast<float>(static_cast<float>(msLeft) / 1000.0);
		mode_timer->Message(TTimerResetTimer, secondsLeft + 15.0f);
		auto roll = [](int count) { return std::min(static_cast<int>(RandFloat() * count), count - 1); };
		TSink* sinks[] = {a_sink4, a_sink1, a_sink2, a_sink18, a_sink17, a_sink16};
		sinks[roll(6)]->Message(TSinkResetTimer, 3.0f);

		int score = 100000;
		bool scored = true;
		switch (dragon_fire_light_group->Message(TLightGroupGetOnCount, 0.0f))
		{
		case 0:
		case 1:
			break;
		case 2:
			score = 1000000;
			break;
		case 3:
			if (!roll(5))
			{
				AwardJackpot();
				scored = false;
			}
			break;
		case 4:
			if (!roll(10))
			{
				tabc35wave->Play(nullptr, "DragonMouthControl");
				info_text_box->Display(pb::get_rc_string(Msg::Dragon_ExtraBallLight), 2.0f);
				v_lite57->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
				scored = false;
			}
			break;
		default:
			if (!roll(10))
			{
				AwardExtraBall(0.0f);
				scored = false;
			}
			break;
		}
		if (scored)
		{
			tabc13wave->Play(nullptr, "DragonMouthControl");
			std::array<char, 64> buffer{};
			snprintf(buffer.data(), buffer.size(), pb::get_rc_string(Msg::Dragon_Score),
					 static_cast<long>(AddScore(caller, score)));
			info_text_box->Display(buffer.data(), 2.0f);
		}
		ShowPicture("!", 3.5f);
		std::array<char, 64> buffer{};
		snprintf(buffer.data(), buffer.size(), pb::get_rc_string(Msg::Dragon_SlayDragonHitsLeft),
				 static_cast<long>(v_lite114->MessageField));
		info_text_box->Display(buffer.data(), 5.0f);
	}

	// a_spot28, 0x425830: while its light flashes in play, the sword's last target starts Slay Dragon
	void SlayTargetControl(MessageCode code, TPinballComponent*)
	{
		if (code != MessageCode::ControlCollision || GameState() != PlayState || !v_lite18->FlasherOnFlag)
			return;
		ShowPicture("!", 2.0f);
		SetLevelLights(-1);
		ClearPickedQuest();
		v_lite76->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		SetState(SlayDragonState);
	}

	// State 7, 0x429b10: Dragon Pass. The gargoyle kickout (a_kout2) lays a dragon egg, the armor kickout (a_kout1)
	// awards the jackpot, and a_spot8 and a_spot12 grow it, until the time runs out
	void DragonPassStateControl(MessageCode code, const TPinballComponent* caller)
	{
		using enum MessageCode;
		switch (code)
		{
		case ControlTimerExpired:
			if (caller == mode_timer)
			{
				v_lite46->Message(TLightResetAndTurnOff, 0.0f);
				v_lite45->Message(TLightResetAndTurnOff, 0.0f);
				gargoyle_spot_light_group->Message(OverrideOff, 0.0f);
				mode_timer->MessageField = GameState();
				SetState(RestartQuestState);
			}
			else if (caller == light_sweeps)
			{
				QuestEndReset();
				v_lite14->Message(TLightResetAndTurnOn, 0.0f);
				gargoyle_spot_light_group->Message(TLightGroupAnimationBackward, 0.1f);
				KickOrShop(a_sink18, 0.0f);
				mode_timer->Message(TTimerResetTimer, 30.65f);
				TableHook(ShowStateText, nullptr);
			}
			break;
		case EnterState:
			light_sweeps->Message(TLightSweepsStart, 3.0f);
			gargoyle_spot_light_group->Message(TLightResetAndTurnOff, 0.0f);
			if (GameState() == PlayState)
				v_lite46->Message(TLightFlasherStartTimed, -1.0f);
			a_rockb->Message(TAnimatedBlockerAnimateOff, 0.0f);
			a_spot14->Message(TSoloTargetDisable, 0.0f);
			v_lite45->Message(TLightFlasherStartTimed, -1.0f);
			a_rocka->Message(TAnimatedBlockerAnimateOff, 0.0f);
			a_spot10->Message(TSoloTargetDisable, 0.0f);
			AddScore(nullptr, 100000);
			info_text_box->Clear();
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_PutBallInKickouts), -1.0f);
			v_lite46->Message(TLightFlasherStartTimed, -1.0f);
			v_lite45->Message(TLightFlasherStartTimed, -1.0f);
			tabc53wave->Play(nullptr, "DragonPassStateControl");
			PlayTrack(MidiTracks::Track5);
			break;
		case QuestResume:
			v_lite46->Message(TLightFlasherStartTimed, -1.0f);
			v_lite45->Message(TLightFlasherStartTimed, -1.0f);
			tabc53wave->Play(nullptr, "DragonPassStateControl");
			gargoyle_spot_light_group->Message(OverrideReset, 0.0f);
			mode_timer->Message(TTimerResetTimer, 15.0f);
			if (info_text_box->CurrentMessage)
				break;
			[[fallthrough]];
		case ShowStateText:
			ShowTimeLeft(Msg::Dragon_DragonPass);
			break;
		case BallDrained:
			GameState() = PlayState;
			mode_timer->Message(TTimerResetTimer, -1.0f);
			[[fallthrough]];
		case QuestEnd:
			QuestLeave();
			gargoyle_spot_light_group->Message(TLightResetAndTurnOff, 0.0f);
			gargoyle_spot_light_group->Message(TLightGroupReset, 0.0f);
			gargoyle_spot_light_group->Message(OverrideReset, 0.0f);
			v_lite46->Message(TLightResetAndTurnOff, 0.0f);
			a_rockb->Message(TAnimatedBlockerAnimateOn, 0.0f);
			v_lite45->Message(TLightResetAndTurnOff, 0.0f);
			a_rocka->Message(TAnimatedBlockerAnimateOn, 0.0f);
			QuestDone();
			break;
		default:
			break;
		}
	}

	// State 8, 0x429ec0: Wizard's Fury. The well (a_sink4) gives the mystery award each time until the time runs out
	void WizardsFuryStateControl(MessageCode code, const TPinballComponent* caller)
	{
		using enum MessageCode;
		switch (code)
		{
		case ControlTimerExpired:
			if (caller == mode_timer)
			{
				well_light_group->Message(TLightResetAndTurnOff, 0.0f);
				spell_spot_light_group->Message(TLightResetAndTurnOff, 0.0f);
				mode_timer->MessageField = GameState();
				well_light_group->Message(OverrideOff, 0.0f);
				SetState(RestartQuestState);
			}
			else if (caller == light_sweeps)
			{
				QuestEndReset();
				KickOrShop(a_sink4, 0.0f);
				mode_timer->Message(TTimerResetTimer, 30.69f);
				TableHook(ShowStateText, nullptr);
			}
			break;
		case EnterState:
			light_sweeps->Message(TLightSweepsStart, 4.0f);
			AddScore(nullptr, 100000);
			info_text_box->Clear();
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_PutBallInWell), -1.0f);
			well_light_group->Message(TLightFlasherStartTimed, -1.0f);
			spell_spot_light_group->Message(TLightFlasherStartTimed, -1.0f);
			tabc55wave->Play(nullptr, "WizardsFuryStateControl");
			PlayTrack(MidiTracks::Track6);
			break;
		case QuestResume:
			tabc55wave->Play(nullptr, "WizardsFuryStateControl");
			well_light_group->Message(TLightFlasherStartTimed, -1.0f);
			spell_spot_light_group->Message(TLightFlasherStartTimed, -1.0f);
			mode_timer->Message(TTimerResetTimer, 15.0f);
			if (info_text_box->CurrentMessage)
				break;
			[[fallthrough]];
		case ShowStateText:
			ShowTimeLeft(Msg::Dragon_WizardsFury);
			break;
		case BallDrained:
			GameState() = PlayState;
			mode_timer->Message(TTimerResetTimer, -1.0f);
			[[fallthrough]];
		case QuestEnd:
			QuestLeave();
			QuestDone();
			break;
		default:
			break;
		}
	}

	// 0x425c30: a ball in a fire lizard sink (a_sink1 or a_sink2) comes out of the other one. In play, the second
	// starts the picked quest; in Fire Lizards each slays a lizard for 100,000 per lizard, and every third the jackpot
	void FireLizardSlain(TSink* sink)
	{
		using enum MessageCode;
		auto other = sink == a_sink1 ? a_sink2 : a_sink1;
		auto otherLight = sink == a_sink1 ? v_lite65 : v_lite66;
		auto state = GameState();
		if (state == PlayState)
		{
			if (++v_lite63->MessageField < 2)
			{
				tabc16wave->Play(nullptr, "FireLizardSlain");
				other->Message(TSinkResetTimer, -1.0f);
				return;
			}
			v_lite63->MessageField = 0;
			lizard_lights[0]->Message(TLightFlasherStartTimedThenStayOn, 1.0f); // v_lite22
			BallHeld = true;
			if (DragonFlurryReady())
				return;
			BallHeld = false;
			if (StartQuest())
				return;
			a_sink1->Message(TSinkResetTimer, -1.0f);
			return;
		}

		tabc16wave->Play(nullptr, "FireLizardSlain");
		other->Message(TSinkResetTimer, -1.0f);
		otherLight->Message(TLightFlasherStartTimed, 1.0f);
		if (state != FireLizardsState)
			return;

		if (v_lite91->MessageField < 10)
			v_lite91->MessageField++;
		auto slain = a_sink2->MessageField;
		if (slain < 0 || slain > 2)
		{
			AwardJackpot();
			v_lite91->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			slain = 0;
		}
		else
		{
			if (slain == 2)
				v_lite91->Message(TLightFlasherStartTimed, -1.0f);
			slain++;
			auto score = AddScore(sink, v_lite91->MessageField * 100000);
			std::array<char, 64> buffer{};
			snprintf(buffer.data(), buffer.size(), pb::get_rc_string(Msg::Dragon_FireLizardSlain),
					 static_cast<long>(score));
			info_text_box->Display(buffer.data(), 2.0f);
		}
		a_sink2->MessageField = slain;
	}

	// State 11, 0x42b7c0: count the bonus, one text at a time, then serve the next ball or end the game
	void EndOfBallStateControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		switch (code)
		{
		case SkipBonusText: // count the rest without showing it
			TallyQuiet = true;
			info_text_box->Clear();
			break;
		case ControlTimerExpired:
			// mode_timer: the bonus took too long; count the rest at once
			if (!caller || caller != mode_timer)
				return;
			caller = nullptr;
			LastBallRelease = 0;
			TallyStep = 8;
			break;
		case EnterState:
			TallyQuiet = false;
			TallyStep = 0;
			TallyTotal = 0;
			mode_timer->Message(TTimerResetTimer, 30.0f);
			if (Table->TiltLockFlag && LastBallRelease != 1)
				TallyStep = 8;
			Table->LightGroup->Message(TLightFtTmpOverrideOff, 0.0f);
			info_text_box->Clear();
			tabc44wave->Play(nullptr, "EndOfBallStateControl");
			break;
		case ShowStateText:
			break;
		case BallDrained:
			if (TallyStep < 8)
				return;
			break;
		default:
			return;
		}

		while (true)
		{
			auto step = TallyStep++;
			int count = 0;
			switch (step)
			{
			case 0:
				if (TargetHits)
				{
					TallyTotal += TargetHits * 2000;
					if (!TallyQuiet)
					{
						ShowBonus(Msg::Dragon_TargetBonus, TargetHits * 2000);
						tabc06wave->Play(nullptr, "EndOfBallStateControl");
						return;
					}
				}
				break;
			case 1:
				// Quest Bonus: v_lite28 counts the quests; 0x423e50 also turns off a quest's light, ported with them
				count = v_lite28->MessageField;
				if (count)
				{
					TallyTotal += count * 100000;
					if (!TallyQuiet)
					{
						ShowBonus(Msg::Dragon_QuestBonus, count * 100000);
						tabc06wave->Play(nullptr, "EndOfBallStateControl");
						return;
					}
				}
				break;
			case 2:
				count = static_cast<int>(v_lite37->light_on()) + static_cast<int>(v_lite38->light_on()) +
						static_cast<int>(v_lite39->light_on());
				if (count)
				{
					TallyTotal += count * 50000;
					if (!TallyQuiet)
					{
						ShowBonus(Msg::Dragon_ArmorBonus, count * 50000);
						tabc06wave->Play(nullptr, "EndOfBallStateControl");
						return;
					}
				}
				break;
			case 3:
				if (SpellCount)
				{
					TallyTotal += SpellCount * 75000;
					if (!TallyQuiet)
					{
						ShowBonus(Msg::Dragon_SpellBonus, SpellCount * 75000);
						tabc06wave->Play(nullptr, "EndOfBallStateControl");
						return;
					}
				}
				break;
			case 4:
				count = dragon_egg_light_group->Message(TLightGroupGetOnCount, 0.0f);
				if (count)
				{
					TallyTotal += count * 150000;
					if (!TallyQuiet)
					{
						ShowBonus(Msg::Dragon_DragonEggBonus, count * 150000);
						tabc06wave->Play(nullptr, "EndOfBallStateControl");
						return;
					}
				}
				break;
			case 5:
				count = Table->ScoreMultiplier;
				if (count > 1)
				{
					TallyTotal += count * 75000;
					if (!TallyQuiet)
					{
						ShowBonus(Msg::Dragon_WeaponBonus, count * 75000);
						tabc06wave->Play(nullptr, "EndOfBallStateControl");
						return;
					}
				}
				break;
			case 6:
				if (BonusHold())
				{
					TallyTotal += BonusHold();
					if (!TallyQuiet)
					{
						ShowBonus(Msg::Dragon_BonusHold, BonusHold());
						tabc06wave->Play(nullptr, "EndOfBallStateControl");
						return;
					}
				}
				break;
			case 7:
				if (v_lite31->light_on())
				{
					v_lite31->Message(TLightResetAndTurnOff, 0.0f);
					BonusHold() = TallyTotal;
				}
				else
				{
					BonusHold() = 0;
				}
				if (TallyTotal)
				{
					AddScoreUnmultiplied(caller, TallyTotal);
					if (!TallyQuiet)
					{
						ShowBonus(Msg::Dragon_TotalBonus, TallyTotal);
						tabc13wave->Play(nullptr, "EndOfBallStateControl");
						return;
					}
				}
				break;
			default:
				// Wait for the last of the tower lock's balls
				if (LastBallRelease && Table->MultiballCount)
					return;
				LastBallRelease = 0;
				mode_timer->Message(TTimerResetTimer, -1.0f);
				if (Table->ExtraBalls)
				{
					Table->ExtraBalls--;
					using enum Msg;
					static constexpr std::array shootAgain{STRING198, STRING199, STRING200, STRING201};
					info_text_box->Display(pb::get_rc_string(shootAgain[std::min(Table->CurrentPlayer, 3)]), -1.0f);
					Table->Message(ClearTiltLock, 0.0f);
					plunger->Message(PlungerStartFeedTimer, 0.96f);
				}
				else
				{
					Table->ChangeBallCount(Table->BallCount - 1);
					if (Table->CurrentPlayer + 1 == Table->PlayerCount && !Table->BallCount)
					{
						Table->LightGroup->Message(TLightResetAndTurnOff, 0.0f);
						SetState(GameOverState);
					}
					else
					{
						Table->Message(ClearTiltLock, 0.0f);
						drain->Message(SetTiltLock, 0.0f);
						Table->Message(SwitchToNextPlayer, 0.0f);
						plunger->Message(PlungerStartFeedTimer, 0.96f);
					}
				}
				Table->LightGroup->Message(TLightFtResetOverride, 0.0f);
				return;
			}
		}
	}

	// 0x42c120
	void StateControl(int state, MessageCode code, TPinballComponent* caller)
	{
		switch (state)
		{
		case GameOverState:
			GameOverStateControl(code, caller);
			break;
		case NewGameState:
			break;
		case PlayState:
			PlayStateControl(code, caller);
			break;
		case RestartQuestState:
			RestartQuestStateControl(code, caller);
			break;
		case FireLizardsState:
			FireLizardsStateControl(code, caller);
			break;
		case DragonHoardState:
			DragonHoardStateControl(code, caller);
			break;
		case RescueMaidenState:
			RescueMaidenStateControl(code, caller);
			break;
		case DragonPassState:
			DragonPassStateControl(code, caller);
			break;
		case WizardsFuryState:
			WizardsFuryStateControl(code, caller);
			break;
		case ShopState:
			ShopStateControl(code, caller);
			break;
		case TowerBarrageState:
			TowerBarrageStateControl(code, caller);
			break;
		case GaggleBallState:
			GaggleBallStateControl(code, caller);
			break;
		case SlayDragonState:
			SlayDragonStateControl(code, caller);
			break;
		case DragonFlurryState:
			DragonFlurryStateControl(code, caller);
			break;
		case LaunchState:
			LaunchStateControl(code, caller);
			break;
		case EndOfBallState:
			EndOfBallStateControl(code, caller);
			break;
		default:
			// DRAGON.EXE goes back to play from any state it doesn't know (0x42c000)
			if (state > 15)
				SetState(PlayState);
			break;
		}
	}

	// 0x42c020: every component's events reach it before the component's own handler
	void TableHook(MessageCode code, TPinballComponent* caller)
	{
		if (!info_text_box)
			return;
		if (caller == drain && code == MessageCode::ControlCollision && !Table->MultiballCount)
			BallDrainedFlag = true;
		// The spot targets' second jackpot while a quest is lit (v_lite28) comes with the quests
		if (dynamic_cast<TSoloTarget*>(caller))
		{
			TargetHits++;
			AddJackpot(50000);
		}
		else if (caller == a_sink4 || caller == a_sink1)
		{
			AddJackpot(75000);
		}
		if (code == MessageCode::Resume ||
			(code == MessageCode::ControlTimerExpired && caller == info_text_box && !info_text_box->CurrentMessage))
			code = ShowStateText;
		StateControl(GameState(), code, caller);
	}

	// drain, 0x424900
	void DrainControl(MessageCode code, TPinballComponent*)
	{
		using enum MessageCode;
		while (true)
		{
			if (code == ControlTimerExpired)
			{
				if (LastBallRelease)
					return;
				if (DrainStep == 2)
					plunger->Message(PlungerStartFeedTimer, 0.96f);
				else if (DrainStep == 3)
					plunger->Message(PlungerRelaunchBall, 0.96f);
				return;
			}
			if (code != ControlCollision)
				return;

			DrainStep = 1;
			if (Table->TiltLockFlag)
			{
				v_lite40->Message(TLightResetAndTurnOff, 0.0f);
				BallSaveStep = 0;
			}
			if (!v_lite40->light_on())
				break;

			// Ball save: the drain's own timer serves the same ball again
			DrainStep = 3;
			drain->Message(SetTiltLock, 0.0f);
			code = ControlTimerExpired;
			Table->BallInDrainFlag = 1; // DRAGON.EXE's table field 0x6a, taken to be this flag
		}

		TableHook(BallDrained, drain);
		if (!Table->MultiballCount && !Table->ExtraBalls && Table->BallCount == 1 &&
			Table->CurrentPlayer + 1 == Table->PlayerCount && !LastBallRelease)
		{
			// The game's last ball: the tower lock's balls come out, with the flippers dead
			LastBallRelease = Table->TiltLockFlag ? 2 : 1;
			Table->TiltLockFlag = 1;
			for (auto flipper : {a_flip1, a_flip3, a_flip2})
				flipper->Message(SetTiltLock, 0.0f);
			// DRAGON.EXE reads only the first of the three flags light_on reads, taken here to be LightOnFlag
			for (auto [light, sink] : {std::pair{v_lite50, a_sink16}, {v_lite49, a_sink17}, {v_lite48, a_sink18}})
			{
				if (light->LightOnFlag)
				{
					GameState() = NewGameState;
					Table->MultiballCount++;
					sink->Message(TSinkResetTimer, 0.0f);
					light->Message(TLightResetAndTurnOff, 0.0f);
				}
			}
		}
		if (Table->MultiballCount && !LastBallRelease)
		{
			DrainStep = 0;
			return;
		}
		if (GameState() == EndOfBallState || GameState() == GameOverState)
			return;
		SetState(EndOfBallState);
	}

	// a_sink1 (0x425ea0) and a_sink2 (0x426000), the fire lizard sinks
	void FireLizardSinkControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		auto sink = static_cast<TSink*>(caller);
		bool first = sink == a_sink1;
		if (GameState() == RestartQuestState)
		{
			// a_sink1's handler turns both lights off twice
			for (int count = first ? 2 : 1; count > 0; count--)
			{
				v_lite63->Message(TLightResetAndTurnOff, 0.0f);
				v_lite64->Message(TLightResetAndTurnOff, 0.0f);
			}
			GameState() = mode_timer->MessageField;
			TableHook(QuestResume, nullptr);
			mode_timer->MessageField = -1;
			sink->Message(TSinkResetTimer, -1.0f);
			return;
		}

		// The lights flash for a_sink9's hold time, in whole seconds
		auto time = static_cast<float>(static_cast<int>(a_sink9->TimerTime));
		if (GameState() != FireLizardsState)
		{
			auto light = first ? v_lite63 : v_lite64;
			light->Message(TLightTurnOn, 0.0f);
			light->Message(TLightFlasherStartTimed, time);
		}
		(first ? v_lite66 : v_lite65)->Message(TLightFlasherStartTimed, time);
		ScoreBy(sink, 0);
		FireLizardSlain(sink);
	}

	// 0x4252b0: with all four gargoyle target lights (v_lite11 to v_lite14) on, the pass is clear: the diverters
	// open, the gargoyles rise, and in play v_lite25 counts toward Dragon Flurry
	void PassClear(TPinballComponent* target)
	{
		using enum MessageCode;
		auto lights = {v_lite14, v_lite13, v_lite12, v_lite11};
		if (!std::ranges::all_of(lights, [](const TLight* light) { return light->light_on(); }))
		{
			if (GameState() != 4)
			{
				tabc08wave->Play(nullptr, "PassClear");
				ScoreBy(target, 0);
			}
			return;
		}
		for (auto light : lights)
			light->Message(TLightFlasherStartTimedThenStayOff, 1.0f);
		diverters[2]->Message(TDiverterDisable, 0.0f);
		diverters[3]->Message(TDiverterEnable, 0.0f);
		diverters[4]->Message(TDiverterDisable, 0.0f);
		diverters[5]->Message(TDiverterEnable, 0.0f);
		v_lite56->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
		v_lite55->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
		v_lite46->Message(TLightResetAndTurnOff, 0.0f);
		a_rockb->Message(TAnimatedBlockerAnimateOn, 0.0f);
		v_lite45->Message(TLightResetAndTurnOff, 0.0f);
		a_rocka->Message(TAnimatedBlockerAnimateOn, 0.0f);
		bool flurry = false;
		if (GameState() == PlayState)
		{
			lizard_lights[3]->Message(TLightFlasherStartTimedThenStayOn, 1.0f); // v_lite25
			flurry = DragonFlurryReady();
		}
		if (!flurry)
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_PassClear), 2.0f);
		if (GameState() != 4)
		{
			tabc08wave->Play(nullptr, "PassClear");
			ScoreBy(target, 1);
		}
	}

	// a_spot10 (0x4254a0) and a_spot14 (0x425570), under the gargoyles: each drops its gargoyle and lights its pass
	// light
	void GargoyleTargetControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		if (caller == a_spot10)
		{
			v_lite45->Message(TLightFlasherStartTimed, -1.0f);
			a_rocka->Message(TAnimatedBlockerAnimateOff, 0.0f);
			a_spot10->Message(TSoloTargetDisable, 0.0f);
			v_lite14->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
		}
		else
		{
			if (GameState() == PlayState)
				v_lite46->Message(TLightFlasherStartTimed, -1.0f);
			a_rockb->Message(TAnimatedBlockerAnimateOff, 0.0f);
			a_spot14->Message(TSoloTargetDisable, 0.0f);
			v_lite12->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
		}
		PassClear(caller);
	}

	// a_spot8 (0x425500) and a_spot12 (0x4255e0): their pass lights, or in Dragon Pass 250,000 more jackpot
	void PassTargetControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		auto light = caller == a_spot8 ? v_lite13 : v_lite11;
		if (GameState() != DragonPassState)
		{
			light->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			PassClear(caller);
			return;
		}
		light->Message(TLightFlasherStartTimed, 0.5f);
		AddJackpot(250000);
		AddScore(caller, 100000);
	}

	// a_kout1, the armor kickout, 0x426d80: an armor piece and the pass reset, or in Dragon Pass the jackpot
	void ArmorKickoutControl(MessageCode code, TPinballComponent*)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		if (GameState() == DragonPassState)
		{
			AwardJackpot();
			a_kout1->Message(TKickoutRestartTimer, -1.0f);
			return;
		}
		AwardArmor();
		for (auto light : {v_lite14, v_lite13, v_lite12, v_lite11})
			light->Message(TLightFlasherStartTimedThenStayOff, 1.0f);
		v_lite45->Message(TLightResetAndTurnOff, 0.0f);
		a_rocka->Message(TAnimatedBlockerAnimateOn, 2.0f);
		a_kout1->Message(TKickoutRestartTimer, -1.0f);
	}

	// a_kout2, the gargoyle kickout, 0x426e40. In play it starts Gargoyle Gaggle-Ball (state 14). Its message field is
	// also the picked quest's state, so it counts up to 3 only while none is picked, and scores the score at that
	// index
	void GargoyleKickoutControl(MessageCode code, TPinballComponent*)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		if (GameState() == PlayState)
		{
			SetState(GaggleBallState);
			return;
		}
		tabc32wave->Play(nullptr, "GargoyleKickoutControl");
		if (GameState() == DragonPassState)
			AwardDragonEggs(1);
		else
		{
			if (a_kout2->MessageField < 3)
				a_kout2->MessageField++;
			ScoreBy(a_kout2, a_kout2->MessageField);
		}
		a_kout2->Message(TKickoutRestartTimer, -1.0f);
	}

	// The tower's tripwires: s_trip5 and s_trip6 (0x427880) light the prison windows, s_trip3 and s_trip4
	// (0x427b30) the watch windows and s_trip8 and s_trip9 (0x427d10) the gate windows, one by one, and score by the
	// windows lit. In Rescue Maiden the prison windows give 250,000, 500,000, the jackpot, then the extra ball light
	void TowerTripControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		bool prison = caller == s_trip5 || caller == s_trip6;
		bool watch = caller == s_trip3 || caller == s_trip4;
		auto windows = gate_window_light_group;
		auto lockLight = v_lite50;
		if (prison)
		{
			windows = prison_window_light_group;
			lockLight = v_lite48;
		}
		else if (watch)
		{
			windows = watch_window_light_group;
			lockLight = v_lite49;
		}
		auto showScore = [](int score)
		{
			std::array<char, 64> buffer{};
			snprintf(buffer.data(), buffer.size(), pb::get_rc_string(Msg::Dragon_Score), static_cast<long>(score));
			info_text_box->Display(buffer.data(), 2.0f);
		};
		switch (GameState())
		{
		case PlayState:
			if (lockLight->MessageField)
				return;
			if (prison)
				tabc04wave->Play(nullptr, "TowerTripControl");
			break;
		case RestartQuestState:
			if (mode_timer->MessageField == RescueMaidenState)
			{
				showScore(ScoreBy(caller, 0));
				return;
			}
			break;
		case RescueMaidenState:
			tabc51wave->Play(nullptr, "TowerTripControl");
			if (!prison)
			{
				showScore(ScoreBy(caller, 0));
				return;
			}
			switch (windows->Message(TLightGroupGetOnCount, 0.0f))
			{
			case 0:
				showScore(AddScore(caller, 250000));
				break;
			case 1:
				showScore(AddScore(caller, 500000));
				break;
			case 2:
				AwardJackpot();
				break;
			default:
				v_lite57->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
				info_text_box->Display(pb::get_rc_string(Msg::Dragon_MaidenRescued), 2.0f);
				break;
			}
			windows->Message(TLightGroupResetAndTurnOn, 1.0f);
			return;
		case TowerBarrageState:
			AddScore(caller, 100000);
			return;
		default:
			break;
		}

		if (watch)
			tabc04wave->Play(nullptr, "TowerTripControl");
		auto lit = windows->Message(TLightGroupGetOnCount, 0.0f);
		showScore(ScoreBy(caller, lit));
		if (windows->Message(TLightGroupGetLightCount, 0.0f) - lit == 1)
		{
			windows->Message(TLightTurnOff, 0.0f);
			windows->Message(TLightFlasherStartTimedThenStayOff, 1.0f);
			return;
		}
		windows->Message(TLightGroupResetAndTurnOn, 1.0f);
	}

	// a_ramp103, 0x4265e0: each shot within 20 s of the last scores 50,000 more, up to 500,000, and adds it to the
	// jackpot; a_ramp103's message field counts the shots. In play v_lite24 counts toward Dragon Flurry
	void JackpotRampControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlRampExitToLayer2)
			return;
		if (GameState() != DragonHoardState)
			tabc18wave->Play(nullptr, "JackpotRampControl");
		if (!v_lite67->light_on())
			caller->MessageField = 0;
		else if (caller->MessageField < 9)
			caller->MessageField++;
		v_lite67->Message(TLightTurnOn, 0.0f);
		v_lite67->Message(TLightFlasherStartTimedThenStayOff, 20.0f);
		if (GameState() != RescueMaidenState && GameState() != TowerBarrageState)
		{
			v_lite125->Message(TLightResetAndTurnOff, 0.0f);
			v_lite126->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			diverters[6]->Message(TDiverterEnable, 0.0f);
			diverters[7]->Message(TDiverterDisable, 0.0f);
		}
		v_lite119->Message(TLightFlasherStartTimed, 2.0f);
		if (GameState() != DragonHoardState)
		{
			auto score = (caller->MessageField + 1) * 50000;
			AddScore(caller, score);
			AddJackpot(score);
		}
		if (GameState() == PlayState)
		{
			lizard_lights[2]->Message(TLightFlasherStartTimedThenStayOn, 1.0f); // v_lite24
			if (!DragonFlurryReady())
				info_text_box->Display(pb::get_rc_string(Msg::Dragon_JackpotGrows), 2.0f);
		}
	}

	// a_ramp6, 0x426740: a spell while v_lite70 is on from the last shot, 10 s; in play v_lite26 counts toward Dragon
	// Flurry
	void SpellRampControl(MessageCode code, TPinballComponent*)
	{
		using enum MessageCode;
		if (code != ControlRampExitToLayer2)
			return;
		bool flurry = false;
		if (GameState() == PlayState)
		{
			lizard_lights[4]->Message(TLightFlasherStartTimedThenStayOn, 1.0f); // v_lite26
			flurry = DragonFlurryReady();
		}
		if (!flurry)
		{
			if (v_lite70->light_on())
				CastRandomSpell();
			else
				tabc09wave->Play(nullptr, "SpellRampControl");
		}
		v_lite70->Message(TLightTurnOn, 0.0f);
		v_lite70->Message(TLightFlasherStartTimedThenStayOff, 10.0f);
	}

	// s_oneway5, 0x4267f0: 200,000 more second jackpot, the Super Jackpot, and its own score by v_lite68's message
	// field, which counts the shots within 10 s of the last, up to 4. In play it opens the tower lane
	void SuperJackpotLaneControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code == ControlTimerExpired)
		{
			v_lite68->MessageField = 0;
			return;
		}
		if (code != ControlCollision)
			return;
		if (GameState() == PlayState)
		{
			v_lite125->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			v_lite126->Message(TLightResetAndTurnOff, 0.0f);
			diverters[6]->Message(TDiverterDisable, 0.0f);
			diverters[7]->Message(TDiverterEnable, 0.0f);
		}
		else if (GameState() == DragonHoardState)
			return;
		tabc20wave->Play(nullptr, "SuperJackpotLaneControl");
		ScoreBy(caller, v_lite68->MessageField);
		if (v_lite68->MessageField < 4)
			v_lite68->MessageField++;
		Table->SecondJackpotScore = std::min(Table->SecondJackpotScore + 200000, 20000000);
		std::array<char, 64> buffer{};
		snprintf(buffer.data(), buffer.size(), pb::get_rc_string(Msg::Dragon_SuperJackpotIncreased), 200000L);
		info_text_box->Display(buffer.data(), 2.0f);
		v_lite68->Message(TLightTurnOn, 0.0f);
		v_lite68->Message(TLightFlasherStartTimedThenStayOff, 10.0f);
	}

	// s_trip1 and s_trip2, Fire Ridge, 0x4269a0: their score by v_lite69's message field, which counts the shots
	// within 20 s of the last, up to 3; v_lite69's timer starts it over
	void FireRidgeControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code == ControlTimerExpired)
		{
			v_lite69->MessageField = 0;
			return;
		}
		if (code != ControlCollision)
			return;
		auto score = ScoreBy(caller, v_lite69->MessageField);
		std::array<char, 64> buffer{};
		snprintf(buffer.data(), buffer.size(), pb::get_rc_string(Msg::Dragon_FireRidge), static_cast<long>(score));
		if (v_lite69->MessageField < 3)
			v_lite69->MessageField++;
		v_lite69->Message(TLightTurnOn, 0.0f);
		v_lite69->Message(TLightFlasherStartTimedThenStayOff, 20.0f);
		info_text_box->Display(buffer.data(), 2.0f);
	}

	// 0x423330: the next weapon light (v_lite33 to v_lite36), its multiplier, 2 to 5, and its picture
	void RaiseWeapon()
	{
		int multiplier = 2;
		for (auto light : {v_lite33, v_lite34, v_lite35, v_lite36})
		{
			if (!light->light_on())
			{
				light->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
				Table->ScoreMultiplier = multiplier;
				AddScoreUnmultiplied(nullptr, 25000 + (multiplier - 2) * 10000);
				ShowPicture(std::string(1, static_cast<char>('%' + multiplier - 2)), 2.0f);
				break;
			}
			multiplier++;
		}
		tabc39wave->Play(nullptr, "RaiseWeapon");
	}

	// a_bump1 to a_bump4, 0x424bc0: every tenth bumper raises a weapon; a_bump1's message field counts them
	void BumperControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		if (GameState() != DragonHoardState)
			tabc03wave->Play(nullptr, "BumperControl");
		if (caller == a_bump3)
			v_lite65->Message(TLightFlasherStartTimed, 0.5f);
		else if (caller == a_bump4)
			v_lite66->Message(TLightFlasherStartTimed, 0.5f);
		if (GameState() != DragonHoardState)
			ScoreBy(caller, 0);
		if (++a_bump1->MessageField == 10)
		{
			a_bump1->MessageField = 0;
			RaiseWeapon();
		}
	}

	// a_rebo1 (0x424cc0) and a_rebo2 (0x424cf0)
	void ReboundControl(MessageCode code, TPinballComponent* caller)
	{
		if (code != MessageCode::ControlCollision)
			return;
		(caller == a_rebo1 ? v_lite120 : v_lite121)->Message(MessageCode::TLightTurnOnTimed, 0.1f);
		ScoreBy(caller, 0);
	}

	// 0x424d20: with all five mystery lights (v_lite3 to v_lite7) on, a spell, and in play v_lite23 toward Dragon
	// Flurry
	void MysteryLights(TPinballComponent* target)
	{
		auto lights = {v_lite3, v_lite4, v_lite5, v_lite6, v_lite7};
		if (!std::ranges::all_of(lights, [](const TLight* light) { return light->light_on(); }))
		{
			if (GameState() != DragonHoardState)
			{
				ScoreBy(target, 0);
				tabc05wave->Play(nullptr, "MysteryLights");
			}
			return;
		}
		for (auto light : lights)
			light->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		if (GameState() == PlayState)
		{
			lizard_lights[1]->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f); // v_lite23
			if (DragonFlurryReady())
				return;
		}
		CastRandomSpell();
	}

	// a_spot4 to a_spot7 and a_spot22 (0x424e60 to 0x425060): their mystery lights, or in Wizard's Fury the
	// mystery award they name
	void MysteryTargetControl(MessageCode code, TPinballComponent* caller)
	{
		if (code != MessageCode::ControlCollision)
			return;
		auto index = static_cast<int>(std::ranges::find(diamond_targets, caller) - diamond_targets.begin());
		if (index >= static_cast<int>(diamond_targets.size()))
			return;
		if (GameState() != WizardsFuryState)
		{
			std::array{v_lite3, v_lite4, v_lite5, v_lite6, v_lite7}[index]->Message(
				MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
			MysteryLights(caller);
			return;
		}
		a_sink4->MessageField = index;
		tabc05wave->Play(nullptr, "MysteryTargetControl");
		using enum Msg;
		static constexpr std::array mysteryTexts{Dragon_MysteryExtraBall, Dragon_MysteryMaxWeapons,
												 Dragon_MysteryMaxArmor, Dragon_MysteryJackpot,
												 Dragon_MysteryOneMillion};
		info_text_box->Display(pb::get_rc_string(mysteryTexts[index]), 2.0f);
	}

	// 0x423d50: the tower lock opens: its lights and diverters, and the first lock light not lit flashes
	void EnableTowerLock()
	{
		using enum MessageCode;
		v_lite47->Message(TLightResetAndTurnOn, 0.0f);
		diverters[0]->Message(TDiverterDisable, 0.0f);
		diverters[1]->Message(TDiverterEnable, 0.0f);
		v_lite125->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
		v_lite126->Message(TLightResetAndTurnOff, 0.0f);
		diverters[6]->Message(TDiverterDisable, 0.0f);
		diverters[7]->Message(TDiverterEnable, 0.0f);
		for (auto light : {v_lite48, v_lite49, v_lite50})
		{
			if (!light->light_on())
			{
				light->Message(TLightFlasherStartTimed, -1.0f);
				break;
			}
		}
		LockLights();
	}

	// a_spot3 (0x425220), a_spot2 (0x425250) and a_spot1 (0x425280), then 0x4250e0: with all three lights (v_lite8 to
	// v_lite10) on, the tower lock opens, and in play v_lite27 counts toward Dragon Flurry
	void TowerLockTargetControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		TLight* hit = v_lite10;
		if (caller == a_spot3)
			hit = v_lite8;
		else if (caller == a_spot2)
			hit = v_lite9;
		hit->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
		auto lights = {v_lite8, v_lite9, v_lite10};
		if (!std::ranges::all_of(lights, [](const TLight* light) { return light->light_on(); }))
		{
			if (GameState() != DragonHoardState)
			{
				ScoreBy(caller, 0);
				tabc06wave->Play(nullptr, "TowerLockTargetControl");
			}
			return;
		}
		for (auto light : lights)
			light->Message(TLightFlasherStartTimedThenStayOff, 1.0f);
		ScoreBy(caller, 1);
		if (GameState() == PlayState)
		{
			lizard_lights[5]->Message(TLightFlasherStartTimedThenStayOn, 1.0f); // v_lite27
			if (!DragonFlurryReady())
			{
				EnableTowerLock();
				tabc07wave->Play(nullptr, "TowerLockTargetControl");
				info_text_box->Display(pb::get_rc_string(Msg::Dragon_TowerLockEnabled), 2.0f);
			}
		}
		if (GameState() != DragonHoardState)
			tabc06wave->Play(nullptr, "TowerLockTargetControl");
	}

	// a_spot27, 0x425650: in play it picks another quest; with the extra ball light (v_lite57) on, the extra ball
	void QuestTargetControl(MessageCode code, TPinballComponent* caller)
	{
		if (code != MessageCode::ControlCollision)
			return;
		if (v_lite57->light_on())
		{
			v_lite57->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			AwardExtraBall(5.0f);
			return;
		}
		tabc96wave->Play(nullptr, "QuestTargetControl");
		v_lite21->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		if (GameState() == PlayState)
		{
			using enum Msg;
			static constexpr std::array selected{Dragon_DragonHoardSelected, Dragon_FireLizardsSelected,
												 Dragon_RescueMaidenSelected, Dragon_DragonPassSelected,
												 Dragon_WizardsFurySelected};
			auto state = PickQuest();
			if (state >= DragonHoardState && state <= WizardsFuryState)
				info_text_box->Display(pb::get_rc_string(selected[state - DragonHoardState]), 2.0f);
		}
		ScoreBy(caller, 0);
	}

	// a_spot19 (0x425750) and a_spot26 (0x4257c0), the sword's targets: while their light flashes in play, the sword
	// goes down a level, with the dragon's picture; the last one, a_spot28, starts Slay Dragon (SlayTargetControl)
	void SwordTargetControl(MessageCode code, TPinballComponent* caller)
	{
		if (code != MessageCode::ControlCollision || GameState() != PlayState)
			return;
		if (auto light = caller == a_spot19 ? v_lite16 : v_lite17; !light->FlasherOnFlag)
			return;
		ShowPicture("!", 2.0f);
		SetLevelLights(caller == a_spot19 ? 2 : 1);
		dragon_fire_light_group->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
		ScoreBy(caller, 0);
	}

	// a_spot30 (0x4258b0) and a_spot31 (0x4258e0)
	void LaneTargetControl(MessageCode code, TPinballComponent* caller)
	{
		if (code != MessageCode::ControlCollision)
			return;
		(caller == a_spot30 ? v_lite20 : v_lite19)->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
		ScoreBy(caller, 0);
	}

	// a_roll7 (0x426a50) lights an outlane kicker and opens the tower lane; a_roll6 (0x426b00) and a_roll1
	// (0x426b70) light one while their light (v_lite56, v_lite55) is on; the others (a_roll2, a_roll5, and
	// v_wlite96 to v_wlite99, 0x427860) only score
	void RolloverControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		auto lightKicker = []
		{
			tabc24wave->Play(nullptr, "RolloverControl");
			outlane_kicker_light_group->Message(TLightGroupOffsetAnimationForward, 0.0f);
			a_kick1->ActiveFlag = 1;
		};
		if (caller == a_roll7)
		{
			ScoreBy(caller, 0);
			lightKicker();
			v_lite47->Message(TLightResetAndTurnOn, 0.0f);
			diverters[0]->Message(TDiverterDisable, 0.0f);
			diverters[1]->Message(TDiverterEnable, 0.0f);
			return;
		}
		if (caller == a_roll6 || caller == a_roll1)
		{
			auto light = caller == a_roll6 ? v_lite56 : v_lite55;
			if (light->light_on())
			{
				lightKicker();
				light->Message(TLightFlasherStartTimedThenStayOff, 1.0f);
			}
		}
		ScoreBy(caller, 0);
	}

	// s_rexit19 (0x426be0) and s_rexit18 (0x426c20): out of the ramp, dragon eggs, and in play the diverters swing
	void RampExitControl(MessageCode code, const TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlRampExitToLayer1)
			return;
		bool first = caller == s_rexit19;
		AwardDragonEggs(first ? 2 : 1);
		if (GameState() == PlayState)
		{
			diverters[first ? 4 : 2]->Message(TDiverterEnable, 0.0f);
			diverters[first ? 5 : 3]->Message(TDiverterDisable, 0.0f);
		}
	}

	// a_flip2, 0x426c60: the upper flipper lets a_kout3's ball go once v_lite29's spell armed it
	void UpperFlipperControl(MessageCode code, TPinballComponent*)
	{
		using enum MessageCode;
		if (code != TFlipperExtend || a_kout3->MessageField != 1)
			return;
		a_kout3->MessageField = 0;
		auto lightCode = GameState() == SlayDragonState ? TLightFlasherStartTimed : TLightFlasherStartTimedThenStayOff;
		v_lite29->Message(lightCode, 1.0f);
		v_lite30->Message(lightCode, 1.0f);
		a_kout3->Message(TKickoutRestartTimer, 0.0f);
	}

	// a_kout3, 0x427ef0: a ball it takes waits there until the upper flipper lets it go
	void SpellKickoutControl(MessageCode code, TPinballComponent*)
	{
		if (code == MessageCode::ControlCollision)
			a_kout3->MessageField = 1;
	}

	// a_kick1, the kickback, 0x426d00: "Ball Saved"; without full armor it uses up an outlane kicker light, and the
	// last one turns it off
	void KickbackControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlTimerExpired)
			return;
		if (!v_lite37->light_on())
		{
			outlane_kicker_light_group->Message(TLightGroupOffsetAnimationBackward, 0.0f);
			if (!outlane_kicker_light_group->Message(TLightGroupGetOnCount, 0.0f))
				caller->ActiveFlag = 0;
		}
		info_text_box->Display(pb::get_rc_string(Msg::Dragon_BallSaved), 2.0f);
	}

	// a_rocka (0x423810) and a_rockb (0x423840): a gargoyle back up enables its target again
	void GargoyleControl(MessageCode code, TPinballComponent* caller)
	{
		if (code != MessageCode::ControlAnimationDone || static_cast<TAnimatedBlocker*>(caller)->State == -1)
			return;
		(caller == a_rocka ? a_spot10 : a_spot14)->Message(MessageCode::TSoloTargetEnable, 0.0f);
	}

	// v_lite37, full armor, 0x428240: on for 53 s, then flashing 5 s, then off
	void ArmorLightControl(MessageCode code, TPinballComponent*)
	{
		using enum MessageCode;
		if (code != ControlTimerExpired)
			return;
		switch (v_lite37->MessageField++)
		{
		case 0:
			v_lite37->Message(TLightTurnOnTimed, 53.0f);
			break;
		case 1:
			v_lite37->Message(TLightFlasherStartTimedThenStayOff, 5.0f);
			break;
		default:
			v_lite37->MessageField = 0;
			break;
		}
	}

	// v_lite29, 0x428290: once its spell's light is lit, a_kout3 takes balls
	void KickoutSpellControl(MessageCode code, TPinballComponent*)
	{
		if (code == MessageCode::ControlTimerExpired && v_lite29->light_on())
			a_kout3->ActiveFlag = 1;
	}

	// a_accl1, 0x426940: its second score while v_lite32's spell is lit
	void AcceleratorControl(MessageCode code, TPinballComponent* caller)
	{
		if (code != MessageCode::ControlCollision || GameState() == DragonHoardState)
			return;
		tabc22wave->Play(nullptr, "AcceleratorControl");
		ScoreBy(caller, v_lite32->light_on() ? 1 : 0);
	}

	// The tower lock. a_sink15 (0x426f90), a_sink13 (0x427290) and a_sink9 or a_sink10 (0x4274d0, 0x427680) lead
	// into its three locks, a_sink18 (0x427100), a_sink17 (0x4273d0) and a_sink16. While a lock's light (v_lite48 to
	// v_lite50) is armed, the ball stays there: "Ball Trapped", and a new ball is served for the first two. With all
	// three lit, Tower Barrage (state 13) starts. In Rescue Maiden a_sink15 and a_sink18 send the ball out of a fire
	// lizard sink
	void TrapBall(TPinballComponent* sink, TLight* lockLight, bool serve)
	{
		tabc10wave->Play(nullptr, "TrapBall");
		Table->MultiballCount--;
		lockLight->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		if (serve)
			plunger->Message(MessageCode::PlungerStartFeedTimer, 0.96f);
		std::array<char, 64> buffer{};
		snprintf(buffer.data(), buffer.size(), pb::get_rc_string(Msg::Dragon_BallTrapped),
				 static_cast<long>(ScoreBy(sink, 0)));
		info_text_box->Display(buffer.data(), 2.0f);
		LockLights();
	}

	void TowerSinkControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		auto state = GameState();
		auto toLizard = []
		{
			tabc51wave->Play(nullptr, "TowerSinkControl");
			if (std::min(static_cast<int>(RandFloat() * 2), 1))
			{
				a_sink1->Message(TSinkResetTimer, -1.0f);
				v_lite66->Message(TLightFlasherStartTimed, 1.0f);
			}
			else
			{
				a_sink2->Message(TSinkResetTimer, -1.0f);
				v_lite65->Message(TLightFlasherStartTimed, 1.0f);
			}
		};
		if (caller == a_sink15)
		{
			if (state == RescueMaidenState)
				toLizard();
			else if (v_lite48->MessageField)
				TrapBall(caller, v_lite48, true);
			else
			{
				a_sink18->Message(TSinkResetTimer, -1.0f);
				tabc04wave->Play(nullptr, "TowerSinkControl");
			}
		}
		else if (caller == a_sink18 || caller == a_sink17)
		{
			auto lockLight = caller == a_sink18 ? v_lite48 : v_lite49;
			if (state == PlayState)
			{
				if (lockLight->MessageField)
					TrapBall(caller, lockLight, true);
				else
				{
					caller->Message(TSinkResetTimer, 0.0f);
					LockLights();
				}
			}
			else if (state == RescueMaidenState && caller == a_sink18)
				toLizard();
			else
			{
				caller->Message(TSinkResetTimer, caller == a_sink18 ? -1.0f : 0.0f);
				LockReset();
			}
		}
		else if (caller == a_sink13)
		{
			if (state == PlayState && v_lite49->MessageField)
				TrapBall(caller, v_lite49, true);
			else
			{
				if (state != RescueMaidenState)
					tabc04wave->Play(nullptr, "TowerSinkControl");
				a_sink17->Message(TSinkResetTimer, -1.0f);
			}
		}
		else
		{
			// a_sink9 and a_sink10: the third lock serves no ball, as its balls come out in Tower Barrage
			if (state == RescueMaidenState)
			{
				a_sink16->Message(TSinkResetTimer, -1.0f);
				return;
			}
			if (state == PlayState && v_lite50->MessageField)
			{
				TrapBall(caller, v_lite50, false);
				return;
			}
			tabc04wave->Play(nullptr, "TowerSinkControl");
			if (state != TowerBarrageState)
			{
				if (caller == a_sink9)
				{
					v_lite47->Message(TLightResetAndTurnOff, 0.0f);
					diverters[0]->Message(TDiverterEnable, 0.0f);
					diverters[1]->Message(TDiverterDisable, 0.0f);
				}
				else
				{
					v_lite125->Message(TLightResetAndTurnOff, 0.0f);
					v_lite126->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
					diverters[6]->Message(TDiverterEnable, 0.0f);
					diverters[7]->Message(TDiverterDisable, 0.0f);
				}
			}
			a_sink16->Message(TSinkResetTimer, -1.0f);
		}
	}

	// a_sink4, the well, 0x426140. In play it starts the picked quest one time in four and casts a spell otherwise;
	// in Wizard's Fury it gives the mystery award its message field holds (0 extra ball, 1 max weapons, 2 max armor,
	// 3 jackpot, 4 one million) and sets the next, one of the other four, at random
	void WellControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (code != ControlCollision)
			return;
		auto roll = [] { return std::min(static_cast<int>(RandFloat() * 4), 3); };
		if (auto state = GameState(); state == PlayState || state != WizardsFuryState)
		{
			if (state != PlayState)
				CastRandomSpell();
			else if (roll() == 1)
			{
				BallHeld = true;
				if (!StartQuest())
				{
					BallHeld = false;
					// DRAGON.EXE lets the ball go twice here, which serves a second ball once the first is clear. It
					// does so only when no quest is picked; a picked quest not ported yet goes out once
					if (!a_kout2->MessageField)
						a_sink4->Message(TSinkResetTimer, -1.0f);
				}
			}
			else
				CastRandomSpell();
			a_sink4->Message(TSinkResetTimer, -1.0f);
			well_light_group->Message(TLightFlasherStartTimed, 1.0f);
			spell_spot_light_group->Message(TLightFlasherStartTimed, 1.0f);
			return;
		}

		auto award = a_sink4->MessageField;
		switch (award)
		{
		case 0:
			AwardExtraBall(2.0f);
			break;
		case 1:
			for (auto light : {v_lite33, v_lite34, v_lite35, v_lite36})
				light->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			Table->ScoreMultiplier = 5;
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_MaxWeapons), 2.0f);
			break;
		case 2:
			AwardMaxArmor();
			tabc24wave->Play(nullptr, "WellControl");
			CenterPostControl(TAnimatedBlockerAnimateOn, a_center_post);
			BloceControl(TAnimatedBlockerAnimateOn, a_bloce);
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_MaxArmor), 2.0f);
			break;
		case 3:
			AwardJackpot();
			break;
		default:
			AddScore(caller, 1000000);
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_OneMillion), 2.0f);
			award = 4;
			break;
		}
		auto next = roll();
		if (next >= award)
			next++;
		a_sink4->MessageField = next;
		using enum Msg;
		static constexpr std::array mysteryTexts{Dragon_MysteryExtraBall, Dragon_MysteryMaxWeapons,
												 Dragon_MysteryMaxArmor, Dragon_MysteryJackpot,
												 Dragon_MysteryOneMillion};
		info_text_box->Display(pb::get_rc_string(mysteryTexts[next]), 4.0f);
		a_sink4->Message(TSinkResetTimer, -1.0f);
	}

	// s_hole118, 0x426ee0: holds the ball until the rules let it go. In play with one ball it opens the shop
	// (state 12)
	void HoleControl(MessageCode code, TPinballComponent*)
	{
		using enum MessageCode;
		if (code == ControlBallCaptured)
		{
			if (GameState() == PlayState && Table->MultiballCount == 1)
			{
				SetState(ShopState);
				return;
			}
			if (GameState() == LaunchState)
			{
				s_hole118->MessageField = 2;
				return;
			}
			tabc32wave->Play(nullptr, "HoleControl");
			s_hole118->MessageField = 1;
			s_hole118->Message(THoleRelease, 1.0f);
		}
		else if (code == ControlBallReleased && GameState() == PlayState && !s_hole118->MessageField)
		{
			tabc34wave->Play(nullptr, "HoleControl");
		}
	}

	// s_plunger1, 0x4245b0
	void PlungerControl(MessageCode code, TPinballComponent*)
	{
		if (code == MessageCode::PlungerFeedBall)
		{
			TableHook(ShowStateText, nullptr);
			return;
		}
		if (code != MessageCode::PlungerStartFeedTimer)
			return;

		// A new ball: the features' lights, diverters and blockers start over, and the next quest is picked
		using enum MessageCode;
		BallDrainedFlag = false;
		TiltResets();
		LockLights();
		SlayDragonReset(true);
		CenterPostControl(TAnimatedBlockerAnimateOff, a_center_post);
		BloceControl(TAnimatedBlockerAnimateOff, a_bloce);
		a_kick1->ActiveFlag = 1;
		v_lite37->MessageField = 0;
		v_lite37->Message(TLightResetAndTurnOff, 0.0f);
		outlane_kicker_light_group->Message(TLightResetAndTurnOff, 0.0f);
		outlane_kicker_light_group->Message(TLightGroupOffsetAnimationForward, 0.0f);
		for (auto light : {v_lite33, v_lite34, v_lite35, v_lite36})
			light->Message(TLightResetAndTurnOff, 0.0f);
		Table->ScoreMultiplier = 1;
		// Rescue Maiden (6) and Tower Barrage (13) keep the first and last diverter pairs
		auto keep = GameState() == RescueMaidenState || GameState() == TowerBarrageState;
		if (!keep)
		{
			v_lite47->Message(TLightResetAndTurnOff, 0.0f);
			diverters[0]->Message(TDiverterEnable, 0.0f);
			diverters[1]->Message(TDiverterDisable, 0.0f);
		}
		diverters[2]->Message(TDiverterDisable, 0.0f);
		diverters[3]->Message(TDiverterEnable, 0.0f);
		diverters[4]->Message(TDiverterDisable, 0.0f);
		diverters[5]->Message(TDiverterEnable, 0.0f);
		if (!keep)
		{
			v_lite125->Message(TLightResetAndTurnOff, 0.0f);
			v_lite126->Message(TLightFlasherStartTimedThenStayOn, 1.0f);
			diverters[6]->Message(TDiverterEnable, 0.0f);
			diverters[7]->Message(TDiverterDisable, 0.0f);
		}
		SetLevelLights(3);
		v_lite46->Message(TLightResetAndTurnOff, 0.0f);
		a_rockb->Message(TAnimatedBlockerAnimateOn, 0.0f);
		v_lite45->Message(TLightResetAndTurnOff, 0.0f);
		a_rocka->Message(TAnimatedBlockerAnimateOn, 0.0f);
		gargoyle_spot_light_group->Message(TLightResetAndTurnOff, 0.0f);
		v_lite67->Message(TLightResetAndTurnOff, 0.0f);
		v_lite70->Message(TLightResetAndTurnOff, 0.0f);
		v_lite68->Message(TLightResetAndTurnOff, 0.0f);
		v_lite68->MessageField = 0;
		v_lite69->Message(TLightResetAndTurnOff, 0.0f);
		v_lite69->MessageField = 0;
		spell_spot_light_group->Message(TLightResetAndTurnOff, 0.0f);
		a_bump1->MessageField = 0;
		TargetHits = 0;
		SpellCount = 0;
		LastScorer = nullptr;
		if (PickQuest() != SlayDragonState)
			SetState(LaunchState);
		v_lite40->Message(TLightResetAndTurnOn, 0.0f);
		BallSaveStep = 3;
	}

	// v_lite40, the ball save's light, 0x424870
	void BallSaveControl(MessageCode code, TPinballComponent*)
	{
		using enum MessageCode;
		if (code != ControlTimerExpired)
			return;
		switch (BallSaveStep)
		{
		case 3:
			BallSaveStep = 2;
			v_lite40->Message(TLightTurnOnTimed, 10.0f);
			break;
		case 2:
			BallSaveStep = 1;
			v_lite40->Message(TLightFlasherStartTimed, 5.0f);
			break;
		case 1:
			BallSaveStep = 0;
			v_lite40->Message(TLightResetAndTurnOff, 0.0f);
			break;
		default:
			break;
		}
	}

	// s_oneway2, the lane's exit, 0x424590: the ball save starts counting once the ball is out
	void LaneExitControl(MessageCode code, TPinballComponent* caller)
	{
		if (code == MessageCode::ControlCollision && BallSaveStep == 3)
			BallSaveControl(MessageCode::ControlTimerExpired, caller);
	}

	// Stand-ins until the features that own them are ported
	void StandInControl(MessageCode code, TPinballComponent* caller)
	{
		using enum MessageCode;
		if (auto kickout = dynamic_cast<TKickout*>(caller))
		{
			if (code == ControlCollision)
				kickout->Message(TKickoutRestartTimer, kickout->TimerTime1);
		}
		else if (auto sink = dynamic_cast<TSink*>(caller))
		{
			if (code == ControlCollision)
				sink->Message(TSinkResetTimer, sink->TimerTime);
		}
	}

	void NoControl(MessageCode, TPinballComponent*)
	{
		// Components whose own handler isn't ported yet; WithHook still passes their events to the table
	}

	// DRAGON.EXE's control::handler (0x423060) calls the table-wide one, then the component's handler
	template <auto& Handler> void WithHook(MessageCode code, TPinballComponent* caller)
	{
		TableHook(code, caller);
		Handler(code, caller);
	}


	// The scores DRAGON.EXE's binding table (0x43afe0) gives its components, for ScoreBy
	const std::map<std::string_view, std::vector<int>> ComponentScores{
		{"a_bump1", {1500}},
		{"a_bump2", {1500}},
		{"a_bump3", {1500}},
		{"a_bump4", {1500}},
		{"a_rebo1", {1000}},
		{"a_rebo2", {1000}},
		{"a_spot4", {2500}},
		{"a_spot5", {2500}},
		{"a_spot6", {2500}},
		{"a_spot7", {2500}},
		{"a_spot22", {2500}},
		{"a_spot3", {2000, 10000}},
		{"a_spot2", {2000, 10000}},
		{"a_spot1", {2000, 10000}},
		{"a_spot10", {3000, 15000}},
		{"a_spot8", {3000, 15000}},
		{"a_spot14", {3000, 15000}},
		{"a_spot12", {3000, 15000}},
		{"a_spot27", {5000}},
		{"a_spot19", {20000}},
		{"a_spot26", {50000}},
		{"a_spot30", {2500}},
		{"a_spot31", {2500}},
		{"a_sink1", {5000}},
		{"a_sink2", {5000}},
		{"s_oneway5", {25000, 75000, 100000, 125000, 150000}},
		{"a_accl1", {75000, 500000}},
		{"s_trip1", {20000, 50000, 100000, 1000000}},
		{"s_trip2", {20000, 50000, 100000, 1000000}},
		{"a_roll7", {15000}},
		{"a_roll5", {30000}},
		{"a_roll2", {30000}},
		{"a_roll6", {10000}},
		{"a_roll1", {10000}},
		{"a_sink15", {100000}},
		{"a_sink18", {100000}},
		{"a_sink13", {200000}},
		{"a_sink17", {200000}},
		{"a_sink9", {300000}},
		{"a_sink10", {300000}},
		{"v_wlite96", {1500}},
		{"v_wlite97", {1500}},
		{"v_wlite98", {1500}},
		{"v_wlite99", {1500}},
		{"s_trip6", {25000, 50000, 75000, 100000}},
		{"s_trip5", {25000, 50000, 75000, 100000}},
		{"s_trip3", {40000, 80000, 120000}},
		{"s_trip4", {40000, 80000, 120000}},
		{"s_trip8", {50000, 100000}},
		{"s_trip9", {50000, 100000}},
	};

	// One binding per component, with its scores; a deque keeps their addresses
	std::deque<component_control> Links;

	// The table's own events (DRAGON.EXE 0x424260)
	void TableControl(MessageCode code)
	{
		using enum MessageCode;
		if (!Table || !info_text_box)
			return;
		switch (code)
		{
		case SetTiltLock:
			TiltResets();
			a_kick1->ActiveFlag = 0;
			break;
		case NewGame:
			LastBallRelease = 0;
			for (auto light : {v_lite113, v_lite114, v_lite115})
				light->ActiveFlag = 0;
			SetLevelLights(3);
			PlayTrack(MidiTracks::Track7);
			BallSaveStep = 3;
			v_lite40->Message(TLightResetAndTurnOn, 0.0f);
			Table->JackpotScore = 1000000;
			Table->SecondJackpotScore = 2500000;
			GameState() = NewGameState;
			break;
		case GameOver:
			BallSaveStep = 0;
			v_lite40->Message(TLightResetAndTurnOff, 0.0f);
			for (auto light : {v_lite113, v_lite114, v_lite115})
				light->ActiveFlag = 0;
			break;
		default:
			break;
		}
	}

	void Link(TPinballComponent* component, void (&handler)(MessageCode, TPinballComponent*))
	{
		static const std::vector<int> none;
		auto scores = component->GroupName ? ComponentScores.find(component->GroupName) : ComponentScores.end();
		auto& list = scores != ComponentScores.end() ? scores->second : none;
		component->Control = &Links.emplace_back(handler, static_cast<unsigned>(list.size()), list.data());
	}

	bool IsKickoutOrSink(const TPinballComponent& component)
	{
		return dynamic_cast<const TKickout*>(&component) || dynamic_cast<const TSink*>(&component);
	}

	void MakeLinks(TPinballTable* table)
	{
		Table = table;
		Find(drain, "drain");
		Find(plunger, "s_plunger1");
		Find(info_text_box, "info_text_box");
		Find(mode_timer, "mode_timer");
		Find(v_lite28, "v_lite28");
		Find(v_lite31, "v_lite31");
		Find(v_lite37, "v_lite37");
		Find(v_lite38, "v_lite38");
		Find(v_lite39, "v_lite39");
		Find(v_lite40, "v_lite40");
		Find(v_lite48, "v_lite48");
		Find(v_lite49, "v_lite49");
		Find(v_lite50, "v_lite50");
		Find(v_lite113, "v_lite113");
		Find(v_lite114, "v_lite114");
		Find(v_lite115, "v_lite115");
		Find(dragon_egg_light_group, "dragon_egg_light_group");
		Find(a_flip1, "a_flip1");
		Find(a_flip2, "a_flip2");
		Find(a_flip3, "a_flip3");
		Find(a_sink1, "a_sink1");
		Find(a_sink4, "a_sink4");
		Find(a_sink16, "a_sink16");
		Find(a_sink17, "a_sink17");
		Find(a_sink18, "a_sink18");
		Find(tabc06wave, "tabc06wave");
		Find(tabc13wave, "tabc13wave");
		Find(tabc44wave, "tabc44wave");
		Find(tabc56wave, "tabc56wave");
		Find(s_oneway2, "s_oneway2");
		Find(s_hole118, "s_hole118");
		Find(a_kout2, "a_kout2");
		Find(a_kout3, "a_kout3");
		Find(a_kick1, "a_kick1");
		Find(a_sink2, "a_sink2");
		Find(a_sink9, "a_sink9");
		Find(light_sweeps, "light_sweeps");
		Find(a_rocka, "a_rocka");
		Find(a_rockb, "a_rockb");
		Find(a_sword, "a_sword");
		Find(a_jaw, "a_jaw");
		Find(a_center_post, "a_center_post");
		Find(a_bloce, "a_bloce");
		Find(a_blocf, "a_blocf");
		constexpr std::array diverterNames{"a_dvtraa", "a_dvtrab", "a_dvtrba", "a_dvtrbb",
										   "a_dvtrca", "a_dvtrcb", "a_dvtrda", "a_dvtrdb"};
		for (size_t index = 0; index < diverters.size(); index++)
			Find(diverters[index], diverterNames[index]);
		Find(a_spot19, "a_spot19");
		Find(a_spot26, "a_spot26");
		Find(a_spot28, "a_spot28");
		Find(a_bump1, "a_bump1");
		Find(a_ramp107, "a_ramp107");
		Find(s_bwal170, "s_bwal170");
		Find(outlane_kicker_light_group, "outlane_kicker_light_group");
		Find(gargoyle_spot_light_group, "gargoyle_spot_light_group");
		Find(spell_spot_light_group, "spell_spot_light_group");
		Find(dragon_letter_light_group, "dragon_letter_light_group");
		Find(dragon_fire_light_group, "dragon_fire_light_group");
		Find(dragon_mouth_light_group, "dragon_mouth_light_group");
		constexpr std::array questLightNames{"v_lite71", "v_lite72", "v_lite73", "v_lite74", "v_lite75"};
		for (size_t index = 0; index < quest_lights.size(); index++)
			Find(quest_lights[index], questLightNames[index]);
		constexpr std::array lizardLightNames{"v_lite22", "v_lite23", "v_lite24", "v_lite25", "v_lite26", "v_lite27"};
		for (size_t index = 0; index < lizard_lights.size(); index++)
			Find(lizard_lights[index], lizardLightNames[index]);
		Find(v_lite16, "v_lite16");
		Find(v_lite17, "v_lite17");
		Find(v_lite18, "v_lite18");
		Find(v_lite29, "v_lite29");
		Find(v_lite30, "v_lite30");
		Find(v_lite32, "v_lite32");
		Find(v_lite33, "v_lite33");
		Find(v_lite34, "v_lite34");
		Find(v_lite35, "v_lite35");
		Find(v_lite36, "v_lite36");
		Find(v_lite45, "v_lite45");
		Find(v_lite46, "v_lite46");
		Find(v_lite47, "v_lite47");
		Find(v_lite63, "v_lite63");
		Find(v_lite64, "v_lite64");
		Find(v_lite65, "v_lite65");
		Find(v_lite66, "v_lite66");
		Find(v_lite67, "v_lite67");
		Find(v_lite68, "v_lite68");
		Find(v_lite69, "v_lite69");
		Find(v_lite70, "v_lite70");
		Find(v_lite76, "v_lite76");
		Find(v_lite91, "v_lite91");
		Find(v_lite119, "v_lite119");
		Find(v_lite125, "v_lite125");
		Find(v_lite126, "v_lite126");
		Find(tabc16wave, "tabc16wave");
		Find(tabc32wave, "tabc32wave");
		Find(tabc34wave, "tabc34wave");
		Find(tabc43wave, "tabc43wave");
		Find(tabc48wave, "tabc48wave");
		Find(well_light_group, "well_light_group");
		Find(tabc21wave, "tabc21wave");
		Find(tabc24wave, "tabc24wave");
		Find(tabc35wave, "tabc35wave");
		Find(tabc41wave, "tabc41wave");
		Find(tabc55wave, "tabc55wave");
		Find(a_kout1, "a_kout1");
		Find(a_spot8, "a_spot8");
		Find(a_spot10, "a_spot10");
		Find(a_spot12, "a_spot12");
		Find(a_spot14, "a_spot14");
		Find(v_lite11, "v_lite11");
		Find(v_lite12, "v_lite12");
		Find(v_lite13, "v_lite13");
		Find(v_lite14, "v_lite14");
		Find(v_lite55, "v_lite55");
		Find(v_lite56, "v_lite56");
		Find(tabc08wave, "tabc08wave");
		Find(tabc27wave, "tabc27wave");
		Find(tabc40wave, "tabc40wave");
		Find(tabc42wave, "tabc42wave");
		Find(tabc53wave, "tabc53wave");
		Find(prison_window_light_group, "prison_window_light_group");
		Find(watch_window_light_group, "watch_window_light_group");
		Find(gate_window_light_group, "gate_window_light_group");
		Find(v_lite57, "v_lite57");
		Find(s_trip5, "s_trip5");
		Find(s_trip6, "s_trip6");
		Find(tabc04wave, "tabc04wave");
		Find(tabc50wave, "tabc50wave");
		Find(tabc51wave, "tabc51wave");
		Find(gem_light_group, "gem_light_group");
		Find(v_lite88, "v_lite88");
		Find(v_lite89, "v_lite89");
		Find(v_lite90, "v_lite90");
		Find(v_lite92, "v_lite92");
		Find(v_lite93, "v_lite93");
		Find(v_lite94, "v_lite94");
		Find(v_lite95, "v_lite95");
		Find(v_lite117, "v_lite117");
		Find(s_oneway5, "s_oneway5");
		Find(a_accl1, "a_accl1");
		Find(a_ramp6, "a_ramp6");
		Find(a_ramp103, "a_ramp103");
		Find(tabc46wave, "tabc46wave");
		Find(tabc47wave, "tabc47wave");
		Find(a_bump3, "a_bump3");
		Find(a_bump4, "a_bump4");
		Find(a_spot27, "a_spot27");
		Find(a_spot30, "a_spot30");
		Find(a_spot31, "a_spot31");
		Find(a_roll1, "a_roll1");
		Find(a_roll2, "a_roll2");
		Find(a_roll5, "a_roll5");
		Find(a_roll6, "a_roll6");
		Find(a_roll7, "a_roll7");
		Find(s_rexit18, "s_rexit18");
		Find(s_rexit19, "s_rexit19");
		Find(v_wlite96, "v_wlite96");
		Find(v_wlite97, "v_wlite97");
		Find(v_wlite98, "v_wlite98");
		Find(v_wlite99, "v_wlite99");
		Find(a_rebo1, "a_rebo1");
		Find(a_rebo2, "a_rebo2");
		Find(a_spot1, "a_spot1");
		Find(a_spot2, "a_spot2");
		Find(a_spot3, "a_spot3");
		Find(a_spot4, "a_spot4");
		Find(a_spot5, "a_spot5");
		Find(a_spot6, "a_spot6");
		Find(a_spot7, "a_spot7");
		Find(a_spot22, "a_spot22");
		Find(v_lite3, "v_lite3");
		Find(v_lite4, "v_lite4");
		Find(v_lite5, "v_lite5");
		Find(v_lite6, "v_lite6");
		Find(v_lite7, "v_lite7");
		Find(v_lite8, "v_lite8");
		Find(v_lite9, "v_lite9");
		Find(v_lite10, "v_lite10");
		Find(v_lite19, "v_lite19");
		Find(v_lite20, "v_lite20");
		Find(v_lite21, "v_lite21");
		Find(v_lite120, "v_lite120");
		Find(v_lite121, "v_lite121");
		Find(tabc03wave, "tabc03wave");
		Find(tabc05wave, "tabc05wave");
		Find(tabc07wave, "tabc07wave");
		Find(tabc22wave, "tabc22wave");
		Find(tabc39wave, "tabc39wave");
		Find(tabc96wave, "tabc96wave");
		Find(a_sink10, "a_sink10");
		Find(a_sink13, "a_sink13");
		Find(a_sink15, "a_sink15");
		Find(tabc10wave, "tabc10wave");
		Find(tabc33wave, "tabc33wave");
		Find(v_lite78, "v_lite78");
		Find(v_lite77, "v_lite77");
		Find(v_lite79, "v_lite79");
		Find(tabc61wave, "tabc61wave");
		Find(a_sink19, "a_sink19");
		Find(tabc57wave, "tabc57wave");
		Find(tabc58wave, "tabc58wave");
		constexpr std::array fireLightNames{"v_lite113", "v_lite114", "v_lite115", "v_lite58",
											"v_lite59",  "v_lite60",  "v_lite61",  "v_lite62"};
		for (size_t index = 0; index < fire_lights.size(); index++)
			Find(fire_lights[index], fireLightNames[index]);
		Find(s_trip1, "s_trip1");
		Find(s_trip2, "s_trip2");
		Find(s_trip3, "s_trip3");
		Find(s_trip4, "s_trip4");
		Find(s_trip8, "s_trip8");
		Find(s_trip9, "s_trip9");
		Find(tabc09wave, "tabc09wave");
		Find(tabc18wave, "tabc18wave");
		Find(tabc20wave, "tabc20wave");
		constexpr std::array diamondNames{"a_spot4", "a_spot5", "a_spot6", "a_spot7", "a_spot22"};
		for (size_t index = 0; index < diamond_targets.size(); index++)
			Find(diamond_targets[index], diamondNames[index]);
		constexpr std::array pearlNames{"a_spot1", "a_spot2", "a_spot3"};
		for (size_t index = 0; index < pearl_targets.size(); index++)
			Find(pearl_targets[index], pearlNames[index]);

		const std::initializer_list<void*> all{drain,
											   plunger,
											   info_text_box,
											   mode_timer,
											   v_lite28,
											   v_lite31,
											   v_lite37,
											   v_lite38,
											   v_lite39,
											   v_lite40,
											   v_lite48,
											   v_lite49,
											   v_lite50,
											   v_lite113,
											   v_lite114,
											   v_lite115,
											   dragon_egg_light_group,
											   a_flip1,
											   a_flip2,
											   a_flip3,
											   a_sink1,
											   a_sink4,
											   a_sink16,
											   a_sink17,
											   a_sink18,
											   tabc06wave,
											   tabc13wave,
											   tabc44wave,
											   tabc56wave,
											   s_oneway2,
											   s_hole118,
											   a_kout2,
											   a_kout3,
											   a_kick1,
											   a_sink2,
											   a_sink9,
											   light_sweeps,
											   a_rocka,
											   a_rockb,
											   a_sword,
											   a_jaw,
											   a_center_post,
											   a_bloce,
											   a_blocf,
											   a_spot19,
											   a_spot26,
											   a_spot28,
											   a_bump1,
											   a_ramp107,
											   s_bwal170,
											   outlane_kicker_light_group,
											   gargoyle_spot_light_group,
											   spell_spot_light_group,
											   dragon_letter_light_group,
											   dragon_fire_light_group,
											   dragon_mouth_light_group,
											   v_lite16,
											   v_lite17,
											   v_lite18,
											   v_lite29,
											   v_lite30,
											   v_lite32,
											   v_lite33,
											   v_lite34,
											   v_lite35,
											   v_lite36,
											   v_lite45,
											   v_lite46,
											   v_lite47,
											   v_lite63,
											   v_lite64,
											   v_lite65,
											   v_lite66,
											   v_lite67,
											   v_lite68,
											   v_lite69,
											   v_lite70,
											   v_lite76,
											   v_lite91,
											   v_lite119,
											   v_lite125,
											   v_lite126,
											   tabc16wave,
											   tabc32wave,
											   tabc34wave,
											   tabc43wave,
											   tabc48wave,
											   well_light_group,
											   tabc21wave,
											   tabc24wave,
											   tabc35wave,
											   tabc41wave,
											   tabc55wave,
											   a_kout1,
											   a_spot8,
											   a_spot10,
											   a_spot12,
											   a_spot14,
											   v_lite11,
											   v_lite12,
											   v_lite13,
											   v_lite14,
											   v_lite55,
											   v_lite56,
											   tabc08wave,
											   tabc27wave,
											   tabc40wave,
											   tabc42wave,
											   tabc53wave,
											   prison_window_light_group,
											   watch_window_light_group,
											   gate_window_light_group,
											   v_lite57,
											   s_trip5,
											   s_trip6,
											   tabc04wave,
											   tabc50wave,
											   tabc51wave,
											   gem_light_group,
											   v_lite88,
											   v_lite89,
											   v_lite90,
											   v_lite92,
											   v_lite93,
											   v_lite94,
											   v_lite95,
											   v_lite117,
											   s_oneway5,
											   a_accl1,
											   a_ramp6,
											   a_ramp103,
											   tabc46wave,
											   tabc47wave,
											   a_bump3,
											   a_bump4,
											   a_spot27,
											   a_spot30,
											   a_spot31,
											   a_roll1,
											   a_roll2,
											   a_roll5,
											   a_roll6,
											   a_roll7,
											   s_rexit18,
											   s_rexit19,
											   v_wlite96,
											   v_wlite97,
											   v_wlite98,
											   v_wlite99,
											   a_rebo1,
											   a_rebo2,
											   a_spot1,
											   a_spot2,
											   a_spot3,
											   a_spot4,
											   a_spot5,
											   a_spot6,
											   a_spot7,
											   a_spot22,
											   v_lite3,
											   v_lite4,
											   v_lite5,
											   v_lite6,
											   v_lite7,
											   v_lite8,
											   v_lite9,
											   v_lite10,
											   v_lite19,
											   v_lite20,
											   v_lite21,
											   v_lite120,
											   v_lite121,
											   tabc03wave,
											   tabc05wave,
											   tabc07wave,
											   tabc22wave,
											   tabc39wave,
											   tabc96wave,
											   a_sink10,
											   a_sink13,
											   a_sink15,
											   tabc10wave,
											   tabc33wave,
											   v_lite78,
											   v_lite77,
											   v_lite79,
											   tabc61wave,
											   a_sink19,
											   tabc57wave,
											   tabc58wave,
											   s_trip1,
											   s_trip2,
											   s_trip3,
											   s_trip4,
											   s_trip8,
											   s_trip9,
											   tabc09wave,
											   tabc18wave,
											   tabc20wave};
		if (const bool listsFound = std::ranges::none_of(diverters, [](auto c) { return !c; }) &&
								std::ranges::none_of(quest_lights, [](auto c) { return !c; }) &&
								std::ranges::none_of(lizard_lights, [](auto c) { return !c; }) &&
								std::ranges::none_of(diamond_targets, [](auto c) { return !c; }) &&
								std::ranges::none_of(pearl_targets, [](auto c) { return !c; }) &&
								std::ranges::none_of(fire_lights, [](auto c) { return !c; });
			std::ranges::find(all, nullptr) != all.end() || !listsFound)
		{
			// Not Dragon's Keep's DAT as DRAGON.EXE knows it; leave the table without rules
			info_text_box = nullptr;
			return;
		}

		Links.clear();
		for (auto component : table->ComponentList)
		{
			if (!component)
				continue;
			if (component == drain)
				Link(component, WithHook<DrainControl>);
			else if (component == plunger)
				Link(component, WithHook<PlungerControl>);
			else if (component == v_lite40)
				Link(component, WithHook<BallSaveControl>);
			else if (component == s_oneway2)
				Link(component, WithHook<LaneExitControl>);
			else if (component == a_sink1 || component == a_sink2)
				Link(component, WithHook<FireLizardSinkControl>);
			else if (component == s_hole118)
				Link(component, WithHook<HoleControl>);
			else if (component == a_sink4)
				Link(component, WithHook<WellControl>);
			else if (component == a_sink19)
				Link(component, WithHook<DragonMouthControl>);
			else if (component == a_spot28)
				Link(component, WithHook<SlayTargetControl>);
			else if (component == a_spot10 || component == a_spot14)
				Link(component, WithHook<GargoyleTargetControl>);
			else if (component == a_spot8 || component == a_spot12)
				Link(component, WithHook<PassTargetControl>);
			else if (component == a_kout1)
				Link(component, WithHook<ArmorKickoutControl>);
			else if (component == a_kout2)
				Link(component, WithHook<GargoyleKickoutControl>);
			else if (component == s_trip5 || component == s_trip6 || component == s_trip3 || component == s_trip4 ||
					 component == s_trip8 || component == s_trip9)
				Link(component, WithHook<TowerTripControl>);
			else if (component == a_ramp103)
				Link(component, WithHook<JackpotRampControl>);
			else if (component == a_sink9 || component == a_sink10 || component == a_sink13 || component == a_sink15 ||
					 component == a_sink17 || component == a_sink18)
				Link(component, WithHook<TowerSinkControl>);
			else if (component == a_ramp6)
				Link(component, WithHook<SpellRampControl>);
			else if (component == s_oneway5)
				Link(component, WithHook<SuperJackpotLaneControl>);
			else if (component == s_trip1 || component == s_trip2 || component == v_lite69)
				Link(component, WithHook<FireRidgeControl>);
			else if (component == a_bump1 || component == a_bump3 || component == a_bump4 ||
					 (component->GroupName && component->GroupName == std::string_view("a_bump2")))
				Link(component, WithHook<BumperControl>);
			else if (component == a_rebo1 || component == a_rebo2)
				Link(component, WithHook<ReboundControl>);
			else if (std::ranges::find(diamond_targets, component) != diamond_targets.end())
				Link(component, WithHook<MysteryTargetControl>);
			else if (component == a_spot1 || component == a_spot2 || component == a_spot3)
				Link(component, WithHook<TowerLockTargetControl>);
			else if (component == a_spot27)
				Link(component, WithHook<QuestTargetControl>);
			else if (component == a_spot19 || component == a_spot26)
				Link(component, WithHook<SwordTargetControl>);
			else if (component == a_spot30 || component == a_spot31)
				Link(component, WithHook<LaneTargetControl>);
			else if (component == a_roll1 || component == a_roll2 || component == a_roll5 || component == a_roll6 ||
					 component == a_roll7 || component == v_wlite96 || component == v_wlite97 ||
					 component == v_wlite98 || component == v_wlite99)
				Link(component, WithHook<RolloverControl>);
			else if (component == s_rexit18 || component == s_rexit19)
				Link(component, WithHook<RampExitControl>);
			else if (component == a_flip2)
				Link(component, WithHook<UpperFlipperControl>);
			else if (component == a_kout3)
				Link(component, WithHook<SpellKickoutControl>);
			else if (component == a_kick1)
				Link(component, WithHook<KickbackControl>);
			else if (component == a_rocka || component == a_rockb)
				Link(component, WithHook<GargoyleControl>);
			else if (component == v_lite37)
				Link(component, WithHook<ArmorLightControl>);
			else if (component == v_lite29)
				Link(component, WithHook<KickoutSpellControl>);
			else if (component == a_accl1)
				Link(component, WithHook<AcceleratorControl>);
			else if (component == a_center_post)
				Link(component, WithHook<CenterPostControl>);
			else if (component == a_bloce)
				Link(component, WithHook<BloceControl>);
			else if (component == a_blocf)
				Link(component, WithHook<BlocfControl>);
			else if (IsKickoutOrSink(*component))
				Link(component, WithHook<StandInControl>);
			else
				Link(component, WithHook<NoControl>);
		}
	}

	bool InBounds(const TBall& ball, const TCollisionComponent& component)
	{
		auto offset = Table->CollisionCompOffset / 2.0f;
		return ball.ActiveFlag && ball.Position.X >= component.AABB.XMin - offset &&
			   ball.Position.X <= component.AABB.XMax + offset && ball.Position.Y >= component.AABB.YMin - offset &&
			   ball.Position.Y <= component.AABB.YMax + offset;
	}

	// A stand-in: a stuck ball is freed as Space Cadet's rules free one, away from the flippers and the plunger,
	// thrown up, and after 20 tries served again
	void UnstuckBall(TBall& ball, int)
	{
		if (Table->Plunger && InBounds(ball, *Table->Plunger))
			return;
		for (auto flippers : {&Table->FlippersL, &Table->FlippersR})
			for (auto flipper : *flippers)
				if (InBounds(ball, *flipper))
					return;

		if (ball.StuckCounter <= 20)
		{
			vector3 throwDir{0.0f, -1.0f, 0.0f};
			ball.throw_ball(&throwDir, 90.0f, 1.0f, 0.0f);
		}
		else if (Table->Plunger)
		{
			ball.Disable();
			Table->MultiballCount--;
			Table->Plunger->Message(MessageCode::PlungerRelaunchBall, 0);
		}
	}

	void ClearLinks()
	{
		Table = nullptr;
		info_text_box = nullptr;
	}
} // namespace

const TableRules control_dragon::Rules{
	MakeLinks,
	ClearLinks,
	TableControl,
	UnstuckBall,
	[](char) { /* DRAGON.EXE's cheats aren't ported yet */ },
	[] { /* nor its rank bump */ },
};
