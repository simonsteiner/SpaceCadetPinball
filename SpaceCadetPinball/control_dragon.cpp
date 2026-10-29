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

// Dragon's Keep's rules, recovered from DRAGON.EXE one feature at a time (Phase F). Ported so far: the serve, drain
// and game-over flow, with the ball save, the end-of-ball bonus and the music; the quest framework and the Fire
// Lizards quest. Everything else is still a stand-in that keeps the table playable: kickouts and sinks give back the
// balls they catch, which only the rules would do otherwise, a quest whose state isn't ported doesn't start, and a
// stuck ball is freed as Space Cadet's rules free one.
//
// DRAGON.EXE's rules run a state machine whose state each player keeps in info_text_box's message field: the
// component handlers and the engine report to one table-wide function (0x42c020), which hands every event to the
// current state's function (0x42c120). States 0 (game over), 1 (new game), 2 (play), 3 (restart quest), 5 (Fire
// Lizards), 10 (launch) and 11 (end of ball) are ported; the other quests and modes (4, 6 to 9, 12 to 15) come with
// their features. Addresses below are DRAGON.EXE's. Its message numbers differ from this port's above the light codes,
// so each call uses the port's MessageCode: CADET.EXE's light groups number theirs 4 above this port's, its targets
// 4 above, its kickouts, sinks and timers 9 or 10 above; its animated blockers and diverters keep CADET.EXE's.
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
	constexpr int SlayDragonState = 9;
	constexpr int LaunchState = 10;
	constexpr int EndOfBallState = 11;
	constexpr int TowerBarrageState = 13;
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
	bool SlayDragonFlag;           // 0x439b18, set by Slay Dragon, not ported yet
	bool TallyQuiet;               // 0x434d0c: count the bonus without showing it
	int TallyStep;                 // 0x434d10
	int TallyTotal;                // 0x434d00
	int TargetHits;                // 0x434d08, Target Bonus
	int SpellCount;                // 0x434d24, Spell Bonus; counted by the spells, not ported yet
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
	void AddScoreUnmultiplied(TPinballComponent* scorer, int score)
	{
		LastScorer = scorer;
		auto bonus = Table->BonusScoreFlag;
		auto jackpot = Table->JackpotScoreFlag;
		auto multiplier = Table->ScoreMultiplier;
		Table->BonusScoreFlag = false;
		Table->JackpotScoreFlag = false;
		Table->ScoreMultiplier = 1;
		Table->AddScore(score);
		Table->BonusScoreFlag = bonus;
		Table->JackpotScoreFlag = jackpot;
		Table->ScoreMultiplier = multiplier;
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
		return state == FireLizardsState;
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

	// 0x424490: the jackpot is scored; DRAGON.EXE also shows a picture in the text box's second font (glyph "\""),
	// which this engine's text box doesn't have yet
	void AwardJackpot()
	{
		tabc43wave->Play(nullptr, "AwardJackpot");
		AddScore(nullptr, Table->JackpotScore);
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
		case LaunchState:
			LaunchStateControl(code, caller);
			break;
		case EndOfBallState:
			EndOfBallStateControl(code, caller);
			break;
		default:
			// The other quests' and modes' states (4, 6 to 9, 12 to 15) aren't ported; DRAGON.EXE goes back to play
			// from any state it doesn't know (0x42c000)
			if (state > 15)
				SetState(PlayState);
			break;
		}
	}

	// Every TSoloTarget on the table, which MakeLinks finds once
	std::vector<TPinballComponent*> SoloTargets;

	// 0x42c020: every component's events reach it after the component's own handler
	void TableHook(MessageCode code, TPinballComponent* caller)
	{
		if (!info_text_box)
			return;
		if (caller == drain && code == MessageCode::ControlCollision && !Table->MultiballCount)
			BallDrainedFlag = true;
		// The spot targets' second jackpot while a quest is lit (v_lite28) comes with the quests
		if (std::ranges::find(SoloTargets, caller) != SoloTargets.end())
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

	// s_hole118, 0x426ee0: holds the ball until the rules let it go. In play with one ball it opens the shop
	// (state 12), which isn't ported yet, so the hole lets the ball go as in the other states
	void HoleControl(MessageCode code, TPinballComponent*)
	{
		using enum MessageCode;
		if (code == ControlBallCaptured)
		{
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
		auto keep = GameState() == 6 || GameState() == TowerBarrageState;
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

	// DRAGON.EXE's control::handler calls the component's handler, then the table-wide one
	template <void (&Handler)(MessageCode, TPinballComponent*)>
	void WithHook(MessageCode code, TPinballComponent* caller)
	{
		Handler(code, caller);
		TableHook(code, caller);
	}

	component_control DrainLink{WithHook<DrainControl>, 0, nullptr};
	component_control PlungerLink{WithHook<PlungerControl>, 0, nullptr};
	component_control BallSaveLink{WithHook<BallSaveControl>, 0, nullptr};
	component_control LaneExitLink{WithHook<LaneExitControl>, 0, nullptr};
	component_control StandInLink{WithHook<StandInControl>, 0, nullptr};
	component_control FireLizardSinkLink{WithHook<FireLizardSinkControl>, 0, nullptr};
	component_control HoleLink{WithHook<HoleControl>, 0, nullptr};
	component_control CenterPostLink{WithHook<CenterPostControl>, 0, nullptr};
	component_control BloceLink{WithHook<BloceControl>, 0, nullptr};
	component_control BlocfLink{WithHook<BlocfControl>, 0, nullptr};
	component_control HookOnlyLink{WithHook<NoControl>, 0, nullptr};

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

	bool IsSoloTarget(const TPinballComponent& component)
	{
		return dynamic_cast<const TSoloTarget*>(&component);
	}

	void MakeLinks(TPinballTable* table)
	{
		Table = table;
		SoloTargets.clear();
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
											   tabc48wave};
		if (const bool listsFound = std::ranges::none_of(diverters, [](auto c) { return !c; }) &&
									std::ranges::none_of(quest_lights, [](auto c) { return !c; }) &&
									std::ranges::none_of(lizard_lights, [](auto c) { return !c; });
			std::ranges::find(all, nullptr) != all.end() || !listsFound)
		{
			// Not Dragon's Keep's DAT as DRAGON.EXE knows it; leave the table without rules
			info_text_box = nullptr;
			return;
		}

		for (auto component : table->ComponentList)
		{
			if (component && IsSoloTarget(*component))
				SoloTargets.push_back(component);
		}
		for (auto component : table->ComponentList)
		{
			if (!component)
				continue;
			if (component == drain)
				component->Control = &DrainLink;
			else if (component == plunger)
				component->Control = &PlungerLink;
			else if (component == v_lite40)
				component->Control = &BallSaveLink;
			else if (component == s_oneway2)
				component->Control = &LaneExitLink;
			else if (component == a_sink1 || component == a_sink2)
				component->Control = &FireLizardSinkLink;
			else if (component == s_hole118)
				component->Control = &HoleLink;
			else if (component == a_center_post)
				component->Control = &CenterPostLink;
			else if (component == a_bloce)
				component->Control = &BloceLink;
			else if (component == a_blocf)
				component->Control = &BlocfLink;
			else if (dynamic_cast<TKickout*>(component) || dynamic_cast<TSink*>(component))
				component->Control = &StandInLink;
			else
				component->Control = &HookOnlyLink;
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
