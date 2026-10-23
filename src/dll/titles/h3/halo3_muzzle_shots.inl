// Halo 3 barrel-muzzle shots: the detour on the native marker resolver that fires from the rendered barrel.
// Only the local player's own weapons are changed; every refusal leaves the stock shot.
#pragma once
// Included once, by src/dll/titles/h3/halo3_independent_shots.inl. Not a standalone header:
//   it uses what the host defines above its include line, namely AcquireHalo3IndependentShot,
//   Halo3IndependentTargetStorage, Halo3ReadOwnedWeapons, ReadCinematicControl,
//   RestoreHalo3IndependentShot, g_enabled, g_halo3Dual, g_halo3IndependentShot,
//   g_halo3NativeQueryContext, g_halo3PlayerUnitGetter, g_halo3RuntimeGeneration,
//   g_legacyCollisionOwnedQuery, g_vrAim.
// H3EK A3ABE0 and retail 343D74 independently establish this six-argument
// resolver and its 0x70 marker/world-matrix layout. It is not H2's wrapper ABI.
// The body after the native marker call is legacy_muzzle_shots.h's (with
// Halo 2). H3 keeps its detour, its gate and refusal reasons, and its
// scoped-shot diagnostic; they are the traits below.
#include "../../core/legacy_muzzle_shots.h"
using Halo3MuzzleMarkersFn = uint64_t(__fastcall*)(uint32_t, uint32_t, void*, int16_t, uint8_t, uint8_t);
struct Halo3MuzzleRuntime
{
    // targets[1] and installedAtMs are for barrel_muzzle_lifecycle.h; nothing
    // reads installedAtMs here.
    uintptr_t base = 0;
    uint32_t generation = 0;
    uint64_t installedAtMs = 0;
    void* targets[1]{};
    Halo3MuzzleMarkersFn original = nullptr;
    std::atomic<bool> enabled{false}, faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> applied{0}, refused{0};
} g_halo3Muzzle;
struct Halo3MuzzleRequest
{
    uint32_t weapon = UINT32_MAX;
    int16_t barrel = -1;
    // The fire call's own prediction flag (T-2 shot evidence). The muzzle
    // substitution path ignores it; it travels with the per-shot request so
    // the firing-path shot event reports what the native call carried.
    uint8_t predicted = 0;
    NativeShotTargetLease<0x24>* lease = nullptr;
};
thread_local Halo3MuzzleRequest g_halo3MuzzleRequest;
// Diagnostic (H3 shots were inaccurate while scoped in headset testing): why the barrel
// trajectory was refused for the current shot (static literal, game thread).
thread_local const char* g_halo3MuzzleRefusal = "none";
__declspec(noinline) void MarkHalo3MuzzleFault()
{
    g_halo3Muzzle.faulted.store(true, std::memory_order_release);
}

