#pragma once
// The two proofs every optional native feature (muzzle, contact melee) makes
// before hooking a pinned address:
//   UniqueSignatureAt: the pattern's first match is exactly base+rva and there
//     is no later match. `find(start, size, pattern)` is the title's scanner
//     (sig::Find, or a test fixture's), passed in so fixtures can substitute it.
//   NativeCallEdge: the 5-byte E8 call at base+call lands exactly on
//     base+target.
#include <cstddef>
#include <cstdint>

template <class Find>
bool UniqueSignatureAt(uintptr_t base, size_t size, uintptr_t rva, const char* pattern, Find find)
{
    const uintptr_t match = find(base, size, pattern);
    return match == base + rva && !find(match + 1, base + size - match - 1, pattern);
}

// The one "exact code signature" proof every resolver uses: the
// unique match is exactly base+rva AND that address is executable code (`isCode`, the
// title's VirtualQuery check or a fixture's). A data-section look-alike is refused.
template <class Find, class IsCode>
bool ExactCodeSignatureAt(uintptr_t base, size_t size, uintptr_t rva, const char* pattern, Find find, IsCode isCode)
{
    return rva < size && UniqueSignatureAt(base, size, rva, pattern, find) && isCode(base + rva);
}

inline bool NativeCallEdge(uintptr_t base, uintptr_t call, uintptr_t target)
{
    const auto* instruction = reinterpret_cast<const uint8_t*>(base + call);
    return instruction[0] == 0xE8 &&
           base + call + 5 + *reinterpret_cast<const int32_t*>(instruction + 1) == base + target;
}

#define MCCVR_SIG_FIND [](uintptr_t s, size_t n, const char* p) { return sig::Find(s, n, p); }
#define MCCVR_SIG_IS_CODE [](uintptr_t a) { return sig::IsCodeAddress(a); }
