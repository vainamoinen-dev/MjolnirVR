#pragma once
// The optional committed-barrel muzzle runtime shared by Reach, Halo 4 and
// ODST. Each title keeps its own rows, offsets, native
// signatures, preconditions and exact log text in its title files; this file
// owns only the common steps, as templates over a per-title traits type.
// ODST's native shapes differ: fire is five
// arguments returning uint64_t, markers return uint64_t, and its view has no
// camera-direction output. Each difference is a constexpr trait (kViewDirection,
// kAssistCaller 0, kInventorySnapshot, kMarkBarrelShot)
// used under if constexpr, so Reach and Halo 4 keep their own bodies.
// Not a standalone header: like muzzle_hook_set.h's users, it uses what the
// host defines above its include line (game.cpp, or a title's muzzle test).
#include "telemetry_recorder.h"

// ---- lifecycle ----
// In barrel_muzzle_lifecycle.h (shared with Halo 2 and Halo 3).
#include "barrel_muzzle_lifecycle.h"
#include "guard_count.h"

// ---- ownership ----
// Traits T: Object(handle,kind) (the title's validated object reader),
// Read<U>(object,offset), kUnit/kWeapon kinds, the weapon's owner/parent-flag/
// parent offsets, the unit's role bytes and four inventory handles, and the
// unit's controlling-parent and shot-target offsets.
template <class T>
bool BarrelMuzzleOwnedWeapon(uint32_t handle, uint32_t owner)
{
    const auto* weapon = T::Object(handle, T::kWeapon);
    if (!weapon)
        return false;
    auto actual = T::template Read<uint32_t>(weapon, T::kOwner);
    if (actual == UINT32_MAX && weapon[T::kHasParent])
        actual = T::template Read<uint32_t>(weapon, T::kParent);
    return actual == owner;
}
template <class T>
bool BarrelMuzzleReadWeapons(uint32_t owner, uint32_t weapons[2], bool* primaryAbsentOut)
{
    if (primaryAbsentOut)
        *primaryAbsentOut = false;
    const auto* unit = T::Object(owner, T::kUnit);
    if (!unit)
        return false;
    // A 0xFF primary role is reported as absence before any other refusal (F03).
    return weapon_inventory::ReadRoleWeapons(
        unit[T::kRoles], unit[T::kRoles + 1], false, weapons, primaryAbsentOut,
        [unit](uint8_t i) { return T::template Read<uint32_t>(unit, T::kHandles + i * 4); },
        [owner](uint32_t handle) { return BarrelMuzzleOwnedWeapon<T>(handle, owner); });
}
// The primary handle and the single owned weapon holding neither role (the stowed one).
template <class T>
bool BarrelMuzzleReadReserveWeapon(uint32_t owner, uint32_t& primary, uint32_t& reserve)
{
    primary = reserve = reserve_weapon::kNoHandle;
    const auto* unit = T::Object(owner, T::kUnit);
    if (!unit)
        return false;
    return weapon_inventory::ReadReserveWeapon(
        unit[T::kRoles], unit[T::kRoles + 1], primary, reserve,
        [unit](uint8_t i) { return T::template Read<uint32_t>(unit, T::kHandles + i * 4); },
        [owner](uint32_t handle) { return BarrelMuzzleOwnedWeapon<T>(handle, owner); });
}
// After the title's own "local, unseated player" test: a unit with a
// controlling parent must not receive a lease for the local biped's target.
template <class T>
void* BarrelMuzzleTargetStorage(uint32_t owner)
{
    const auto* unit = T::Object(owner, T::kUnit);
    if (!unit || T::template Read<uint32_t>(unit, T::kControllingParent) != UINT32_MAX)
        return nullptr;
    return const_cast<uint8_t*>(unit) + T::kTargetStorage;
}

