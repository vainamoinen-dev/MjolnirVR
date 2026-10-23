#pragma once
// Pure logic for the crash guard around Reach's action-test builder: prologue proof, call-site decoding
// and the call decision. Used by src/dll/titles/reach/reach_action_test_guard.cpp; tested in
// tests/reach_action_test_guard_tests.cpp. No Windows calls, no globals, no logging.
//
// Halo Reach action-test builder guard (haloreach.dll +0x657DC).
//
// The builder ORs this tick's look/move action tests into a per-user
// accumulator at [engine TLS + 0xD8] + user * 0x20 and dereferences that TLS
// member without checking it. Two native crash dumps (unmodified upstream
// 0.7.0, and a headset session with this mod on 2026-10-09) fault at +0x65BA9 reading
// null + 4: the player input update ran while the action-test state was not
// allocated. The guard calls the builder only when the member exists - the
// check the engine lacks - and otherwise skips this one tick's action tests.
// Pure logic: prologue proof/decoding and the call decision.
#include <cstddef>
#include <cstdint>

namespace reach_action_test_guard
{
    inline constexpr uintptr_t kBuilderRva = 0x657DC;
    inline constexpr uintptr_t kCallerCallRva = 0x61D43; // e8 rel32 -> builder
    inline constexpr uint32_t kTlsMember = 0xD8;

    // Exact prologue; -1 = the rip displacement of the engine TLS index load.
    // clang-format off
inline constexpr int kPrologue[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18,
    0x48, 0x89, 0x78, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20,
    0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00,          // mov rax, gs:[0x58]
    0x41, 0xBE, 0x01, 0x00, 0x00, 0x00,
    0x44, 0x8B, 0x0D, -1, -1, -1, -1,                              // mov r9d, [tls index]
    0x49, 0x8B, 0xD8, 0x48, 0x63, 0xF1, 0x48, 0x8B, 0xFA,
    0xB9, 0xD8, 0x00, 0x00, 0x00,                                  // mov ecx, 0xD8
    0x4C, 0x8B, 0xDE, 0x49, 0xC1, 0xE3, 0x05,                      // r11 = user << 5
    0x4E, 0x8B, 0x14, 0xC8, 0x4D, 0x8B, 0x14, 0x0A};               // r10 = [[tls]+0xD8]
    // clang-format on
    inline constexpr size_t kPrologueSize = sizeof(kPrologue) / sizeof(kPrologue[0]);
    inline constexpr size_t kTlsDispOffset = 43, kTlsNextOffset = 47;

    inline bool PrologueMatches(const uint8_t* bytes) noexcept
    {
        for (size_t i = 0; i < kPrologueSize; ++i)
            if (kPrologue[i] >= 0 && bytes[i] != uint8_t(kPrologue[i]))
                return false;
        return true;
    }

    inline int32_t Rel32(const uint8_t* p) noexcept
    {
        return int32_t(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24);
    }

    // RVA of the engine TLS index dword the builder loads; 0 when the bytes at
    // builderRva are not the proven prologue.
    inline uintptr_t DecodeTlsIndexRva(const uint8_t* bytes, uintptr_t builderRva) noexcept
    {
        if (!PrologueMatches(bytes))
            return 0;
        return builderRva + kTlsNextOffset + intptr_t(Rel32(bytes + kTlsDispOffset));
    }

    // The caller's e8 rel32 lands exactly on the builder.
    inline bool CallEdgeMatches(const uint8_t* call, uintptr_t callRva, uintptr_t builderRva) noexcept
    {
        return call[0] == 0xE8 && callRva + 5 + intptr_t(Rel32(call + 1)) == builderRva;
    }

    // The decision the detour makes for one call.
    template <class ReadMember, class CallOriginal>
    bool CallIfStateExists(ReadMember readMember, CallOriginal callOriginal)
    {
        if (!readMember())
            return false;
        callOriginal();
        return true;
    }
} // namespace reach_action_test_guard
