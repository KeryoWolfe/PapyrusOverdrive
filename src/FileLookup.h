#pragma once

// Plugin lookup by name, cached.
//
// Game.GetFormFromFile (1.11.240: the native at +0x11226B0) finds the plugin
// with TESDataHandler::LookupModByName (ID 4476758). That walks the data
// handler's whole list of plugin files (TESDataHandler::files, +0xFB0) and
// _stricmp's each name, on every call: a few hundred string compares per
// GetFormFromFile in a big load order, and settlement frameworks make
// hundreds of those calls a frame. A name that isn't loaded (a compatibility
// check for an optional plugin) walks the whole list every time.
//
// Papyrus Overdrive replaces LookupModByName with a lookup in a table of
// every file, built once the game's data is loaded (the list doesn't change
// after that). Before then, and for names not in the table, it walks the list
// as the game does. The answers are the same; Game.IsPluginInstalled and the
// engine's own lookups get faster too.
//
// Skyrim's Engine Fixes caches form lookups ("bFormCaching"); this is the same
// kind of fix aimed at the part of GetFormFromFile that costs the most here.
namespace FileLookup
{
	// Replaces LookupModByName. At plugin load.
	void Install();

	// Builds the table. At kGameDataReady, on the main thread.
	void Build();
}
