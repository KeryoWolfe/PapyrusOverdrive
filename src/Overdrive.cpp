#include "Overdrive.h"

#include "Events.h"
#include "Settings.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Overdrive
{
	namespace
	{
		using IFunction = RE::BSScript::IFunction;
		using NativeFunctionBase = RE::BSScript::NF_util::NativeFunctionBase;
		using CallResult = IFunction::CallResult;
		using Clock = std::chrono::steady_clock;

		// IVirtualMachine::BindNativeMethod and IFunction::Call's vtable slots.
		constexpr std::size_t BIND_SLOT = 0x1B;
		constexpr std::size_t CALL_SLOT = 0x0F;

		struct Entry
		{
			RE::BSTSmartPointer<IFunction> function;  // kept alive, so its address is never reused
			std::string                    className;
			std::string                    name;
			bool                           game = false;     // bound by Fallout4.exe (its vtable is in the game's image)
			bool                           spedUp = false;   // flag set by Papyrus Overdrive
			std::atomic<std::uint64_t>     calls{ 0 };
			std::atomic<std::uint64_t>     nanos{ 0 };
			std::uint64_t                  lastCalls = 0;    // profiler thread only
			std::uint64_t                  lastNanos = 0;
		};

		using call_t = CallResult (*)(const IFunction*, const RE::BSTSmartPointer<RE::BSScript::Stack>*, RE::BSScript::ErrorLogger*,
			RE::BSScript::Internal::VirtualMachine*, bool);
		using bind_t = bool (*)(RE::BSScript::IVirtualMachine*, IFunction*);

		bind_t g_bind = nullptr;

		// Written when functions are bound (startup, and F4SE plugins at
		// their load), read by every native call.
		std::shared_mutex                                  g_lock;
		std::vector<std::unique_ptr<Entry>>                g_entries;
		std::unordered_map<const IFunction*, Entry*>       g_byFunction;
		std::unordered_map<const void*, call_t>            g_originalCall;  // per vtable

		// Sped-up functions never run at the same time as each other (Papyrus
		// Tweaks NG's AttemptFunctionCallHook does the same with one lock).
		std::recursive_mutex g_callLock;

		std::atomic<bool> g_profiling{ false };

		struct Counts
		{
			int game = 0;
			int plugin = 0;
			int alreadySafe = 0;
			int latent = 0;
			int spedUp = 0;
			int filtered = 0;
		} g_counts;

		std::uintptr_t g_imageStart = 0;
		std::uintptr_t g_imageEnd = 0;

		void FindImage()
		{
			// Fallout4.exe's mapped image: base, then the PE header's SizeOfImage.
			g_imageStart = REX::FModule::GetExecutingModule().GetBaseAddress();
			const auto ntHeaders = g_imageStart + *reinterpret_cast<const std::uint32_t*>(g_imageStart + 0x3C);
			const auto sizeOfImage = *reinterpret_cast<const std::uint32_t*>(ntHeaders + 0x18 + 0x38);
			g_imageEnd = g_imageStart + sizeOfImage;
		}

		bool InGame(const void* a_address)
		{
			const auto address = reinterpret_cast<std::uintptr_t>(a_address);
			return address >= g_imageStart && address < g_imageEnd;
		}

		bool Same(std::string_view a_lhs, std::string_view a_rhs)
		{
			return a_lhs.size() == a_rhs.size() && std::ranges::equal(a_lhs, a_rhs, [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
		}

		bool StartsWith(std::string_view a_text, std::string_view a_prefix)
		{
			return a_text.size() >= a_prefix.size() && Same(a_text.substr(0, a_prefix.size()), a_prefix);
		}

		// "Get" takes GetValue, not "Getaway": the prefix must end a word.
		bool StartsWithWord(std::string_view a_text, std::string_view a_prefix)
		{
			if (!StartsWith(a_text, a_prefix)) {
				return false;
			}
			if (a_text.size() == a_prefix.size()) {
				return true;
			}
			const auto next = static_cast<unsigned char>(a_text[a_prefix.size()]);
			return std::isupper(next) || std::isdigit(next) || next == '_';
		}

		bool Listed(const std::vector<std::string>& a_list, std::string_view a_name)
		{
			return std::ranges::any_of(a_list, [&](const std::string& a_item) { return Same(a_item, a_name); });
		}

		// Whether a not-yet-safe, non-latent function may be sped up.
		bool Allowed(const Entry& a_entry)
		{
			const auto full = a_entry.className + "." + a_entry.name;
			if (Listed(Settings::excludeFunctions, full)) {
				return false;
			}
			if (Listed(Settings::includeFunctions, full)) {
				return true;
			}
			if (!a_entry.game && !Settings::includePluginNatives) {
				return false;
			}
			if (Listed(Settings::excludeClasses, a_entry.className)) {
				return false;
			}
			if (std::ranges::any_of(Settings::excludePrefixes, [&](const std::string& a_prefix) { return StartsWith(a_entry.name, a_prefix); })) {
				return false;
			}
			return std::ranges::any_of(Settings::allowPrefixes, [&](const std::string& a_prefix) { return StartsWithWord(a_entry.name, a_prefix); });
		}

		// IFunction::Call on every native function vtable Papyrus Overdrive
		// has seen: the lock for sped-up ones, the profiler's counts.
		CallResult Call(const IFunction* a_this, const RE::BSTSmartPointer<RE::BSScript::Stack>* a_stack, RE::BSScript::ErrorLogger* a_log,
			RE::BSScript::Internal::VirtualMachine* a_vm, bool a_inTasklet)
		{
			call_t original = nullptr;
			Entry* entry = nullptr;
			{
				std::shared_lock l(g_lock);
				if (const auto it = g_originalCall.find(*reinterpret_cast<const void* const*>(a_this)); it != g_originalCall.end()) {
					original = it->second;
				}
				if (const auto it = g_byFunction.find(a_this); it != g_byFunction.end()) {
					entry = it->second;
				}
			}
			if (!entry) {
				return original(a_this, a_stack, a_log, a_vm, a_inTasklet);
			}
			const bool timed = g_profiling.load(std::memory_order_relaxed);
			const auto start = timed ? Clock::now() : Clock::time_point{};
			CallResult result;
			if (entry->spedUp) {
				std::lock_guard l(g_callLock);
				result = original(a_this, a_stack, a_log, a_vm, a_inTasklet);
			} else {
				result = original(a_this, a_stack, a_log, a_vm, a_inTasklet);
			}
			if (timed) {
				entry->calls.fetch_add(1, std::memory_order_relaxed);
				entry->nanos.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()), std::memory_order_relaxed);
			}
			return result;
		}

		bool BindNativeMethod(RE::BSScript::IVirtualMachine* a_this, IFunction* a_function)
		{
			if (!a_function) {
				return g_bind(a_this, a_function);
			}
			// Held before the bind: a failed bind may release the function.
			RE::BSTSmartPointer<IFunction> keep{ a_function };
			if (!g_bind(a_this, a_function)) {
				return false;
			}
			if (!a_function->GetIsNative()) {
				return true;
			}

			const auto vtable = *reinterpret_cast<const void* const*>(a_function);
			auto       entry = std::make_unique<Entry>();
			entry->function = std::move(keep);
			entry->className = a_function->GetObjectTypeName().c_str();
			entry->name = a_function->GetName().c_str();
			entry->game = InGame(vtable);

			std::unique_lock l(g_lock);
			++(entry->game ? g_counts.game : g_counts.plugin);
			if (a_function->CanBeCalledFromTasklets()) {
				++g_counts.alreadySafe;
			} else if (static_cast<const NativeFunctionBase*>(a_function)->isLatent) {
				++g_counts.latent;  // takes real time anyway (Wait, PathToReference...)
			} else if (Settings::speedUpNatives && Allowed(*entry)) {
				a_function->SetCallableFromTasklets(true);
				entry->spedUp = true;
				++g_counts.spedUp;
			} else {
				++g_counts.filtered;
			}

			if (entry->spedUp || Settings::profile) {
				if (!g_originalCall.contains(vtable)) {
					REL::Relocation<std::uintptr_t> table{ reinterpret_cast<std::uintptr_t>(vtable) };
					g_originalCall[vtable] = reinterpret_cast<call_t>(table.write_vfunc(CALL_SLOT, &Call));
				}
				g_byFunction[a_function] = entry.get();
			}
			g_entries.push_back(std::move(entry));
			return true;
		}

		std::string Tag(const Entry& a_entry)
		{
			if (a_entry.spedUp) {
				return "sped up";
			}
			return a_entry.function->CanBeCalledFromTasklets() ? "thread-safe" : "WAITS A FRAME";
		}

		void Profile()
		{
			for (;;) {
				std::this_thread::sleep_for(std::chrono::seconds(Settings::profileSeconds));

				struct Row
				{
					const Entry*  entry;
					std::uint64_t calls;
					std::uint64_t nanos;
				};
				std::vector<Row> rows;
				std::uint64_t    spedUp = 0, waited = 0, safe = 0;
				{
					std::shared_lock l(g_lock);
					for (const auto& entry : g_entries) {
						const auto calls = entry->calls.load(std::memory_order_relaxed);
						const auto nanos = entry->nanos.load(std::memory_order_relaxed);
						const auto dCalls = calls - entry->lastCalls;
						const auto dNanos = nanos - entry->lastNanos;
						entry->lastCalls = calls;
						entry->lastNanos = nanos;
						if (dCalls == 0) {
							continue;
						}
						rows.push_back({ entry.get(), dCalls, dNanos });
						if (entry->spedUp) {
							spedUp += dCalls;
						} else if (entry->function->CanBeCalledFromTasklets()) {
							safe += dCalls;
						} else {
							waited += dCalls;
						}
					}
				}
				if (rows.empty()) {
					Events::Report(Settings::profileSeconds);
					continue;
				}
				REX::INFO("Profiler, last {} s: {} calls sped up, {} made a script wait a frame, {} were already thread-safe", Settings::profileSeconds, spedUp, waited, safe);
				std::ranges::sort(rows, [](const Row& a, const Row& b) { return a.calls > b.calls; });
				const auto top = std::min<std::size_t>(rows.size(), static_cast<std::size_t>(Settings::profileTop));
				for (std::size_t i = 0; i < top; ++i) {
					const auto& row = rows[i];
					REX::INFO("  {:>8} calls  {:>8.1f} us each  {}.{}  [{}{}]", row.calls, static_cast<double>(row.nanos) / static_cast<double>(row.calls) / 1000.0,
						row.entry->className, row.entry->name, Tag(*row.entry), row.entry->game ? "" : ", plugin");
				}
				Events::Report(Settings::profileSeconds);
			}
		}
	}

	void Install()
	{
		FindImage();
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::BSScript__Internal__VirtualMachine[0] };
		g_bind = reinterpret_cast<bind_t>(vtbl.write_vfunc(BIND_SLOT, &BindNativeMethod));
		REX::INFO("Hooked IVirtualMachine::BindNativeMethod (game image {:X}-{:X})", g_imageStart, g_imageEnd);
	}

	void Report()
	{
		static bool reported = false;
		if (reported) {
			return;
		}
		reported = true;

		std::map<std::string, std::string> byClass;
		{
			std::shared_lock l(g_lock);
			REX::INFO("Natives bound: {} by the game, {} by plugins; {} already thread-safe, {} latent (left alone), {} sped up, {} left as they were (filters)",
				g_counts.game, g_counts.plugin, g_counts.alreadySafe, g_counts.latent, g_counts.spedUp, g_counts.filtered);
			if (g_counts.game == 0) {
				REX::WARN("No game natives were seen being bound: Papyrus Overdrive loaded after the game registered them, so nothing is sped up");
			}
			if (Settings::logSpedUp) {
				for (const auto& entry : g_entries) {
					if (entry->spedUp) {
						auto& line = byClass[entry->className];
						line += line.empty() ? entry->name : ", " + entry->name;
					}
				}
			}
		}
		for (const auto& [className, names] : byClass) {
			REX::INFO("Sped up: {}: {}", className, names);
		}

		if (Settings::profile || Settings::profileEvents) {
			g_profiling.store(Settings::profile);
			std::thread(Profile).detach();
			REX::INFO("Profiler: on every {} s ({} natives, {} events)", Settings::profileSeconds, Settings::profile ? std::to_string(Settings::profileTop) + " hottest" : "no",
				Settings::profileEvents ? std::to_string(Settings::profileEventsTop) + " most-sent" : "no");
		}
	}
}
