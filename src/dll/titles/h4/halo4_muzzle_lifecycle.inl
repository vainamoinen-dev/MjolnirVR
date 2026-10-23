// Halo 4 barrel-muzzle hook set: the title's binding rows and log text, plus install and remove.
// The five optional hooks retire before the camera core and native seat reader.
#pragma once
// Included once, by src/dll/titles/h4/halo4_camera_core.cpp; also compiled by
//   tests/halo4_muzzle_tests.cpp, which supplies its own versions. Not a standalone header: it
//   uses what the host defines above its include line, namely g_halo4Camera.
// H4EK-first firing/targeting/ownership witnesses matched to pinned Halo4.
// Five optional hooks retire before the camera core and native seat reader.
// The install/remove steps are barrel_muzzle_runtime.h's; the rows stay here.
#include "../../core/barrel_muzzle_runtime.h"
bool RemoveHalo4Muzzle();
// +3B1B4C is the first-person producer that
// InstallHalo4Vrik hooks just before this set installs, so its entry bytes are
// our own jump when this proof runs; a plain pattern check here kept the barrel
// set from ever installing. Vrik hooks it only after proving a longer form of this
// same pattern unique at exactly this RVA, so that owned hook IS the proof for
// this one binding. Any failure logs which proof failed.
constexpr uint32_t kHalo4MuzzleProducerRva = 0x3B1B4C;
bool Halo4MuzzleBound(uintptr_t base, size_t size, const MuzzleBinding& binding)
{
    const uintptr_t match = sig::Find(base, size, binding.pattern);
    const char* failure = !match                        ? "no match in the module"
                          : match != base + binding.rva ? "matched at another address"
                          : sig::Find(match + 1, base + size - match - 1, binding.pattern) ? "matched more than once"
                                                                                           : nullptr;
    if (!failure)
        return true;
    const bool producer = binding.rva == kHalo4MuzzleProducerRva;
    if (producer && g_halo4Camera.firstPersonProducerTarget &&
        g_halo4Camera.firstPersonProducerTarget == reinterpret_cast<void*>(base + binding.rva))
        return true;
    LOG("Halo4 muzzle binding +%X: %s (first match +%llX)%s", binding.rva, failure,
        static_cast<unsigned long long>(match ? match - base : 0),
        producer ? "; not owned by the avatar producer hook either" : "");
    return false;
}
MCCVR_MUZZLE_HOOK_OPS(Halo4MuzzleHookOps, RemoveHalo4Muzzle, Halo4MuzzleBound(base, size, binding));
constexpr MuzzleLogText kHalo4MuzzleLog{"Halo4 muzzle StockFallback: missing/ambiguous binding +%X",
    "Halo4 muzzle StockFallback: changed native edge +%X", "Halo4 muzzle StockFallback: hook creation failed",
    "Halo4 muzzle StockFallback: hook enable failed", "Halo4 muzzle CleanupRequired: disable failed",
    "Halo4 muzzle CleanupRequired: callbacks or ingress busy", "Halo4 muzzle CleanupRequired: removal failed"};

