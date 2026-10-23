// Halo Reach barrel-muzzle shots: the view and marker detours that fire from the rendered barrel, plus the shot line.
// Shares the already-installed core unit-adjust hook; a missing muzzle transaction never fabricates an identity.
#pragma once
// Included once, by src/dll/core/game.cpp; also compiled by
//   tests/reach_muzzle_tests.cpp, which supplies its own versions. Not a standalone header: it
//   uses what the host defines above its include line, namely ReachApplyBarrelAim,
//   ReachReadFiringContextForTelemetry, g_enabled, g_legacyCollisionOwnedQuery,
//   g_origReachUnitAdjust, g_reachBarrelMuzzles, g_reachCamera, g_reachCinematicLocked,
//   g_reachEngineZoomLevel, g_reachEngineZoomMs, g_servoAimPitch, g_servoDesiredPitch,
//   g_servoHandReferenceYaw, g_vrAim.
// HREK DE4290 / retail 4C2710: four-argument void outer firing scope.
// Reach shares its already-installed ten-argument core unit-adjust hook.
// The T-2 shot evidence uses the recorder's fixed index sentinel only (the
// gate and the shared publisher are wired in game.cpp).
// The shot steps are barrel_muzzle_runtime.h's (with Halo 4); Reach keeps
// its view and markers detours (their native signatures), the shared core
// unit-adjust call, the engine zoom publication and its shot line.
#include "../../core/telemetry_recorder.h"
#include "../../core/barrel_muzzle_runtime.h"
using ReachMuzzleFireFn = BarrelMuzzleFireFn;
using ReachMuzzleQueryFn = BarrelMuzzleQueryFn;
using ReachMuzzleViewFn = void(__fastcall*)(uint32_t, uint8_t, float*, float*, float*, float*);
using ReachMuzzleMarkersFn = int16_t(__fastcall*)(uint32_t, uint32_t, void*, int16_t, uint8_t, uint8_t);
struct ReachMuzzleRuntime
{
    uintptr_t base = 0;
    uint32_t generation = 0;
    uint64_t installedAtMs = 0;
    void* targets[4]{};
    ReachMuzzleFireFn fireOriginal = nullptr;
    ReachMuzzleQueryFn queryOriginal = nullptr;
    ReachMuzzleViewFn viewOriginal = nullptr;
    ReachMuzzleMarkersFn markersOriginal = nullptr;
    std::atomic<bool> enabled{false}, faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> applied{0}, refused{0}, queries{0}, restoreRefused{0};
} g_reachBarrel;
using ReachMuzzleQueryContext = BarrelMuzzleQueryContext;
using ReachMuzzleQueryCapture = BarrelMuzzleQueryCapture;
using ReachMuzzleShotScope = BarrelMuzzleShotScope;
using ReachMuzzleRequest = BarrelMuzzleRequest;
thread_local ReachMuzzleQueryContext g_reachBarrelQueryContext;
thread_local ReachMuzzleQueryCapture g_reachBarrelCapture;
thread_local ReachMuzzleShotScope g_reachBarrelShot;
thread_local ReachMuzzleRequest g_reachBarrelRequest;
__declspec(noinline) void MarkReachMuzzleFault()
{
    g_reachBarrel.faulted.store(true, std::memory_order_release);
}

// T-2 shot evidence (declared in game.cpp): the firing context of the
// projectile transaction on this thread. Both values are the engine's own
// arguments to the four-argument outer firing scope; a missing optional
// muzzle transaction leaves the sentinels, never a fabricated identity.
void ReachReadFiringContextForTelemetry(uint32_t& weapon, uint8_t& barrel)
{
    const auto request = g_reachBarrelRequest;
    weapon = request.weapon;
    barrel = (request.barrel >= 0 && request.barrel < 2) ? uint8_t(request.barrel) : kTelemetryShotIndexUnknown;
}

