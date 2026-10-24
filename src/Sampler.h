#pragma once

#include <cstdint>

// Load sampler: where the game spends a save load.
//
// From the moment a save starts loading (F4SE's kPreLoadGame) until it's
// loaded (kPostLoadGame), it stops the game's main thread every
// iSampleMS milliseconds, reads its call stack, and lets it go; once a
// second it also does the same to whichever other thread used the most CPU
// in the last second. When the load ends, the functions seen most are
// logged: by module and offset ("Fallout4.exe+0x1234567"), how often each
// was anywhere on the stack (inclusive) and on top of it (self), and the
// most common stacks. A save load that takes minutes then says where those
// minutes went.
//
// It reads stacks the way profilers do (SuspendThread, GetThreadContext,
// RtlVirtualUnwind). It does nothing outside a save load, and changes
// nothing in the game.
namespace Sampler
{
	// The thread F4SE loads plugins on: the game's main thread.
	void SetMainThread(std::uint32_t a_threadID);

	// kPreLoadGame: start sampling. kPostLoadGame / kNewGame: stop and log.
	void Start();
	void Stop();
}
