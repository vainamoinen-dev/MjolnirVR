// Golden tests for the Halo 2 weapon-haptics lifecycle (src/dll/titles/h2/halo2_weapon_haptics.inl and
// halo2_haptic_lifecycle.inl). Built as halo2_weapon_haptics_lifecycle_golden_tests with
// src/dll/core/sigscan.cpp and run by ctest. Fakes come from golden_hook_fakes.h; no game process.
//
// Pins the CURRENT Halo 2 weapon-haptics
// install/remove/report lifecycle, including Halo 2's own quirks: install
// returns true while already enabled, removal failures log nothing, the
// quiescence list has eight entries (five hooks plus three bodies with no
// trampoline), and Report always logs and resets its counters (exchange(0)).
#include "golden_hook_fakes.h"
#include <algorithm>
#include <cmath>
#include "../src/common/runtime_types.h"
#include "../src/common/weapon_hand_logic.h"
#include "hook_checked_forwarders.h" // DLL hook sites call MinHook through the checked names
#include "../src/dll/core/sigscan.h"
#include "../src/dll/core/guard_count.h"

static std::atomic<bool> g_armed{true}, g_levelLive{true}, g_teardownRequested{false};
static std::atomic<uint32_t> g_generation{7};
static struct
{
    std::atomic<bool> enabled{true};
    uint32_t generation = 7;
} g_halo2Dual;
static struct
{
    bool persistent_support_grip = true;
} g_config;
struct SupportGripRelationshipSnapshot
{
    bool engaged{};
    GameTitle title{};
    uint32_t generation{}, unit{}, weapon{};
};
static uint32_t Halo2OwnedUnit()
{
    return 0x12340001;
}
static void* Halo2IndependentTargetStorage(uint32_t)
{
    return nullptr;
}
static bool Halo2ReadIndependentWeapons(uint32_t, uint32_t (&)[2], bool)
{
    return false;
}
static bool VR_IsTwoHandAiming()
{
    return false;
}
static bool VR_GetSupportGripRelationship(SupportGripRelationshipSnapshot&)
{
    return false;
}
static uint64_t VR_WeaponHapticToken(GameTitle, uint32_t, bool) noexcept
{
    return 1;
}
static bool VR_PulseWeaponHaptics(GameTitle, uint32_t, bool, bool, float, uint64_t) noexcept
{
    return true;
}
// Production scans region by region under SEH; the lifecycle only needs its count/first-match contract.
static bool CountPatternMatches(
    uintptr_t base, size_t size, const char* pattern, uintptr_t& first, uint32_t& count) noexcept
{
    first = 0;
    count = 0;
    for (uintptr_t at = base; at < base + size;)
    {
        const uintptr_t hit = sig::Find(at, base + size - at, pattern);
        if (!hit)
            break;
        if (!count)
            first = hit;
        ++count;
        at = hit + 1;
    }
    return true;
}

#include "../src/dll/titles/h2/halo2_weapon_haptics.inl"
#include "../src/dll/titles/h2/halo2_haptic_lifecycle.inl"

namespace
{
    using golden::Op;
    namespace c = halo2_haptic_contract;
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

    std::string ExpectedFor(uintptr_t rva)
    {
        char text[256];
        for (const auto& b : c::entries)
            if (rva >= b.rva && rva < b.rva + Length(b.pattern))
            {
                snprintf(
                    text, sizeof(text), "Halo 2 weapon haptics StockFallback: binding +%X missing/ambiguous", b.rva);
                return text;
            }
        for (const auto& e : c::edges)
            if (rva >= e.call && rva < e.call + 5)
            {
                snprintf(text, sizeof(text), "Halo 2 weapon haptics StockFallback: native call +%X changed", e.call);
                return text;
            }
        for (const auto& w : c::witnesses)
            if (rva >= w.rva && rva < w.rva + Length(w.pattern))
            {
                snprintf(text, sizeof(text), "Halo 2 weapon haptics StockFallback: layout/ABI +%X changed", w.rva);
                return text;
            }
        for (const auto& r : c::references)
            if (rva >= r.instruction && rva < r.instruction + r.length)
            {
                snprintf(
                    text, sizeof(text), "Halo 2 weapon haptics StockFallback: data witness +%X changed", r.instruction);
                return text;
            }
        return "(none)";
    }
    bool AnyTarget()
    {
        using namespace h2_recoil;
        return triggerHook.target || effectHook.target || enqueueHook.target || updateHook.target ||
               evaluateHook.target;
    }
    void Clean()
    {
        golden::Reset();
        if (AnyTarget())
            (void)RemoveHalo2WeaponHaptics();
        h2_recoil::enabled = false;
        h2_recoil::fault = false;
        h2_recoil::callbacks = 0;
        h2_recoil::captured = 0;
        h2_recoil::fallback = 0;
        h2_recoil::faults = 0;
        golden::Reset();
    }
}

