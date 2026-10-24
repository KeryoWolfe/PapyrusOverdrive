#pragma once

#include <string>
#include <vector>

// Data\F4SE\Plugins\PapyrusOverdrive.ini (see that file for what each one does).
namespace Settings
{
	inline bool speedUpNatives = true;   // [General] bSpeedUpNatives
	inline bool includePluginNatives = false;  // [General] bIncludePluginNatives
	inline bool logSpedUp = true;        // [General] bLogSpedUp
	inline bool cacheFileLookups = true; // [General] bCacheFileLookups

	inline bool profile = true;          // [Profiler] bEnabled
	inline int  profileSeconds = 60;     // [Profiler] iIntervalSeconds
	inline int  profileTop = 20;         // [Profiler] iTop

	inline int  stackDumpTimeoutMS = 15000;     // [VM] iStackDumpTimeoutMS (-1: the game's 5000; 0: never)
	inline bool ignoreMemoryLimit = false;      // [VM] bIgnoreMemoryLimit
	inline bool fixScriptPageAllocation = true; // [VM] bFixScriptPageAllocation

	inline bool profileEvents = true;           // [Events] bProfile
	inline int  profileEventsTop = 15;          // [Events] iTop
	inline std::vector<std::string> mergeEvents;  // [Events] sMergeEvents
	inline int  mergeWindowMS = 100;            // [Events] iMergeWindowMS

	inline bool samplerEnabled = true;          // [Sampler] bEnabled
	inline int  samplerMS = 20;                 // [Sampler] iSampleMS
	inline int  samplerTop = 25;                // [Sampler] iTop

	inline std::vector<std::string> allowPrefixes;     // [Filters] sAllowPrefixes
	inline std::vector<std::string> excludePrefixes;   // [Filters] sExcludePrefixes
	inline std::vector<std::string> excludeClasses;    // [Filters] sExcludeClasses
	inline std::vector<std::string> excludeFunctions;  // [Filters] sExcludeFunctions ("Class.Function")
	inline std::vector<std::string> includeFunctions;  // [Filters] sIncludeFunctions ("Class.Function")

	void Load();
}
