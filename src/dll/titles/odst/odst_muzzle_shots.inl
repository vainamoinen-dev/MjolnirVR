// ODST barrel-muzzle shots: the fire, view, marker, camera and aim detours that fire from the rendered barrel.
// Only the local player's shots change; every refusal is counted and leaves the stock shot.
#pragma once
// Included once, by src/dll/titles/odst/odst_camera_core.cpp; also compiled by tests/odst_muzzle_tests.cpp,
//   which supplies its own versions. Not a standalone header: it uses what the host defines
//   above its include line, namely ReadOdstCinematicControl, g_enabled,
//   g_legacyCollisionOwnedQuery, g_odstMuzzles, g_odstRuntimeGeneration, g_vrAim.
// ODSTEK B0FCB0 / retail 3AF230: five-argument outer firing scope.
// The six-argument 3AE8A4 is a downstream firing-data helper, not outer fire.
// Independent dual aim is H2/H3 only; ODST has none.
// The shot steps are barrel_muzzle_runtime.h's (with Reach and Halo 4).
// ODST keeps its fire, view and markers detours (their native signatures), the
// camera and aim detours (ODST only), and its shot line; its differences are the
// traits below (no camera-direction output, no assist caller, the both-slot
// inventory snapshot, the barrel flag, every refusal counted).
#include "../../core/barrel_muzzle_runtime.h"
using OdstMuzzleFireFn = uint64_t(__fastcall*)(uint32_t, int16_t, void*, int32_t, uint8_t);
using OdstMuzzleAimFn = void(__fastcall*)(uint32_t, float*, float*, uint64_t, float*, uint8_t, uint8_t);
using OdstMuzzleQueryFn = BarrelMuzzleQueryFn;
using OdstMuzzleViewFn = void(__fastcall*)(uint32_t, uint64_t, float*, float*, float*);
using OdstMuzzleCameraFn = int32_t(__fastcall*)(uint32_t, float*, float*);
using OdstMuzzleMarkersFn = uint64_t(__fastcall*)(uint32_t, uint32_t, void*, int16_t, uint8_t, uint8_t);
struct OdstMuzzleRuntime
{
    uintptr_t base = 0;
    uint32_t generation = 0;
    uint64_t installedAtMs = 0;
    void* targets[6]{};
    OdstMuzzleFireFn fireOriginal = nullptr;
    OdstMuzzleAimFn aimOriginal = nullptr;
    OdstMuzzleQueryFn queryOriginal = nullptr;
    OdstMuzzleViewFn viewOriginal = nullptr;
    OdstMuzzleCameraFn cameraOriginal = nullptr;
    OdstMuzzleMarkersFn markersOriginal = nullptr;
    std::atomic<bool> enabled{false}, faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> applied{0}, refused{0}, queries{0}, restoreRefused{0};
} g_odstMuzzle;
using OdstMuzzleQueryContext = BarrelMuzzleQueryContext;
using OdstMuzzleQueryCapture = BarrelMuzzleQueryCapture;
using OdstMuzzleShotScope = BarrelMuzzleShotScope;
using OdstMuzzleRequest = BarrelMuzzleRequest;
thread_local OdstMuzzleQueryContext g_odstMuzzleQueryContext;
thread_local OdstMuzzleQueryCapture g_odstMuzzleCapture;
thread_local OdstMuzzleShotScope g_odstMuzzleShot;
thread_local OdstMuzzleRequest g_odstMuzzleRequest;
__declspec(noinline) void MarkOdstMuzzleFault()
{
    g_odstMuzzle.faulted.store(true, std::memory_order_release);
}

