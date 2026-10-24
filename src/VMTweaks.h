#pragma once

// Papyrus Tweaks NG's VM tweaks (Skyrim, by Nightfallstorm), found in Fallout
// 4's GameVM::Update (1.11.240: ID 2251364), which works like Skyrim's
// (SkyrimVM, IDs 53195/54006):
//
// - Stack dumps: once the VM has been overstressed for 5000 ms (the
//   "add eax, 1388h" at +0x127), it dumps every stack to the Papyrus log, and
//   again every 60 s while it stays overstressed. With a big load order that
//   dump is a hitch of its own. iStackDumpTimeoutMS changes the 5000 (0: never
//   dump, by making the "not yet" branch at +0x12E always taken).
// - Memory limit: GameVM::Update sets its memory page policy's
//   ignoreMemoryLimit (GameVM +0xC8 + 0x18) when the VM is overstressed and
//   clears it when it isn't (+0x191: mov byte ptr [rdi+0E0h], r12b), so the
//   cap (iMaxAllocatedMemoryBytes) only holds while things are calm.
//   bIgnoreMemoryLimit writes 1 there instead: never capped.
// - Page allocation: SimpleAllocMemoryPagePolicy::GetLargestAvailablePage
//   (vtable slot 4) works out the room left as max - current, unsigned, so
//   when the VM is over the cap (allowed while overstressed) the subtraction
//   wraps and it hands out a full page anyway. bFixScriptPageAllocation makes
//   it see no room, as Papyrus Tweaks NG's fix does.
//
// Each code patch first checks the bytes it replaces; if they aren't the
// expected ones (another game version, or another plugin got there first) it
// changes nothing and says so in the log.
//
// Not ported: Papyrus Tweaks NG's recursion fix. Fallout 4 already stops
// scripts at a maximum stack depth (100, "Maximum stack depth" in the Papyrus
// log); Skyrim has no such limit.
namespace VMTweaks
{
	// At plugin load.
	void Install();
}
