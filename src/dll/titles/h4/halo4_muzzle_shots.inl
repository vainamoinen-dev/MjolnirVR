// Halo 4 barrel-muzzle shots: the view, marker and aim detours that fire from the rendered barrel, plus the shot event and log line.
// Native velocity, collision and simulation stay native; a native exception is never replayed.
#pragma once
// Included once, by src/dll/titles/h4/halo4_camera_core.cpp; also compiled by
//   tests/halo4_muzzle_tests.cpp, which supplies its own versions. Not a standalone header: it
//   uses what the host defines above its include line, namely PublishShotDiagnostic, g_enabled,
//   g_halo4Camera, g_halo4MuzzleOwnedQuery, g_vrAim.
// H4EK E977E0 / retail 6176B8: four-argument void firing scope.
// H4EK E66CD0 / retail 5F3510: nine arguments, including native output velocity.
// The T-2 shot event uses the recorder's fixed payload/kind declarations only
// (the gate and the shared publisher are wired in game.cpp).
// The shot steps are barrel_muzzle_runtime.h's (with Reach); Halo 4 keeps
// its view, markers and aim detours (their native signatures), its own aim
// trampoline, the T-2 shot event and its shot line.
#include "../../core/telemetry_recorder.h"
#include "../../core/barrel_muzzle_runtime.h"
using Halo4MuzzleFireFn = BarrelMuzzleFireFn;
using Halo4MuzzleQueryFn = BarrelMuzzleQueryFn;
using Halo4MuzzleViewFn = void(__fastcall*)(uint32_t, uint8_t, uintptr_t, float*, float*, float*);
using Halo4MuzzleMarkersFn = int16_t(__fastcall*)(uint32_t, uint32_t, void*, int16_t, uint8_t, uint8_t, uint8_t);
using Halo4MuzzleAimFn = void(__fastcall*)(
    uint32_t, float*, float*, float*, const float*, const float*, uint8_t, uint8_t, uint32_t);
struct Halo4MuzzleRuntime
{
    uintptr_t base = 0;
    uint32_t generation = 0;
    uint64_t installedAtMs = 0;
    void* targets[5]{};
    Halo4MuzzleFireFn fireOriginal = nullptr;
    Halo4MuzzleQueryFn queryOriginal = nullptr;
    Halo4MuzzleViewFn viewOriginal = nullptr;
    Halo4MuzzleMarkersFn markersOriginal = nullptr;
    Halo4MuzzleAimFn aimOriginal = nullptr;
    std::atomic<bool> enabled{false}, faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> applied{0}, refused{0}, queries{0}, restoreRefused{0};
} g_halo4Barrel;
using Halo4MuzzleQueryContext = BarrelMuzzleQueryContext;
using Halo4MuzzleQueryCapture = BarrelMuzzleQueryCapture;
using Halo4MuzzleShotScope = BarrelMuzzleShotScope;
using Halo4MuzzleRequest = BarrelMuzzleRequest;
thread_local Halo4MuzzleQueryContext g_halo4BarrelQueryContext;
thread_local Halo4MuzzleQueryCapture g_halo4BarrelCapture;
thread_local Halo4MuzzleShotScope g_halo4BarrelShot;
thread_local Halo4MuzzleRequest g_halo4BarrelRequest;
__declspec(noinline) void MarkHalo4MuzzleFault()
{
    g_halo4Barrel.faulted.store(true, std::memory_order_release);
}

// One line per local player shot, at most 4 a second: whether the
// barrel was applied and where the shot leaves (the marker's world origin and
// direction; with the barrel applied that is the gun's muzzle receipt).
void Halo4ShotLine(bool applied, int16_t count, const void* markers) noexcept
{
    static ULONGLONG second = 0;
    static unsigned lines = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - second >= 1000)
    {
        second = now;
        lines = 0;
    }
    if (++lines > 4 || count < 1 || !markers)
        return;
    __try
    {
        const auto* bytes = static_cast<const uint8_t*>(markers);
        float direction[3]{}, origin[3]{};
        std::memcpy(direction, bytes + 0x3C, 12);
        std::memcpy(origin, bytes + 0x60, 12);
        char frame[256];
        Game_FormatShotFrame(GameTitle::Halo4, frame, sizeof frame);
        LOG("Halo 4 shot: barrel %s; origin %.3f %.3f %.3f, direction %.3f %.3f %.3f%s",
            applied ? "applied" : "refused", origin[0], origin[1], origin[2], direction[0], direction[1], direction[2],
            frame);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("the Halo 4 shot diagnostic faulted");
    }
}
struct Halo4MuzzleShots
{
    static Halo4MuzzleRuntime& Feature() { return g_halo4Barrel; }
    static Halo4MuzzleQueryContext& Context() { return g_halo4BarrelQueryContext; }
    static Halo4MuzzleQueryCapture& Capture() { return g_halo4BarrelCapture; }
    static Halo4MuzzleShotScope& Shot() { return g_halo4BarrelShot; }
    static Halo4MuzzleRequest& Request() { return g_halo4BarrelRequest; }
    static bool& OwnedQuery() { return g_halo4MuzzleOwnedQuery; }
    static constexpr GameTitle kTitle = GameTitle::Halo4;
    static uint32_t CameraGeneration() { return g_halo4Camera.generation.load(); }
    static bool Armed()
    {
        return g_halo4Camera.armed.load() && ReadHalo4CinematicControl() == CinematicControlState::PlayerControlled;
    }
    static void* TargetStorage(uint32_t owner) { return Halo4MuzzleTargetStorage(owner); }
    static bool ReadWeapons(uint32_t owner, uint32_t weapons[2]) { return Halo4ReadMuzzleWeapons(owner, weapons); }
    static bool ValidObject(uint32_t handle) { return Halo4BarrelValidObject(handle); }
    static weapon_muzzle::Store& Muzzles() { return g_halo4BarrelMuzzles; }
    static void MarkFault() { MarkHalo4MuzzleFault(); }
    // Halo 4's own aim hook (nine arguments, native output velocity), called
    // through its trampoline.
    static bool AimReady() { return g_halo4Barrel.aimOriginal != nullptr; }
    static void Aim(uint32_t unit, float* origin, float* direction, float* velocity, uint8_t simulation)
    {
        g_halo4Barrel.aimOriginal(unit, origin, direction, velocity, nullptr, nullptr, 0, 0, simulation);
    }
    static void AfterQuery(int32_t, int16_t) {}
    static void ShotLine(bool applied, int16_t count, const void* markers) { Halo4ShotLine(applied, count, markers); }
    static constexpr uintptr_t kQueryCaller = 0x1DB9A6, kQueryCaller2 = 0x1DBDB6, kAssistCaller = 0x1DC8C4,
                               kMarkersCaller = 0x6179B7;
    // H4EK 586DC0 / retail 1DC9E4: the native view's third argument is unused.
    static constexpr auto kViewDirection = BarrelMuzzleViewDirection::ShotDirection;
    static constexpr bool kInventorySnapshot = false, kMarkBarrelShot = false;
};

