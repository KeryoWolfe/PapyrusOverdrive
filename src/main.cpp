#include "Events.h"
#include "FileLookup.h"
#include "Overdrive.h"
#include "Settings.h"
#include "VMTweaks.h"

namespace
{
	void OnMessage(F4SE::MessagingInterface::Message* a_message)
	{
		if (a_message && a_message->type == F4SE::MessagingInterface::kGameDataReady) {
			FileLookup::Build();
			Overdrive::Report();
		}
	}
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se, { .trampoline = true, .trampolineSize = 64 });

	REX::INFO("Papyrus Overdrive {}.{}.{}: game {}, F4SE {}", PAPYRUSOVERDRIVE_VERSION_MAJOR, PAPYRUSOVERDRIVE_VERSION_MINOR, PAPYRUSOVERDRIVE_VERSION_PATCH,
		a_f4se->RuntimeVersion().string(), a_f4se->F4SEVersion().string());

	Settings::Load();
	// Before the game binds its natives (it does once plugins are loaded).
	Overdrive::Install();
	FileLookup::Install();
	VMTweaks::Install();
	Events::Install();
	F4SE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
