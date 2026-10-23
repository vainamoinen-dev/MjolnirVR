#pragma once
// Golden-fixture fakes shared by the *_golden_tests.cpp files and golden_haptic_lifecycle.h.
// Test-only: never in the DLL, and no game process.
//
// Provides a recording MinHook, retirement disable, detour quiescence and LOG sink, plus
// a synthetic module image that satisfies a title's exact-RVA signature and
// call-edge proofs. A fixture includes the CURRENT production lifecycle file
// after this header and pins its observable behaviour (hook order, retire
// kind, failure cleanup, log text) so a fold can be checked against it.
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace golden
{
    enum class Op
    {
        Create,
        Enable,
        DisableRetire,
        DisableMh,
        Remove
    };
    struct Call
    {
        Op op;
        void* target;
        void* detour;
    };

    inline std::vector<Call> calls;
    inline std::vector<std::string> logs;
    inline int failCreateAt = -1, failEnableAt = -1; // nth call of that kind fails (0-based)
    inline MH_STATUS disableStatus = MH_OK, removeStatus = MH_OK;
    inline bool quiescent = true;
    inline std::vector<std::vector<const void*>> quiescenceFunctions;
    inline int createCount = 0, enableCount = 0;
    inline uintptr_t trampolineSeed = 0x7000'0000'0000ull;

    inline void Reset()
    {
        calls.clear();
        logs.clear();
        quiescenceFunctions.clear();
        failCreateAt = failEnableAt = -1;
        disableStatus = removeStatus = MH_OK;
        quiescent = true;
        createCount = enableCount = 0;
    }
    inline unsigned Count(Op op)
    {
        unsigned n = 0;
        for (const auto& c : calls)
            n += c.op == op;
        return n;
    }
    inline bool Logged(const char* text)
    {
        for (const auto& l : logs)
            if (l == text)
                return true;
        return false;
    }

    // Synthetic image: VirtualAlloc'd, zero-filled, patterns written at their RVAs.
    struct Image
    {
        uint8_t* bytes = nullptr;
        size_t size = 0;
        explicit Image(size_t s) : size(s)
        {
            bytes = static_cast<uint8_t*>(VirtualAlloc(nullptr, s, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        }
        ~Image()
        {
            if (bytes)
                VirtualFree(bytes, 0, MEM_RELEASE);
        }
        uintptr_t Base() const { return reinterpret_cast<uintptr_t>(bytes); }
        // Space-separated hex; "??" writes 0xCC so a wildcard never matches by accident elsewhere.
        void Pattern(uintptr_t rva, const char* pattern)
        {
            uintptr_t at = rva;
            for (const char* p = pattern; *p;)
            {
                while (*p == ' ')
                    ++p;
                if (!*p)
                    break;
                if (p[0] == '?')
                {
                    bytes[at++] = 0xCC;
                    p += 2;
                    continue;
                }
                unsigned v = 0;
                sscanf_s(p, "%2x", &v);
                bytes[at++] = uint8_t(v);
                p += 2;
            }
        }
        void Edge(uintptr_t site, uintptr_t target)
        {
            bytes[site] = 0xE8;
            const int32_t rel = int32_t(intptr_t(target) - intptr_t(site + 5));
            std::memcpy(bytes + site + 1, &rel, 4);
        }
    };
    // Teardown verification (WaitForNativeDetourQuiescence) resolves each detour's
    // code range with RtlLookupFunctionEntry and refuses a detour without one: a
    // one-line forwarder can compile to a leaf tail-jump with no unwind data.
    inline bool HasUnwindRange(const void* function)
    {
        DWORD64 imageBase = 0;
        const auto* entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(function), &imageBase, nullptr);
        const DWORD64 at = reinterpret_cast<DWORD64>(function);
        return entry && entry->EndAddress > entry->BeginAddress && imageBase + entry->BeginAddress <= at &&
               at < imageBase + entry->EndAddress;
    }
} // namespace golden

// ---- the production symbols the lifecycle files call ----
inline void GoldenLogf(const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    golden::logs.emplace_back(buffer);
}
#define LOG(...) GoldenLogf(__VA_ARGS__)

MH_STATUS WINAPI MH_CreateHook(LPVOID target, LPVOID detour, LPVOID* original)
{
    golden::calls.push_back({golden::Op::Create, target, detour});
    if (golden::createCount++ == golden::failCreateAt)
        return MH_ERROR_ALREADY_CREATED;
    if (original)
        *original = reinterpret_cast<LPVOID>(golden::trampolineSeed += 0x100);
    return MH_OK;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID target)
{
    golden::calls.push_back({golden::Op::Enable, target, nullptr});
    return golden::enableCount++ == golden::failEnableAt ? MH_ERROR_NOT_EXECUTABLE : MH_OK;
}
MH_STATUS WINAPI MH_DisableHook(LPVOID target)
{
    golden::calls.push_back({golden::Op::DisableMh, target, nullptr});
    return golden::disableStatus;
}
MH_STATUS WINAPI MH_RemoveHook(LPVOID target)
{
    golden::calls.push_back({golden::Op::Remove, target, nullptr});
    return golden::removeStatus;
}
MH_STATUS WINAPI MCCVR_DisableHookForRetirement(LPVOID target)
{
    golden::calls.push_back({golden::Op::DisableRetire, target, nullptr});
    return golden::disableStatus;
}
bool WaitForNativeDetourQuiescence(
    const void* const* functions, const void* const*, size_t count, const std::atomic<uint32_t>& callbacks)
{
    golden::quiescenceFunctions.emplace_back(functions, functions + count);
    return golden::quiescent && callbacks.load() == 0;
}
