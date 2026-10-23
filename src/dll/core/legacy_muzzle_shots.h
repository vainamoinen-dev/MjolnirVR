#pragma once
// The barrel-marker body shared by Halo 2 and Halo 3, the sibling of
// legacy_muzzle_publication.h. In both titles the shot
// transaction (fire, query, camera, lease, acquire, restore) belongs to the
// dual-wield runtime; the muzzle feature is one markers hook that borrows it.
// This file owns only what that hook does after the native call. Each title
// keeps its markers detour (its own native signature), its gate, its refusal
// reasons and its diagnostic; where the titles differ, the template calls a
// title function at a fixed point. There are no switches.
// Not a standalone header: it uses what the host defines above its include
// line (weapon_muzzle, VR_GetContactTrackingSnapshot, ResolveEquippedWeaponSlot,
// g_config). Halo 2 includes it inside the observer's anonymous namespace, so
// it includes nothing itself.
//
// Traits T: Feature() (the muzzle runtime: base, generation, the counters);
// Request() (weapon, barrel, lease); Shot() (the dual runtime's shot scope,
// saved and restored whole); Gate(request, object, count, capacity, markers,
// unit) (the title's checks, verbatim and in its order; it outputs the query
// unit); Refuse(why) (returns false; Halo 3 records the reason); BeforeApply();
// ReadWeapons(unit, weapons); kTitle and Muzzles(); AimReady(),
// TargetStorage(unit), Aim(unit, position, direction, velocity) and
// OwnedQuery() (the title's collision flag); Acquire(weapon, lease, muzzle);
// Restore(lease); MarkFault(); PlayerShot(request) (the request's weapon is
// one of the local player's own); AfterShot(applied, playerShot, result,
// markers, capacity); kMarkersCaller.
#include "guard_count.h"

// After the title's gate: weapons and tracking, the slot, the committed
// receipt, the aim and target storage, the private-velocity obstruction
// preflight under the title's collision flag, the finite check, the shot
// lease, then the marker frame.
template <class T, class Count>
bool LegacyMuzzleApplyMarker(uint32_t object, Count count, void* markers, int16_t capacity)
{
    auto& feature = T::Feature();
    const auto request = T::Request();
    uint32_t unit = UINT32_MAX;
    if (!T::Gate(request, object, count, capacity, markers, unit))
        return false;
    uint32_t weapons[2]{};
    VrContactTrackingSnapshot tracking{};
    if (!T::ReadWeapons(unit, weapons) || !VR_GetContactTrackingSnapshot(tracking))
        return T::Refuse("weapons-or-tracking");
    const int slot = ResolveEquippedWeaponSlot(request.weapon, weapons[0], weapons[1], true, weapons[1] != UINT32_MAX);
    weapon_muzzle::Receipt muzzle{};
    if (slot < 0)
        return T::Refuse("slot");
    if (!T::Muzzles().Read(T::kTitle, feature.generation, unit, request.weapon, tracking.referenceEpoch,
            GetTickCount64(), tracking.timeNs, uint8_t(slot), uint8_t(request.barrel), g_config.left_handed, muzzle))
        return T::Refuse("muzzle-receipt-stale-or-missing");
    if (!T::AimReady() || !T::TargetStorage(unit))
        return T::Refuse("aim-or-target-storage");
    float velocity[3]{}, direction[3]{};
    std::memcpy(direction, muzzle.ray.direction, 12);
    bool& ownedQuery = T::OwnedQuery();
    const bool previousCollisionQuery = ownedQuery;
    ownedQuery = true;
    __try
    {
        T::Aim(unit, muzzle.ray.position, direction, velocity);
    }
    __finally
    {
        ownedQuery = previousCollisionQuery;
    }
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(muzzle.ray.position[axis]) || !std::isfinite(direction[axis]))
            return T::Refuse("non-finite");
    if (!T::Acquire(request.weapon, *request.lease, &muzzle))
        return T::Refuse("shot-lease");
    // Preserve the native local matrix, node/region identity, scale and flags.
    // Its world orientation/position now matches the verified visible marker.
    auto* bytes = static_cast<uint8_t*>(markers);
    const auto& ray = muzzle.ray;
    weapon_muzzle::WriteMarkerWorldFrame(bytes, ray.direction, ray.up, ray.position);
    return true;
}

// The markers detour's body after the native call. The CALLER (the title's
// markers detour) counts itself in feature.callbacks as its first statement and
// releases the count in its own __finally; this function neither takes nor
// releases one. It holds __try, so it is never inlined and is not on retire's
// quiescence list: a count taken here would leave a window in which retire
// could free the trampoline.
// Only the weapon firing caller with a live muzzle lease is a transaction. A
// refusal (or an exception in the apply, which marks the fault) restores the
// lease and the shot scope whole. As in Reach, Halo 4 and ODST:
// every actor fires through here, and only the local player's own refused
// shot is counted. The player test runs once, in its own __try (a fault
// means "not the player's"), before the restore, which writes only the
// target storage the test does not read; AfterShot gets the same answer.
// Result is the native count type.
template <class T, class Result>
void LegacyMuzzleAfterMarkers(uintptr_t caller, uint32_t object, Result result, void* markers, int16_t capacity)
{
    auto& feature = T::Feature();
    if (caller != feature.base + T::kMarkersCaller || !T::Request().lease)
        return;
    const auto previous = T::Shot();
    bool applied = false;
    T::BeforeApply();
    __try
    {
        applied = LegacyMuzzleApplyMarker<T>(object, result, markers, capacity);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("applying the muzzle marker faulted");
        T::MarkFault();
    }
    bool playerShot = false;
    __try
    {
        playerShot = T::PlayerShot(T::Request());
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("checking whether the player fired faulted");
        playerShot = false;
    }
    if (applied)
        feature.applied.fetch_add(1, std::memory_order_relaxed);
    else
    {
        T::Restore(*T::Request().lease);
        T::Shot() = previous;
        if (playerShot)
            feature.refused.fetch_add(1, std::memory_order_relaxed);
    }
    T::AfterShot(applied, playerShot, result, markers, capacity);
}
