#include "FileLookup.h"

#include "Settings.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>

namespace FileLookup
{
	namespace
	{
		// TESDataHandler::LookupModByName(const char*): the file named so (any
		// case), loaded or not, or none.
		constexpr REL::ID LOOKUP_MOD_BY_NAME{ 4476758 };

		// Its first instruction on 1.11.240 (mov [rsp+8], rbx): if it's
		// something else, another plugin has hooked it, and it's left alone.
		constexpr std::array<std::uint8_t, 5> EXPECTED_START{ 0x48, 0x89, 0x5C, 0x24, 0x08 };

		// Case-insensitive, and looked up by string_view: no copy per call.
		struct NoCaseHash
		{
			using is_transparent = void;
			std::size_t operator()(std::string_view a_name) const noexcept
			{
				std::size_t hash = 14695981039346656037ULL;  // FNV-1a
				for (const unsigned char c : a_name) {
					hash = (hash ^ static_cast<unsigned char>(std::tolower(c))) * 1099511628211ULL;
				}
				return hash;
			}
		};
		struct NoCaseEqual
		{
			using is_transparent = void;
			bool operator()(std::string_view a_lhs, std::string_view a_rhs) const noexcept
			{
				return a_lhs.size() == a_rhs.size() &&
				       std::ranges::equal(a_lhs, a_rhs, [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
			}
		};

		// Name -> file. Written once by Build, before g_ready is set; only
		// read after.
		std::unordered_map<std::string, RE::TESFile*, NoCaseHash, NoCaseEqual> g_byName;
		std::atomic<bool>                                                      g_ready{ false };

		// As the game's: the list in order, case-insensitive.
		RE::TESFile* Walk(RE::TESDataHandler* a_handler, const char* a_name)
		{
			for (const auto file : a_handler->files) {
				if (file && _stricmp(a_name, file->filename) == 0) {
					return file;
				}
			}
			return nullptr;
		}

		RE::TESFile* LookupModByName(RE::TESDataHandler* a_handler, const char* a_name)
		{
			if (!a_name || !a_handler) {
				return nullptr;
			}
			if (g_ready.load(std::memory_order_acquire)) {
				const auto it = g_byName.find(std::string_view{ a_name });
				return it != g_byName.end() ? it->second : nullptr;
			}
			return Walk(a_handler, a_name);
		}
	}

	void Install()
	{
		if (!Settings::cacheFileLookups) {
			return;
		}
		REL::Relocation<std::uintptr_t> target{ LOOKUP_MOD_BY_NAME };
		if (std::memcmp(reinterpret_cast<const void*>(target.address()), EXPECTED_START.data(), EXPECTED_START.size()) != 0) {
			REX::WARN("File lookups: TESDataHandler::LookupModByName doesn't start as expected (another plugin hooks it?): not cached");
			return;
		}
		target.write_jmp<5>(&LookupModByName);
		REX::INFO("File lookups: TESDataHandler::LookupModByName replaced (cached once the game's data is loaded)");
	}

	void Build()
	{
		if (!Settings::cacheFileLookups || g_ready.load()) {
			return;
		}
		const auto handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			REX::WARN("File lookups: no data handler; not cached");
			return;
		}
		std::size_t count = 0;
		for (const auto file : handler->files) {
			if (file && file->filename[0] != '\0') {
				// The first of a name wins, as in the game's walk.
				g_byName.try_emplace(std::string{ file->filename }, file);
				++count;
			}
		}
		g_ready.store(true, std::memory_order_release);
		REX::INFO("File lookups: {} plugin files cached ({} names)", count, g_byName.size());
	}
}