// One line per local player shot, at most 4 a second: whether the
// barrel was applied and where the shot leaves (the marker's world origin and
// direction; with the barrel applied that is the gun's muzzle receipt).
void ReachShotLine(bool applied, int16_t count, const void* markers) noexcept
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
        // Diagnostic: the stick servo's target vs measured game-aim pitch and
        // the hand's yaw from the recentre reference, to split a non-converging loop from a gun
        // placed off a drifting game aim.
        constexpr float deg = 57.2957795f;
        char frame[256];
        Game_FormatShotFrame(GameTitle::HaloReach, frame, sizeof frame);
        LOG("Reach shot: barrel %s; origin %.3f %.3f %.3f, direction %.3f %.3f %.3f; "
            "servo pitch target %+.2f game %+.2f deg, hand yaw from reference %+.2f deg%s",
            applied ? "applied" : "refused", origin[0], origin[1], origin[2], direction[0], direction[1], direction[2],
            g_servoDesiredPitch.load(std::memory_order_relaxed) * deg,
            g_servoAimPitch.load(std::memory_order_relaxed) * deg,
            g_servoHandReferenceYaw.load(std::memory_order_relaxed) * deg, frame);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("the Reach shot diagnostic faulted");
    }
}
struct ReachMuzzleShots
{
    static ReachMuzzleRuntime& Feature() { return g_reachBarrel; }
    static ReachMuzzleQueryContext& Context() { return g_reachBarrelQueryContext; }
    static ReachMuzzleQueryCapture& Capture() { return g_reachBarrelCapture; }
    static ReachMuzzleShotScope& Shot() { return g_reachBarrelShot; }
    static ReachMuzzleRequest& Request() { return g_reachBarrelRequest; }
    static bool& OwnedQuery() { return g_legacyCollisionOwnedQuery; }
    static constexpr GameTitle kTitle = GameTitle::HaloReach;
    static uint32_t CameraGeneration() { return g_reachCamera.generation.load(); }
    static bool Armed() { return g_reachCamera.armed.load() && !g_reachCinematicLocked.load(); }
    static void* TargetStorage(uint32_t owner) { return ReachMuzzleTargetStorage(owner); }
    static bool ReadWeapons(uint32_t owner, uint32_t weapons[2]) { return ReachReadMuzzleWeapons(owner, weapons); }
    static bool ValidObject(uint32_t handle) { return ReachBarrelValidObject(handle); }
    static weapon_muzzle::Store& Muzzles() { return g_reachBarrelMuzzles; }
    static void MarkFault() { MarkReachMuzzleFault(); }
    // The shared core unit-adjust hook (ten arguments; the fourth is an output
    // velocity), called through its trampoline with collision on.
    static bool AimReady() { return g_origReachUnitAdjust != nullptr; }
    static void Aim(uint32_t unit, float* origin, float* direction, float* velocity, uint8_t simulation)
    {
        g_origReachUnitAdjust(int32_t(unit), origin, direction, velocity, nullptr, nullptr, 0, 0, 1, simulation);
    }
    static void AfterQuery(int32_t inputUser, int16_t zoom)
    {
        if (inputUser >= 0 && inputUser < 4)
        {
            g_reachEngineZoomLevel.store(zoom, std::memory_order_relaxed);
            g_reachEngineZoomMs.store(GetTickCount64(), std::memory_order_relaxed);
        }
    }
    static void ShotLine(bool applied, int16_t count, const void* markers) { ReachShotLine(applied, count, markers); }
    static constexpr uintptr_t kQueryCaller = 0x10EACD, kQueryCaller2 = 0x10EEF1, kAssistCaller = 0x10F954,
                               kMarkersCaller = 0x4C2AA0;
    // HREK 4E3EE0: the native view's third argument is the query direction.
    static constexpr auto kViewDirection = BarrelMuzzleViewDirection::QueryInput;
    static constexpr bool kInventorySnapshot = false, kMarkBarrelShot = false;
};

__declspec(noinline) void __fastcall ReachMuzzleViewDetour(
    uint32_t unit, uint8_t flags, float* direction, float* origin, float* cameraDirection, float* camera)
{
    auto& feature = g_reachBarrel;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (!feature.viewOriginal)
            __leave;
        feature.viewOriginal(unit, flags, direction, origin, cameraDirection, camera);
        BarrelMuzzleAfterView<ReachMuzzleShots>(
            reinterpret_cast<uintptr_t>(_ReturnAddress()), unit, direction, origin, cameraDirection, camera);
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}
constexpr auto& ReachMuzzleQueryDetour = BarrelMuzzleQueryDetour<ReachMuzzleShots>;
__declspec(noinline) int16_t __fastcall ReachMuzzleMarkersDetour(
    uint32_t object, uint32_t name, void* markers, int16_t capacity, uint8_t originalObject, uint8_t interpolated)
{
    auto& feature = g_reachBarrel;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    int16_t result = 0;
    __try
    {
        if (!feature.markersOriginal)
            __leave;
        result = feature.markersOriginal(object, name, markers, capacity, originalObject, interpolated);
        BarrelMuzzleAfterMarkers<ReachMuzzleShots>(
            reinterpret_cast<uintptr_t>(_ReturnAddress()), object, result, markers, capacity);
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}
// Invoked by the existing ReachUnitAdjustHook before its accepted fallback.
// The fourth native argument is an output velocity vector (HREK D48D40),
// despite the legacy core name basisForward; pass the real output unchanged.
bool ReachApplyBarrelAim(
    int32_t unit, float* origin, float* direction, const float* velocity, uint8_t collision, uint32_t simulation)
{
    auto& shot = g_reachBarrelShot;
    if (!shot.active || shot.query || shot.unit != uint32_t(unit) || !origin || !direction || !g_origReachUnitAdjust)
        return false;
    std::memcpy(origin, shot.position, 12);
    std::memcpy(direction, shot.direction, 12);
    g_origReachUnitAdjust(unit, origin, direction, velocity, nullptr, nullptr, 0, 0, collision, simulation);
    if (std::isfinite(origin[0]) && std::isfinite(origin[1]) && std::isfinite(origin[2]))
        std::memcpy(shot.position, origin, 12);
    return true;
}
constexpr auto& ReachMuzzleFireDetour = BarrelMuzzleFireDetour<ReachMuzzleShots>;
