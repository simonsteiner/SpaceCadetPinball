#include "pch.h"
#include "timer.h"

#include "pb.h"

int timer::SetCount;
timer_struct* timer::ActiveList;
int timer::MaxCount;
int timer::Count;
timer_struct* timer::FreeList;
timer_struct* timer::TimerBuffer;

// Allocates count timers and links them all into the free list; returns 0, or 1 if there is no buffer
int timer::init(int count)
{
	auto buf = new timer_struct[count];
	TimerBuffer = buf;
	if (!buf)
		return 1;
	Count = 0;
	MaxCount = count;
	SetCount = 1;

	for (int index = 0; index < count - 1; index++)
		buf[index].NextTimer = &buf[index + 1];
	buf[count - 1].NextTimer = nullptr;

	ActiveList = nullptr;
	FreeList = buf;
	return 0;
}

void timer::uninit()
{
	delete[] TimerBuffer;
	TimerBuffer = nullptr;
}

// Removes the active timer with this id without calling it; returns the id, or 0 if no such timer is active
int timer::kill(int timerId)
{
	timer_struct* current = ActiveList;
	int index = 0;
	timer_struct* prev = nullptr;
	if (Count <= 0)
		return 0;
	while (timerId != current->TimerId)
	{
		++index;
		prev = current;
		current = current->NextTimer;
		if (index >= Count)
			return 0;
	}
	if (prev)
		prev->NextTimer = current->NextTimer;
	else
		ActiveList = current->NextTimer;

	--Count;
	current->NextTimer = FreeList;
	FreeList = current;
	return timerId;
}

// Removes active timers with this callback without calling them and returns how many it removed. The walk gets one
// step shorter with each removal, as in CADET.EXE, so a match near the end of the list can stay active
int timer::kill(void(* callback)(int, void*))
{
	auto count = 0;
	timer_struct* current = ActiveList, * prev = nullptr;
	for (auto index = 0; index < Count; index++)
	{
		if (current->Callback == callback)
		{
			// Keep the next timer before this one joins the free list, as CADET.EXE does (0x41f960). Like it, the
			// index still steps after a removal, so a match right after another is skipped.
			auto next = current->NextTimer;
			count++;
			if (prev)
				prev->NextTimer = next;
			else
				ActiveList = next;
			current->NextTimer = FreeList;
			FreeList = current;
			if (--Count == index)
				break;
			current = next;
		}
		else
		{
			prev = current;
			current = current->NextTimer;
		}
	}

	return count;
}

// Sets a timer that calls callback with its id and caller after time seconds of game time, behind those already due
// at the same tick. Returns the timer's id, which is never 0, or 0 when all timers are in use
int timer::set(float time, void* caller, void (* callback)(int, void*))
{
	if (Count >= MaxCount)
		return 0;

	auto timer = FreeList;
	FreeList = timer->NextTimer;
	timer->NextTimer = nullptr;

	auto prev = ActiveList;
	auto current = ActiveList;

	auto targetTime = pb::time_ticks + static_cast<int>(time * 1000.0f);
	for (int index = 0; index < Count && targetTime >= current->TargetTime; ++index)
	{
		prev = current;
		current = current->NextTimer;
	}

	if (current != prev)
	{
		timer->NextTimer = prev->NextTimer;
		prev->NextTimer = timer;
	}
	else
	{
		timer->NextTimer = ActiveList;
		ActiveList = timer;
	}

	timer->Caller = caller;
	timer->Callback = callback;
	timer->TimerId = SetCount;
	timer->TargetTime = targetTime;
	Count++;

	SetCount++;
	if (SetCount <= 0)
		SetCount = 1;
	return timer->TimerId;
}

// Milliseconds until the timer expires, or -1 if it isn't set
int timer::time_left(int timerId)
{
	auto current = ActiveList;
	for (int index = 0; index < Count; ++index, current = current->NextTimer)
	{
		if (current->TimerId == timerId)
			return std::max(current->TargetTime - pb::time_ticks, 0);
	}
	return -1;
}

// Calls and frees the timers that are due, in order: at most two, and after those only timers at least 100 ms
// overdue. Returns how many it freed, those without a callback too
int timer::check()
{
	timer_struct* current = ActiveList;
	int index = 0;
	if (ActiveList)
	{
		while (pb::time_ticks >= current->TargetTime)
		{
			--Count;
			// Advance active list, move current to free
			ActiveList = current->NextTimer;
			current->NextTimer = FreeList;
			FreeList = current;
			if (current->Callback != nullptr)
				current->Callback(current->TimerId, current->Caller);

			current = ActiveList;
			++index;
			if (index > 1)
				break;
			if (!current)
				return index;
		}
		while (current && pb::time_ticks >= current->TargetTime + 100)
		{
			--Count;
			ActiveList = current->NextTimer;
			current->NextTimer = FreeList;
			FreeList = current;
			if (current->Callback != nullptr)
				current->Callback(current->TimerId, current->Caller);

			current = ActiveList;
			++index;
		}
	}
	return index;
}