// One line per local player shot, at most 4 a second, like Halo 4 and Reach.
void OdstShotLine(bool applied, uint64_t count, const void* markers) noexcept
{
    static ULONGLONG second = 0;
    static unsigned lines = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - second >= 1000)
    {
        second = now;
        lines = 0;
    }
    if (count < 1 || !markers || ++lines > 4)
        return;
    __try
    {
        const auto* bytes = static_cast<const uint8_t*>(markers);
        float direction[3]{}, origin[3]{};
        std::memcpy(direction, bytes + 0x3C, 12);
        std::memcpy(origin, bytes + 0x60, 12);
        char frame[256];
        Game_FormatShotFrame(GameTitle::Halo3ODST, frame, sizeof frame);
        LOG("ODST shot: barrel %s; origin %.3f %.3f %.3f, direction %.3f %.3f %.3f%s", applied ? "applied" : "refused",
            origin[0], origin[1], origin[2], direction[0], direction[1], direction[2], frame);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("the ODST shot diagnostic faulted");
    }
}
struct OdstMuzzleShots
{
    static OdstMuzzleRuntime& Feature() { return g_odstMuzzle; }
    static OdstMuzzleQueryContext& Context() { return g_odstMuzzleQueryContext; }
    static OdstMuzzleQueryCapture& Capture() { return g_odstMuzzleCapture; }
    static OdstMuzzleShotScope& Shot() { return g_odstMuzzleShot; }
    static OdstMuzzleRequest& Request() { return g_odstMuzzleRequest; }
    static bool& OwnedQuery() { return g_legacyCollisionOwnedQuery; }
    static constexpr GameTitle kTitle = GameTitle::Halo3ODST;
    static uint32_t CameraGeneration() { return g_odstRuntimeGeneration.load(); }
    static bool Armed()
    {
        int32_t scene = -1, shot = -1;
        return ReadOdstCinematicControl(scene, shot) == CinematicControlState::PlayerControlled;
    }
    static void* TargetStorage(uint32_t owner) { return OdstMuzzleTargetStorage(owner); }
    static bool ReadWeapons(uint32_t owner, uint32_t weapons[2]) { return OdstReadMuzzleWeapons(owner, weapons); }
    // ODST's 0x28-byte native target: strengths +10/+14, lead +18..20, flags +24;
    // ODSTEK 41D850/41D860: object handle +8, marker +4.
    static bool ValidObject(uint32_t handle) { return OdstContactObject(handle); }
    static auto& Muzzles() { return g_odstMuzzles; }
    static void MarkFault() { MarkOdstMuzzleFault(); }
    // ODST's own obstruction call (seven arguments; the fourth is an output
    // velocity), no offset and no projection. ODST's fire has no simulation byte.
    static bool AimReady() { return g_odstMuzzle.aimOriginal != nullptr; }
    static void Aim(uint32_t unit, float* origin, float* direction, float* velocity, uint8_t)
    {
        g_odstMuzzle.aimOriginal(unit, origin, direction, reinterpret_cast<uint64_t>(velocity), nullptr, 0, 0);
    }
    static void AfterQuery(int32_t, int16_t) {}
    static void ShotLine(bool applied, uint64_t count, const void* markers) { OdstShotLine(applied, count, markers); }
    // Both view callers are query callers; ODST has no assist caller.
    static constexpr uintptr_t kQueryCaller = 0x160647, kQueryCaller2 = 0x160A5D, kAssistCaller = 0,
                               kMarkersCaller = 0x3AF4CA;
    // The native view replaces origin and camera only; the direction stays the
    // query's own input, including native lead calculations.
    static constexpr auto kViewDirection = BarrelMuzzleViewDirection::None;
    static constexpr bool kInventorySnapshot = true, kMarkBarrelShot = true;
};

