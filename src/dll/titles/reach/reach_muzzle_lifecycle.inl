// Halo Reach barrel-muzzle hook set: the title's binding rows and log text, plus install, remove and report.
// Its four optional hooks share the core unit-adjust trampoline and retire before that core.
#pragma once
// Included once, by src/dll/core/game.cpp; also compiled by
//   tests/reach_muzzle_tests.cpp, which supplies its own versions. Not a standalone header: it
//   uses what the host defines above its include line, namely InstallReachMuzzle,
//   RemoveReachMuzzle, ReportReachMuzzle, g_origReachUnitAdjust, g_reachBarrelBindingsReady,
//   g_reachUnitAdjustTarget, g_reachVehicleShotRedirectEnabled.
// HREK-first witnesses matched to pinned Reach. Four optional hooks share
// the installed core unit-adjust trampoline; retire these before that core.
// The install/remove steps are barrel_muzzle_runtime.h's; the rows stay here.
#include "../../core/barrel_muzzle_runtime.h"
bool RemoveReachMuzzle();
MCCVR_MUZZLE_HOOK_OPS(ReachMuzzleHookOps, RemoveReachMuzzle, MCCVR_MUZZLE_SIGNATURE_BOUND);
constexpr MuzzleLogText kReachMuzzleLog{"Reach muzzle StockFallback: missing/ambiguous binding +%X",
    "Reach muzzle StockFallback: changed native edge +%X", "Reach muzzle StockFallback: hook creation failed",
    "Reach muzzle StockFallback: hook enable failed", "Reach muzzle CleanupRequired: disable failed",
    "Reach muzzle CleanupRequired: callbacks or ingress busy", "Reach muzzle CleanupRequired: removal failed"};

