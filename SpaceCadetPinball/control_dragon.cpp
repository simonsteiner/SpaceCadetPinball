#include "pch.h"
#include "control_dragon.h"

#include "control.h"
#include "midi.h"
#include "pb.h"
#include "TBall.h"
#include "TBumper.h"
#include "TDrain.h"
#include "TFlagSpinner.h"
#include "TFlipper.h"
#include "TKickback.h"
#include "TKickout.h"
#include "TLightBargraph.h"
#include "TLight.h"
#include "TLightGroup.h"
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
// and game-over flow, with the ball save, the end-of-ball bonus and the music. Everything else is still a stand-in
// that keeps the table playable: kickouts and sinks give back the balls they catch, which only the rules would do
// otherwise, and a stuck ball is freed as Space Cadet's rules free one.
//
// DRAGON.EXE's rules run a state machine whose state each player keeps in info_text_box's message field: the
// component handlers and the engine report to one table-wide function (0x42c020), which hands every event to the
// current state's function (0x42c120). States 0 (game over), 1 (new game), 2 (play), 10 (launch) and 11 (end of
// ball) are ported; the quests and modes (3 to 9, 12 to 15) come with their features. Addresses below are DRAGON.EXE's.
// Its message numbers differ from this port's above the light codes, so each call uses the port's MessageCode.
namespace
{
	// Codes DRAGON.EXE's rules functions send only among themselves; SkipBonusText comes from a rule not ported yet
	constexpr auto EnterState = MessageCode::DragonEnterState;
	constexpr auto ShowStateText = MessageCode::DragonShowStateText;
	constexpr auto BallDrained = MessageCode::DragonBallDrained;
	constexpr auto SkipBonusText = MessageCode::DragonSkipBonusText;

	enum State
	{
		GameOverState = 0,
		NewGameState = 1,
		PlayState = 2,
		LaunchState = 10,
		EndOfBallState = 11,
	};

	TPinballTable* Table = nullptr;

	TDrain* drain;
	TPlunger* plunger;
	TTextBox* info_text_box;
	TTimer* mode_timer;
	TLight *v_lite28, *v_lite31, *v_lite37, *v_lite38, *v_lite39, *v_lite40, *v_lite48, *v_lite49, *v_lite50;
	TLight *v_lite113, *v_lite114, *v_lite115;
	TLightGroup* dragon_egg_light_group;
	TFlipper *a_flip1, *a_flip2, *a_flip3;
	TSink *a_sink1, *a_sink4, *a_sink16, *a_sink17, *a_sink18;
	TSound *tabc06wave, *tabc13wave, *tabc44wave, *tabc56wave;

	template <typename T> void Find(T*& component, const char* name)
	{
		component = dynamic_cast<T*>(Table->find_component(name));
	}

	// info_text_box's message field in DRAGON.EXE, which keeps one value per player
	std::array<int, 4> PlayerState{};
	// drain's message field, the bonus a lit Bonus Hold (v_lite31) carries to the next ball, also per player
	std::array<int, 4> PlayerBonusHold{};

	int& GameState()
	{
		return PlayerState[Table->CurrentPlayer];
	}

	int BallSaveStep;              // 0x434d2c: 3 lit until the ball leaves the lane, 2 lit 10 s, 1 flashing 5 s, 0 off
	int DrainStep;                 // 0x43bb6c: what the drain's timer does next
	int LastBallRelease;           // 0x439b1c: the last ball released the tower lock's balls, 2 if it was tilted
	bool BallDrainedFlag;          // 0x439b14
	bool KeepTextOnPlay;           // 0x439b10: the launch's text stays when play starts
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
	void AddScore(TPinballComponent* scorer, int score)
	{
		LastScorer = scorer;
		Table->AddScore(score);
	}

