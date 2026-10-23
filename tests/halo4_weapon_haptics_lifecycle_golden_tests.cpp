// Golden tests for the Halo 4 weapon-haptics lifecycle (src/dll/titles/h4/halo4_weapon_haptics.inl), driven by
// golden_haptic_lifecycle.h. Built as halo4_weapon_haptics_lifecycle_golden_tests with src/dll/core/sigscan.cpp
// and run by ctest. Pins the CURRENT install/remove/report lifecycle (size check after the binding proofs; no
// generation guard) so the NativeHapticLifecycle fold can be checked against it. No game process.
#include "golden_haptic_lifecycle.h"
#include <cmath>
#include "../src/common/runtime_types.h"
#include "../src/common/titles/h4/halo4_vehicle_input_logic.h"
#include "hook_checked_forwarders.h" // DLL hook sites call MinHook through the checked names
#include "../src/dll/core/sigscan.h"
#include "../src/dll/core/guard_count.h"

struct
{
    std::atomic<bool> armed{true}, teardownRequested{false};
    std::atomic<uint32_t> generation{7};
} g_halo4Camera;
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
    return GameTitle::Halo4;
}
RuntimeMode TitleAdapter_GetRuntimeMode()
{
    return RuntimeMode::Gameplay;
}
const uint8_t* Halo4MuzzleTls()
{
    return nullptr;
}
const uint8_t* Halo4MuzzleObject(uint32_t, uint32_t)
{
    return nullptr;
}
CinematicControlState ReadHalo4CinematicControl() noexcept
{
    return CinematicControlState::PlayerControlled;
}
bool Halo4ReadMuzzleWeapons(uint32_t, uint32_t[2], bool* = nullptr)
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

#include "../src/dll/titles/h4/halo4_weapon_haptics.inl"

int main()
{
    using namespace halo4_weapon_haptics;
    auto& r = runtime;
    golden::HapticSpec s;
    s.name = "Halo 4";
    s.imageSize = 0x4A00000;
    s.smallSize = 0x700000;
    s.smallSizeLog = "Halo 4 weapon haptics unavailable: own source/evaluation edges mismatch; generic rumble retained";
    s.rejectsZeroGeneration = false;
    s.bindings = {
        {0x6161DC,
            "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 56 48 83 EC 40 49 8B D8 48 63 EA 8B F9 E8 37 C0 FF FF 44 8B D0"},
        {0x603DE0,
            "48 8B C4 55 53 56 57 41 56 48 8D A8 F8 FD FF FF 48 81 EC E0 02 00 00 0F 29 70 C8 48 8B 05 0E 72 83 00 48 33 C4 48 89 85"},
        {0x145E80,
            "48 89 7C 24 08 83 CF FF 0F 28 DA 44 8B DA 3B D7 0F 84 16 01 00 00 44 8B 05 7B 13 F1 00 65 48 8B 04 25 58 00 00 00 BA D0"},
        {0x145C48,
            "48 8B C4 48 89 58 08 48 89 68 18 48 89 70 20 57 41 54 41 55 41 56 41 57 48 83 EC 40 33 F6 0F 29 70 C8 40 38 35 67 5E F3"},
        {0x1E7B64,
            "48 8B C4 48 89 58 18 55 56 57 41 55 41 56 48 8D 68 A1 48 81 EC D0 00 00 00 83 39 FF 48 8B D9 0F 29 70 C8 0F 29 78 B8 44"},
        {0x234E08,
            "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 57 48 81 EC 90 00 00 00 8B 59 0C 4C 8D 3D 52 53 73 04 0F 29"},
        {0x235460,
            "8B 51 0C 48 8D 0D 16 4D 73 04 8B C2 48 C1 E8 1C 48 8B 0C C1 80 7C 91 02 00 75 43 F6 44 91 01 04 F3 0F 10 54 91 08 74 24"},
        {0x1460C8,
            "48 8B C4 48 89 58 10 48 89 68 18 48 89 70 20 57 41 54 41 55 41 56 41 57 48 83 EC 50 F3 0F 10 81 D0 01 00 00 48 8D 79 04"},
        {0x1E69E3,
            "8B 54 9F 58 4C 8D 4D B7 0F 28 D6 8B CE E8 8B F4 F5 FF 41 83 C8 FF 45 3B E8 0F 84 AA 00 00 00 48 8B 05 A7 56 E9 00 4C 8B"},
        {0x615390,
            "45 84 ED 74 0E 4C 8D 44 24 78 8B D5 8B CF E8 39 0E 00 00 4C 8D 44 24 78 8B D5 8B CF E8 9F 0F 00 00 48 8B 54 24 68 4C 8B"},
        {0x1460FF,
            "4C 8B 25 AA 5F F3 00 48 8B D9 0F 29 70 C8 41 BF 08 00 00 00 F3 0F 10 35 B5 F5 C4 00 45 33 ED 0F 29 78 B8 0F 57 FF F3 0F"},
        {0x14613E,
            "0F B7 47 FC 48 8D 0D 37 40 82 04 49 8B ED 41 8B 54 C4 04 8B C2 48 C1 E8 1C 48 8D 72 01 48 8B 0C C1 48 8D 34 B1 F3 0F 10"},
        {0x145CE3, "8B FE B8 D0 02 00 00 4C 8B F6 49 69 DE D8 01 00 00 8B CF 4A 03 1C 28 E8 85 FE F4 FF 83 F8 FF"}};
    s.bindingLogFormat =
        "Halo 4 weapon haptics unavailable: own native binding +0x%llX missing/ambiguous; generic rumble retained";
    s.edges = {{0x61539E, 0x6161DC}, {0x616232, 0x603DE0}, {0x616324, 0x603DE0}, {0x1E69F0, 0x145E80},
        {0x145D72, 0x1460C8}, {0x146194, 0x234E08}, {0x14619F, 0x235460}};
    s.edgeLog = "Halo 4 weapon haptics unavailable: own source/evaluation edges mismatch; generic rumble retained";
    s.hookRvas = {0x6161DC, 0x603DE0, 0x145E80, 0x145C48, 0x1460C8};
    s.detours = {reinterpret_cast<const void*>(ScopeHook), reinterpret_cast<const void*>(EffectHook),
        reinterpret_cast<const void*>(EnqueueHook), reinterpret_cast<const void*>(UpdateHook),
        reinterpret_cast<const void*>(EvaluateHook)};
    s.retireOp = golden::Op::DisableRetire;
    s.logAfterCleanup = true;
    s.quiescesWhenEmpty = true;
    s.installedLog =
        "Halo 4 hand-specific weapon haptics installed: native authored firing response, independent primary/secondary, generic rumble unchanged; headset verification pending";
    s.createFailLog = "Halo 4 weapon haptics unavailable: hook creation failed; generic rumble retained";
    s.enableFailLog = "Halo 4 weapon haptics unavailable: hook enable failed; cleanup retained if needed";
    s.disableFailLog = "Halo 4 weapon haptics CleanupRequired: hook disable failed";
    s.busyLog = "Halo 4 weapon haptics CleanupRequired: callbacks or ingress busy";
    s.removeFailLog = "Halo 4 weapon haptics CleanupRequired: hook removal failed";
    s.reportHealthy = "Halo 4 weapon haptics: enabled=1 fault=0 captured=0 stockFallback=0 faults=0";
    s.reportFaulted = "Halo 4 weapon haptics: enabled=1 fault=1 captured=0 stockFallback=0 faults=0";
    s.install = InstallHalo4WeaponHaptics;
    s.remove = RemoveHalo4WeaponHaptics;
    s.report = ReportHalo4WeaponHaptics;
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
