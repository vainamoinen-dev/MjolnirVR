// ODST barrel-muzzle hook set: the title's bindings, hook rows and log text, plus install, remove and report.
// Owns six optional hooks and never gates camera-core installation.
#pragma once
// Included once, by src/dll/titles/odst/odst_camera_core.cpp; also compiled by tests/odst_muzzle_tests.cpp,
//   which supplies its own versions. Not a standalone header: it uses what the host defines
//   above its include line, namely InstallOdstMuzzle, RemoveOdstMuzzle, ReportOdstMuzzle,
//   g_odstMuzzleBindingsReady.
// All addresses below are ODSTEK-matched, pinned ODST witnesses. This feature
// owns six optional hooks and never gates camera-core installation.
// Install and retire are barrel_muzzle_runtime.h's (with Reach and Halo 4);
// ODST keeps its bindings, edges, hook rows, size precondition and log text.
#include "../../core/barrel_muzzle_runtime.h"
bool RemoveOdstMuzzle();
MCCVR_MUZZLE_HOOK_OPS(OdstMuzzleHookOps, RemoveOdstMuzzle, MCCVR_MUZZLE_SIGNATURE_BOUND);
constexpr MuzzleLogText kOdstMuzzleLog{"ODST muzzle StockFallback: missing/ambiguous binding +%X",
    "ODST muzzle StockFallback: changed native edge +%X", "ODST muzzle StockFallback: hook creation failed",
    "ODST muzzle StockFallback: hook enable failed", "ODST muzzle CleanupRequired: disable failed",
    "ODST muzzle CleanupRequired: callbacks or ingress busy", "ODST muzzle CleanupRequired: removal failed"};