	// 0x423130: the score without multiplier, bonus or jackpot, as Space Cadet's SpecialAddScore
	void AddScoreUnmultiplied(TPinballComponent* scorer, int score)
	{
		LastScorer = scorer;
		auto bonus = Table->BonusScoreFlag;
		auto jackpot = Table->JackpotScoreFlag;
		auto multiplier = Table->ScoreMultiplier;
		Table->BonusScoreFlag = false;
		Table->JackpotScoreFlag = false;
		Table->ScoreMultiplier = 0;
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

	void ShowBonus(Msg text, int score)
	{
		char buffer[64];
		snprintf(buffer, sizeof buffer, pb::get_rc_string(text), static_cast<long>(score));
		info_text_box->Display(buffer, 2.0f);
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
			if (!KeepTextOnPlay)
				info_text_box->Clear();
			KeepTextOnPlay = false;
			PlayTrack(MidiTracks::Track7);
		}
		else if (code == ShowStateText)
		{
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_HitFireLizards), -1.0f);
		}
	}

	// The launch ends at the first object of these types the ball hits (DRAGON.EXE: 1000, 1005, 1006, 1012, 1014,
	// 1015, 1017, 1018, 1019, 1020, 1021, 1024, 1030); TLightRollover and TTripwire are TRollovers
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
				KeepTextOnPlay = true;
			info_text_box->Display(pb::get_rc_string(Msg::Dragon_LaunchBall), -1.0f);
			return;
		}
		// DRAGON.EXE's s_hole118 starts a feature on its code 0x42 here (0x424210), ported with it
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

	// State 11, 0x42b7c0: count the bonus, one text at a time, then serve the next ball or end the game
	void EndOfBallStateControl(MessageCode code, TPinballComponent* caller)
	{
		switch (code)
		{
		case SkipBonusText: // count the rest without showing it
			TallyQuiet = true;
			info_text_box->Clear();
			break;
		case MessageCode::ControlTimerExpired:
			// mode_timer: the bonus took too long; count the rest at once
			if (caller != mode_timer)
				return;
			caller = nullptr;
			LastBallRelease = 0;
			TallyStep = 8;
			break;
		case EnterState:
			TallyQuiet = false;
			TallyStep = 0;
			TallyTotal = 0;
			mode_timer->Message(MessageCode::TTimerResetTimer, 30.0f);
			if (Table->TiltLockFlag && LastBallRelease != 1)
				TallyStep = 8;
			Table->LightGroup->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
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
				count = v_lite37->light_on() + v_lite38->light_on() + v_lite39->light_on();
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
				count = dragon_egg_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
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
				// DRAGON.EXE's multiplier is the factor itself; this engine keeps an index into score_multipliers
				count = TPinballTable::score_multipliers[Table->ScoreMultiplier];
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
				if (PlayerBonusHold[Table->CurrentPlayer])
				{
					TallyTotal += PlayerBonusHold[Table->CurrentPlayer];
					if (!TallyQuiet)
					{
						ShowBonus(Msg::Dragon_BonusHold, PlayerBonusHold[Table->CurrentPlayer]);
						tabc06wave->Play(nullptr, "EndOfBallStateControl");
						return;
					}
				}
				break;
			case 7:
				if (v_lite31->light_on())
				{
					v_lite31->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
					PlayerBonusHold[Table->CurrentPlayer] = TallyTotal;
				}
				else
				{
					PlayerBonusHold[Table->CurrentPlayer] = 0;
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
				mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f);
				if (Table->ExtraBalls)
				{
					Table->ExtraBalls--;
					static constexpr Msg shootAgain[]{Msg::STRING198, Msg::STRING199, Msg::STRING200, Msg::STRING201};
					info_text_box->Display(pb::get_rc_string(shootAgain[std::min(Table->CurrentPlayer, 3)]), -1.0f);
					Table->Message(MessageCode::ClearTiltLock, 0.0f);
					plunger->Message(MessageCode::PlungerStartFeedTimer, 0.96f);
				}
				else
				{
					Table->ChangeBallCount(Table->BallCount - 1);
					if (Table->CurrentPlayer + 1 == Table->PlayerCount && !Table->BallCount)
					{
						Table->LightGroup->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
						SetState(GameOverState);
					}
					else
					{
						Table->Message(MessageCode::ClearTiltLock, 0.0f);
						drain->Message(MessageCode::SetTiltLock, 0.0f);
						Table->Message(MessageCode::SwitchToNextPlayer, 0.0f);
						plunger->Message(MessageCode::PlungerStartFeedTimer, 0.96f);
					}
				}
				Table->LightGroup->Message(MessageCode::TLightFtResetOverride, 0.0f);
				return;
			}
		}
	}

	// 0x42c120
	void StateControl(MessageCode code, TPinballComponent* caller)
	{
		switch (GameState())
		{
		case GameOverState:
			GameOverStateControl(code, caller);
			break;
		case NewGameState:
			break;
		case PlayState:
			PlayStateControl(code, caller);
			break;
		case LaunchState:
			LaunchStateControl(code, caller);
			break;
		case EndOfBallState:
			EndOfBallStateControl(code, caller);
			break;
		default:
			// The quests' and modes' states (3 to 9, 12 to 15) aren't ported; DRAGON.EXE goes back to play from
			// any state it doesn't know (0x42c000)
			if (GameState() > 15)
				SetState(PlayState);
			break;
		}
	}

	// 0x42c020: every component's events reach it after the component's own handler
	void TableHook(MessageCode code, TPinballComponent* caller)
	{
		if (!info_text_box)
			return;
		if (caller == drain && code == MessageCode::ControlCollision && !Table->MultiballCount)
			BallDrainedFlag = true;
		if (caller)
		{
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
		}
		if (code == MessageCode::Resume ||
			(code == MessageCode::ControlTimerExpired && caller == info_text_box && !info_text_box->CurrentMessage))
			code = ShowStateText;
		StateControl(code, caller);
	}

	// drain, 0x424900
	void DrainControl(MessageCode code, TPinballComponent*)
	{
		while (true)
		{
			if (code == MessageCode::ControlTimerExpired)
			{
				if (LastBallRelease)
					return;
				if (DrainStep == 2)
					plunger->Message(MessageCode::PlungerStartFeedTimer, 0.96f);
				else if (DrainStep == 3)
					plunger->Message(MessageCode::PlungerRelaunchBall, 0.96f);
				return;
			}
			if (code != MessageCode::ControlCollision)
				return;

			DrainStep = 1;
			if (Table->TiltLockFlag)
			{
				v_lite40->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
				BallSaveStep = 0;
			}
			if (!v_lite40->light_on())
				break;

			// Ball save: the drain's own timer serves the same ball again
			DrainStep = 3;
			drain->Message(MessageCode::SetTiltLock, 0.0f);
			code = MessageCode::ControlTimerExpired;
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
				flipper->Message(MessageCode::SetTiltLock, 0.0f);
			// DRAGON.EXE reads only the first of the three flags light_on reads, taken here to be LightOnFlag
			for (auto [light, sink] : {std::pair{v_lite50, a_sink16}, {v_lite49, a_sink17}, {v_lite48, a_sink18}})
			{
				if (light->LightOnFlag)
				{
					GameState() = NewGameState;
					Table->MultiballCount++;
					sink->Message(MessageCode::TSinkResetTimer, 0.0f);
					light->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
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

		// A new ball. DRAGON.EXE also resets the features' lights, diverters and blockers here, and picks the
		// next quest (0x423e90), which starts state 9 once every quest is done; those come with their features.
		BallDrainedFlag = false;
		Table->ScoreMultiplier = 0;
		TargetHits = 0;
		SpellCount = 0;
		LastScorer = nullptr;
		SetState(LaunchState);
		v_lite40->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		BallSaveStep = 3;
	}

	// v_lite40, the ball save's light, 0x424870
	void BallSaveControl(MessageCode code, TPinballComponent*)
	{
		if (code != MessageCode::ControlTimerExpired)
			return;
		switch (BallSaveStep)
		{
		case 3:
			BallSaveStep = 2;
			v_lite40->Message(MessageCode::TLightTurnOnTimed, 10.0f);
			break;
		case 2:
			BallSaveStep = 1;
			v_lite40->Message(MessageCode::TLightFlasherStartTimed, 5.0f);
			break;
		case 1:
			BallSaveStep = 0;
			v_lite40->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
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
		if (auto kickout = dynamic_cast<TKickout*>(caller))
		{
			if (code == MessageCode::ControlCollision)
				kickout->Message(MessageCode::TKickoutRestartTimer, kickout->TimerTime1);
		}
		else if (auto sink = dynamic_cast<TSink*>(caller))
		{
			if (code == MessageCode::ControlCollision)
				sink->Message(MessageCode::TSinkResetTimer, sink->TimerTime);
		}
	}

	void NoControl(MessageCode, TPinballComponent*)
	{
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
	component_control HookOnlyLink{WithHook<NoControl>, 0, nullptr};

	// The table's own events (DRAGON.EXE 0x424260)
	void TableControl(MessageCode code)
	{
		if (!Table || !info_text_box)
			return;
		switch (code)
		{
		case MessageCode::SetTiltLock:
			// DRAGON.EXE also turns off the quest lights and the bonus hold and resets a_kout3 and a_blocf (0x423780),
			// ported with those features
			break;
		case MessageCode::NewGame:
			LastBallRelease = 0;
			for (auto light : {v_lite113, v_lite114, v_lite115})
				light->ActiveFlag = 0;
			PlayTrack(MidiTracks::Track7);
			BallSaveStep = 3;
			v_lite40->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
			Table->JackpotScore = 1000000;
			// DRAGON.EXE also sets its engine's second jackpot to 2,500,000; this engine has none yet
			GameState() = NewGameState;
			break;
		case MessageCode::GameOver:
			BallSaveStep = 0;
			v_lite40->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			for (auto light : {v_lite113, v_lite114, v_lite115})
				light->ActiveFlag = 0;
			break;
		case MessageCode::Reset:
			// DRAGON.EXE's components clear their message fields for every player on Reset
			PlayerState.fill(0);
			PlayerBonusHold.fill(0);
			break;
		default:
			break;
		}
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
											   tabc56wave};
		if (std::find(all.begin(), all.end(), nullptr) != all.end())
		{
			// Not Dragon's Keep's DAT as DRAGON.EXE knows it; leave the table without rules
			info_text_box = nullptr;
			return;
		}

		auto laneExit = table->find_component("s_oneway2");
		for (auto component : table->ComponentList)
		{
			if (component == drain)
				component->Control = &DrainLink;
			else if (component == plunger)
				component->Control = &PlungerLink;
			else if (component == v_lite40)
				component->Control = &BallSaveLink;
			else if (component == laneExit)
				component->Control = &LaneExitLink;
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
	MakeLinks, ClearLinks, TableControl, UnstuckBall, [](char) {}, [] {},
};
