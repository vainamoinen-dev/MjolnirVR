#pragma once
// Included once, by src/dll/titles/ce/haloce_first_person.cpp. Not a standalone header: it
//   uses what the host defines above its include line, namely AimCurrent,
//   CurrentPaletteContext, Hook, LocalOnFootShooter.
// HCEEK-derived native target record is 0x10 bytes, not a later-title target.
using MuzzleFireFn = void(__fastcall*)(uint32_t, int16_t, uint32_t, int16_t, const void*, uint8_t);
using MuzzleMarkersFn = int16_t(__fastcall*)(uint32_t, const char*, void*, int16_t);
using MuzzleQueryFn = void(__fastcall*)(int32_t, int16_t, Vec3*, void*);
using MuzzleDirectQueryFn = void(__fastcall*)(int32_t, int16_t, void*);
Hook muzzleFireHook, muzzleMarkersHook, muzzleQueryHook, muzzleDirectQueryHook;
std::atomic<bool> muzzleInstalled{}, muzzleFaulted{};
bool muzzleRetiring{};
uint32_t muzzleFailedGeneration{};
uint64_t muzzleInstalledAt{};
std::atomic<uint64_t> muzzleApplied{}, muzzleRefused{}, muzzleRestoreRefused{};
struct MuzzlePalette
{
    RenderContext context{};
    weapon_muzzle::Palette palette{};
    uint32_t unit = UINT32_MAX, weapon = UINT32_MAX;
};
Snapshot<MuzzlePalette> muzzlePalette;
struct MuzzleQueryContext
{
    uint32_t generation{}, unit = UINT32_MAX;
    int32_t input = -1;
    int16_t zoom = -1;
    bool direct{};
    uint64_t at{};
};
struct MuzzleShot
{
    bool active{}, query{}, queryApplied{};
    uint32_t unit = UINT32_MAX;
    Vec3 position{}, direction{};
};
struct MuzzleRequest
{
    uint32_t weapon = UINT32_MAX;
    int16_t barrel = -1;
    bool replicated{};
    NativeShotTargetLease<0x10>* lease{};
};
thread_local MuzzleQueryContext muzzleQueryContext;
thread_local MuzzleShot muzzleShot;
thread_local MuzzleRequest muzzleRequest;
bool MuzzleShotMatches(uint32_t unit) noexcept
{
    return muzzleShot.active && !muzzleShot.query && muzzleShot.unit == unit;
}
bool MuzzleOwner(uint32_t unit, uint32_t weapon) noexcept
{
    HaloCELocalPlayerState state{};
    return LocalOnFootShooter(unit) && HaloCEControls_GetLocalPlayerState(state) && state.unit == unit &&
           state.weapon == weapon && state.generation == generation.load();
}
void PublishMuzzlePalette(
    const Scope& owner, const FirstPersonBinding& binding, const NodeMatrix* nodes, uint64_t now) noexcept
{
    MuzzlePalette pending{};
    const auto& context = owner.context;
    if (muzzleInstalled.load() && !muzzleFaulted.load() && context.tracking.controllers.gunBarrelAim && nodes &&
        binding.count && MuzzleOwner(owner.muzzleUnit, owner.muzzleWeapon))
    {
        pending.context = context;
        pending.unit = owner.muzzleUnit;
        pending.weapon = owner.muzzleWeapon;
        const weapon_muzzle::Receipt base{GameTitle::HaloCE, context.tracking.generation, pending.unit, pending.weapon,
            binding.nodeIdentity, context.tracking.spaceEpoch, context.tracking.serial, now,
            context.tracking.predictedDisplayTimeNs, 0, 0, context.tracking.controllers.leftHanded};
        weapon_muzzle::FillPalette(pending.palette, GameTitle::HaloCE, binding.nodeIdentity, binding.count, base,
            [&](const weapon_muzzle::Marker& marker, weapon_muzzle::Ray& ray)
            {
                const auto& node = nodes[marker.node];
                const float basis[]{node.forward.x, node.forward.y, node.forward.z, node.left.x, node.left.y,
                    node.left.z, node.up.x, node.up.y, node.up.z};
                return weapon_muzzle::Transform(marker, node.scale, basis, &node.position.x, ray);
            });
    }
    (void)muzzlePalette.Publish(pending);
}
// The scope glass on the committed gun (kit row at "frame gun"),
// CE's twin of LegacyPublishLensAnchor. An empty receipt on any failure, so a
// stale lens never survives.
void PublishLensPalette(const Scope& owner, const FirstPersonBinding& binding, const NodeMatrix* nodes, uint64_t now,
    const contact_melee::TrackingToWorld* transform) noexcept
{
    weapon_muzzle::Palette lens{};
    const auto& context = owner.context;
    if (transform && nodes && binding.count && binding.gun >= 0 && binding.gun < binding.count &&
        owner.muzzleUnit != UINT32_MAX && owner.muzzleWeapon != UINT32_MAX &&
        MuzzleOwner(owner.muzzleUnit, owner.muzzleWeapon))
    {
        const weapon_muzzle::Receipt base{GameTitle::HaloCE, context.tracking.generation, owner.muzzleUnit,
            owner.muzzleWeapon, binding.nodeIdentity, context.tracking.spaceEpoch, context.tracking.serial, now,
            context.tracking.predictedDisplayTimeNs, 0, 0, context.tracking.controllers.leftHanded};
        const bool filled = lens_anchor::Fill(
            lens, GameTitle::HaloCE, binding.nodeIdentity, binding.count, base,
            [&](const weapon_muzzle::Marker& marker, weapon_muzzle::Ray& ray)
            {
                const auto& node = nodes[marker.node];
                const float basis[]{node.forward.x, node.forward.y, node.forward.z, node.left.x, node.left.y,
                    node.left.z, node.up.x, node.up.y, node.up.z};
                return weapon_muzzle::Transform(marker, node.scale, basis, &node.position.x, ray);
            },
            static_cast<uint32_t>(binding.gun));
        // The kit row is bind pose only: log the live frame-gun pose once.
        static bool loggedPose = false;
        if (filled && !loggedPose)
        {
            loggedPose = true;
            const auto& gun = nodes[binding.gun];
            const auto& ray = lens.barrels[0].ray;
            LOG("CE lens ray: frame gun node %d at (%.4f,%.4f,%.4f) fwd (%.3f,%.3f,%.3f) up (%.3f,%.3f,%.3f) scale %.3f -> lens (%.4f,%.4f,%.4f) wu",
                int(binding.gun), gun.position.x, gun.position.y, gun.position.z, gun.forward.x, gun.forward.y,
                gun.forward.z, gun.up.x, gun.up.y, gun.up.z, gun.scale, ray.position[0], ray.position[1],
                ray.position[2]);
        }
    }
    VR_PublishWeaponLensRay(
        GameTitle::HaloCE, 0, lens.barrels[0], transform ? *transform : contact_melee::TrackingToWorld{});
}
bool ReadMuzzle(uint32_t unit, uint32_t weapon, int16_t barrel, weapon_muzzle::Receipt& out) noexcept
{
    MuzzlePalette sample{};
    RenderContext live{}, committed{};
    if (barrel < 0 || barrel >= 2 || !muzzleInstalled.load() || muzzleFaulted.load() || !AimCurrent() ||
        !CurrentPaletteContext(committed) || !HaloCE_GetGameplayContext(live) ||
        !live.tracking.controllers.gunBarrelAim || live.tracking.controllers.controlsPresentationBlocked ||
        !live.tracking.controllers.primaryAim.valid || !MuzzleOwner(unit, weapon) || !muzzlePalette.Read(sample) ||
        sample.unit != unit || sample.weapon != weapon || !HaloCE_RenderContextCurrent(sample.context) ||
        sample.context.rendererEpoch != live.rendererEpoch ||
        sample.context.referenceRevision != live.referenceRevision)
        return false;
    const auto& candidate = sample.palette.barrels[barrel];
    if (!weapon_muzzle::Fresh(candidate, GameTitle::HaloCE, live.tracking.generation, unit, weapon,
            live.tracking.spaceEpoch, GetTickCount64(), live.tracking.predictedDisplayTimeNs, 0, uint8_t(barrel),
            live.tracking.controllers.leftHanded))
        return false;
    out = candidate;
    return true;
}
void* MuzzleTargetStorage(uint32_t unit)
{
    if (!LocalOnFootShooter(unit))
        return nullptr;
    using ObjectFn = uintptr_t(__fastcall*)(uint32_t, uint32_t);
    const auto object = reinterpret_cast<ObjectFn>(moduleBase + contract::player_state::state_object_try_get)(unit, 3);
    // Own HCEEK 8EFD90 and retail B7A374: parent308, target1E0.
    if (!object || *reinterpret_cast<const uint32_t*>(object + 0x308) != UINT32_MAX)
        return nullptr;
    return reinterpret_cast<void*>(object + 0x1E0);
}
void RestoreMuzzleTarget(NativeShotTargetLease<0x10>& lease)
{
    if (!lease.active)
        return;
    __try
    {
        if (!lease.Restore(generation.load(), lease.owner, MuzzleTargetStorage(lease.owner)))
            ++muzzleRestoreRefused;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("restoring the CE muzzle lease faulted");
        lease.active = false;
        muzzleFaulted = true;
        ++muzzleRestoreRefused;
    }
}
