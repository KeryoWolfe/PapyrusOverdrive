#include "Sampler.h"

#include "Settings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The Windows API after CommonLib (REX/W32/BASE.h asks for that order).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>

namespace Sampler
{
	namespace
	{
		constexpr std::size_t MAX_FRAMES = 48;

		struct Stack
		{
			std::uint64_t frames[MAX_FRAMES];
			std::size_t   count = 0;
		};

		// Walks a suspended thread's stack. No allocation (the thread may hold
		// the heap's lock), and no C++ objects (SEH).
		std::size_t Walk(HANDLE a_thread, std::uint64_t* a_frames, std::size_t a_max)
		{
			CONTEXT context{};
			context.ContextFlags = CONTEXT_FULL;
			if (!GetThreadContext(a_thread, &context)) {
				return 0;
			}
			std::size_t count = 0;
			__try {
				while (count < a_max && context.Rip != 0) {
					a_frames[count++] = context.Rip;
					DWORD64    imageBase = 0;
					const auto function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
					if (!function) {
						// A leaf: the return address is on top of the stack.
						context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
						context.Rsp += 8;
						continue;
					}
					PVOID   handlerData = nullptr;
					DWORD64 establisher = 0;
					RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr);
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
			return count;
		}

		bool Sample(DWORD a_threadID, Stack& a_out)
		{
			const auto thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, a_threadID);
			if (!thread) {
				return false;
			}
			bool ok = false;
			if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
				a_out.count = Walk(thread, a_out.frames, MAX_FRAMES);
				ResumeThread(thread);
				ok = a_out.count > 0;
			}
			CloseHandle(thread);
			return ok;
		}

		// A code address as "module+0xoffset of the function it's in": the
		// function's start from the unwind data, so samples anywhere in it count
		// together.
		struct Site
		{
			std::uint64_t function = 0;  // absolute start (or the address itself without unwind data)
			bool          operator==(const Site&) const = default;
		};

