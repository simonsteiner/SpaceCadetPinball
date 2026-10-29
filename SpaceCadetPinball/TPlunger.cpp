#include "pch.h"
#include "TPlunger.h"


#include "control.h"
#include "loader.h"
#include "maths.h"
#include "pb.h"
#include "render.h"
#include "TBall.h"
#include "timer.h"
#include "TPinballTable.h"

TPlunger::TPlunger(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	visualStruct visual{};

	loader::query_visual(groupIndex, 0, &visual);
	Boost = 0.0;
	BallFeedTimer_ = 0;
	PullbackTimer_ = 0;
	SoundIndexP1 = visual.SoundIndex4;
	SoundIndexP2 = visual.SoundIndex3;
	HardHitSoundId = visual.Kicker.HardHitSoundId;
	Threshold = 1000000000.0;

    // FT:MaxPullback = 50; 3DPB: MaxPullback = 100, PullbackIncrement is floored.
    if (pb::FullTiltMode)
    {
        MaxPullback = 50;
        PullbackIncrement = MaxPullback / (ListBitmap->size() * 8.0f);
    }
    else
    {
        MaxPullback = 100;
        PullbackIncrement = std::floor(MaxPullback / (ListBitmap->size() * 8.0f));
    }

	Elasticity = 0.5f;
	Smoothness = 0.5f;
	
	PullbackDelay = 0.025f;
	float* floatArr = loader::query_float_attribute(groupIndex, 0, 601);
	table->PlungerPosition = {floatArr[0], floatArr[1]};
}

void TPlunger::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	if (SomeCounter > 0 || (pb::FullTiltMode && PinballTable->TiltLockFlag))
	{
		auto boost = RandFloat() * MaxPullback * 0.1f + MaxPullback;
		maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, 0, boost);
		if (SomeCounter)
			SomeCounter--;
		Message(MessageCode::PlungerInputReleased, 0.0);
	}
	else 
	{
		// 3D Pinball sends Launch Ball when a ball hits the plunger on a tilted table, then pushes with that
		if (PinballTable->TiltLockFlag)
			Message(MessageCode::PlungerLaunchBall, 0.0f);
		auto boost = RandFloat() * Boost * 0.1f + Boost;
		maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, Threshold, boost);
		ReleaseTouched = true;
		if (ReleaseWaitsForTouch)
		{
			// The push kept past PullbackDelay ends with the touch it waited for
			timer::kill(ReleaseTimerId);
			ReleasedTimer(ReleaseTimerId, this);
		}
	}
}

bool TPlunger::CanStartPull() const
{
	if (pb::FullTiltMode)
		return !PullbackStartedFlag && PinballTable->MultiballCount > 0 && !PinballTable->TiltLockFlag;

	// 3D Pinball refuses a new pull only while the pullback timer runs
	return !PullbackTimer_;
}

int TPlunger::Message(MessageCode code, float value)
{
	if (pb::BonusTable())
		KeepMessageFieldPerPlayer(code, value);
	switch (code)
	{
	case MessageCode::PlungerInputPressed:
		if (CanStartPull())
		{
			CancelReleaseWait();
			PullbackStartedFlag = true;
			Boost = 0.0;
			Threshold = 1000000000.0;
			loader::play_sound(HardHitSoundId, this, "TPlunger1");
			PullbackTimer(0, this);
		}
		break;
	case MessageCode::PlungerFeedBall:
		{
			// AddBall fails only when all 20 ball slots are in use, which only cheats can cause; try again later
			if (auto position = PinballTable->PlungerPosition;
				PinballTable->BallCountInRect(position, PinballTable->CollisionCompOffset * 1.2f) ||
				!PinballTable->AddBall(position))
			{
				timer::set(1.0f, this, BallFeedTimer);
			}
			else
			{
				PinballTable->MultiballCount++;
				PinballTable->BallInDrainFlag = 0;
				pb::tilt_no_more();
			}
			break;
		}
	case MessageCode::PlungerStartFeedTimer:
		timer::set(0.95999998f, this, BallFeedTimer);
		loader::play_sound(SoundIndexP1, this, "TPlunger2");
		break;
	case MessageCode::PlungerLaunchBall:
		if (!pb::FullTiltMode)
		{
			// 3D Pinball's Launch Ball command keeps the full push for 0.2 s, not the 25 ms of a released pull
			Threshold = 0.0f;
			Boost = MaxPullback;
			CancelReleaseWait();
			timer::set(0.2f, this, ReleasedTimer);
			break;
		}
		PullbackStartedFlag = true;
		Boost = MaxPullback;
		Message(MessageCode::PlungerInputReleased, 0.0f);
		break;
	case MessageCode::PlungerRelaunchBall:
		SomeCounter++;
		timer::set(value, this, BallFeedTimer);
		loader::play_sound(SoundIndexP1, this, "TPlunger2_1");
		PullbackStartedFlag = true;
		PullbackTimer(0, this);
		break;
	case MessageCode::PlayerChanged:
		TakePlayerChange();
		break;
	case MessageCode::SetTiltLock:
		TakeTiltLock();
		break;
	case MessageCode::Reset:
		if (pb::FullTiltMode)
		{
			Stop();
			SpriteSet(0);
			break;
		}
		// 3D Pinball's Reset stops the ball feed, then lets the plunger go as a release does, without the sound
		SomeCounter = 0;
		timer::kill(BallFeedTimer);
		[[fallthrough]];
	case MessageCode::PlungerInputReleased:
	case MessageCode::Resume:
	case MessageCode::LooseFocus:
		// 3D Pinball releases even without a pull and sounds only when the plunger input is released
		if ((!pb::FullTiltMode || PullbackStartedFlag) && !SomeCounter)
		{
			PullbackStartedFlag = false;
			Threshold = 0.0;
			if (PullbackTimer_)
				timer::kill(PullbackTimer_);
			PullbackTimer_ = 0;
			if (pb::FullTiltMode || code == MessageCode::PlungerInputReleased)
				loader::play_sound(SoundIndexP2, this, "TPlunger3");
			SpriteSet(0);
			// The originals' short physics steps let a ball resting on the plunger touch it within PullbackDelay;
			// with ours it can bounce for longer, so ReleasedTimer keeps the push until it touches
			CancelReleaseWait();
			ReleasePushPending = true;
			ReleaseTouched = false;
			ReleaseTimerId = timer::set(PullbackDelay, this, ReleasedTimer);
		}
		break;
	default:
		break;
	}

	control::handler(code, this);
	return 0;
}

