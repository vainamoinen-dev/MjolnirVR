// Tests the shared native proofs in native_binding.h: a unique signature at the pinned RVA (the
// scan calls and their order are pinned), the exact-code proof, and the E8 call edge.
// Compiles native_binding.h with sigscan.cpp; built as halomccvr_native_binding_tests. Scans only
// buffers this test allocates; no game process.
#include <windows.h>
#include "../src/dll/core/sigscan.h"
#include "../src/dll/core/native_binding.h"
#include <cstdio>
#include <cstring>
#include <vector>

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

int main()
{
    const uintptr_t base = 0x10000;
    std::vector<std::pair<uintptr_t, size_t>> calls;
    uintptr_t first = 0, second = 0;
    const auto find = [&](uintptr_t start, size_t size, const char*)
    {
        calls.push_back({start, size});
        return calls.size() == 1 ? first : second;
    };
    first = base + 0x40;
    second = 0;
    Check(UniqueSignatureAt(base, 0x1000, 0x40, "p", find), "unique match at rva");
    Check(calls.size() == 2 && calls[1].first == base + 0x41 && calls[1].second == 0x1000 - 0x40 - 1,
        "second scan starts after the match and covers the rest of the image");
    calls.clear();
    second = base + 0x80;
    Check(!UniqueSignatureAt(base, 0x1000, 0x40, "p", find), "later duplicate refused");
    calls.clear();
    first = base + 0x44;
    Check(!UniqueSignatureAt(base, 0x1000, 0x40, "p", find) && calls.size() == 1,
        "wrong address refused without a second scan");
    calls.clear();
    first = 0;
    Check(!UniqueSignatureAt(base, 0x1000, 0x40, "p", find), "missing refused");

    // The shared exact-code proof: unique, at the RVA, and in executable code.
    calls.clear();
    first = base + 0x40;
    second = 0;
    Check(ExactCodeSignatureAt(base, 0x1000, 0x40, "p", find, [](uintptr_t) { return true; }),
        "exact code signature: unique, at the rva, executable");
    calls.clear();
    first = base + 0x40;
    second = 0;
    Check(!ExactCodeSignatureAt(base, 0x1000, 0x40, "p", find, [](uintptr_t) { return false; }),
        "exact code signature: a data-section look-alike is refused");
    calls.clear();
    first = base + 0x40;
    second = 0;
    Check(!ExactCodeSignatureAt(base, 0x40, 0x40, "p", find, [](uintptr_t) { return true; }) && calls.empty(),
        "exact code signature: an rva outside the image is refused before any scan");

    {
        // A scan over memory that was freed (a title DLL unmapped
        // mid-scan) returns "not found" instead of faulting; RipTarget likewise.
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        const size_t page = system.dwPageSize;
        auto* region =
            static_cast<unsigned char*>(VirtualAlloc(nullptr, 2 * page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Check(region != nullptr, "test region allocated");
        if (region)
        {
            const uintptr_t r = reinterpret_cast<uintptr_t>(region);
            region[page + 16] = 0xAB;
            region[page + 17] = 0xCD;
            Check(sig::Find(r, 2 * page, "AB CD") == r + page + 16, "the pattern is found while mapped");
            // Second page gone: the scan walks off the readable page into freed memory.
            VirtualFree(region + page, page, MEM_DECOMMIT);
            const uint64_t faultsBefore = sig::ScanFaults();
            Check(sig::Find(r, 2 * page, "AB CD") == 0 && sig::ScanFaults() == faultsBefore + 1,
                "a scan reaching unmapped memory returns not-found and counts the fault");
            VirtualFree(region, 0, MEM_RELEASE);
            Check(sig::Find(r, 2 * page, "AB CD") == 0 && sig::RipTarget(r + 4, r + 8) == 0 &&
                      sig::ScanFaults() == faultsBefore + 3,
                "a scan or rip read over a fully released range never faults");
        }
        Check(sig::IsCodeAddress(reinterpret_cast<uintptr_t>(&GetSystemInfo)) &&
                  !sig::IsCodeAddress(reinterpret_cast<uintptr_t>(&page)) && !sig::IsCodeAddress(0),
            "IsCodeAddress: code yes, stack and null no");
    }

    unsigned char image[0x100]{};
    const auto b = reinterpret_cast<uintptr_t>(image);
    image[0x10] = 0xE8;
    const int32_t rel = 0x80 - (0x10 + 5);
    std::memcpy(image + 0x11, &rel, 4);
    Check(NativeCallEdge(b, 0x10, 0x80), "E8 edge lands on target");
    Check(!NativeCallEdge(b, 0x10, 0x84), "wrong target refused");
    image[0x10] = 0xE9;
    Check(!NativeCallEdge(b, 0x10, 0x80), "non-call opcode refused");

    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("native_binding_tests: all passed\n");
    return 0;
}