// ---- shots ----
// Per-thread state of one native shot transaction; the same for both titles.
struct BarrelMuzzleQueryContext
{
    uint32_t unit = UINT32_MAX, generation = 0;
    int32_t inputUser = -1;
    uint8_t flags = 0;
    int16_t zoom = -1;
    uint64_t sampleMs = 0;
};
struct BarrelMuzzleQueryCapture
{
    bool active = false;
    uint32_t unit = UINT32_MAX, count = 0;
};
struct BarrelMuzzleShotScope
{
    bool active = false, query = false, viewApplied = false;
    uint32_t unit = UINT32_MAX;
    float position[3]{}, direction[3]{};
    // Weapon slot the committed muzzle receipt resolved for this shot (0/1), or
    // kTelemetryShotIndexUnknown when no receipt owned it. T-2 evidence only.
    uint8_t slot = kTelemetryShotIndexUnknown;
    // ODST (kMarkBarrelShot): the marker was applied; ODST's aim detour reads it.
    bool barrel = false;
};
struct BarrelMuzzleRequest
{
    uint32_t weapon = UINT32_MAX;
    int16_t barrel = -1;
    uint8_t simulation = 0;
    NativeShotTargetLease<0x28>* lease = nullptr;
};
// Reach and Halo 4: four-argument void firing scope. All three: six-argument aim query.
using BarrelMuzzleFireFn = void(__fastcall*)(uint32_t, int16_t, void*, uint8_t);
using BarrelMuzzleQueryFn = void(__fastcall*)(int32_t, uint8_t, float*, int16_t, float*, void*);
// What the native view's camera-direction output receives: the query's own
// input direction (Reach), the shot direction (Halo 4), or nothing, because
// the native view has no such output (ODST).
enum class BarrelMuzzleViewDirection
{
    QueryInput,
    ShotDirection,
    None
};

// Traits T: Feature() (the runtime: base, generation, installedAtMs, the
// originals, enabled/faulted, callbacks, counters); the per-thread
// Context()/Capture()/Shot()/Request(); OwnedQuery() (the title's
// collision-ownership flag); kTitle; CameraGeneration(); Armed() (camera armed
// and player in control); TargetStorage/ReadWeapons/ValidObject; Muzzles();
// MarkFault(); AimReady() and Aim() (the native obstruction call);
// AfterQuery() (a title's extra publication after the native query);
// ShotLine(); the native return addresses kQueryCaller, kQueryCaller2,
// kAssistCaller (0: the title has no assist caller), kMarkersCaller;
// kViewDirection (above); kInventorySnapshot (ODST: both inventory slots read
// before the re-query must be unchanged after it, and the re-queried shot ray
// finite); and kMarkBarrelShot (ODST: an applied marker sets shot.barrel).
// ODST counts refusals by the same rule (the player's own shots only).