void TPlunger::TakePlayerChange()
{
	// Pinball.exe's plunger takes no PlayerChanged, so a pull held as the player changes stays for the next player's
	// ball; CADET.EXE's lets go of it (0x3fe)
	if (pb::FullTiltMode)
		Stop();
}

void TPlunger::TakeTiltLock()
{
	// Nor SetTiltLock: a tilt leaves 3D Pinball's ball feed running, and the ball it serves ends the tilt; CADET.EXE's
	// stops the feed (0x3f3)
	if (pb::FullTiltMode)
	{
		SomeCounter = 0;
		timer::kill(BallFeedTimer);
	}
}

void TPlunger::Stop()
{
	PullbackStartedFlag = false;
	Boost = 0.0f;
	Threshold = 1000000000.0f;
	SomeCounter = 0;

	timer::kill(BallFeedTimer);
	timer::kill(PullbackTimer);
	PullbackTimer_ = 0;
	timer::kill(ReleasedTimer);
	CancelReleaseWait();
}

void TPlunger::CancelReleaseWait()
{
	// Only a push kept past PullbackDelay has a timer of its own to stop; the originals' timers stay as they were
	if (ReleaseWaitsForTouch)
		timer::kill(ReleaseTimerId);
	ReleasePushPending = false;
	ReleaseWaitsForTouch = false;
}

void TPlunger::BallFeedTimer(int timerId, void* caller)
{
	auto plunger = static_cast<TPlunger*>(caller);
	plunger->Message(MessageCode::PlungerFeedBall, 0.0);
}

void TPlunger::PullbackTimer(int timerId, void* caller)
{
	auto plunger = static_cast<TPlunger*>(caller);
	plunger->Boost += plunger->PullbackIncrement;
	if (plunger->Boost <= plunger->MaxPullback)
	{
		if (plunger->SomeCounter)
		{
			plunger->PullbackTimer_ = timer::set(plunger->PullbackDelay / 4.0f, plunger, PullbackTimer);
		}
		else
		{
			plunger->PullbackTimer_ = timer::set(plunger->PullbackDelay, plunger, PullbackTimer);
		}
	}
	else
	{
		plunger->PullbackTimer_ = 0;
		plunger->Boost = plunger->MaxPullback;
	}

	int index = static_cast<int>(floor(
		static_cast<float>(plunger->ListBitmap->size() - 1) *
		(plunger->Boost / plunger->MaxPullback)));
	plunger->SpriteSet(index);
}

void TPlunger::ReleasedTimer(int timerId, void* caller)
{
	auto plunger = static_cast<TPlunger*>(caller);
	// A newer release's push is under way: an older release's timer leaves it alone
	if ((plunger->ReleasePushPending || plunger->ReleaseWaitsForTouch) && timerId != plunger->ReleaseTimerId)
		return;
	if (plunger->ReleasePushPending && !plunger->ReleaseTouched)
	{
		plunger->ReleasePushPending = false;
		plunger->ReleaseWaitsForTouch = true;
		plunger->ReleaseTimerId = timer::set(ReleasePushLimit - plunger->PullbackDelay, plunger, ReleasedTimer);
		return;
	}
	plunger->ReleasePushPending = false;
	plunger->ReleaseWaitsForTouch = false;
	plunger->Threshold = 1000000000.0;
	plunger->Boost = 0.0;
}