		std::string ModuleName(std::uint64_t a_address, std::uint64_t& a_base)
		{
			static std::unordered_map<std::uint64_t, std::string> names;  // by module base
			HMODULE module = nullptr;
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(a_address),
					&module) ||
				!module) {
				a_base = 0;
				return "?";
			}
			a_base = reinterpret_cast<std::uint64_t>(module);
			if (const auto it = names.find(a_base); it != names.end()) {
				return it->second;
			}
			wchar_t path[MAX_PATH]{};
			GetModuleFileNameW(module, path, MAX_PATH);
			std::wstring wide{ path };
			const auto   slash = wide.find_last_of(L"\\/");
			wide = slash == std::wstring::npos ? wide : wide.substr(slash + 1);
			std::string name;
			for (const auto c : wide) {
				name.push_back(c < 128 ? static_cast<char>(c) : '?');
			}
			return names[a_base] = name;
		}

		std::uint64_t FunctionOf(std::uint64_t a_address)
		{
			DWORD64    imageBase = 0;
			const auto function = RtlLookupFunctionEntry(a_address, &imageBase, nullptr);
			return function ? imageBase + function->BeginAddress : a_address;
		}

		std::string Describe(std::uint64_t a_function)
		{
			std::uint64_t base = 0;
			const auto    module = ModuleName(a_function, base);
			return base ? std::format("{}+0x{:X}", module, a_function - base) : std::format("0x{:X}", a_function);
		}

		struct Profile
		{
			std::size_t                                    samples = 0;
			std::unordered_map<std::uint64_t, std::size_t> inclusive;  // function -> samples with it on the stack
			std::unordered_map<std::uint64_t, std::size_t> self;       // function -> samples with it on top
			std::map<std::vector<std::uint64_t>, std::size_t> stacks;  // top 6 functions -> samples
			std::unordered_map<std::string, std::size_t>   modules;    // module -> samples on top

			void Add(const Stack& a_stack)
			{
				++samples;
				std::vector<std::uint64_t> functions;
				functions.reserve(a_stack.count);
				for (std::size_t i = 0; i < a_stack.count; ++i) {
					functions.push_back(FunctionOf(a_stack.frames[i]));
				}
				std::unordered_set<std::uint64_t> seen;
				for (const auto f : functions) {
					if (seen.insert(f).second) {
						++inclusive[f];
					}
				}
				if (!functions.empty()) {
					++self[functions.front()];
					std::uint64_t base = 0;
					++modules[ModuleName(a_stack.frames[0], base)];
				}
				functions.resize(std::min<std::size_t>(functions.size(), 6));
				++stacks[functions];
			}

			void Log(const char* a_what, double a_seconds) const
			{
				if (samples == 0) {
					REX::INFO("Sampler: {}: no samples", a_what);
					return;
				}
				const auto pct = [&](std::size_t a_n) { return 100.0 * static_cast<double>(a_n) / static_cast<double>(samples); };
				REX::INFO("Sampler: {}: {} samples over {:.1f} s", a_what, samples, a_seconds);
				const auto top = [](const auto& a_map, std::size_t a_n) {
					std::vector<std::pair<typename std::decay_t<decltype(a_map)>::key_type, std::size_t>> rows(a_map.begin(), a_map.end());
					std::ranges::sort(rows, [](const auto& a, const auto& b) { return a.second > b.second; });
					rows.resize(std::min(rows.size(), a_n));
					return rows;
				};
				const auto n = static_cast<std::size_t>(Settings::samplerTop);
				REX::INFO("Sampler: {}: on top of the stack, by module:", a_what);
				for (const auto& [module, count] : top(modules, 10)) {
					REX::INFO("  {:5.1f}%  {}", pct(count), module);
				}
				REX::INFO("Sampler: {}: functions anywhere on the stack (inclusive):", a_what);
				for (const auto& [function, count] : top(inclusive, n)) {
					REX::INFO("  {:5.1f}%  {}", pct(count), Describe(function));
				}
				REX::INFO("Sampler: {}: functions on top of the stack (self):", a_what);
				for (const auto& [function, count] : top(self, n)) {
					REX::INFO("  {:5.1f}%  {}", pct(count), Describe(function));
				}
				REX::INFO("Sampler: {}: most common stacks (top 6 functions, innermost first):", a_what);
				for (const auto& [stack, count] : top(stacks, 10)) {
					std::string line;
					for (const auto f : stack) {
						line += (line.empty() ? "" : " < ") + Describe(f);
					}
					REX::INFO("  {:5.1f}%  {}", pct(count), line);
				}
			}
		};

		std::atomic<DWORD> g_mainThread{ 0 };
		std::atomic<bool>  g_running{ false };
		std::thread        g_thread;
		std::mutex         g_lock;  // Start / Stop

		// Other threads' CPU time, to find the busiest each second.
		std::unordered_map<DWORD, std::uint64_t> g_cpu;

		DWORD BusiestOtherThread(DWORD a_skip1, DWORD a_skip2)
		{
			const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
			if (snapshot == INVALID_HANDLE_VALUE) {
				return 0;
			}
			const auto    pid = GetCurrentProcessId();
			THREADENTRY32 entry{ sizeof(entry) };
			DWORD         best = 0;
			std::uint64_t bestDelta = 0;
			std::unordered_map<DWORD, std::uint64_t> now;
			for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
				if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == a_skip1 || entry.th32ThreadID == a_skip2) {
					continue;
				}
				const auto thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
				if (!thread) {
					continue;
				}
				FILETIME created, exited, kernel, user;
				if (GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
					const auto total = (static_cast<std::uint64_t>(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
					                   (static_cast<std::uint64_t>(user.dwHighDateTime) << 32 | user.dwLowDateTime);
					now[entry.th32ThreadID] = total;
					const auto before = g_cpu.find(entry.th32ThreadID);
					const auto delta = before != g_cpu.end() && total >= before->second ? total - before->second : 0;
					if (delta > bestDelta) {
						bestDelta = delta;
						best = entry.th32ThreadID;
					}
				}
				CloseHandle(thread);
			}
			CloseHandle(snapshot);
			g_cpu = std::move(now);
			// At least 30% of a core in the last second (100 ns units).
			return bestDelta > 3'000'000 ? best : 0;
		}

		void Run()
		{
			const auto self = GetCurrentThreadId();
			const auto main = g_mainThread.load();
			const auto start = std::chrono::steady_clock::now();
			const auto interval = std::chrono::milliseconds(Settings::samplerMS);
			Profile    mainProfile;
			Profile    otherProfile;
			Stack      stack;
			DWORD      other = 0;
			auto       nextPick = start;
			g_cpu.clear();
			while (g_running.load()) {
				const auto now = std::chrono::steady_clock::now();
				if (now >= nextPick) {
					nextPick = now + std::chrono::seconds(1);
					other = BusiestOtherThread(self, main);
				}
				if (main && Sample(main, stack)) {
					mainProfile.Add(stack);
				}
				if (other && Sample(other, stack)) {
					otherProfile.Add(stack);
				}
				std::this_thread::sleep_for(interval);
			}
			const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			REX::INFO("Sampler: the save load took {:.1f} s", seconds);
			mainProfile.Log("main thread", seconds);
			otherProfile.Log("busiest other thread (re-picked each second)", seconds);
		}
	}

	void SetMainThread(std::uint32_t a_threadID)
	{
		g_mainThread.store(a_threadID);
	}

	void Start()
	{
		if (!Settings::samplerEnabled) {
			return;
		}
		std::scoped_lock lock(g_lock);
		if (g_running.load()) {
			return;
		}
		if (g_thread.joinable()) {
			g_thread.join();
		}
		g_running.store(true);
		g_thread = std::thread(Run);
		REX::INFO("Sampler: a save is loading; sampling the main thread every {} ms", Settings::samplerMS);
	}

	void Stop()
	{
		std::scoped_lock lock(g_lock);
		if (!g_running.exchange(false)) {
			return;
		}
		// The report is written by the sampler thread as it ends; not waited
		// for here (kPostLoadGame is on the main thread).
		g_thread.detach();
	}
}