__declspec(noinline) void __fastcall Halo4MuzzleViewDetour(
    uint32_t unit, uint8_t flags, uintptr_t inputDirection, float* origin, float* cameraDirection, float* camera)
{
    auto& feature = g_halo4Barrel;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (!feature.viewOriginal)
            __leave;
        feature.viewOriginal(unit, flags, inputDirection, origin, cameraDirection, camera);
        BarrelMuzzleAfterView<Halo4MuzzleShots>(
            reinterpret_cast<uintptr_t>(_ReturnAddress()), unit, nullptr, origin, cameraDirection, camera);
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}
constexpr auto& Halo4MuzzleQueryDetour = BarrelMuzzleQueryDetour<Halo4MuzzleShots>;
__declspec(noinline) int16_t __fastcall Halo4MuzzleMarkersDetour(uint32_t object, uint32_t name, void* markers,
    int16_t capacity, uint8_t originalObject, uint8_t firstPerson, uint8_t interpolated)
{
    auto& feature = g_halo4Barrel;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    int16_t result = 0;
    __try
    {
        if (!feature.markersOriginal)
            __leave;
        result = feature.markersOriginal(object, name, markers, capacity, originalObject, firstPerson, interpolated);
        BarrelMuzzleAfterMarkers<Halo4MuzzleShots>(
            reinterpret_cast<uintptr_t>(_ReturnAddress()), object, result, markers, capacity);
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}
// T-2 shot event for Halo 4. The disabled gate is passed FIRST: with recording
// off this returns on a single atomic load, before any TLS/context read. The
// hooks exist regardless of `gun_barrel_aim`, so this fires on real shots with
// or without substitution; `substituted` says whether the committed muzzle
// barrel produced the final ray.
void PublishHalo4ShotDiagnostic(uint32_t unit, const float origin[3], const float direction[3], bool substituted,
    bool firesFromCamera, bool unitAim)
{
    if (!Telemetry_WeaponEventsAccepting())
        return;
    const auto request = g_halo4BarrelRequest;
    const auto& shot = g_halo4BarrelShot;
    const uint8_t slot =
        (shot.active && shot.unit == unit && shot.slot <= 1) ? uint8_t(shot.slot) : kTelemetryShotIndexUnknown;
    const uint8_t barrel =
        (request.barrel >= 0 && request.barrel < 2) ? uint8_t(request.barrel) : kTelemetryShotIndexUnknown;
    // The four-argument Halo 4 fire scope carries no prediction flag, so bit0
    // stays "unknown" (0) rather than inventing one from the simulation byte.
    PublishShotDiagnostic(GameTitle::Halo4, g_halo4Camera.generation.load(std::memory_order_acquire), unit,
        request.weapon, slot, barrel, false, substituted, firesFromCamera, unitAim, origin, direction);
}
// Exact outer-fire call only. Native velocity, collision and simulation remain
// native; no replay when the native callback raises an exception.
__declspec(noinline) void __fastcall Halo4MuzzleAimDetour(uint32_t unit, float* origin, float* direction,
    float* velocity, const float* offset, const float* camera, uint8_t project, uint8_t unitAim, uint32_t simulation)
{
    auto& feature = g_halo4Barrel;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (!feature.aimOriginal)
            __leave;
        auto& shot = g_halo4BarrelShot;
        const bool firingCall = reinterpret_cast<uintptr_t>(_ReturnAddress()) == feature.base + 0x617E95;
        const bool owned = firingCall && shot.active && !shot.query && shot.unit == unit && origin && direction;
        if (owned)
        {
            std::memcpy(origin, shot.position, 12);
            std::memcpy(direction, shot.direction, 12);
            offset = camera = nullptr;
            project = unitAim = 0;
        }
        feature.aimOriginal(unit, origin, direction, velocity, offset, camera, project, unitAim, simulation);
        if (owned && std::isfinite(origin[0]) && std::isfinite(origin[1]) && std::isfinite(origin[2]))
            std::memcpy(shot.position, origin, 12);
        // Publish once per actual shot invocation, after the final ray is
        // decided, whether or not the mod substituted it.
        if (firingCall && origin && direction)
            PublishHalo4ShotDiagnostic(unit, origin, direction, owned, project != 0, unitAim != 0);
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}
constexpr auto& Halo4MuzzleFireDetour = BarrelMuzzleFireDetour<Halo4MuzzleShots>;
