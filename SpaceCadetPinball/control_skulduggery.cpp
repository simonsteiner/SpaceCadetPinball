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
// and game-over flow, with the ball save, the end-of-ball bonus, the launch's rolling skill shot and the music.
// Everything else is still a stand-in that keeps the table playable: kickouts and sinks give back the balls they
// catch, which only the rules would do otherwise, and a stuck ball is freed as Space Cadet's rules free one.
//
// PIRATES.EXE's rules run a state machine, as DRAGON.EXE's do, whose state each player keeps in info_text_box's
// message field: the engine's control::handler (0x423080) hands every event to one table-wide function (0x42d850),
// which passes it to the current state's function (0x42d900), and then to the component's own handler. States 1
// (game over), 2 (play), 3 (launch), 16 (end of ball) and 17 (the game's last ball) are ported; the modes (4 to 13)
// come with their features. Addresses below are PIRATES.EXE's. Its message numbers are CADET.EXE's, which differ from
// this port's above the light codes, so each call uses the port's MessageCode.
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
	constexpr int LastModeState = 11;
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
	void ShowPicture(const char* glyphs, float time, Msg words)
	{
		info_text_box->Display(glyphs, time, false, 1, pb::get_rc_string(words));
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
		// The modes' lights and the next mode's pick (0x4255a0, 0x425850) come with the modes; a mode's end adds
		// 750,000 to the jackpot
		if (code == ModeEnd && state >= FirstModeState && state <= LastModeState)
			AddJackpot(750000);

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
		default:
			// The modes' states (4 to 13) aren't ported yet; PIRATES.EXE goes back to play from any state it doesn't
			// know (0x42d830)
			if (state < FirstModeState || state > 13)
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
