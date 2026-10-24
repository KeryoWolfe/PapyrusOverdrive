#include "Events.h"

#include "Settings.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Events
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		// IVirtualMachine::SendEvent(handle, event name, arguments functor,
		// filter functor, callback); the functors and callback aren't read here.
		constexpr std::size_t SEND_EVENT_SLOT = 0x2B;
		using send_t = void (*)(RE::BSScript::IVirtualMachine*, std::uint64_t, const RE::BSFixedString*, const void*, const void*, const void*);
		send_t g_send = nullptr;

		struct Key
		{
			std::uint64_t handle;
			const char*   name;  // the string pool's entry: one per distinct name
			bool          operator==(const Key&) const = default;
		};
		struct KeyHash
		{
			std::size_t operator()(const Key& a_key) const noexcept
			{
				return std::hash<std::uint64_t>{}(a_key.handle) ^ (std::hash<const void*>{}(a_key.name) * 31);
			}
		};

		struct Count
		{
			std::uint64_t sent = 0;
			std::uint64_t repeats = 0;  // within the window, to the same object
			std::uint64_t merged = 0;   // dropped
		};

		std::mutex                                     g_lock;
		std::unordered_map<Key, Clock::time_point, KeyHash> g_lastSent;
		std::unordered_map<const char*, Count>         g_counts;
		std::unordered_set<const char*>                g_merge;  // pool entries of sMergeEvents' names
		std::unordered_set<const char*>                g_notMerged;  // pool entries already checked, not listed
		Clock::time_point                              g_lastPrune = Clock::now();

		bool Listed(const char* a_name)
		{
			if (g_merge.contains(a_name)) {
				return true;
			}
			if (g_notMerged.contains(a_name)) {
				return false;
			}
			const std::string_view name{ a_name };
			const bool             listed = std::ranges::any_of(Settings::mergeEvents, [&](const std::string& a_item) {
                return a_item.size() == name.size() &&
                       std::ranges::equal(a_item, name, [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
            });
			(listed ? g_merge : g_notMerged).insert(a_name);
			return listed;
		}

		void SendEvent(RE::BSScript::IVirtualMachine* a_this, std::uint64_t a_handle, const RE::BSFixedString* a_name, const void* a_args, const void* a_filter,
			const void* a_callback)
		{
			const char* name = a_name ? a_name->c_str() : nullptr;
			if (!name || !*name) {
				return g_send(a_this, a_handle, a_name, a_args, a_filter, a_callback);
			}
			const bool hasCallback = a_callback && *reinterpret_cast<void* const*>(a_callback) != nullptr;
			const auto now = Clock::now();
			const auto window = std::chrono::milliseconds(Settings::mergeWindowMS);
			bool       drop = false;
			{
				std::lock_guard l(g_lock);
				auto&           count = g_counts[name];
				++count.sent;
				const Key  key{ a_handle, name };
				const auto it = g_lastSent.find(key);
				const bool repeat = it != g_lastSent.end() && now - it->second < window;
				if (repeat) {
					++count.repeats;
					if (!hasCallback && Listed(name)) {
						++count.merged;
						drop = true;
					}
				}
				if (!drop) {
					g_lastSent[key] = now;
				}
				// Forget objects not sent anything for a while.
				if (now - g_lastPrune > std::chrono::seconds(10)) {
					g_lastPrune = now;
					std::erase_if(g_lastSent, [&](const auto& a_entry) { return now - a_entry.second > window * 10; });
				}
			}
			if (!drop) {
				g_send(a_this, a_handle, a_name, a_args, a_filter, a_callback);
			}
		}
	}

	void Install()
	{
		if (!Settings::profileEvents && Settings::mergeEvents.empty()) {
			return;
		}
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::BSScript__Internal__VirtualMachine[0] };
		g_send = reinterpret_cast<send_t>(vtbl.write_vfunc(SEND_EVENT_SLOT, &SendEvent));
		if (Settings::mergeEvents.empty()) {
			REX::INFO("Events: counted for the profiler; none merged");
		} else {
			std::string list;
			for (const auto& name : Settings::mergeEvents) {
				list += list.empty() ? name : ", " + name;
			}
			REX::INFO("Events: repeats within {} ms to the same object are merged for: {}", Settings::mergeWindowMS, list);
		}
	}

	void Report(int a_seconds)
	{
		if (!g_send || !Settings::profileEvents) {
			return;
		}
		std::vector<std::pair<const char*, Count>> rows;
		{
			std::lock_guard l(g_lock);
			rows.assign(g_counts.begin(), g_counts.end());
			g_counts.clear();
		}
		if (rows.empty()) {
			return;
		}
		std::uint64_t sent = 0, repeats = 0, merged = 0;
		for (const auto& [name, count] : rows) {
			sent += count.sent;
			repeats += count.repeats;
			merged += count.merged;
		}
		REX::INFO("Events, last {} s: {} sent to scripts, {} were repeats to the same object within {} ms, {} merged", a_seconds, sent, repeats,
			Settings::mergeWindowMS, merged);
		std::ranges::sort(rows, [](const auto& a, const auto& b) { return a.second.sent > b.second.sent; });
		const auto top = std::min<std::size_t>(rows.size(), static_cast<std::size_t>(Settings::profileEventsTop));
		for (std::size_t i = 0; i < top; ++i) {
			const auto& [name, count] = rows[i];
			REX::INFO("  {:>8} sent  {:>8} repeats  {:>8} merged  {}", count.sent, count.repeats, count.merged, name);
		}
	}
}
