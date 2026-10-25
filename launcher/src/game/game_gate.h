// game_gate.h - the handshake that lets the loader hold the game until the user says PLAY.
//
// The loader (version.dll) stops the game's startup inside DllMain and waits on a named event.
// Nothing happens in the game - no window, no renderer - until this launcher signals it. That
// is why the gate MUST be released before waiting for the game to become ready, and on every
// path out of the launcher, including "the user closed the window without pressing PLAY".
#pragma once

namespace oreo {

// Remembers which game process is being held (0 = we were started by hand, no gate exists).
void gate_init(unsigned long game_pid);

// Lets the game continue. Safe to call any number of times; only the first call does anything.
void gate_release(const char* reason);

// Tells the loader NOT to start the game at all (the user closed the manager instead of
// pressing PLAY). The game process ends before it has drawn anything.
void gate_cancel(const char* reason);

bool gate_released();

}  // namespace oreo
