#pragma once

#include <string>

// Script census: what's filling the save with script instances.
//
// Every script instance the VM has bound to something in the game (a
// reference, an actor, a quest, an alias, a magic effect...) is in its table
// of attached scripts (VirtualMachine::attachedScripts, handle -> scripts).
// Those are the "instances" a save stores, ReSaver's count, and each one is
// restored, one by one, when the save loads. The census walks that table and
// logs:
//
//   - how many instances there are, and on how many objects;
//   - the scripts with the most instances, and the change since the last
//     census;
//   - the plugins they come from: the plugin of the object the script is on,
//     or for an object created in game (an FF form, PlaceAtMe), the plugin of
//     what it was created from, since those are the instances that pile up;
//   - the scripts that grew the most since the last census.
//
// Run when a game is loaded (once its scripts have started), every
// iIntervalMinutes, and as the game saves. Arrays and structs aren't in the
// table (they hang off instances' variables), so they aren't counted.
namespace Census
{
	// a_why: what prompted it ("game loaded", "saving", "every 10 min").
	void Run(const std::string& a_why);

	// kPostLoadGame / kNewGame: a census in a minute, then every interval.
	void GameStarted();
}
