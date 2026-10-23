// Golden tests for the Halo 3 weapon-haptics lifecycle (src/dll/titles/h3/halo3_weapon_haptics.inl).
// Built as halo3_weapon_haptics_lifecycle_golden_tests with src/dll/core/sigscan.cpp and run by ctest.
// Fakes come from golden_hook_fakes.h; no game process.
//
// Pins the CURRENT Halo 3 weapon-haptics
// install/remove/report lifecycle. Halo 3's own quirks are pinned on purpose:
// seven hooks, the create-failure line is logged BEFORE cleanup and names the
// RVA, binding failures name the contract entry, the faulted report logs a
// second StockFallback/CleanupRequired line.
#include "golden_hook_fakes.h"
#include <algorithm>
#include <cmath>
#include "hook_checked_forwarders.h" // DLL hook sites call MinHook through the checked names
#include "../src/dll/core/sigscan.h"
#include "../src/dll/core/guard_count.h"

enum class GameTitle
{
    Halo3
};
struct
{
    bool persistent_support_grip = true;
} g_config;
struct SupportGripRelationshipSnapshot
{
    bool engaged{};
    GameTitle title{};
    uint32_t generation{}, unit{}, weapon{};
};
std::atomic<bool> g_vrAim{true};
std::atomic<uint32_t> g_halo3RuntimeGeneration{7};
uint32_t engineTlsIndex = 1;
uint32_t* g_engineTlsIndex = &engineTlsIndex;
int64_t __fastcall PlayerUnit(int)
{
    return -1;
}
int64_t(__fastcall* g_halo3PlayerUnitGetter)(int) = PlayerUnit;
void* tagDataBase = nullptr;
void** g_halo3TagDataBase = &tagDataBase;
uint8_t tagInstances[16]{};
void* g_halo3TagInstanceTable = tagInstances;
uint8_t* Halo3LoadedTagDefinition(uint32_t)
{
    return nullptr;
}
bool Halo3ReadOwnedWeapons(uint32_t, uint32_t[2], bool)
{
    return false;
}
int ResolveEquippedWeaponSlot(uint32_t, uint32_t, uint32_t, bool, bool)
{
    return -1;
}
uint64_t VR_WeaponHapticToken(GameTitle, uint32_t, bool) noexcept
{
    return 1;
}
bool VR_PulseWeaponHaptics(GameTitle, uint32_t, bool, bool, float, uint64_t) noexcept
{
    return true;
}
bool VR_GetSupportGripRelationship(SupportGripRelationshipSnapshot&) noexcept
{
    return false;
}
bool VR_IsTwoHandAiming() noexcept
{
    return false;
}

#include "../src/dll/titles/h3/halo3_weapon_haptics.inl"

namespace
{
    using golden::Op;
    namespace c = halo3_haptic_contract;
    unsigned checks = 0, failures = 0;
    void Check(bool ok, const std::string& what)
    {
        ++checks;
        if (!ok)
        {
            ++failures;
            std::printf("FAIL: %s\n", what.c_str());
        }
    }

    size_t Length(const char* pattern)
    {
        size_t n = 0;
        for (const char* p = pattern; *p; ++p)
            if (*p != ' ' && (p == pattern || p[-1] == ' '))
                ++n;
        return n;
    }

    // The first proof, in Install's own order, that covers the byte at rva.
    std::string ExpectedFor(uintptr_t rva)
    {
        char text[256];
        for (const auto& e : c::entries)
            if (rva >= e.rva && rva < e.rva + Length(e.pattern))
            {
                snprintf(text, sizeof(text), "Halo 3 weapon haptics StockFallback: missing/ambiguous %s", e.name);
                return text;
            }
        for (const auto& r : c::relatives)
            if (rva >= r.rva && rva < r.rva + r.size)
            {
                snprintf(
                    text, sizeof(text), "Halo 3 weapon haptics StockFallback: caller edge mismatch at +0x%X", r.rva);
                return text;
            }
        for (const auto& w : c::witnesses)
            if (rva >= w.rva && rva < w.rva + Length(w.pattern))
            {
                snprintf(text, sizeof(text),
                    "Halo 3 weapon haptics StockFallback: queue-write witness mismatch at +0x%X", w.rva);
                return text;
            }
        return "(none)";
    }

    bool AnyTarget()
    {
        const auto& f = g_halo3WeaponHaptics;
        return f.weaponTarget || f.triggerUpdateTarget || f.wrapperTarget || f.playerEffectTarget || f.workerTarget ||
               f.evaluatorTarget || f.updateTarget;
    }
    void Clean()
    {
        golden::Reset();
        if (AnyTarget())
            (void)RemoveHalo3WeaponHaptics();
        golden::Reset();
        g_halo3WeaponHaptics.callbacks = 0;
        g_halo3WeaponHaptics.faulted = false;
    }
}