// The view body, run by the title's view detour after the native view. It
// replaces the projected position and camera-ray outputs in this shot only,
// preserving the native view class and result.
template <class T>
void BarrelMuzzleAfterView(
    uintptr_t caller, uint32_t unit, const float* inputDirection, float* origin, float* cameraDirection, float* camera)
{
    using Direction = BarrelMuzzleViewDirection;
    auto& feature = T::Feature();
    const bool query = caller == feature.base + T::kQueryCaller || caller == feature.base + T::kQueryCaller2;
    bool assist = false;
    if constexpr (T::kAssistCaller != 0)
        assist = caller == feature.base + T::kAssistCaller;
    if (!query && !assist)
        return;
    auto& capture = T::Capture();
    if (capture.active && caller == feature.base + T::kQueryCaller)
    {
        capture.unit = unit;
        ++capture.count;
    }
    auto& shot = T::Shot();
    if (!shot.active || shot.unit != unit || !origin || !camera || (shot.query ? !query : !assist))
        return;
    if constexpr (T::kViewDirection != Direction::None)
    {
        if (!cameraDirection)
            return;
    }
    if constexpr (T::kViewDirection == Direction::QueryInput)
    {
        if (!inputDirection)
            return;
    }
    std::memcpy(origin, shot.position, 12);
    std::memcpy(camera, shot.position, 12);
    if constexpr (T::kViewDirection == Direction::QueryInput)
        std::memmove(cameraDirection, shot.query ? inputDirection : shot.direction, 12);
    else if constexpr (T::kViewDirection == Direction::ShotDirection)
        std::memcpy(cameraDirection, shot.direction, 12);
    shot.viewApplied = true;
}
// The aim-query detour (same native signature in both titles).
template <class T>
__declspec(noinline) void __fastcall BarrelMuzzleQueryDetour(
    int32_t inputUser, uint8_t flags, float* direction, int16_t zoom, float* control, void* targeting)
{
    auto& feature = T::Feature();
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    const auto previous = T::Capture();
    T::Capture() = {};
    const bool capture = feature.enabled.load() && !feature.faulted.load() && g_config.gun_barrel_aim &&
                         inputUser >= 0 && inputUser < 4 && !(flags & 1) && direction && control && targeting;
    T::Capture().active = capture;
    __try
    {
        if (!feature.queryOriginal)
            __leave;
        feature.queryOriginal(inputUser, flags, direction, zoom, control, targeting);
        T::AfterQuery(inputUser, zoom);
        if (capture && T::Capture().count == 1)
            T::Context() = {T::Capture().unit, feature.generation, inputUser, flags, zoom, GetTickCount64()};
    }
    __finally
    {
        T::Capture() = previous;
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}
template <class T>
void BarrelMuzzleRestoreTarget(NativeShotTargetLease<0x28>& lease)
{
    if (!lease.active)
        return;
    __try
    {
        if (!lease.Restore(T::CameraGeneration(), lease.owner, T::TargetStorage(lease.owner)))
            T::Feature().restoreRefused.fetch_add(1, std::memory_order_relaxed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("restoring the barrel muzzle lease faulted");
        lease.active = false;
        T::MarkFault();
        T::Feature().restoreRefused.fetch_add(1, std::memory_order_relaxed);
    }
}
// Runs the native query once more from the barrel, so native targeting and
// obstruction see the authored ray, then leases the result into the unit.
template <class T>
bool BarrelMuzzleAcquireTarget(const weapon_muzzle::Receipt& muzzle, NativeShotTargetLease<0x28>& lease)
{
    auto& feature = T::Feature();
    const auto context = T::Context();
    void* storage = T::TargetStorage(context.unit);
    if (!storage || !feature.queryOriginal)
        return false;
    [[maybe_unused]] uint32_t before[2]{};
    if constexpr (T::kInventorySnapshot)
    {
        if (muzzle.slot >= 2 || !T::ReadWeapons(context.unit, before) || before[muzzle.slot] != muzzle.weapon)
            return false;
    }
    auto& shot = T::Shot();
    shot = {};
    shot.active = true;
    shot.query = true;
    shot.unit = context.unit;
    shot.slot = muzzle.slot;
    std::memcpy(shot.position, muzzle.ray.position, 12);
    std::memcpy(shot.direction, muzzle.ray.direction, 12);
    alignas(8) unsigned char targeting[0x28]{};
    float control[3]{};
    const bool previous = T::OwnedQuery();
    T::OwnedQuery() = true;
    __try
    {
        feature.queryOriginal(context.inputUser, context.flags, shot.direction, context.zoom, control, targeting);
    }
    __finally
    {
        T::OwnedQuery() = previous;
        shot.query = false;
    }
    uint32_t weapons[2]{};
    if (!shot.viewApplied || feature.generation != T::CameraGeneration() || T::TargetStorage(context.unit) != storage ||
        !T::ReadWeapons(context.unit, weapons))
        return false;
    if constexpr (T::kInventorySnapshot)
    {
        // ODST: neither slot may change, and the native query (handed the shot
        // direction) must leave the shot ray finite.
        if (weapons[0] != before[0] || weapons[1] != before[1])
            return false;
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(shot.position[axis]) || !std::isfinite(shot.direction[axis]))
                return false;
    }
    else if (weapons[muzzle.slot] != muzzle.weapon)
        return false;
    // The 0x28-byte native target (HREK 4E2330; H4EK 585160 / retail 1DB840):
    // strengths +10/+14 and lead +18..20 must be finite; flags +24.
    for (size_t at = 0x10; at <= 0x20; at += 4)
        if (!std::isfinite(*reinterpret_cast<const float*>(targeting + at)))
            return false;
    // Object handle +8, marker +4 (HREK 4C4790/4C47A0; H4EK 56F420/56F010, retail 1CD454).
    const uint32_t target = *reinterpret_cast<const uint32_t*>(targeting + 8);
    if (target != UINT32_MAX && !T::ValidObject(target))
        return false;
    if (!lease.Apply(feature.generation, context.unit, storage, targeting))
        return false;
    feature.queries.fetch_add(1, std::memory_order_relaxed);
    return true;
}
template <class T>
bool BarrelMuzzleApplyMarker(uint32_t object, uint64_t count, void* markers, int16_t capacity)
{
    auto& feature = T::Feature();
    const auto request = T::Request();
    if (!feature.enabled.load() || feature.faulted.load() || !g_config.gun_barrel_aim || exclusive_input::Active() ||
        !g_vrAim.load() || !g_enabled.load() || !VR_IsStereoEnabled() || TitleAdapter_GetActiveTitle() != T::kTitle ||
        feature.generation != T::CameraGeneration() || !request.lease || request.lease->active || request.barrel < 0 ||
        request.barrel >= 2 || object != request.weapon || count != 1 || capacity < 1 || !markers || !T::AimReady())
        return false;
    if (!T::Armed())
        return false;
    const auto context = T::Context();
    const auto now = GetTickCount64();
    if (!weapon_muzzle::QuerySampleFresh(
            context.sampleMs, context.generation, feature.installedAtMs, feature.generation, now) ||
        !T::TargetStorage(context.unit))
        return false;
    uint32_t weapons[2]{};
    VrContactTrackingSnapshot tracking{};
    if (!T::ReadWeapons(context.unit, weapons) || !VR_GetContactTrackingSnapshot(tracking))
        return false;
    const int slot = ResolveEquippedWeaponSlot(request.weapon, weapons[0], weapons[1], true, weapons[1] != UINT32_MAX);
    weapon_muzzle::Receipt muzzle{};
    if (slot < 0 || !tracking.hands[slot == 0 ? 1 : 0].valid ||
        !T::Muzzles().Read(T::kTitle, feature.generation, context.unit, request.weapon, tracking.referenceEpoch, now,
            tracking.timeNs, uint8_t(slot), uint8_t(request.barrel), g_config.left_handed, muzzle))
        return false;
    float velocity[3]{}, direction[3]{};
    std::memcpy(direction, muzzle.ray.direction, 12);
    const bool previous = T::OwnedQuery();
    T::OwnedQuery() = true;
    __try
    {
        T::Aim(context.unit, muzzle.ray.position, direction, velocity, request.simulation);
    }
    __finally
    {
        T::OwnedQuery() = previous;
    }
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(muzzle.ray.position[axis]) || !std::isfinite(direction[axis]))
            return false;
    if (!BarrelMuzzleAcquireTarget<T>(muzzle, *request.lease))
        return false;
    const auto& ray = muzzle.ray;
    auto* bytes = static_cast<uint8_t*>(markers);
    weapon_muzzle::WriteMarkerWorldFrame(bytes, ray.direction, ray.up, ray.position);
    if constexpr (T::kMarkBarrelShot)
        T::Shot().barrel = true;
    return true;
}
// The markers body, run by the title's markers detour after the native call
// returned `result` (int16_t in Reach and Halo 4, uint64_t in ODST). Only the
// outer firing call's markers are replaced.
template <class T, class Result>
void BarrelMuzzleAfterMarkers(uintptr_t caller, uint32_t object, Result result, void* markers, int16_t capacity)
{
    auto& feature = T::Feature();
    if (caller != feature.base + T::kMarkersCaller || !T::Request().lease)
        return;
    const auto previous = T::Shot();
    bool applied = false;
    __try
    {
        applied = BarrelMuzzleApplyMarker<T>(object, result, markers, capacity);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("applying the barrel muzzle marker faulted");
        T::MarkFault();
    }
    // Every actor fires through here; only the local player's
    // own shots count as refused and get the per-shot line, so a real refusal shows.
    bool playerShot = false;
    __try
    {
        uint32_t playerWeapons[2]{UINT32_MAX, UINT32_MAX};
        const uint32_t viewUnit = T::Context().unit;
        playerShot = viewUnit && T::ReadWeapons(viewUnit, playerWeapons) &&
                     (object == playerWeapons[0] || object == playerWeapons[1]);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("reading the player's weapons for a barrel shot faulted");
        playerShot = false;
    }
    if (applied)
        feature.applied.fetch_add(1, std::memory_order_relaxed);
    else
    {
        BarrelMuzzleRestoreTarget<T>(*T::Request().lease);
        T::Shot() = previous;
        if (playerShot)
            feature.refused.fetch_add(1, std::memory_order_relaxed);
    }
    if (playerShot)
        T::ShotLine(applied, result, markers);
}
// The outer firing scope: one lease per shot, nested scopes restored LIFO, the
// lease restored once even when the native fire raises. `native` makes the
// title's own native call and its result (void in Reach and Halo 4, uint64_t
// in ODST) is returned.
// The CALLER (the title's fire detour) has already counted itself in
// feature.callbacks, as its first statement; this scope only releases that
// count. Retire's quiescence wait sees a thread by its Rip inside a listed
// detour or trampoline, or by the counter. This function is not listed and is
// never inlined (it holds __try), so a count taken here would leave a window
// in which retire could free the trampoline that native() is about to call.
template <class T>
void BarrelMuzzleEndFire(
    NativeShotTargetLease<0x28>& lease, const BarrelMuzzleShotScope& shot, const BarrelMuzzleRequest& request)
{
    BarrelMuzzleRestoreTarget<T>(lease);
    T::Shot() = shot;
    T::Request() = request;
    T::Feature().callbacks.fetch_sub(1, std::memory_order_acq_rel);
}
template <class T, class Native>
auto BarrelMuzzleFireScope(uint32_t weapon, int16_t barrel, uint8_t simulation, Native native)
{
    const auto previousShot = T::Shot();
    const auto previousRequest = T::Request();
    NativeShotTargetLease<0x28> lease{};
    T::Shot() = {};
    T::Request() = {weapon, barrel, simulation, &lease};
    using Result = decltype(native());
    if constexpr (std::is_void_v<Result>)
    {
        __try
        {
            native();
        }
        __finally
        {
            BarrelMuzzleEndFire<T>(lease, previousShot, previousRequest);
        }
    }
    else
    {
        Result result{};
        __try
        {
            result = native();
        }
        __finally
        {
            BarrelMuzzleEndFire<T>(lease, previousShot, previousRequest);
        }
        return result;
    }
}
// The four-argument void firing-scope detour of Reach and Halo 4.
template <class T>
__declspec(noinline) void __fastcall BarrelMuzzleFireDetour(
    uint32_t weapon, int16_t barrel, void* data, uint8_t simulation)
{
    auto& feature = T::Feature();
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel); // before the scope: see above
    BarrelMuzzleFireScope<T>(weapon, barrel, simulation,
        [&]
        {
            if (feature.fireOriginal)
                feature.fireOriginal(weapon, barrel, data, simulation);
        });
}
