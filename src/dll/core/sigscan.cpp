// Byte-pattern signature scanner (declared in sigscan.h). Read-only; a fault while scanning
// counts as not found.
#include <windows.h>
#include <psapi.h>
#include <vector>
#include <cstring>
#include "sigscan.h"
#include <atomic>
#include "guard_count.h"

namespace
{
    // Parse "48 8B ?? C0" into parallel arrays: byte values, and a mask that
    // says which positions are wildcards.
    // The mask is uint8_t, NOT vector<bool>. vector<bool> is a bitset whose
    // operator[] does a shift-and-mask per access, and this mask is read once
    // per byte compared - tens of millions of times per module scan.
    bool ParsePattern(const char* pattern, std::vector<uint8_t>& bytes, std::vector<uint8_t>& wild)
    {
        for (const char* p = pattern; *p;)
        {
            if (*p == ' ')
            {
                p++;
                continue;
            }
            if (p[0] == '?')
            {
                bytes.push_back(0);
                wild.push_back(1);
                p++;
                if (*p == '?')
                    p++;
            }
            else
            {
                auto hex = [](char c) -> int
                {
                    if (c >= '0' && c <= '9')
                        return c - '0';
                    if (c >= 'a' && c <= 'f')
                        return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F')
                        return c - 'A' + 10;
                    return -1;
                };
                const int hi = hex(p[0]);
                const int lo = hex(p[1]);
                if (hi < 0 || lo < 0)
                    return false;
                bytes.push_back((uint8_t)((hi << 4) | lo));
                wild.push_back(0);
                p += 2;
            }
        }
        return !bytes.empty();
    }
}

namespace
{
    std::atomic<uint64_t> g_scanFaults{0};

    // The raw scan, with no C++ objects so it can run under SEH (see sig::Find).
    uintptr_t ScanRaw(
        const uint8_t* data, size_t last, const uint8_t* bytes, const uint8_t* wild, size_t n, size_t anchor) noexcept
    {
        const uint8_t anchorByte = bytes[anchor];
        size_t i = 0;
        while (i <= last)
        {
            // Candidate offsets are exactly those whose anchor byte matches.
            const uint8_t* hit = static_cast<const uint8_t*>(memchr(data + i + anchor, anchorByte, last - i + 1));
            if (!hit)
                return 0;
            i = static_cast<size_t>(hit - data) - anchor;
            size_t j = 0;
            for (; j < n; j++)
                if (!wild[j] && data[i + j] != bytes[j])
                    break;
            if (j == n)
                return reinterpret_cast<uintptr_t>(data) + i;
            ++i;
        }
        return 0;
    }

    uintptr_t ScanGuarded(
        const uint8_t* data, size_t last, const uint8_t* bytes, const uint8_t* wild, size_t n, size_t anchor) noexcept
    {
        __try
        {
            return ScanRaw(data, last, bytes, wild, n, anchor);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("the signature scan faulted on unreadable memory");
            g_scanFaults.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
    }
}

namespace sig
{
    // Matching semantics are EXACTLY those of the original byte-by-byte loop:
    // the first offset in [base, base+size-n] where every non-wildcard byte
    // equals the pattern. Only the search order changed - instead of testing
    // every offset, memchr skips directly to offsets where the pattern's first
    // concrete byte can sit. memchr is SIMD-optimised, while the old loop paid
    // a bounds-checked bitset read per byte compared, and a whole-module scan
    // is ~74 MB. Nothing about verification is relaxed: every candidate is
    // still compared in full, and the ambiguity re-scans callers run to prove
    // a signature is unique are sped up identically. Proven equivalent against
    // a brute-force reference over randomised data in tests/core_tests.cpp.
    uintptr_t Find(uintptr_t base, size_t size, const char* pattern)
    {
        std::vector<uint8_t> bytes;
        std::vector<uint8_t> wild;
        if (!ParsePattern(pattern, bytes, wild))
            return 0;
        const size_t n = bytes.size();
        if (!n || size < n)
            return 0;

        const uint8_t* data = reinterpret_cast<const uint8_t*>(base);
        const size_t last = size - n;

        // First concrete byte to anchor the skip on.
        size_t anchor = 0;
        while (anchor < n && wild[anchor])
            ++anchor;
        // An all-wildcard pattern matches at the first offset, as before.
        if (anchor == n)
            return base;

        return ScanGuarded(data, last, bytes.data(), wild.data(), n, anchor);
    }

    uint64_t ScanFaults()
    {
        return g_scanFaults.load(std::memory_order_relaxed);
    }

    bool IsCodeAddress(uintptr_t address)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!address || !VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD))
            return false;
        const DWORD protection = info.Protect & 0xFFu;
        return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
               protection == PAGE_EXECUTE_WRITECOPY;
    }

    bool ModuleRange(const wchar_t* moduleName, uintptr_t& base, size_t& size)
    {
        HMODULE mod = GetModuleHandleW(moduleName);
        if (!mod)
            return false;
        MODULEINFO mi{};
        if (!GetModuleInformation(GetCurrentProcess(), mod, &mi, sizeof(mi)))
            return false;
        base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
        size = mi.SizeOfImage;
        return true;
    }

    bool SelfModuleRange(uintptr_t& base, size_t& size)
    {
        HMODULE mod = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&SelfModuleRange), &mod))
            return false;
        MODULEINFO mi{};
        if (!GetModuleInformation(GetCurrentProcess(), mod, &mi, sizeof(mi)))
            return false;
        base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
        size = mi.SizeOfImage;
        return true;
    }

    uintptr_t FindInModule(const wchar_t* moduleName, const char* pattern)
    {
        uintptr_t base = 0;
        size_t size = 0;
        if (!ModuleRange(moduleName, base, size))
            return 0;
        return Find(base, size, pattern);
    }

    uintptr_t RipTarget(uintptr_t dispAddr, uintptr_t nextInstr)
    {
        int32_t disp = 0;
        __try
        {
            disp = *reinterpret_cast<const int32_t*>(dispAddr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading a RIP-relative displacement faulted");
            g_scanFaults.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        return nextInstr + disp;
    }
}
