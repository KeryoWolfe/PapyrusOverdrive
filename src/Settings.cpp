#include "Settings.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>

namespace Settings
{
	namespace
	{
		constexpr auto PATH = "Data\\F4SE\\Plugins\\PapyrusOverdrive.ini";

		// Used when the INI doesn't set them. Read-only functions only: a name
		// that starts with one of these, followed by a capital letter (so "Is"
		// takes IsDead but not "Isolate", "Can" takes CanFlyHere but not
		// CancelTimer).
		constexpr auto DEFAULT_ALLOW = "Get, Is, Has, Can, Was";
		// Reads that touch the animation graph or quest data, which Papyrus
		// Tweaks NG (Skyrim) also leaves alone.
		constexpr auto DEFAULT_EXCLUDE_PREFIXES = "GetAnimation, GetQuest";
		// Papyrus Tweaks NG's class list, as far as Fallout 4 has them, plus
		// Scene and InputEnableLayer.
		constexpr auto DEFAULT_EXCLUDE_CLASSES = "UI, Input, InputEnableLayer, Debug, Utility, Quest, Scene";

		std::string Trim(std::string a_text)
		{
			const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
			a_text.erase(a_text.begin(), std::ranges::find_if(a_text, notSpace));
			a_text.erase(std::find_if(a_text.rbegin(), a_text.rend(), notSpace).base(), a_text.end());
			return a_text;
		}

		std::string Lower(std::string a_text)
		{
			std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		std::vector<std::string> List(const std::string& a_text)
		{
			std::vector<std::string> out;
			std::size_t              start = 0;
			while (start <= a_text.size()) {
				const auto comma = a_text.find(',', start);
				auto       item = Trim(a_text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
				if (!item.empty()) {
					out.push_back(std::move(item));
				}
				if (comma == std::string::npos) {
					break;
				}
				start = comma + 1;
			}
			return out;
		}

		// "section.key" (lowercase) -> value
		std::unordered_map<std::string, std::string> Read()
		{
			std::unordered_map<std::string, std::string> values;
			std::ifstream                                file(PATH);
			std::string                                  line;
			std::string                                  section;
			while (std::getline(file, line)) {
				line = Trim(line);
				if (line.empty() || line[0] == ';' || line[0] == '#') {
					continue;
				}
				if (line.front() == '[' && line.back() == ']') {
					section = Lower(Trim(line.substr(1, line.size() - 2)));
					continue;
				}
				const auto equals = line.find('=');
				if (equals == std::string::npos) {
					continue;
				}
				auto value = Trim(line.substr(equals + 1));
				if (const auto comment = value.find(';'); comment != std::string::npos) {
					value = Trim(value.substr(0, comment));
				}
				values[section + "." + Lower(Trim(line.substr(0, equals)))] = value;
			}
			return values;
		}
	}

	void Load()
	{
		const auto values = Read();
		const auto text = [&](const char* a_key, const char* a_default) {
			const auto it = values.find(a_key);
			return it != values.end() ? it->second : std::string{ a_default };
		};
		const auto flag = [&](const char* a_key, bool a_default) {
			const auto it = values.find(a_key);
			if (it == values.end() || it->second.empty()) {
				return a_default;
			}
			const auto v = Lower(it->second);
			return v == "1" || v == "true" || v == "on" || v == "yes";
		};
		const auto number = [&](const char* a_key, int a_default, int a_min, int a_max) {
			const auto it = values.find(a_key);
			if (it == values.end()) {
				return a_default;
			}
			try {
				return std::clamp(std::stoi(it->second), a_min, a_max);
			} catch (...) {
				return a_default;
			}
		};

		speedUpNatives = flag("general.bspeedupnatives", speedUpNatives);
		includePluginNatives = flag("general.bincludepluginnatives", includePluginNatives);
		logSpedUp = flag("general.blogspedup", logSpedUp);
		cacheFileLookups = flag("general.bcachefilelookups", cacheFileLookups);

		profile = flag("profiler.benabled", profile);
		profileSeconds = number("profiler.iintervalseconds", profileSeconds, 5, 3600);
		profileTop = number("profiler.itop", profileTop, 1, 200);

		stackDumpTimeoutMS = number("vm.istackdumptimeoutms", stackDumpTimeoutMS, -1, 50'000'000);
		ignoreMemoryLimit = flag("vm.bignorememorylimit", ignoreMemoryLimit);
		fixScriptPageAllocation = flag("vm.bfixscriptpageallocation", fixScriptPageAllocation);

		profileEvents = flag("events.bprofile", profileEvents);
		profileEventsTop = number("events.itop", profileEventsTop, 1, 200);
		mergeEvents = List(text("events.smergeevents", ""));
		mergeWindowMS = number("events.imergewindowms", mergeWindowMS, 1, 10'000);

		allowPrefixes = List(text("filters.sallowprefixes", DEFAULT_ALLOW));
		excludePrefixes = List(text("filters.sexcludeprefixes", DEFAULT_EXCLUDE_PREFIXES));
		excludeClasses = List(text("filters.sexcludeclasses", DEFAULT_EXCLUDE_CLASSES));
		excludeFunctions = List(text("filters.sexcludefunctions", ""));
		includeFunctions = List(text("filters.sincludefunctions", ""));

		REX::INFO("Settings: {} (speed up natives {}, plugin natives {}, cache file lookups {}, profiler {} every {} s)", values.empty() ? "INI not found, defaults" : "read",
			speedUpNatives, includePluginNatives, cacheFileLookups, profile, profileSeconds);
	}
}