bool RemoveHalo4Muzzle()
{
    auto& feature = g_halo4Barrel;
    const void* const functions[]{reinterpret_cast<const void*>(&Halo4MuzzleFireDetour),
        reinterpret_cast<const void*>(&Halo4MuzzleQueryDetour), reinterpret_cast<const void*>(&Halo4MuzzleViewDetour),
        reinterpret_cast<const void*>(&Halo4MuzzleMarkersDetour), reinterpret_cast<const void*>(&Halo4MuzzleAimDetour)};
    void** const originals[]{reinterpret_cast<void**>(&feature.fireOriginal),
        reinterpret_cast<void**>(&feature.queryOriginal), reinterpret_cast<void**>(&feature.viewOriginal),
        reinterpret_cast<void**>(&feature.markersOriginal), reinterpret_cast<void**>(&feature.aimOriginal)};
    return RemoveBarrelMuzzle<Halo4MuzzleHookOps>(
        feature, g_halo4BarrelBindingsReady, functions, originals, kHalo4MuzzleLog);
}
bool InstallHalo4Muzzle(uintptr_t base, size_t size, uint32_t generation)
{
    auto& feature = g_halo4Barrel;
    if (BarrelMuzzleHeld(feature))
        return false;
    if (!generation || size <= 0x619607 || !g_halo4VehicleInput.ready.load(std::memory_order_acquire))
        return false;
    constexpr MuzzleBinding bindings[]{
        {0x6176B8,
            "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 88 DC FF FF B8 78 24 00 00 E8 39 91 44 00 48 2B E0 0F 29 B4 24 60 24"},
        {0x1DB840,
            "48 8B C4 48 89 58 10 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 B8 FE FF FF 48 81 EC 10 02 00 00 0F 29 70 B8 0F 29 78 A8"},
        {0x1DC9E4,
            "48 8B C4 48 89 58 08 48 89 70 18 55 57 41 56 48 8D 68 B1 48 81 EC B0 00 00 00 0F 29 70 D8 49 8B F1 0F 29 78 C8 8B F9 0F"},
        {0x5D5B74,
            "48 89 5C 24 10 66 44 89 4C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 80 00 00 00 45 33 E4 8B F1 49 8B F8 44 8B FA"},
        {0x5F3510,
            "48 8B C4 55 53 56 57 41 56 41 57 48 8D 68 C9 48 81 EC D8 00 00 00 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 98 44 0F 29 48 88"},
        {0x5FA1C4,
            "44 8B 05 4D D0 A5 00 44 0F B7 CA 65 48 8B 04 25 58 00 00 00 BA 18 00 00 00 4A 8B 04 C0 4C 8B 04 02 83 C8 FF 66 41 3B C1"},
        {0x5F9E20,
            "44 8B 05 F1 D3 A5 00 65 48 8B 04 25 58 00 00 00 48 63 D2 41 BA 18 00 00 00 4A 8B 04 C0 44 0F B7 C1 4D 8B 14 02 4F 8D 0C"},
        {0x612238,
            "8B 15 DA 4F A4 00 65 48 8B 04 25 58 00 00 00 0F B7 C9 41 B8 18 00 00 00 48 8B 04 D0 48 8D 14 49 4D 8B 04 00 49 8B 40 50"},
        {0x5F1ABC,
            "65 48 8B 04 25 58 00 00 00 44 8B C1 8B 0D 4A 57 A6 00 BA 18 00 00 00 48 8B 04 C8 48 8B 0C 02 4C 8B 49 50 41 0F B7 C8 48"},
        {0x1CD454,
            "40 53 48 83 EC 20 48 8B D9 83 CA FF 8B 49 08 E8 98 CF 40 00 80 3B 00 4C 8B C8 74 0A 48 85 C0 74 05 41 B0 01 EB 03 45 32"},
        {0x1DC86C,
            "4C 8B DC 49 89 5B 08 49 89 6B 10 49 89 73 18 49 89 7B 20 41 56 48 83 EC 60 48 8B 84 24 B8 00 00 00 44 8B D2 48 8B AC 24"},
        {0x3B1B4C,
            "48 8B C4 4C 89 48 20 44 89 40 18 89 50 10 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 C8 FE FF FF 48 81 EC F8"},
        {0x3B1F11,
            "8B 4B D0 48 89 44 24 28 89 7C 24 20 E8 42 76 00 00 4C 8B 2D 87 A1 CC 00 45 85 FF 75 32 48 85 F6 74 2D 4D 85 E4 74 28 8B"},
        {0x610F6C,
            "45 33 C0 4C 63 CA 83 F9 FF 0F 84 97 00 00 00 41 83 F9 01 0F 87 8D 00 00 00 44 8B 05 8C 62 A4 00 4C 8D 15 ED 91 35 04 65"},
    };
    constexpr MuzzleEdge edges[]{
        {0x6179B2, 0x5D5B74},
        {0x617E90, 0x5F3510},
        {0x618103, 0x1DCB8C},
        {0xA364C, 0x1DB840},
        {0xA36AE, 0x1DB840},
        {0xA3723, 0x1DB840},
        {0x1DB9A1, 0x1DC9E4},
        {0x1DBDB1, 0x1DC9E4},
        {0x1DC8BF, 0x1DC9E4},
        {0x1DC42B, 0x1DC86C},
    };
    const MuzzleHook hooks[]{
        {0x6176B8, reinterpret_cast<void*>(&Halo4MuzzleFireDetour), reinterpret_cast<void**>(&feature.fireOriginal)},
        {0x1DB840, reinterpret_cast<void*>(&Halo4MuzzleQueryDetour), reinterpret_cast<void**>(&feature.queryOriginal)},
        {0x1DC9E4, reinterpret_cast<void*>(&Halo4MuzzleViewDetour), reinterpret_cast<void**>(&feature.viewOriginal)},
        {0x5D5B74, reinterpret_cast<void*>(&Halo4MuzzleMarkersDetour),
            reinterpret_cast<void**>(&feature.markersOriginal)},
        {0x5F3510, reinterpret_cast<void*>(&Halo4MuzzleAimDetour), reinterpret_cast<void**>(&feature.aimOriginal)}};
    if (!InstallBarrelMuzzle<Halo4MuzzleHookOps>(
            feature, g_halo4BarrelBindingsReady, base, size, generation, bindings, edges, hooks, kHalo4MuzzleLog))
        return false;
    LOG("Halo4 muzzle Installed: optional committed FP barrel with native targeting/obstruction; option=%d",
        g_config.gun_barrel_aim ? 1 : 0);
    return true;
}
void ReportHalo4Muzzle()
{
    ReportMuzzleTrajectory<Halo4MuzzleHookOps>(
        "Halo4 barrel trajectory: enabled=%d fault=%d applied=%llu refused=%llu queries=%llu restoreRefused=%llu option=%d",
        g_halo4Barrel, g_config.gun_barrel_aim);
}