__declspec(noinline) void __fastcall OdstMuzzleViewDetour(
    uint32_t unit, uint64_t flags, float* direction, float* origin, float* camera)
{
    auto& feature = g_odstMuzzle;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (!feature.viewOriginal)
            __leave;
        feature.viewOriginal(unit, flags, direction, origin, camera);
        // The native helper projects its camera through the world object's
        // centre. Replace its private outputs after that projection.
        BarrelMuzzleAfterView<OdstMuzzleShots>(
            reinterpret_cast<uintptr_t>(_ReturnAddress()), unit, direction, origin, nullptr, camera);
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}
__declspec(noinline) int32_t __fastcall OdstMuzzleCameraDetour(uint32_t unit, float* position, float* direction)
{
    auto& feature = g_odstMuzzle;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    int32_t result = 0;
    __try
    {
        if (!feature.cameraOriginal)
            __leave;
        result = feature.cameraOriginal(unit, position, direction);
        const auto& shot = g_odstMuzzleShot;
        if (!shot.active || shot.query || shot.unit != unit || !position || !direction ||
            reinterpret_cast<uintptr_t>(_ReturnAddress()) != feature.base + 0x161204)
            __leave;
        std::memcpy(position, shot.position, 12);
        std::memcpy(direction, shot.direction, 12);
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}
constexpr auto& OdstMuzzleQueryDetour = BarrelMuzzleQueryDetour<OdstMuzzleShots>;
__declspec(noinline) uint64_t __fastcall OdstMuzzleMarkersDetour(
    uint32_t object, uint32_t name, void* markers, int16_t capacity, uint8_t originalObject, uint8_t interpolated)
{
    auto& feature = g_odstMuzzle;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint64_t result = 0;
    __try
    {
        if (!feature.markersOriginal)
            __leave;
        result = feature.markersOriginal(object, name, markers, capacity, originalObject, interpolated);
        BarrelMuzzleAfterMarkers<OdstMuzzleShots>(
            reinterpret_cast<uintptr_t>(_ReturnAddress()), object, result, markers, capacity);
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}
__declspec(noinline) void __fastcall OdstMuzzleAimDetour(uint32_t unit, float* origin, float* direction,
    uint64_t velocity, float* offset, uint8_t project, uint8_t useUnitAim)
{
    auto& feature = g_odstMuzzle;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (!feature.aimOriginal)
            __leave;
        auto& shot = g_odstMuzzleShot;
        if (shot.active && !shot.query && shot.barrel && shot.unit == unit && origin && direction &&
            reinterpret_cast<uintptr_t>(_ReturnAddress()) == feature.base + 0x3AEB2B)
        {
            std::memcpy(origin, shot.position, 12);
            std::memcpy(direction, shot.direction, 12);
            feature.aimOriginal(unit, origin, direction, velocity, nullptr, 0, 0);
            if (std::isfinite(origin[0]) && std::isfinite(origin[1]) && std::isfinite(origin[2]))
                std::memcpy(shot.position, origin, 12);
        }
        else
        {
            feature.aimOriginal(unit, origin, direction, velocity, offset, project, useUnitAim);
            if (shot.active && !shot.query && !shot.barrel && shot.unit == unit && origin && direction &&
                reinterpret_cast<uintptr_t>(_ReturnAddress()) == feature.base + 0x3AEB2B)
            {
                float candidate[3]{};
                if (BuildIndependentWeaponDirection(origin, shot.position, shot.direction,
                        std::clamp(g_config.crosshair_distance_m, 2.f, 50.f) * Game_GetWorldScale(), candidate))
                    std::memcpy(direction, candidate, 12);
            }
        }
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}
// ODST's request has no simulation byte (its aim never reads one): 0. The
// detour counts itself before the scope (see BarrelMuzzleFireScope).
__declspec(noinline) uint64_t __fastcall OdstMuzzleFireDetour(
    uint32_t weapon, int16_t barrel, void* data, int32_t index, uint8_t predicted)
{
    auto& feature = g_odstMuzzle;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    return BarrelMuzzleFireScope<OdstMuzzleShots>(weapon, barrel, 0, [&]() -> uint64_t
        { return feature.fireOriginal ? feature.fireOriginal(weapon, barrel, data, index, predicted) : 0; });
}