bool RemoveReachMuzzle()
{
    auto& feature = g_reachBarrel;
    const void* const functions[]{reinterpret_cast<const void*>(&ReachMuzzleFireDetour),
        reinterpret_cast<const void*>(&ReachMuzzleQueryDetour), reinterpret_cast<const void*>(&ReachMuzzleViewDetour),
        reinterpret_cast<const void*>(&ReachMuzzleMarkersDetour)};
    void** const originals[]{reinterpret_cast<void**>(&feature.fireOriginal),
        reinterpret_cast<void**>(&feature.queryOriginal), reinterpret_cast<void**>(&feature.viewOriginal),
        reinterpret_cast<void**>(&feature.markersOriginal)};
    return RemoveBarrelMuzzle<ReachMuzzleHookOps>(
        feature, g_reachBarrelBindingsReady, functions, originals, kReachMuzzleLog);
}
bool InstallReachMuzzle(uintptr_t base, size_t size, uint32_t generation)
{
    auto& feature = g_reachBarrel;
    if (BarrelMuzzleHeld(feature))
        return false;
    if (!generation || size <= 0x4C4924 || !g_origReachUnitAdjust ||
        g_reachUnitAdjustTarget != reinterpret_cast<void*>(base + 0x484F24) ||
        !g_reachVehicleShotRedirectEnabled.load(std::memory_order_acquire))
        return false;
    constexpr MuzzleBinding bindings[]{
        {0x4C2710,
            "48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 C0 D9 FF FF B8 40 27 00 00 E8 9E 9B 3A 00 48 2B E0 0F 29 B4"},
        {0x10E970,
            "48 8B C4 48 89 58 10 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 E8 FE FF FF 48 81 EC E0 01 00 00 0F 29 70 B8 0F 29 78 A8"},
        {0x10FA74,
            "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 30 49 8B F1 0F B6 DA 49 8B E8 8B F9 E8 D7 B1 37"},
        {0x47044C,
            "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 66 44 89 48 20 57 41 54 41 55 41 56 41 57 48 81 EC 80 00 00 00 45 33 D2 8B"},
        // +0x484F24 (unit adjust) is not re-read here: the vehicle shot line
        // proved its exact body before hooking it, so its entry now holds that
        // hook's jump and a byte check always failed ("missing/ambiguous
        // binding +484F24" in every session). Ownership of that exact entry is
        // required below instead; the +0x4C303A call edge still lands on it.
        {0x2B1234,
            "BA A0 06 00 00 4E 8B 0C C0 48 63 C1 48 69 C8 A8 53 00 00 33 C0 49 03 0C 11 8B D0 48 83 C1 3C 44 39 19 74 14 FF C0 48 FF C2 48 81 C1 78 29 00 00 48 83 FA 02 7C E9"},
        {0x48B3D0,
            "83 C8 FF 44 0F B7 CA 66 3B C2 74 38 44 8B 05 35 C7 78 00 65 48 8B 04 25 58 00 00 00 BA 10 00 00 00 0F B7 C9 4A 8B 04 C0 4C 8B 04 10 48 8D 14 49 49 0F BF C9 49 8B 40 50 48 8B 44 D0 10 8B 84 88"},
        {0x4C2F57, "48 8D 82 BB 02 00 00 48 83 E0 FC 48 89 85 A8 00 00 00 F6 40 24 02"},
        {0x48379E, "44 8B 81 90 03 00 00 41 83 F8 FF 75 E3 41 8B C1 C3"},
        {0x4C2A4F, "0F BE 90 4B 03 00 00 E8 75 89 FC FF"},
        {0x4C31B0, "0F BE 91 4A 03 00 00 8B CB E8 12 82 FC FF"},
        {0x4BE0E8, "8B 91 2C 03 00 00 3B D0 74 03 8B C2 C3 80 B9 A9 01 00 00 00 74 06 8B 81 B4 01 00 00 C3"},
        {0x106F04,
            "40 53 48 83 EC 20 33 DB 38 19 74 12 8B 49 08 83 CA FF E8 B1 CA 36 00 48 85 C0 74 02 B3 01 8A C3 48 83 C4 20 5B"},
        {0x10F8F4,
            "4C 8B DC 49 89 5B 08 49 89 6B 10 49 89 73 18 49 89 7B 20 41 56 48 83 EC 60 48 8B 84 24 B8 00 00 00 41 8A F0 48 8B AC 24"},
    };
    constexpr MuzzleEdge edges[]{
        {0x4C2A9B, 0x47044C},
        {0x4C303A, 0x484F24},
        {0x4C3293, 0x10FB94},
        {0x5EF44, 0x10E970},
        {0x10EAC8, 0x10FA74},
        {0x10EEEC, 0x10FA74},
        {0x10F94F, 0x10FA74},
        {0x10F583, 0x10F8F4},
    };
    const MuzzleHook hooks[]{
        {0x4C2710, reinterpret_cast<void*>(&ReachMuzzleFireDetour), reinterpret_cast<void**>(&feature.fireOriginal)},
        {0x10E970, reinterpret_cast<void*>(&ReachMuzzleQueryDetour), reinterpret_cast<void**>(&feature.queryOriginal)},
        {0x10FA74, reinterpret_cast<void*>(&ReachMuzzleViewDetour), reinterpret_cast<void**>(&feature.viewOriginal)},
        {0x47044C, reinterpret_cast<void*>(&ReachMuzzleMarkersDetour),
            reinterpret_cast<void**>(&feature.markersOriginal)}};
    if (!InstallBarrelMuzzle<ReachMuzzleHookOps>(
            feature, g_reachBarrelBindingsReady, base, size, generation, bindings, edges, hooks, kReachMuzzleLog))
        return false;
    LOG("Reach muzzle Installed: optional committed FP barrel with native targeting/obstruction; option=%d",
        g_config.gun_barrel_aim ? 1 : 0);
    return true;
}
void ReportReachMuzzle()
{
    ReportMuzzleTrajectory<ReachMuzzleHookOps>(
        "Reach barrel trajectory: enabled=%d fault=%d applied=%llu refused=%llu queries=%llu restoreRefused=%llu option=%d",
        g_reachBarrel, g_config.gun_barrel_aim);
}
