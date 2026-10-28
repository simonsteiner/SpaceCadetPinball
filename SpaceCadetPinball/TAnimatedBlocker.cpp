#include "pch.h"
#include "TAnimatedBlocker.h"

#include "control.h"
#include "loader.h"
#include "TEdgeSegment.h"
#include "timer.h"
#include "TPinballTable.h"

TAnimatedBlocker::TAnimatedBlocker(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	visualStruct visual{};
	loader::query_visual(groupIndex, 0, &visual);

	auto stateCount = loader::query_visual_states(groupIndex);
	assertm(stateCount <= MaxStates, "TAnimatedBlocker: too many states");
	for (int state = 0; state < std::min<int>(stateCount, MaxStates); state++)
	{
		auto wall = loader::query_float_attribute(groupIndex, state, 600);
		if (wall)
			TEdgeSegment::install_wall(wall, this, &StateActive[state], visual.CollisionGroup,
									   table->CollisionCompOffset, 0);
	}
	OnSoundIndex = visual.SoundIndex4;
	OffSoundIndex = visual.SoundIndex3;

	FrameTime = loader::query_float_attribute(groupIndex, 0, 903, 0.1f);
	InitialState = static_cast<int>(loader::query_float_attribute(groupIndex, 0, 409, -1.0f));
	Threshold = 1000000000.0f;
	Message(MessageCode::Reset, 0.0f);
}

int TAnimatedBlocker::StateCount() const
{
	return ListBitmap ? static_cast<int>(ListBitmap->size()) : 0;
}

int TAnimatedBlocker::Message(MessageCode code, float value)
{
	using enum MessageCode;
	switch (code)
	{
	case TAnimatedBlockerRestartTimeout:
		if (TimeoutTimer)
			timer::kill(TimeoutTimer);
		TimeoutTimer = timer::set(std::max(value, 0.0f), this, TimeoutExpired);
		break;
	case TAnimatedBlockerAnimateOff:
		if (value > 0.0f)
		{
			TargetState = -1;
			if (AnimationTimer)
				timer::kill(AnimationTimer);
			AnimationTimer = timer::set(value, this, AnimationStep);
		}
		else
			Message(TAnimatedBlockerAnimateTo, -1.0f);
		loader::play_sound(OffSoundIndex, this, "TAnimatedBlocker1");
		break;
	case TAnimatedBlockerAnimateOn:
		if (value > 0.0f)
		{
			TargetState = StateCount() - 1;
			if (AnimationTimer)
				timer::kill(AnimationTimer);
			AnimationTimer = timer::set(value, this, AnimationStep);
		}
		else
			Message(TAnimatedBlockerAnimateTo, static_cast<float>(StateCount() - 1));
		loader::play_sound(OnSoundIndex, this, "TAnimatedBlocker2");
		break;
	case TAnimatedBlockerSetState:
		{
			// A state that blocks waits while a ball is in the way, and is asked again every half second
			if (DeferredTimer)
				timer::kill(DeferredTimer);
			DeferredTimer = 0;
			if (AnimationTimer)
				timer::kill(AnimationTimer);
			AnimationTimer = 0;
			auto state = static_cast<int>(value);
			if (state >= 0 && PinballTable->BallCountInRect(AABB))
			{
				DeferredCode = code;
				DeferredValue = value;
				DeferredTimer = timer::set(0.5f, this, DeferredMessage);
				break;
			}
			SetState(state);
			break;
		}
	case TAnimatedBlockerAnimateTo:
		TargetState = static_cast<int>(value);
		if (AnimationTimer)
			timer::kill(AnimationTimer);
		AnimationTimer = timer::set(FrameTime, this, AnimationStep);
		break;
	case TAnimatedBlockerSetOn:
		Message(TAnimatedBlockerSetState, static_cast<float>(StateCount() - 1));
		loader::play_sound(OnSoundIndex, this, "TAnimatedBlocker3");
		break;
	case TAnimatedBlockerSetOff:
		Message(TAnimatedBlockerSetState, -1.0f);
		loader::play_sound(OffSoundIndex, this, "TAnimatedBlocker4");
		break;
	case PlayerChanged:
		// CADET.EXE's components keep their message field for each player
		PlayerMessageFields[PinballTable->CurrentPlayer] = MessageField;
		MessageField = PlayerMessageFields[static_cast<int>(value)];
		KillTimers();
		Message(TAnimatedBlockerSetState, static_cast<float>(InitialState));
		break;
	case Reset:
		MessageField = 0;
		std::ranges::fill(PlayerMessageFields, 0);
		[[fallthrough]];
	case SetTiltLock:
		KillTimers();
		Message(TAnimatedBlockerSetState, static_cast<float>(InitialState));
		break;
	default:
		break;
	}
	return 0;
}

void TAnimatedBlocker::SetState(int state)
{
	auto count = StateCount();
	if (State >= count)
		State = count - 1;
	if (State >= 0 && State < MaxStates)
		StateActive[State] = 0;
	State = state;
	if (State >= 0 && State < MaxStates)
		StateActive[State] = 1;
	SpriteSet(State < count ? State : -1);
}

void TAnimatedBlocker::KillTimers()
{
	if (TimeoutTimer)
		timer::kill(TimeoutTimer);
	TimeoutTimer = 0;
	if (AnimationTimer)
		timer::kill(AnimationTimer);
	AnimationTimer = 0;
}

void TAnimatedBlocker::TimeoutExpired(int, void* caller)
{
	auto blocker = static_cast<TAnimatedBlocker*>(caller);
	blocker->TimeoutTimer = 0;
	control::handler(MessageCode::ControlTimerExpired, blocker);
}

void TAnimatedBlocker::AnimationStep(int, void* caller)
{
	auto blocker = static_cast<TAnimatedBlocker*>(caller);
	blocker->AnimationTimer = 0;
	if (blocker->State == blocker->TargetState)
	{
		control::handler(MessageCode::ControlAnimationDone, blocker);
		return;
	}

	// One state at a time towards the target, waiting while a ball is in the way
	if (!blocker->PinballTable->BallCountInRect(blocker->AABB))
	{
		auto next = blocker->State < blocker->TargetState ? blocker->State + 1 : blocker->State - 1;
		blocker->Message(MessageCode::TAnimatedBlockerSetState, static_cast<float>(next));
	}
	blocker->AnimationTimer = timer::set(blocker->FrameTime, blocker, AnimationStep);
}

void TAnimatedBlocker::DeferredMessage(int, void* caller)
{
	auto blocker = static_cast<TAnimatedBlocker*>(caller);
	blocker->DeferredTimer = 0;
	blocker->Message(blocker->DeferredCode, blocker->DeferredValue);
}
