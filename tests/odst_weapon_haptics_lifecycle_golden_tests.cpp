// Golden fixture: pins the ODST weapon-haptics install/remove/report lifecycle
// (odst_weapon_haptics.inl) exactly, so any refactor of it, such as a shared haptic
// lifecycle, can be checked against it unchanged. Compiled with sigscan.cpp and the
// golden_haptic_lifecycle.h MinHook fixtures; no real hook is installed.
#include "golden_haptic_lifecycle.h"
#include <cmath>
#include "../src/common/runtime_types.h"
#include "hook_checked_forwarders.h" // DLL hook sites call MinHook through the checked names
#include "../src/dll/core/sigscan.h"
#include "../src/dll/core/guard_count.h"

// ---- game-side stubs the lifecycle file references ----
struct
{
    std::atomic<bool> armed{true}, teardownRequested{false};
} g_odstCamera;
std::atomic<uint32_t> g_odstRuntimeGeneration{7};
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
    return GameTitle::Halo3ODST;
}
RuntimeMode TitleAdapter_GetRuntimeMode()
{
    return RuntimeMode::Gameplay;
}
int32_t (*g_odstPlayerUnitGetter)(int32_t) = nullptr;
void* OdstMuzzleTargetStorage(uint32_t)
{
    return nullptr;
}
CinematicControlState ReadOdstCinematicControl(int32_t&, int32_t&)
{
    return CinematicControlState::PlayerControlled;
}
bool OdstReadMuzzleWeapons(uint32_t, uint32_t (&)[2])
{
    return false;
}
int ResolveEquippedWeaponSlot(uint32_t, uint32_t, uint32_t, bool, bool)
{
    return -1;
}
const uint8_t* OdstContactTls()
{
    return nullptr;
}
uint64_t VR_WeaponHapticToken(GameTitle, uint32_t, bool)
{
    return 1;
}
bool VR_PulseWeaponHaptics(GameTitle, uint32_t, bool, bool, float, uint64_t)
{
    return true;
}

#include "../src/dll/titles/odst/odst_weapon_haptics.inl"

int main()
{
    using namespace odst_weapon_haptics;
    auto& r = runtime;
    golden::HapticSpec s;
    s.name = "ODST";
    s.imageSize = 0x2100000;
    s.smallSize = 0x2022AB0;
    s.smallSizeLog = nullptr;
    s.rejectsZeroGeneration = true;
    s.bindings = {
        {0x3ACCC8,
            "48 8B C4 48 89 58 18 66 89 50 10 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 10 01 00 00 4C 8B 2D B9 5D C7 01 45"},
        {0x3A1464,
            "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 4C 89 70 20 55 48 8D 68 D8 48 81 EC 20 01 00 00 44 8B 05 92 E6 6E 00 0F 29"},
        {0x1E3D40,
            "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 41 54 41 55 41 56 41 57 48 83 EC 50 4C 8B 3D 45 ED E3 01 33 FF 45 0F B7"},
        {0x1BE0B0,
            "48 89 5C 24 08 55 56 57 41 56 41 57 48 83 EC 40 45 33 FF 0F 29 74 24 30 44 38 3D 39 45 8D 00 0F 28 F0 44 89 7C 24 78 0F"},
        {0x1BE2B0,
            "48 8B C4 48 89 58 18 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 70 F3 0F 10 81 90 00 00 00 48 8D 79"},
        {0x27B458,
            "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 41 56 41 57 48 81 EC 90 00 00 00 8B 71 0C 33 FF 4C 8B 35 29 76 DA 01 0F"},
        {0x1E3F6E,
            "4D 69 C2 98 00 00 00 B8 B0 02 00 00 45 8B D1 B9 01 00 00 00 4E 03 04 30 F3 41 0F 10 48 70 49 8D 50 74 F3 0F 10 02 0F 28"},
        {0x3AD4B0,
            "40 84 ED 0F 84 13 06 00 00 8B 4C 24 68 85 C9 74 1D F3 0F 10 8C 1E CC 01 00 00 66 0F 6E C1 0F 5B C0 F3 0F 5C C8 F3 0F 11"},
        {0x1BE2E5,
            "4C 8B 3D BC 47 E6 01 41 BD 08 00 00 00 48 8B 1D AF 0D 8E 00 45 33 C0 0F 29 70 B8 F3 0F 10 35 88 B0 6C 00 0F 29 78 A8 44"},
        {0x1BE028,
            "40 53 48 83 EC 20 8B 0D E8 1A 8D 00 33 D2 65 48 8B 04 25 58 00 00 00 41 B8 6C 02 00 00 BB B0 02 00 00 48 8B 04 C8 48 8B"},
        {0x1BE055,
            "E8 CF 23 4A 00 48 83 C3 08 BA 04 00 00 00 41 83 C8 FF 48 8B C3 B9 08 00 00 00 44 89 40 F8 44 89 40 FC C7 00 00 00 80 3F 48 8D 40 0C 48 83 E9 01 75 E8 48 81 C3 98 00 00 00 48 83 EA 01 75 D3 48 83 C4 20 5B"},
    };
    s.bindingLogFormat = "ODST weapon haptics StockFallback: own binding +%llX missing/ambiguous";
    s.edges = {{0x3AD8ED, 0x3A1464}, {0x3AD9B8, 0x3A1464}, {0x3A15FC, 0x1E3B98}, {0x1E3CD6, 0x1E3D40},
        {0x1E3D1B, 0x1E3D40}, {0x1BE11D, 0x1BE2B0}, {0x1BE39F, 0x27B458}};
    s.edgeLog = "ODST weapon haptics StockFallback: native edge mismatch";
    s.hookRvas = {0x3ACCC8, 0x3A1464, 0x1E3D40, 0x1BE0B0, 0x1BE2B0};
    s.detours = {reinterpret_cast<const void*>(ScopeHook), reinterpret_cast<const void*>(EffectHook),
        reinterpret_cast<const void*>(WorkerHook), reinterpret_cast<const void*>(UpdateHook),
        reinterpret_cast<const void*>(EvaluateHook)};
    s.retireOp = golden::Op::DisableRetire;
    s.logAfterCleanup = true;
    s.quiescesWhenEmpty = true;
    s.installedLog =
        "ODST hand-specific weapon haptics installed: admitted barrel response, authored private voices, generic rumble retained; headset verification pending";
    s.createFailLog = "ODST weapon haptics StockFallback: create failed";
    s.enableFailLog = "ODST weapon haptics StockFallback: enable failed; retained cleanup if needed";
    s.disableFailLog = "ODST weapon haptics CleanupRequired: disable failed";
    s.busyLog = "ODST weapon haptics CleanupRequired: callbacks or ingress busy";
    s.removeFailLog = "ODST weapon haptics CleanupRequired: removal failed";
    s.reportHealthy = "ODST weapon haptics: enabled=1 fault=0 captured=0 stockFallback=0 faults=0";
    s.reportFaulted = "ODST weapon haptics: enabled=1 fault=1 captured=0 stockFallback=0 faults=0";
    s.install = InstallOdstWeaponHaptics;
    s.remove = RemoveOdstWeaponHaptics;
    s.report = ReportOdstWeaponHaptics;
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
