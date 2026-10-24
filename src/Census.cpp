#include "Census.h"

#include "Settings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Census
{
	namespace
	{
		using VM = RE::BSScript::Internal::VirtualMachine;

		std::mutex                                      g_lock;      // one census at a time
		std::unordered_map<std::string, std::size_t>    g_lastByScript;
		std::size_t                                     g_lastTotal = 0;
		std::chrono::steady_clock::time_point           g_lastTime{};
		int                                             g_number = 0;
		std::atomic<std::uint32_t>                      g_generation{ 0 };  // a new game stops the old timer

		std::string FileName(const RE::TESForm* a_form)
		{
			const auto file = a_form ? a_form->GetFile(0) : nullptr;
			return file ? std::string(file->filename) : std::string("(no plugin)");
		}

		// Where an object's instances count: its plugin, or for one created in
		// game, what it was created from.
		std::string Source(std::uint64_t a_handle)
		{
			const auto formID = static_cast<RE::TESFormID>(a_handle & 0xFFFFFFFF);
			const auto form = RE::TESForm::GetFormByID(formID);
			if (!form) {
				return std::format("(object {:08X} not loaded)", formID);
			}
			if ((formID >> 24) == 0xFF) {
				const auto ref = form->As<RE::TESObjectREFR>();
				const auto base = ref ? ref->GetObjectReference() : nullptr;
				return "created in game from " + (base ? FileName(base) : std::string("?"));
			}
			return FileName(form);
		}

		std::string Number(std::size_t a_n)
		{
			auto text = std::to_string(a_n);
			for (int i = static_cast<int>(text.size()) - 3; i > 0; i -= 3) {
				text.insert(static_cast<std::size_t>(i), ",");
			}
			return text;
		}

		std::string Delta(long long a_n)
		{
			return a_n == 0 ? std::string("=") : (a_n > 0 ? "+" : "-") + Number(static_cast<std::size_t>(a_n > 0 ? a_n : -a_n));
		}

		template <class Map>
		auto Top(const Map& a_map, std::size_t a_n)
		{
			std::vector<std::pair<std::string, std::size_t>> rows(a_map.begin(), a_map.end());
			std::ranges::sort(rows, [](const auto& a, const auto& b) { return a.second > b.second; });
			rows.resize(std::min(rows.size(), a_n));
			return rows;
		}
	}

	void Run(const std::string& a_why)
	{
		if (!Settings::censusEnabled) {
			return;
		}
		const auto vm = VM::GetSingleton();
		if (!vm) {
			return;
		}
		std::scoped_lock lock(g_lock);

		// The table, copied under the VM's lock: each instance's object handle
		// and script type. Names and plugins are worked out after.
		struct Row
		{
			std::uint64_t                         handle;
			const RE::BSScript::ObjectTypeInfo*   type;
		};
		std::vector<Row> rows;
		std::size_t      objects = 0;
		{
			RE::BSAutoLock tableLock{ vm->attachedScriptsLock };
			rows.reserve(vm->attachedScripts.size() * 2);
			for (const auto& [handle, scripts] : vm->attachedScripts) {
				++objects;
				for (const auto& script : scripts) {
					const auto object = script.get();
					rows.push_back({ handle, object ? object->GetTypeInfo() : nullptr });
				}
			}
		}

		std::unordered_map<std::string, std::size_t>   byScript;
		std::unordered_map<std::string, std::size_t>   byPlugin;
		std::unordered_map<std::uint64_t, std::string> sources;  // per object, worked out once
		for (const auto& row : rows) {
			const char* name = row.type ? row.type->GetName() : nullptr;
			++byScript[name && *name ? name : "(unknown)"];
			auto it = sources.find(row.handle);
			if (it == sources.end()) {
				it = sources.emplace(row.handle, Source(row.handle)).first;
			}
			++byPlugin[it->second];
		}

		++g_number;
		const auto now = std::chrono::steady_clock::now();
		const auto total = rows.size();
		const auto n = static_cast<std::size_t>(Settings::censusTop);
		std::string since;
		if (g_number > 1) {
			since = std::format(", {} since census #{} {:.0f} min ago", Delta(static_cast<long long>(total) - static_cast<long long>(g_lastTotal)), g_number - 1,
				std::chrono::duration<double>(now - g_lastTime).count() / 60.0);
		}
		REX::INFO("Script census #{} ({}): {} script instances on {} objects, {} scripts{}", g_number, a_why, Number(total), Number(objects), Number(byScript.size()),
			since);

		REX::INFO("  Scripts with the most instances:");
		for (const auto& [name, count] : Top(byScript, n)) {
			const auto last = g_lastByScript.find(name);
			const auto change = g_number > 1 ? "  (" + Delta(static_cast<long long>(count) - static_cast<long long>(last != g_lastByScript.end() ? last->second : 0)) + ")" : std::string{};
			REX::INFO("    {:>9}  {}{}", Number(count), name, change);
		}
		REX::INFO("  By plugin (of the object; objects created in game by what they were created from):");
		for (const auto& [plugin, count] : Top(byPlugin, n)) {
			REX::INFO("    {:>9}  {}", Number(count), plugin);
		}
		if (g_number > 1) {
			std::unordered_map<std::string, std::size_t> grew;
			for (const auto& [name, count] : byScript) {
				const auto last = g_lastByScript.find(name);
				const auto before = last != g_lastByScript.end() ? last->second : 0;
				if (count > before) {
					grew[name] = count - before;
				}
			}
			if (!grew.empty()) {
				REX::INFO("  Grew the most since census #{}:", g_number - 1);
				for (const auto& [name, count] : Top(grew, std::min<std::size_t>(n, 15))) {
					REX::INFO("    {:>9}  {}", "+" + Number(count), name);
				}
			}
		}
		g_lastByScript = std::move(byScript);
		g_lastTotal = total;
		g_lastTime = now;
	}

	void GameStarted()
	{
		if (!Settings::censusEnabled) {
			return;
		}
		const auto generation = ++g_generation;
		std::thread([generation]() {
			// A minute in: scripts that start on load have had their go.
			std::this_thread::sleep_for(std::chrono::seconds(60));
			if (g_generation.load() != generation) {
				return;
			}
			Run("a minute after the game loaded");
			const auto interval = std::chrono::minutes(Settings::censusMinutes);
			while (Settings::censusMinutes > 0) {
				std::this_thread::sleep_for(interval);
				if (g_generation.load() != generation) {
					return;
				}
				Run(std::format("every {} min", Settings::censusMinutes));
			}
		}).detach();
	}
}
