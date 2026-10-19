#pragma once

// Gamepad rumble on table events. It only shakes the controllers; nothing in the table depends on it.
class Rumble
{
public:
	static void Flipper();
	static void Bumper();

private:
	static void Pulse(Uint16 lowFrequency, Uint16 highFrequency, Uint32 durationMs);
};
