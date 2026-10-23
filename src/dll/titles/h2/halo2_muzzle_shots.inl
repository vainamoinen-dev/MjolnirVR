// Halo 2 muzzle shots: a detour of the native weapon marker lookup used when a weapon fires.
// Included once, by src/dll/titles/h2/halo2_independent_shots.inl. Not a standalone header:
//   it uses what the host defines above its include line, namely AcquireHalo2IndependentShot,
//   Halo2IndependentTargetStorage, Halo2ReadIndependentWeapons, RestoreHalo2IndependentTarget,
//   g_generation, g_halo2CollisionOwnQuery, g_halo2Dual, g_halo2IndependentQueryContext,
//   g_halo2IndependentShot.
#pragma once
// H2EK 867EB0 / retail 8D11A0: s_model_marker has a 0x70 stride,
// with world matrix at +38, forward +3C, up +54 and translation +60.
// Only the weapon firing caller of the four-argument wrapper is admitted.
// The body after the native marker call is legacy_muzzle_shots.h's (shared with
// Halo 3). H2 keeps its detour and its gate (with the parent-unit admission);
// it records no refusal reason and has no diagnostic.
#include "../../core/legacy_muzzle_shots.h"
using Halo2MuzzleMarkersFn = uint16_t(__fastcall*)(uint32_t, uint32_t, void*, int16_t);
struct Halo2MuzzleRuntime
{
    // targets[1] and installedAtMs exist for barrel_muzzle_lifecycle.h; nothing
    // reads installedAtMs here.
    uintptr_t base = 0;
    uint32_t generation = 0;
    uint64_t installedAtMs = 0;
    void* targets[1]{};
    Halo2MuzzleMarkersFn original = nullptr;
    std::atomic<bool> enabled{false}, faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> applied{0}, refused{0};
} g_halo2Muzzle;
struct Halo2MuzzleRequest
{
    uint32_t weapon = UINT32_MAX;
    int16_t barrel = -1;
    NativeShotTargetLease<0x24>* lease = nullptr;
};
thread_local Halo2MuzzleRequest g_halo2MuzzleRequest;
__declspec(noinline) void MarkHalo2MuzzleFault()
{
    g_halo2Muzzle.faulted.store(true, std::memory_order_release);
}

struct Halo2MuzzleShots
{
    static Halo2MuzzleRuntime& Feature() { return g_halo2Muzzle; }
    static Halo2MuzzleRequest& Request() { return g_halo2MuzzleRequest; }
    static Halo2IndependentShotScope& Shot() { return g_halo2IndependentShot; }
    static bool Refuse(const char*) { return false; }
    static bool Gate(const Halo2MuzzleRequest& request, uint32_t object, uint16_t count, int16_t capacity,
        void* markers, uint32_t& unit)
    {
        auto& feature = g_halo2Muzzle;
        if (!feature.enabled.load() || feature.faulted.load() || !g_config.gun_barrel_aim ||
            !g_halo2Dual.enabled.load() || g_halo2Dual.faulted.load() || exclusive_input::Active() ||
            !Game_Halo2ControllerAimActive() || !Halo2Observer6Dof_FinalPaletteArmed() ||
            !Halo2Observer6Dof_DirectWeaponAimArmed() || feature.generation != g_generation.load() || !request.lease ||
            request.lease->active || request.barrel < 0 || request.barrel >= 2 || count != 1 || capacity < 1 ||
            !markers)
            return false;
        const auto context = g_halo2IndependentQueryContext;
        // H2EK 4A0F50 / retail 8E8990 selects the firing weapon OR its
        // parent as the native marker owner. The firing scope still identifies
        // the exact weapon/role; a parent marker must not force the secondary
        // weapon back to its ordinary native origin. No other object is admitted.
        if (object != request.weapon && object != context.unit)
            return false;
        unit = context.unit;
        return true;
    }
    static void BeforeApply() {}
    static bool ReadWeapons(uint32_t unit, uint32_t (&weapons)[2])
    {
        return Halo2ReadIndependentWeapons(unit, weapons, false);
    }
    static constexpr GameTitle kTitle = GameTitle::Halo2;
    static weapon_muzzle::Store& Muzzles() { return g_halo2Muzzles; }
    static bool AimReady() { return g_halo2Dual.aimOriginal != nullptr; }
    static void* TargetStorage(uint32_t unit) { return Halo2IndependentTargetStorage(unit); }
    // Some authored barrel flags skip the later unit-adjust call or restore
    // the marker afterwards. Clamp the marker itself through the proven native
    // helper first, using private velocity storage; its body only reads unit
    // state and writes caller-owned origin/direction/velocity. The ordinary
    // call later still supplies the engine's actual projectile velocity.
    static void Aim(uint32_t unit, float* position, float* direction, float* velocity)
    {
        g_halo2Dual.aimOriginal(unit, position, direction, reinterpret_cast<uint64_t>(velocity), nullptr, 0, 0, 1);
    }
    static bool& OwnedQuery() { return g_halo2CollisionOwnQuery; }
    static bool Acquire(uint32_t weapon, NativeShotTargetLease<0x24>& lease, const weapon_muzzle::Receipt* muzzle)
    {
        return AcquireHalo2IndependentShot(weapon, lease, muzzle);
    }
    static void Restore(NativeShotTargetLease<0x24>& lease) { RestoreHalo2IndependentTarget(lease); }
    static void MarkFault() { MarkHalo2MuzzleFault(); }
    // H2's weapons reader reads the local user's own first-person weapons and
    // checks them against the unit; the local player's unit is
    // Halo2OwnedUnit(). The firing weapon is request.weapon, because the
    // gate also admits its parent unit as the marker owner.
    static bool PlayerShot(const Halo2MuzzleRequest& request)
    {
        uint32_t weapons[2]{UINT32_MAX, UINT32_MAX};
        return request.weapon != UINT32_MAX && Halo2ReadIndependentWeapons(Halo2OwnedUnit(), weapons, false) &&
               (request.weapon == weapons[0] || request.weapon == weapons[1]);
    }
    static void AfterShot(bool, bool, uint16_t, void*, int16_t) {}
    static constexpr uintptr_t kMarkersCaller = 0x8E4BAF;
};

// The detour counts itself first and releases in its own __finally; the shared
// body neither takes nor releases a count (see legacy_muzzle_shots.h).
__declspec(noinline) uint16_t __fastcall Halo2MuzzleMarkersDetour(
    uint32_t object, uint32_t name, void* markers, int16_t capacity)
{
    auto& feature = g_halo2Muzzle;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint16_t result = 0;
    __try
    {
        if (!feature.original)
            __leave;
        result = feature.original(object, name, markers, capacity);
        LegacyMuzzleAfterMarkers<Halo2MuzzleShots>(
            reinterpret_cast<uintptr_t>(_ReturnAddress()), object, result, markers, capacity);
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}
