#pragma once

#include <string>
#include <vector>

// Data\F4SE\Plugins\PapyrusOverdrive.ini (see that file for what each one does).
namespace Settings
{
	inline bool speedUpNatives = true;   // [General] bSpeedUpNatives
	inline bool includePluginNatives = false;  // [General] bIncludePluginNatives
	inline bool logSpedUp = true;        // [General] bLogSpedUp

	inline bool profile = true;          // [Profiler] bEnabled
	inline int  profileSeconds = 60;     // [Profiler] iIntervalSeconds
	inline int  profileTop = 20;         // [Profiler] iTop

	inline std::vector<std::string> allowPrefixes;     // [Filters] sAllowPrefixes
	inline std::vector<std::string> excludePrefixes;   // [Filters] sExcludePrefixes
	inline std::vector<std::string> excludeClasses;    // [Filters] sExcludeClasses
	inline std::vector<std::string> excludeFunctions;  // [Filters] sExcludeFunctions ("Class.Function")
	inline std::vector<std::string> includeFunctions;  // [Filters] sIncludeFunctions ("Class.Function")

	void Load();
}