int main()
{
    using namespace h2_recoil;
    size_t size = 0x1000;
    for (const auto& b : c::entries)
        size = std::max<size_t>(size, b.rva + 0x1000);
    for (const auto& w : c::witnesses)
        size = std::max<size_t>(size, w.rva + 0x1000);
    for (const auto& r : c::references)
        size = std::max<size_t>(size, std::max(r.instruction, r.target) + 0x1000);
    golden::Image image(size);
    if (!image.bytes)
        return 2;
    for (const auto& b : c::entries)
        image.Pattern(b.rva, b.pattern);
    for (const auto& w : c::witnesses)
        image.Pattern(w.rva, w.pattern);
    for (const auto& e : c::edges)
        image.Edge(e.call, e.target);
    for (const auto& r : c::references)
    {
        const int32_t rel = int32_t(intptr_t(r.target) - intptr_t(r.instruction + r.length));
        std::memcpy(image.bytes + r.instruction + r.displacement, &rel, 4);
    }
    const uintptr_t module = image.Base();
    const uint32_t rvas[]{c::trigger, c::effect, c::enqueue, c::update, c::evaluate};
    const std::vector<const void*> quiesced{reinterpret_cast<void*>(&Trigger), reinterpret_cast<void*>(&Effect),
        reinterpret_cast<void*>(&Enqueue), reinterpret_cast<void*>(&Update), reinterpret_cast<void*>(&Evaluate),
        reinterpret_cast<void*>(&EffectBody), reinterpret_cast<void*>(&EnqueueBody),
        reinterpret_cast<void*>(&EvaluateBody)};
    const char* installed =
        "Halo 2 weapon haptics Installed: native authored recoil per owned weapon/hand; general rumble preserved";

    Clean();
    Check(InstallHalo2WeaponHaptics(module, size), "install succeeds on a matching image");
    bool order = golden::calls.size() == 10;
    for (size_t i = 0; order && i < 5; ++i)
        order = golden::calls[i].op == Op::Create &&
                golden::calls[i].target == reinterpret_cast<void*>(module + rvas[i]) &&
                golden::calls[i].detour == quiesced[i] && golden::calls[5 + i].op == Op::Enable &&
                golden::calls[5 + i].target == reinterpret_cast<void*>(module + rvas[i]);
    Check(order, "create x5 then enable x5 in contract order");
    Check(
        enabled.load() && base == module && generation == 7 && golden::logs.size() == 1 && golden::logs[0] == installed,
        "enabled, generation from g_generation, exact installed line");
    bool distinct = true;
    for (size_t i = 0; i < quiesced.size(); ++i)
        for (size_t j = i + 1; j < quiesced.size(); ++j)
            distinct = distinct && quiesced[i] != quiesced[j];
    Check(distinct, "eight distinct hook/body addresses (no /OPT:ICF folding)");
    bool unwind = true;
    for (const void* function : quiesced)
        unwind = unwind && golden::HasUnwindRange(function);
    Check(unwind, "every quiesced hook/body has an unwind range (teardown quiescence can verify it)");

    golden::Reset();
    Check(InstallHalo2WeaponHaptics(module, size) && golden::calls.empty() && golden::logs.empty(),
        "quirk: install while enabled returns true silently");

    golden::Reset();
    captured = 5;
    ReportHalo2WeaponHaptics();
    Check(golden::logs.size() == 1 &&
              golden::logs[0] == "Halo 2 weapon haptics: enabled=1 fault=0 captured=5 stockFallback=0 faults=0",
        "healthy report line");
    golden::Reset();
    ReportHalo2WeaponHaptics();
    Check(golden::logs.size() == 1 &&
              golden::logs[0] == "Halo 2 weapon haptics: enabled=1 fault=0 captured=0 stockFallback=0 faults=0",
        "quirk: report counters reset (exchange)");

    golden::Reset();
    Check(RemoveHalo2WeaponHaptics(), "remove succeeds");
    Check(golden::Count(Op::DisableRetire) == 5 && golden::Count(Op::DisableMh) == 0 && golden::Count(Op::Remove) == 5,
        "remove: retire-disable x5 then remove x5");
    Check(golden::quiescenceFunctions.size() == 1 && golden::quiescenceFunctions[0] == quiesced,
        "quirk: quiescence on five hooks plus three bodies");
    Check(!AnyTarget() && base == 0 && generation == 0 && golden::logs.empty(), "cleared silently");

    Clean();
    InstallHalo2WeaponHaptics(module, size);
    golden::Reset();
    callbacks = 1;
    Check(!RemoveHalo2WeaponHaptics() && golden::logs.empty() && AnyTarget(),
        "quirk: busy removal fails silently, hooks kept");
    callbacks = 0;
    golden::Reset();
    golden::disableStatus = MH_ERROR_MEMORY_PROTECT;
    Check(!RemoveHalo2WeaponHaptics() && golden::logs.empty() && golden::quiescenceFunctions.empty(),
        "disable failure is silent");
    golden::disableStatus = MH_OK;

    // A not-enabled runtime with leftover hooks refuses to install.
    golden::Reset();
    enabled = false;
    Check(!InstallHalo2WeaponHaptics(module, size) &&
              golden::Logged("Halo 2 weapon haptics CleanupRequired: prior hooks retained") && golden::calls.empty(),
        "leftover hooks: CleanupRequired line");

    for (int failAt = 0; failAt < 5; ++failAt)
    {
        Clean();
        golden::failCreateAt = failAt;
        char expected[128];
        snprintf(expected, sizeof(expected), "Halo 2 weapon haptics StockFallback: create +%X failed", rvas[failAt]);
        Check(!InstallHalo2WeaponHaptics(module, size) && !golden::logs.empty() && golden::logs[0] == expected &&
                  golden::Count(Op::Remove) == unsigned(failAt) && !AnyTarget(),
            "create failure at hook " + std::to_string(failAt));
    }
    for (int failAt = 0; failAt < 5; ++failAt)
    {
        Clean();
        golden::failEnableAt = failAt;
        char expected[128];
        snprintf(expected, sizeof(expected), "Halo 2 weapon haptics StockFallback: enable +%X failed", rvas[failAt]);
        Check(!InstallHalo2WeaponHaptics(module, size) && !golden::logs.empty() && golden::logs[0] == expected &&
                  golden::Count(Op::Remove) == 5,
            "enable failure at hook " + std::to_string(failAt));
    }

    std::vector<uintptr_t> proofs;
    for (const auto& b : c::entries)
        proofs.push_back(b.rva);
    for (const auto& e : c::edges)
        proofs.push_back(e.call);
    for (const auto& w : c::witnesses)
        proofs.push_back(w.rva);
    for (const auto& r : c::references)
        proofs.push_back(r.instruction + r.displacement);
    for (uintptr_t rva : proofs)
    {
        Clean();
        const uint8_t saved = image.bytes[rva];
        image.bytes[rva] ^= 0xFF;
        Check(!InstallHalo2WeaponHaptics(module, size) && golden::Logged(ExpectedFor(rva).c_str()) &&
                  golden::calls.empty(),
            "proof byte +" + std::to_string(rva) + " refused with its exact line");
        image.bytes[rva] = saved;
    }

    Clean();
    g_halo2Dual.enabled = false;
    Check(!InstallHalo2WeaponHaptics(module, size) &&
              golden::Logged("Halo 2 weapon haptics StockFallback: owned weapon reader unavailable"),
        "dual reader unavailable line");
    g_halo2Dual.enabled = true;

    Clean();
    InstallHalo2WeaponHaptics(module, size);
    golden::Reset();
    fault = true;
    ReportHalo2WeaponHaptics();
    Check(golden::logs.size() == 2 &&
              golden::logs[0] == "Halo 2 weapon haptics: enabled=1 fault=1 captured=0 stockFallback=0 faults=0" &&
              golden::logs[1] == "Halo 2 weapon haptics StockFallback: optional hooks retired; camera unchanged" &&
              !AnyTarget(),
        "faulted report retires with its second line");
    Clean();
    ReportHalo2WeaponHaptics();
    Check(golden::logs.size() == 1 &&
              golden::logs[0] == "Halo 2 weapon haptics: enabled=0 fault=0 captured=0 stockFallback=0 faults=0",
        "quirk: report logs even with nothing installed");

    std::printf("Halo 2 haptic lifecycle golden: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
