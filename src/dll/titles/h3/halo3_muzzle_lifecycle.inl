// Halo 3 barrel-muzzle hook set: the title's binding rows and log text, plus install and remove.
// Remove withdraws the enabled flag before the bindings flag, then retires the hooks.
#pragma once
// Included once, by src/dll/titles/h3/halo3_dual_wield_runtime.inl; also compiled by
//   tests/halo3_muzzle_lifecycle_fixture.inl, which supplies its own versions. Not a standalone
//   header: it uses what the host defines above its include line, namely g_halo3Dual.
// The install/remove steps are barrel_muzzle_lifecycle.h's; the rows stay
// here. Remove withdraws enabled before the bindings flag:
// both are release stores before retire's drain, each read by its own path.
#include "../../core/barrel_muzzle_lifecycle.h"
bool RemoveHalo3Muzzle();
MCCVR_MUZZLE_HOOK_OPS(Halo3MuzzleHookOps, RemoveHalo3Muzzle, MCCVR_MUZZLE_SIGNATURE_BOUND);
constexpr MuzzleLogText kHalo3MuzzleLog{"Halo 3 muzzle StockFallback: missing/ambiguous binding +%X",
    "Halo 3 muzzle StockFallback: marker firing edge changed",
    "Halo 3 muzzle StockFallback: marker hook creation failed",
    "Halo 3 muzzle StockFallback: marker hook enable failed",
    "Halo 3 muzzle CleanupRequired: disable failed (status %d)",
    "Halo 3 muzzle CleanupRequired: callbacks or ingress busy", "Halo 3 muzzle CleanupRequired: removal failed", true};

bool RemoveHalo3Muzzle()
{
    auto& feature = g_halo3Muzzle;
    const void* const functions[]{reinterpret_cast<const void*>(&Halo3MuzzleMarkersDetour)};
    void** const originals[]{reinterpret_cast<void**>(&feature.original)};
    return RemoveBarrelMuzzle<Halo3MuzzleHookOps>(
        feature, g_halo3MuzzleBindingsReady, functions, originals, kHalo3MuzzleLog);
}

bool InstallHalo3Muzzle(uintptr_t base, size_t size)
{
    auto& feature = g_halo3Muzzle;
    if (BarrelMuzzleHeld(feature) || !g_halo3Dual.enabled.load() || size <= 0x368651)
    {
        LOG("Halo 3 muzzle StockFallback: firing unavailable or cleanup pending");
        return false;
    }
    constexpr MuzzleBinding bindings[]{
        {0x343D74,
            "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 66 44 89 48 20 57 41 54 41 55 41 56 41 57 48 83 EC 70 45 33 F6 8B E9 49 8B"},
        {0x2C24AC, "B9 68 05 00 00 4A 8B 14 D0 4D 69 D1 30 24 00 00 49 69 EC BC 11 00 00 48 8B 3C 11 49 03 FA 45 33"},
        {0x2C250F, "0F B7 44 2F 3C 48 8D 0C 40 48 8B 42 48 48 8B 4C C8 10 48 8B 05 F0 6A 78 00 0F B7 11 8B 4C D0 04"}};
    constexpr MuzzleEdge edges[]{{0x36864C, 0x343D74}};
    const MuzzleHook hooks[]{
        {0x343D74, reinterpret_cast<void*>(&Halo3MuzzleMarkersDetour), reinterpret_cast<void**>(&feature.original)}};
    if (!InstallBarrelMuzzle<Halo3MuzzleHookOps>(feature, g_halo3MuzzleBindingsReady, base, size,
            g_halo3Dual.generation, bindings, edges, hooks, kHalo3MuzzleLog))
        return false;
    LOG("Halo 3 muzzle Installed: optional committed FP marker, native targeting/obstruction; option=%d",
        g_config.gun_barrel_aim ? 1 : 0);
    return true;
}
