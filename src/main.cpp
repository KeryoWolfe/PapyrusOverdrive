#include "Events.h"
#include "FileLookup.h"
#include "Overdrive.h"
#include "Sampler.h"
#include "Settings.h"
#include "VMTweaks.h"

namespace
{
	void OnMessage(F4SE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}
		switch (a_message->type) {
		case F4SE::MessagingInterface::kGameDataReady:
			FileLookup::Build();
			Overdrive::Report();
			break;
		case F4SE::MessagingInterface::kPreLoadGame:
			Sampler::Start();
			break;
		case F4SE::MessagingInterface::kPostLoadGame:
		case F4SE::MessagingInterface::kNewGame:
			Sampler::Stop();
			break;
		default:
			break;
		}
	}
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se, { .trampoline = true, .trampolineSize = 64 });

	REX::INFO("Papyrus Overdrive {}.{}.{}: game {}, F4SE {}", PAPYRUSOVERDRIVE_VERSION_MAJOR, PAPYRUSOVERDRIVE_VERSION_MINOR, PAPYRUSOVERDRIVE_VERSION_PATCH,
		a_f4se->RuntimeVersion().string(), a_f4se->F4SEVersion().string());

	Settings::Load();
	Sampler::SetMainThread(REX::W32::GetCurrentThreadId());  // F4SE loads plugins on the game's main thread
	// Before the game binds its natives (it does once plugins are loaded).
	Overdrive::Install();
	FileLookup::Install();
	VMTweaks::Install();
	Events::Install();
	F4SE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