struct Halo3MuzzleShots
{
    static Halo3MuzzleRuntime& Feature() { return g_halo3Muzzle; }
    static Halo3MuzzleRequest& Request() { return g_halo3MuzzleRequest; }
    static Halo3IndependentShotScope& Shot() { return g_halo3IndependentShot; }
    static bool Refuse(const char* why)
    {
        g_halo3MuzzleRefusal = why;
        return false;
    }
    static bool Gate(const Halo3MuzzleRequest& request, uint32_t object, uint64_t count, int16_t capacity,
        void* markers, uint32_t& unit)
    {
        auto& feature = g_halo3Muzzle;
        if (!feature.enabled.load() || feature.faulted.load() || !g_config.gun_barrel_aim ||
            !g_halo3Dual.enabled.load() || g_halo3Dual.faulted.load())
            return Refuse("feature-off-or-faulted");
        if (exclusive_input::Active())
            return Refuse("exclusive-input");
        if (!g_vrAim.load() || !g_enabled.load() || !VR_IsStereoEnabled() ||
            TitleAdapter_GetActiveTitle() != GameTitle::Halo3)
            return Refuse("vr-aim-or-stereo-off");
        if (feature.generation != g_halo3RuntimeGeneration.load())
            return Refuse("generation");
        if (!request.lease || request.lease->active)
            return Refuse("lease");
        if (request.barrel < 0 || request.barrel >= 2 || object != request.weapon)
            return Refuse("barrel-or-object");
        if (count != 1 || capacity < 1 || !markers)
            return Refuse("marker-count");
        int32_t scene = -1, shot = -1;
        if (ReadCinematicControl(scene, shot) != CinematicControlState::PlayerControlled)
            return Refuse("cinematic");
        unit = g_halo3NativeQueryContext.unit;
        return true;
    }
    // An exception in the apply leaves this reason.
    static void BeforeApply() { g_halo3MuzzleRefusal = "fault"; }
    static bool ReadWeapons(uint32_t unit, uint32_t (&weapons)[2])
    {
        return Halo3ReadOwnedWeapons(unit, weapons, false);
    }
    static constexpr GameTitle kTitle = GameTitle::Halo3;
    static weapon_muzzle::Store& Muzzles() { return g_halo3Muzzles; }
    static bool AimReady() { return g_halo3Dual.aimOriginal != nullptr; }
    static void* TargetStorage(uint32_t unit) { return Halo3IndependentTargetStorage(unit); }
    // H3's helper always performs its own obstruction test. Private velocity
    // output keeps this preflight separate from the ordinary firing call.
    static void Aim(uint32_t unit, float* position, float* direction, float* velocity)
    {
        g_halo3Dual.aimOriginal(unit, position, direction, reinterpret_cast<uint64_t>(velocity), nullptr, 0, 0);
    }
    static bool& OwnedQuery() { return g_legacyCollisionOwnedQuery; }
    static bool Acquire(uint32_t weapon, NativeShotTargetLease<0x24>& lease, const weapon_muzzle::Receipt* muzzle)
    {
        return AcquireHalo3IndependentShot(weapon, lease, muzzle);
    }
    static void Restore(NativeShotTargetLease<0x24>& lease) { RestoreHalo3IndependentShot(lease); }
    static void MarkFault() { MarkHalo3MuzzleFault(); }
    // Only the local player's own weapons: the fire path runs for every actor, and
    // one test log held 77 AI shots (no player shot) with 40-148 deg "errors". The H3
    // query unit is the firing actor's, so the player comes from the getter.
    static bool PlayerShot(const Halo3MuzzleRequest& request)
    {
        uint32_t playerWeapons[2]{UINT32_MAX, UINT32_MAX};
        return g_halo3PlayerUnitGetter &&
               Halo3ReadOwnedWeapons(static_cast<uint32_t>(g_halo3PlayerUnitGetter(0)), playerWeapons, false) &&
               request.weapon != UINT32_MAX &&
               (request.weapon == playerWeapons[0] || request.weapon == playerWeapons[1]);
    }
    // Diagnostic after the bookkeeping, isolated: it can never skip a restore or
    // fault into the game. Markers only when the resolver returned one.
    static void AfterShot(bool applied, bool playerShot, uint64_t result, void* markers, int16_t capacity)
    {
        if (!playerShot)
            return;
        const void* shotMarkers = result >= 1 && capacity >= 1 ? markers : nullptr;
        __try
        {
            Halo3ScopedShotDiagnostic(applied, applied ? "" : g_halo3MuzzleRefusal, shotMarkers);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("the Halo 3 shot diagnostic faulted");
        }
    }
    static constexpr uintptr_t kMarkersCaller = 0x368651;
};

// The detour counts itself first and releases in its own __finally; the shared
// body neither takes nor releases a count (see legacy_muzzle_shots.h).
__declspec(noinline) uint64_t __fastcall Halo3MuzzleMarkersDetour(
    uint32_t object, uint32_t name, void* markers, int16_t capacity, uint8_t originalObject, uint8_t interpolated)
{
    auto& feature = g_halo3Muzzle;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint64_t result = 0;
    __try
    {
        if (!feature.original)
            __leave;
        result = feature.original(object, name, markers, capacity, originalObject, interpolated);
        LegacyMuzzleAfterMarkers<Halo3MuzzleShots>(
            reinterpret_cast<uintptr_t>(_ReturnAddress()), object, result, markers, capacity);
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}