bool RemoveOdstMuzzle()
{
    auto& feature = g_odstMuzzle;
    const void* const functions[]{reinterpret_cast<const void*>(&OdstMuzzleFireDetour),
        reinterpret_cast<const void*>(&OdstMuzzleAimDetour), reinterpret_cast<const void*>(&OdstMuzzleQueryDetour),
        reinterpret_cast<const void*>(&OdstMuzzleViewDetour), reinterpret_cast<const void*>(&OdstMuzzleCameraDetour),
        reinterpret_cast<const void*>(&OdstMuzzleMarkersDetour)};
    void** const originals[]{reinterpret_cast<void**>(&feature.fireOriginal),
        reinterpret_cast<void**>(&feature.aimOriginal), reinterpret_cast<void**>(&feature.queryOriginal),
        reinterpret_cast<void**>(&feature.viewOriginal), reinterpret_cast<void**>(&feature.cameraOriginal),
        reinterpret_cast<void**>(&feature.markersOriginal)};
    return RemoveBarrelMuzzle<OdstMuzzleHookOps>(
        feature, g_odstMuzzleBindingsReady, functions, originals, kOdstMuzzleLog);
}
bool InstallOdstMuzzle(uintptr_t base, size_t size, uint32_t generation)
{
    auto& feature = g_odstMuzzle;
    if (BarrelMuzzleHeld(feature))
        return false;
    if (!generation || size <= 0x3B092E)
        return false;
    constexpr MuzzleBinding bindings[]{
        {0x139F3C, "40 53 48 83 EC 20 33 DB 38 19 74 12 8B 49 08 83 CA FF E8 31 80 24 00 48 85 C0 74 02 B3 01 8A C3"},
        {0x3AF230,
            "48 8B C4 48 89 58 20 4C 89 40 18 66 89 50 10 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 B8 DE FF FF B8 10 22 00"},
        {0x396B7C,
            "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 56 41 57 48 8D 68 C1 48 81 EC C0 00 00 00 44 8B 15 7A 8F 6F 00 48 8B"},
        {0x1604E0,
            "48 8B C4 66 44 89 48 20 4C 89 40 18 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 08 FF FF FF 48 81 EC B8 01 00"},
        {0x160FA0,
            "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 40 49 8B E9 49 8B F0 44 8B F1 E8 ED 94 23 00 44"},
        {0x242340,
            "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 48 83 EC 20 48 8B EA 49 8B F0 48 8D 50 20 8B D9 E8 BF 11 F1 FF 8B F8 4C"},
        {0x37F514,
            "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 66 44 89 48 20 57 41 54 41 55 41 56 41 57 48 83 EC 70 45 33 F6 8B E9 49 8B"},
        {0x1610D4,
            "48 8B C4 44 88 48 20 44 89 40 18 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 C8 FE FF FF 48 81 EC F8 01 00 00 44 8B 15"},
        {0x2E9E65, "B9 98 05 00 00 41 8B D5 48 63 C3 48 69 F0 38 4F 00 00 4A 03 34 11"},
        {0x2E9E8E, "48 69 C8 40 27 00 00 F6 44 31 30 02 74 A3 8B 4C 31 3C"},
        {0x39AB60,
            "83 C8 FF 44 0F B7 CA 66 3B D0 74 38 44 8B 05 A9 4F 6F 00 65 48 8B 04 25 58 00 00 00 BA 20 00 00 00 0F B7 C9 4A 8B 04 C0 4C 8B 04 10 48 8D 14 49 49 0F BF C9 49 8B 40 48 48 8B 44 D0 10 8B 84 88 7C 02 00 00 C3"},
        {0x3AE994, "49 8D B6 28 02 00 00"},
        {0x3AEA59, "45 8B 86 B8 02 00 00"},
        {0x3AE9F7, "41 0F BE 96 77 02 00 00"},
        {0x160FF8, "0F BE 91 76 02 00 00 8B C8 E8 5A"},
        {0x3AB1CC, "80 B9 55 01 00 00 00 74 06 8B 81 60 01 00 00"},
    };
    constexpr MuzzleEdge edges[]{{0x3AF4C5, 0x37F514}, {0x3AF889, 0x3AE8A4}, {0x3AEB26, 0x396B7C}, {0x3AED8A, 0x1610D4},
        {0x110719, 0x1604E0}, {0x160642, 0x160FA0}, {0x160A58, 0x160FA0}, {0x1611FF, 0x242340}};
    const MuzzleHook hooks[]{
        {0x3AF230, reinterpret_cast<void*>(&OdstMuzzleFireDetour), reinterpret_cast<void**>(&feature.fireOriginal)},
        {0x396B7C, reinterpret_cast<void*>(&OdstMuzzleAimDetour), reinterpret_cast<void**>(&feature.aimOriginal)},
        {0x1604E0, reinterpret_cast<void*>(&OdstMuzzleQueryDetour), reinterpret_cast<void**>(&feature.queryOriginal)},
        {0x160FA0, reinterpret_cast<void*>(&OdstMuzzleViewDetour), reinterpret_cast<void**>(&feature.viewOriginal)},
        {0x242340, reinterpret_cast<void*>(&OdstMuzzleCameraDetour), reinterpret_cast<void**>(&feature.cameraOriginal)},
        {0x37F514, reinterpret_cast<void*>(&OdstMuzzleMarkersDetour),
            reinterpret_cast<void**>(&feature.markersOriginal)}};
    if (!InstallBarrelMuzzle<OdstMuzzleHookOps>(
            feature, g_odstMuzzleBindingsReady, base, size, generation, bindings, edges, hooks, kOdstMuzzleLog))
        return false;
    LOG("ODST muzzle Installed: optional committed FP barrel with native targeting/obstruction; option=%d",
        g_config.gun_barrel_aim ? 1 : 0);
    return true;
}
void ReportOdstMuzzle()
{
    ReportMuzzleTrajectory<OdstMuzzleHookOps>(
        "ODST barrel trajectory: enabled=%d fault=%d applied=%llu refused=%llu queries=%llu restoreRefused=%llu option=%d",
        g_odstMuzzle, g_config.gun_barrel_aim);
}
