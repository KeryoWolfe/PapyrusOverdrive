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

**File lookup cache.** `Game.GetFormFromFile` finds the plugin with the game's
`TESDataHandler::LookupModByName`, which walks the list of every plugin file and
compares names one by one (`_stricmp`) on every call. A name that isn't loaded,
such as a compatibility check for an optional plugin, walks the whole list every
time. Once the game's data is loaded, Papyrus Overdrive answers from a table
instead. The answers are the same. `Game.IsPluginInstalled` and the engine's own
lookups use the same function, so they get faster too. This is the same kind of
fix as Skyrim Engine Fixes' form caching, aimed at the part that costs most here.
The profiler's "us each" for `Game.GetFormFromFile` shows the difference.

**VM tweaks**, ported from Papyrus Tweaks NG. All three are in Fallout 4's
GameVM::Update, which works like Skyrim's:
- `iStackDumpTimeoutMS` (default 15000): how long the VM can stay overstressed
  before it dumps every stack to the Papyrus log. The game's 5000 makes the dump
  a hitch of its own.
- `bIgnoreMemoryLimit` (off by default): never cap script memory. The game only
  lifts the cap while the VM is overstressed.
- `bFixScriptPageAllocation` (on): stops a full memory page being handed out
  when the VM is already over the cap.

Each patch checks the bytes it replaces first, and changes nothing if they
aren't the expected ones. Papyrus Tweaks' recursion fix isn't ported, because
Fallout 4 already has a maximum stack depth (100).

**Events.** Every event sent to scripts goes through `IVirtualMachine::SendEvent`.
The profiler logs the most-sent events, and how many repeated an event already
sent to the same object within `iMergeWindowMS`. Events you list in
`sMergeEvents` are merged, meaning repeats within the window are dropped. This
is opt-in: only list an event whose scripts don't care how often it fires or
with which arguments.

**Script census.** A minute after a game loads, every 10 minutes and whenever
the game saves, it counts the script instances the game is keeping. These are
what a save stores and a load restores one by one. The log shows the scripts
with the most instances, the plugins they come from, and what grew since the
last count. Objects created in game are counted under the plugin of what they
were created from. Arrays and structs aren't counted.

**Load sampler.** While a save loads, it reads the game's main thread's call
stack every 20 ms, plus the busiest other thread's. When the load is done, it
logs where the time went: which modules and functions were seen most, both
anywhere on the stack and on top of it, and the most common stacks. This is for
loads that take minutes. It does nothing outside a load.

It doesn't shorten save loading: rebuilding the script objects stored in a save
isn't affected by how fast functions answer.

## Credit

The idea comes from Papyrus Tweaks NG's `bSpeedUpNativeCalls` for Skyrim, by
Nightfallstorm (GPL-3.0), including its one-lock rule and its excluded classes
and prefixes. Its stack dump timeout, memory limit and page allocation fixes are
ported too. Papyrus Tweaks NG patches the VM's check in code. Papyrus Overdrive
sets the function flag at bind time instead.

## Build

`xmake build -r` in this folder. The output, `PapyrusOverdrive.dll` and
`PapyrusOverdrive.pdb`, ends up in `build\windows\x64\releasedbg\`. The default mode
is releasedbg, which is optimized and comes with a .pdb. If you configured
another mode before, run `xmake f -m releasedbg` first. It needs CommonLibF4: either this project's own
copy (`git submodule update --init --recursive`, which fetches libxse/commonlibf4
into `lib\commonlibf4`) or, when that folder is missing, the one in a 4Stim
checkout next to this folder (`..\4Stim\lib\commonlibf4`).

## Install

Make a mod in MO2 with `F4SE\Plugins\PapyrusOverdrive.dll` (and `.pdb`) from the
build output, plus `Data\F4SE\Plugins\PapyrusOverdrive.ini` from this folder.
The log goes to `Documents\My Games\Fallout4\F4SE\PapyrusOverdrive.log`.

Turn it off by setting `bSpeedUpNatives=0` (profile only) or by disabling the mod.
It changes nothing in saves.