int main()
{
    size_t kSize = c::haptic_curve + 0x2000;
    for (const auto& e : c::entries)
        kSize = std::max<size_t>(kSize, e.rva + 0x1000);
    for (const auto& r : c::relatives)
        kSize = std::max<size_t>(kSize, r.rva + 0x1000);
    for (const auto& w : c::witnesses)
        kSize = std::max<size_t>(kSize, w.rva + 0x1000);
    golden::Image image(kSize);
    if (!image.bytes)
        return 2;
    for (const auto& e : c::entries)
        image.Pattern(e.rva, e.pattern);
    for (const auto& r : c::relatives)
    {
        image.bytes[r.rva] = 0xE8;
        const int32_t rel = int32_t(intptr_t(r.target) - intptr_t(r.rva + r.size));
        std::memcpy(image.bytes + r.rva + r.displacement, &rel, 4);
    }
    for (const auto& w : c::witnesses)
        image.Pattern(w.rva, w.pattern);
    const uintptr_t base = image.Base();
    auto& f = g_halo3WeaponHaptics;

    const uint32_t hookRvas[]{c::successful_weapon_fire, c::weapon_trigger_update, c::damage_effect_wrapper,
        c::player_effect, c::effect_worker, c::haptic_evaluator, c::haptic_update};
    const std::vector<const void*> detours{reinterpret_cast<const void*>(&Halo3WeaponHapticShotDetour),
        reinterpret_cast<const void*>(&Halo3WeaponHapticTriggerUpdateDetour),
        reinterpret_cast<const void*>(&Halo3WeaponHapticDamageWrapperDetour),
        reinterpret_cast<const void*>(&Halo3WeaponHapticEffectDetour),
        reinterpret_cast<const void*>(&Halo3WeaponHapticWorkerDetour),
        reinterpret_cast<const void*>(&Halo3WeaponHapticEvaluatorDetour),
        reinterpret_cast<const void*>(&Halo3WeaponHapticUpdateDetour)};
    const char* installed =
        "Halo 3 authored weapon haptics installed: admitted shots plus exact local charge effects, per-weapon role, stock evaluator admission";

    Clean();
    Check(InstallHalo3WeaponHaptics(base, kSize, 7), "install succeeds on a matching image");
    bool order = golden::calls.size() == 14;
    for (size_t i = 0; order && i < 7; ++i)
        order = golden::calls[i].op == Op::Create &&
                golden::calls[i].target == reinterpret_cast<void*>(base + hookRvas[i]) &&
                golden::calls[i].detour == detours[i] && golden::calls[7 + i].op == Op::Enable &&
                golden::calls[7 + i].target == reinterpret_cast<void*>(base + hookRvas[i]);
    Check(order, "create x7 in contract order with per-hook detours, then enable x7 in the same order");
    Check(f.enabled.load() && f.generation == 7 && golden::logs.size() == 1 && golden::logs[0] == installed,
        "exact installed line");
    bool distinct = true;
    for (size_t i = 0; i < 7; ++i)
        for (size_t j = i + 1; j < 7; ++j)
            distinct = distinct && detours[i] != detours[j];
    Check(distinct, "seven distinct detour addresses (no /OPT:ICF folding)");
    bool unwind = true;
    for (const void* detour : detours)
        unwind = unwind && golden::HasUnwindRange(detour);
    Check(unwind, "every detour has an unwind range (teardown quiescence can verify it)");

    golden::Reset();
    Check(!InstallHalo3WeaponHaptics(base, kSize, 7) && golden::calls.empty(), "install refused while installed");
    golden::Reset();
    ReportHalo3WeaponHaptics();
    Check(
        golden::logs.size() == 1 &&
            golden::logs[0] ==
                "Halo 3 authored weapon haptics: enabled=1 isolatedFault=0 captured=0 retired=0 nativeFallback=0 faults=0" &&
            golden::Count(Op::Remove) == 0,
        "healthy report: exact line, hooks kept");

    golden::Reset();
    Check(RemoveHalo3WeaponHaptics(), "remove succeeds");
    Check(golden::Count(Op::DisableRetire) == 7 && golden::Count(Op::DisableMh) == 0 && golden::Count(Op::Remove) == 7,
        "remove: MCCVR_DisableHookForRetirement x7 then MH_RemoveHook x7");
    Check(golden::quiescenceFunctions.size() == 1 && golden::quiescenceFunctions[0] == detours,
        "quiescence on the seven detours in order");
    Check(!AnyTarget() && !f.enabled.load() && f.generation == 0 && golden::logs.empty(), "cleared, nothing logged");

    Clean();
    InstallHalo3WeaponHaptics(base, kSize, 7);
    golden::Reset();
    f.callbacks = 1;
    Check(!RemoveHalo3WeaponHaptics() &&
              golden::Logged("Halo 3 weapon haptics CleanupRequired: callbacks or ingress busy") && AnyTarget(),
        "busy: exact line, hooks kept");
    f.callbacks = 0;
    golden::Reset();
    golden::disableStatus = MH_ERROR_MEMORY_PROTECT;
    Check(!RemoveHalo3WeaponHaptics() && golden::Logged("Halo 3 weapon haptics CleanupRequired: hook disable failed"),
        "disable failure line");
    golden::disableStatus = MH_OK;
    golden::Reset();
    golden::removeStatus = MH_ERROR_MEMORY_PROTECT;
    Check(!RemoveHalo3WeaponHaptics() && golden::Logged("Halo 3 weapon haptics CleanupRequired: hook removal failed"),
        "removal failure line");
    golden::removeStatus = MH_OK;

    for (int failAt = 0; failAt < 7; ++failAt)
    {
        Clean();
        golden::failCreateAt = failAt;
        char expected[128];
        snprintf(expected, sizeof(expected), "Halo 3 weapon haptics StockFallback: hook create failed at +0x%X",
            hookRvas[failAt]);
        Check(!InstallHalo3WeaponHaptics(base, kSize, 7) && !golden::logs.empty() && golden::logs[0] == expected &&
                  golden::Count(Op::Remove) == unsigned(failAt) && !AnyTarget(),
            "create failure at hook " + std::to_string(failAt) + ": line with RVA logged first, earlier hooks removed");
    }
    for (int failAt = 0; failAt < 7; ++failAt)
    {
        Clean();
        golden::failEnableAt = failAt;
        Check(!InstallHalo3WeaponHaptics(base, kSize, 7) && !golden::logs.empty() &&
                  golden::logs[0] == "Halo 3 weapon haptics StockFallback: hook enable failed" &&
                  golden::Count(Op::Remove) == 7,
            "enable failure at hook " + std::to_string(failAt) + ": line first, all removed");
    }

    std::vector<uintptr_t> proofs;
    for (const auto& e : c::entries)
        proofs.push_back(e.rva);
    for (const auto& r : c::relatives)
        proofs.push_back(r.rva);
    for (const auto& w : c::witnesses)
        proofs.push_back(w.rva);
    for (uintptr_t rva : proofs)
    {
        Clean();
        const uint8_t saved = image.bytes[rva];
        image.bytes[rva] ^= 0xFF;
        Check(!InstallHalo3WeaponHaptics(base, kSize, 7) && golden::Logged(ExpectedFor(rva).c_str()) &&
                  golden::calls.empty(),
            "proof at +" + std::to_string(rva) + " refused with its exact line");
        image.bytes[rva] = saved;
    }

    Clean();
    void* savedTable = g_halo3TagInstanceTable;
    g_halo3TagInstanceTable = nullptr;
    Check(!InstallHalo3WeaponHaptics(base, kSize, 7) &&
              golden::Logged("Halo 3 weapon haptics StockFallback: local-player/tag/TLS bindings unavailable") &&
              golden::calls.empty(),
        "missing title bindings: exact line");
    g_halo3TagInstanceTable = savedTable;

    Clean();
    Check(!InstallHalo3WeaponHaptics(base, kSize, 0) && !InstallHalo3WeaponHaptics(base, c::haptic_curve + 0x1000, 7) &&
              golden::logs.empty() && golden::calls.empty(),
        "zero generation and undersized image refused silently");

    Clean();
    InstallHalo3WeaponHaptics(base, kSize, 7);
    golden::Reset();
    f.faulted = true;
    ReportHalo3WeaponHaptics();
    Check(
        golden::logs.size() == 2 &&
            golden::logs[0] ==
                "Halo 3 authored weapon haptics: enabled=1 isolatedFault=1 captured=0 retired=0 nativeFallback=0 faults=0" &&
            golden::logs[1] == "Halo 3 weapon haptics StockFallback: optional hooks retired; camera unchanged" &&
            !AnyTarget(),
        "faulted report: counter line then StockFallback retired line");
    Clean();
    InstallHalo3WeaponHaptics(base, kSize, 7);
    golden::Reset();
    f.faulted = true;
    f.callbacks = 1;
    ReportHalo3WeaponHaptics();
    Check(golden::logs.size() == 3 &&
              golden::logs[2] ==
                  "Halo 3 weapon haptics CleanupRequired: optional hooks retained for retry; camera unchanged" &&
              AnyTarget(),
        "faulted report while busy: CleanupRequired retained line");
    f.callbacks = 0;
    Clean();
    ReportHalo3WeaponHaptics();
    Check(golden::logs.empty(), "report with nothing installed is silent");

    std::printf("Halo 3 haptic lifecycle golden: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
