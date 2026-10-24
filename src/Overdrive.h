#pragma once

// Papyrus Overdrive: lets read-only engine functions answer scripts at once
// instead of a frame later, and profiles native calls.
//
// Why scripts wait: a native function not marked "callable from tasklets"
// can't run on the script threads. A script that calls one is suspended and
// the call is queued for the main thread's next Papyrus update
// (VirtualMachine::AttemptFunctionCall, 1.11.240 at +0x2124F6C: if called from
// a tasklet and !IFunction::CanBeCalledFromTasklets(), queue it), so every
// such call costs the script about a frame. A loop of 500 GetFormFromFile
// calls takes 500 trips through that queue.
//
// What this does (the idea is Papyrus Tweaks NG's "bSpeedUpNativeCalls" for
// Skyrim, by Nightfallstorm, done differently): every native the game binds
// passes through IVirtualMachine::BindNativeMethod (vtable slot 0x1B; the game
// makes ~950 such calls, all through the vtable). Once everything is bound
// (kGameDataReady: the game marks its own thread-safe natives with
// SetCallableFromTasklets right after binding them), for a game function whose
// name matches the INI's read-only filters and isn't latent, its
// NativeFunctionBase::isCallableFromTasklet flag (+0x41, what slot 0x13 returns
// on all 456 of the game's native function vtables) is set, so the VM calls it
// right away on the script thread. Papyrus Tweaks NG instead patches the check
// inside the VM's code; setting the flag needs no code patch and no
// per-version offsets.
//
// Safety, as Papyrus Tweaks NG's: sped-up functions never run at the same time
// as each other (one lock around their Call, through their vtables' slot
// 0x0F). They can still run while the main thread changes the game, which is
// why only reads are allowed by default.
namespace Overdrive
{
	// Hooks BindNativeMethod. At plugin load, before the game binds its natives.
	void Install();

	// Logs what was seen and sped up; starts the profiler. At kGameDataReady.
	void Report();
}
