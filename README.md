# Papyrus Overdrive

An F4SE plugin for Fallout 4 AE (1.11.x) that makes heavy script load run
faster. It's a standalone project, not part of 4Stim, and works with any load
order.

## What it does

Scripts run on their own threads. Most engine functions they call
(`GetValue`, `HasKeyword`, `Game.GetFormFromFile`...) aren't marked safe for those
threads, so the VM pauses the script and queues the call for the main thread's
next Papyrus update (VirtualMachine::AttemptFunctionCall). Each such call costs
the script about a frame. A Workshop Framework loop of 500 `GetFormFromFile`
calls takes 500 trips through that queue, which is the backlog that builds up in
big settlement load orders.

Papyrus Overdrive marks the game's **read-only** functions as safe for the script
threads, so they answer at once:

- Every native the game binds goes through `IVirtualMachine::BindNativeMethod`
  (vtable slot 0x1B), which the plugin hooks at load. For each game function that
  isn't latent and passes the INI's filters, it sets the function's
  `isCallableFromTasklet` flag (NativeFunctionBase +0x41). That flag is what
  `CanBeCalledFromTasklets` returns on all 456 of the game's native function
  vtables. It isn't a code patch, so there are no per-version offsets.
- Sped-up functions never run at the same time as each other: there's one lock
  around their `Call` (vtable slot 0x0F).
- They can still run while the main thread is changing the game. That's why the
  default filters allow reads only (`Get`/`Is`/`Has`/`Can`/`Was` plus a capital
  letter), and exclude UI, Input, Debug, Utility, Quest and Scene, plus
  animation-graph and quest reads.
- Functions added by F4SE plugins are left alone unless `bIncludePluginNatives=1`.

The **profiler** (on by default) logs the most-called natives once a minute: how
many calls, how long each took, and whether each was sped up, already
thread-safe, or **WAITS A FRAME**. Reads in that last group are the next
candidates for `sIncludeFunctions`.

It doesn't shorten save loading: rebuilding the script objects stored in a save
isn't affected by how fast functions answer.

## Credit

The idea comes from Papyrus Tweaks NG's `bSpeedUpNativeCalls` for Skyrim, by
Nightfallstorm (GPL-3.0), including its one-lock rule and its excluded classes
and prefixes. Papyrus Tweaks NG patches the VM's check in code. Papyrus Overdrive
sets the function flag at bind time instead.

## Build

`xmake build -r` in this folder. It needs CommonLibF4: either this project's own
copy (`git submodule update --init --recursive`, which fetches libxse/commonlibf4
into `lib\commonlibf4`) or, when that folder is missing, the one in a 4Stim
checkout next to this folder (`..\4Stim\lib\commonlibf4`).

## Install

Make a mod in MO2 with `F4SE\Plugins\PapyrusOverdrive.dll` (and `.pdb`) from the
build output, plus `Data\F4SE\Plugins\PapyrusOverdrive.ini` from this folder.
The log goes to `Documents\My Games\Fallout4\F4SE\PapyrusOverdrive.log`.

Turn it off by setting `bSpeedUpNatives=0` (profile only) or by disabling the mod.
It changes nothing in saves.
