#include "pch.h"
#include "Rumble.h"

#include "options.h"
#include "winmain.h"

void Rumble::Flipper()
{
	Pulse(0x3000, 0x1800, 50);
}

void Rumble::Bumper()
{
	Pulse(0x7000, 0xA000, 90);
}

void Rumble::Pulse(Uint16 lowFrequency, Uint16 highFrequency, Uint32 durationMs)
{
	// Not while the table plays itself
	if (!options::Options.Rumble || winmain::DemoActive)
		return;

	int count = 0;
	auto ids = SDL_GetGamepads(&count);
	for (int index = 0; ids && index < count; index++)
	{
		if (auto gamepad = SDL_GetGamepadFromID(ids[index]))
			SDL_RumbleGamepad(gamepad, lowFrequency, highFrequency, durationMs);
	}
	SDL_free(ids);
	SDL_ClearError();
}
