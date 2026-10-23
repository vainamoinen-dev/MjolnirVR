// Reach weapon-haptics lifecycle on the shared hook set:
// contract bindings, call edges, witnesses, local-user/tag proofs, hook order,
// retire kind, cleanup on every failure, exact log text and the reports.
#include "golden_haptic_lifecycle.h"
#include <cmath>
#include "../src/common/runtime_types.h"
#include "../src/common/titles/reach/reach_render_logic.h"
#include "hook_checked_forwarders.h" // DLL hook sites call MinHook through the checked names
#include "../src/dll/core/sigscan.h"
#include "../src/dll/core/native_binding.h"
#include "../src/dll/core/guard_count.h"

using ReachPlayerUnitByOutputUserFn = int(__fastcall*)(int);
struct
{
    std::atomic<bool> armed{true}, teardownRequested{false};
    std::atomic<uint32_t> generation{7};
} g_reachCamera;
std::atomic<bool> g_enabled{true}, g_vrAim{true};
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
namespace exclusive_input
{
    inline bool Active()
    {
        return false;
    }
}
bool VR_IsStereoEnabled()
{
    return true;
}
bool VR_IsCutsceneTheaterActive()
{
    return false;
}
bool VR_IsTwoHandAiming()
{
    return false;
}
bool VR_GetSupportGripRelationship(SupportGripRelationshipSnapshot&)
{
    return false;
}
GameTitle TitleAdapter_GetActiveTitle()
{
    return GameTitle::HaloReach;
}
RuntimeMode TitleAdapter_GetRuntimeMode()
{
    return RuntimeMode::Gameplay;
}
bool ReachReadMuzzleWeapons(uint32_t, uint32_t[2], bool* = nullptr)
{
    return false;
}
int ResolveEquippedWeaponSlot(uint32_t, uint32_t, uint32_t, bool, bool)
{
    return -1;
}
uint64_t VR_WeaponHapticToken(GameTitle, uint32_t, bool)
{
    return 1;
}
bool VR_PulseWeaponHaptics(GameTitle, uint32_t, bool, bool, float, uint64_t)
{
    return true;
}

#include "../src/dll/titles/reach/reach_weapon_haptics.inl"

int main()
{
    using namespace reach_weapon_haptics;
    namespace c = reach_haptic_contract;
    auto& r = runtime;
    golden::HapticSpec s;
    s.name = "Reach";
    s.imageSize = 0x4F00000;
    s.smallSize = 0x4E39F30;
    s.smallSizeLog = nullptr;
    s.rejectsZeroGeneration = true;
    for (const auto& b : c::entries)
        s.bindings.push_back({b.rva, b.pattern});
    s.bindingLogFormat = "Reach weapon haptics StockFallback: signature +%X missing/ambiguous";
    for (const auto& e : c::edges)
        s.edges.push_back({e.call, e.target});
    s.edgeLog = "Reach weapon haptics StockFallback: call edge mismatch at +%X";
    for (const auto& w : c::witnesses)
    {
        char text[128];
        snprintf(text, sizeof(text), "Reach weapon haptics StockFallback: witness mismatch at +%X", w.rva);
        s.extras.push_back({w.rva, w.pattern, text});
    }
    const char* userTag = "Reach weapon haptics StockFallback: local-user/tag bindings missing or ambiguous";
    s.extras.push_back({kReachPlayerUnitByOutputUserRva,
        "83 C8 FF 83 F9 03 77 27 8B 15 12 3C BC 00 65 48 8B 04 25 58 00 00 00 41 B8 58 01 00 00 48 63 C9 48 8B 04 D0 4A 8B 04 00 8B 84 88 C8 00 00 00 C3",
        userTag});
    s.extras.push_back({kReachTagGetRva,
        "4C 8B 15 ?? ?? ?? ?? 45 33 DB 4C 0F BF C2 45 8B CB 49 63 82 F8 FF 03 00 4C 3B C0 73 17 48 8B 05 ?? ?? ?? ?? C1 FA 10 4E 8D 04 C0 66 41 3B 50 02 4D 0F 44 C8 4D 85 C9 74 3E 49 0F BF 01 48 03 C0 41 39 8C C2 FC FF 03 00 74 14 41 39 8C C2 00 00 04 00 74 0A 41 39 8C C2 04 00 04 00 75 19 41 8B 49 04 48 8D 15 ?? ?? ?? ?? 8B C1 48 C1 E8 1C 48 8B 04 C2 4C 8D 1C 88 49 8B C3 C3",
        userTag});
    s.hookRvas = {c::admitted_effect_source, c::authored_effect_wrapper, c::queue_writer, 0x0DACD8, 0x0DB19C};
    s.detours = {reinterpret_cast<const void*>(SourceHook), reinterpret_cast<const void*>(EffectHook),
        reinterpret_cast<const void*>(QueueHook), reinterpret_cast<const void*>(UpdateHook),
        reinterpret_cast<const void*>(EvaluateHook)};
    s.retireOp = golden::Op::DisableRetire;
    s.logAfterCleanup = true;
    s.quiescesWhenEmpty = true;
    s.installedLog =
        "Reach hand-specific weapon haptics installed: admitted barrel firing effects only; trigger/charge and general rumble remain native; headset verification pending";
    s.createFailLog = "Reach weapon haptics StockFallback: hook create failed";
    s.enableFailLog = "Reach weapon haptics StockFallback: hook enable failed";
    s.disableFailLog = "Reach weapon haptics CleanupRequired: disable failed";
    s.busyLog = "Reach weapon haptics CleanupRequired: callbacks or native ingress busy";
    s.removeFailLog = "Reach weapon haptics CleanupRequired: hook removal failed";
    s.reportHealthy = "Reach weapon haptics: enabled=1 fault=0 captured=0 stockFallback=0 faults=0";
    s.reportFaulted = "Reach weapon haptics: enabled=1 fault=1 captured=0 stockFallback=0 faults=0";
    s.install = InstallReachWeaponHaptics;
    s.remove = RemoveReachWeaponHaptics;
    s.report = ReportReachWeaponHaptics;
    s.anyTarget = [&]
    {
        for (auto t : r.targets)
            if (t)
                return true;
        return false;
    };
    s.enabled = [&] { return r.enabled.load(); };
    s.callbacks = [&]() -> std::atomic<uint32_t>& { return r.callbacks; };
    s.faulted = [&]() -> std::atomic<bool>& { return r.faulted; };
    return golden::RunHapticLifecycle(s);
}
