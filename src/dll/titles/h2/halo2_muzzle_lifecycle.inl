// Install and remove steps for the Halo 2 muzzle marker hook.
// Included once, by src/dll/titles/h2/halo2_dual_wield_runtime.inl; also compiled by
//   tests/halo2_muzzle_lifecycle_fixture.inl, which supplies its own versions. Not a standalone
//   header: it uses what the host defines above its include line, namely g_halo2Dual.
#pragma once
// The install/remove steps are barrel_muzzle_lifecycle.h's (H2 has no
// bindings flag, so it uses the flagless overloads); the rows stay here.
#include "../../core/barrel_muzzle_lifecycle.h"
bool RemoveHalo2Muzzle();
// H2 counts every match: exactly one, at the pinned RVA.
MCCVR_MUZZLE_HOOK_OPS(Halo2MuzzleHookOps, RemoveHalo2Muzzle,
    [&]
    {
        uintptr_t match = 0;
        uint32_t count = 0;
        return CountPatternMatches(base, size, binding.pattern, match, count) && count == 1 &&
               match == base + binding.rva;
    }());
constexpr MuzzleLogText kHalo2MuzzleLog{"Halo 2 muzzle StockFallback: missing/ambiguous marker binding +%X",
    "Halo 2 muzzle StockFallback: native marker edge changed +%X",
    "Halo 2 muzzle StockFallback: marker hook creation failed",
    "Halo 2 muzzle StockFallback: marker hook enable failed",
    "Halo 2 muzzle CleanupRequired: disable failed (status %d)",
    "Halo 2 muzzle CleanupRequired: callbacks or ingress busy", "Halo 2 muzzle CleanupRequired: removal failed", true};

bool RemoveHalo2Muzzle()
{
    auto& feature = g_halo2Muzzle;
    const void* const functions[]{reinterpret_cast<const void*>(&Halo2MuzzleMarkersDetour)};
    void** const originals[]{reinterpret_cast<void**>(&feature.original)};
    return RemoveBarrelMuzzle<Halo2MuzzleHookOps>(feature, functions, originals, kHalo2MuzzleLog);
}

bool InstallHalo2Muzzle(uintptr_t base, size_t size)
{
    auto& feature = g_halo2Muzzle;
    if (BarrelMuzzleHeld(feature) || !g_halo2Dual.enabled.load() || size <= 0x8E8A10)
    {
        LOG("Halo 2 muzzle StockFallback: firing transaction unavailable or cleanup pending");
        return false;
    }
    constexpr MuzzleBinding bindings[]{
        {0x8D6570, "48 83 EC 38 C6 44 24 28 00 C6 44 24 20 00 E8 1D AC FF FF 48 83 C4 38 C3"},
        {0x8D11A0,
            "48 89 5C 24 10 66 44 89 4C 24 20 55 56 57 41 55 41 56 48 83 EC 60 33 F6 44 8B F1 49 8B D8 44 8B EA 8B F9 0F B7 EE"},
        {0x8E8990,
            "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B 05 F2 E9 FC 00 8B D9 48 8B 48 48 48 85 C9 74 06 48 8D 14 01 EB 02 33 D2"},
        {0x8E89E7, "E8 64 E7 FE FF 84 C0 75 06 83 7F 38 FF 75 08 83 7E 14 FF 0F 45 5E 14 48 8B 74 24 38 8B C3"}};
    constexpr MuzzleEdge edges[]{{0x8E4B94, 0x8E8990}, {0x8E4BAA, 0x8D6570}, {0x8D657E, 0x8D11A0}};
    const MuzzleHook hooks[]{
        {0x8D6570, reinterpret_cast<void*>(&Halo2MuzzleMarkersDetour), reinterpret_cast<void**>(&feature.original)}};
    if (!InstallBarrelMuzzle<Halo2MuzzleHookOps>(
            feature, base, size, g_halo2Dual.generation, bindings, edges, hooks, kHalo2MuzzleLog))
        return false;
    LOG("Halo 2 muzzle Installed: optional committed FP marker, native targeting/obstruction; option=%d",
        g_config.gun_barrel_aim ? 1 : 0);
    return true;
}
