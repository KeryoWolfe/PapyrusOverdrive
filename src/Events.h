#pragma once

#include <string>

// Script events: counted for the profiler, and optionally merged.
//
// Every event the game sends to scripts (OnItemAdded, OnCellLoad, OnHit,
// timers...) goes through IVirtualMachine::SendEvent (vtable slot 0x2B; the
// game's 255 calls to it are all through the vtable). Each one starts a
// script call on every script attached to the target that handles it, so a
// burst of the same event on the same object, from many mods listening,
// is a lot of VM work.
//
// The profiler logs which events were sent most, and how many repeated an
// event already sent to the same object within iMergeWindowMS (what merging
// would drop). Events listed in sMergeEvents are merged: a repeat within the
// window is dropped. Off by default: only list an event when every script
// that handles it only cares that it happened, not how many times or with
// which arguments (merging ignores the arguments). Events someone is waiting
// on (sent with a callback) are never merged.
namespace Events
{
	// Hooks SendEvent. At plugin load.
	void Install();

	// The profiler's event section for the last interval; resets the counts.
	void Report(int a_seconds);
}
