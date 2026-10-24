#include "VMTweaks.h"

#include "Settings.h"

#include <array>
#include <cstring>
#include <span>

namespace VMTweaks
{
	namespace
	{
		constexpr REL::ID GAMEVM_UPDATE{ 2251364 };

		// Replaces a_expected at a_id + a_offset with a_new (same length), if
		// a_expected is what's there.
		bool Patch(REL::ID a_id, std::ptrdiff_t a_offset, std::span<const std::uint8_t> a_expected, std::span<const std::uint8_t> a_new, const char* a_what)
		{
			const auto address = REL::Relocation<std::uintptr_t>{ a_id }.address() + a_offset;
			if (a_expected.size() != a_new.size() || std::memcmp(reinterpret_cast<const void*>(address), a_expected.data(), a_expected.size()) != 0) {
				REX::WARN("VM: {}: the game's code isn't as expected (another version, or another plugin patched it); left alone", a_what);
				return false;
			}
			REL::WriteSafe(address, a_new.data(), a_new.size());
			return true;
		}

		void StackDumpTimeout()
		{
			const int timeout = Settings::stackDumpTimeoutMS;
			if (timeout < 0) {
				return;
			}
			if (timeout == 0) {
				// jae (not overstressed long enough yet) -> jmp: never dump.
				constexpr std::array<std::uint8_t, 2> expected{ 0x73, 0x29 };
				constexpr std::array<std::uint8_t, 2> always{ 0xEB, 0x29 };
				if (Patch(GAMEVM_UPDATE, 0x12E, expected, always, "stack dump timeout")) {
					REX::INFO("VM: overstressed stacks are never dumped to the Papyrus log");
				}
				return;
			}
			// add eax, 1388h (5000 ms)
			constexpr std::array<std::uint8_t, 5> expected{ 0x05, 0x88, 0x13, 0x00, 0x00 };
			const auto                            ms = static_cast<std::uint32_t>(std::min(timeout, 50'000'000));
			const std::array<std::uint8_t, 5>     patched{ 0x05, static_cast<std::uint8_t>(ms), static_cast<std::uint8_t>(ms >> 8), static_cast<std::uint8_t>(ms >> 16),
                    static_cast<std::uint8_t>(ms >> 24) };
			if (Patch(GAMEVM_UPDATE, 0x127, expected, patched, "stack dump timeout")) {
				REX::INFO("VM: stacks are dumped after {} ms overstressed (the game's: 5000)", ms);
			}
		}

		void IgnoreMemoryLimit()
		{
			if (!Settings::ignoreMemoryLimit) {
				return;
			}
			// mov byte ptr [rdi+0E0h], r12b  ->  mov byte ptr [rdi+0E0h], 1
			constexpr std::array<std::uint8_t, 7> expected{ 0x44, 0x88, 0xA7, 0xE0, 0x00, 0x00, 0x00 };
			constexpr std::array<std::uint8_t, 7> patched{ 0xC6, 0x87, 0xE0, 0x00, 0x00, 0x00, 0x01 };
			if (Patch(GAMEVM_UPDATE, 0x191, expected, patched, "ignore memory limit")) {
				REX::INFO("VM: script memory is never capped (iMaxAllocatedMemoryBytes no longer applies)");
			}
		}

		using largest_t = std::int32_t (*)(RE::BSScript::SimpleAllocMemoryPagePolicy*, void*);
		largest_t g_largest = nullptr;

		// SimpleAllocMemoryPagePolicy::GetLargestAvailablePage, as Papyrus
		// Tweaks NG's FixScriptPageAllocation: over the cap, the original sees
		// current == max (no room: out of memory) instead of a wrapped
		// subtraction.
		std::int32_t GetLargestAvailablePage(RE::BSScript::SimpleAllocMemoryPagePolicy* a_this, void* a_newPage)
		{
			// The game's lock is re-entrant for the thread holding it, as the
			// original takes it again.
			a_this->dataLock.lock();
			const auto max = a_this->maxAllocatedMemory;
			const auto current = a_this->currentMemorySize;
			const bool over = current > max;
			if (over) {
				a_this->currentMemorySize = max;
			}
			const auto result = g_largest(a_this, a_newPage);
			if (over) {
				// What the original allocated (nothing, when it saw no room) on top
				// of where it really was.
				a_this->currentMemorySize = current + (a_this->currentMemorySize - max);
			}
			a_this->dataLock.unlock();
			return result;
		}

		void FixPageAllocation()
		{
			if (!Settings::fixScriptPageAllocation) {
				return;
			}
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::BSScript__SimpleAllocMemoryPagePolicy[0] };
			g_largest = reinterpret_cast<largest_t>(vtbl.write_vfunc(4, &GetLargestAvailablePage));
			REX::INFO("VM: script page allocation fixed (no page handed out over the memory cap)");
		}
	}

	void Install()
	{
		StackDumpTimeout();
		IgnoreMemoryLimit();
		FixPageAllocation();
	}
}
