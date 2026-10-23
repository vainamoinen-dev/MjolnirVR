// Halo 4 muzzle publication: captures the rendered gun's barrel and publishes the reticle ray and scope lens anchor.
// Publishes only for the local player's own weapon; an unowned frame publishes an empty barrel.
#pragma once
// Included once, by src/dll/titles/h4/halo4_camera_core.cpp; also compiled by
//   tests/halo4_muzzle_tests.cpp, which supplies its own versions. Not a standalone header: it
//   uses what the host defines above its include line, namely BoneMatrix,
//   Halo4FloatingPairMatchesCurrent, g_halo4Camera, g_halo4FloatingPair.
struct Halo4MuzzlePending
{
    bool admitted = false;
    uint32_t generation = 0, unit = UINT32_MAX, weapon = UINT32_MAX;
    uint8_t slot = 0;
    weapon_muzzle::Palette palette{};
    weapon_muzzle::Palette lens{};
    bool lensHasTransform = false;
    contact_melee::TrackingToWorld lensTransform{};
    // XR-local, for the reticle ray (lensTransform is the contact frame's).
    bool reticleHasTransform = false;
    contact_melee::TrackingToWorld reticleTransform{};
};
Halo4MuzzlePending Halo4PrepareMuzzlePalette(
    uint32_t weapon, uint16_t renderModelIndex, uint32_t checksum, int count, const BoneMatrix* palette)
{
    Halo4MuzzlePending pending{};
    if (!g_config.gun_barrel_aim || count <= 0 || count > kHalo4FirstPersonBankTransforms ||
        !Halo4FloatingPairMatchesCurrent())
        return pending;
    __try
    {
        if (!Halo4CaptureMuzzleOwner(weapon, pending.unit, pending.slot))
            return pending;
        pending.admitted = true;
        pending.weapon = weapon;
        pending.generation = g_halo4FloatingPair.generation;
        if (!palette)
            return pending;
        const auto* fp = Halo4VehicleRead<const uint8_t*>(Halo4MuzzleTls(), 0x6A0);
        if (Halo4VehicleRead<uint16_t>(fp, pending.slot * 0x2EC8 + 0x74) != renderModelIndex)
            return pending;
        const auto& tracking = g_halo4FloatingPair.contactFrames[pending.slot == 0 ? 1 : 0];
        if (!tracking.serial || tracking.serial != g_halo4FloatingPair.preparedSerial || !tracking.referenceEpoch ||
            !g_halo4FloatingPair.contactRawEpoch || tracking.timeNs <= 0)
            return pending;
        const auto now = GetTickCount64();
        // Keyed by the raw tracking epoch the shot reads; the
        // frame's own referenceEpoch is the hashed contact-space epoch and never matched.
        const weapon_muzzle::Receipt base{GameTitle::Halo4, pending.generation, pending.unit, weapon, checksum,
            g_halo4FloatingPair.contactRawEpoch, tracking.serial, now, tracking.timeNs, pending.slot, 0,
            g_config.left_handed};
        weapon_muzzle::FillPalette(pending.palette, GameTitle::Halo4, checksum, uint32_t(count), base,
            [&](const weapon_muzzle::Marker& marker, weapon_muzzle::Ray& ray)
            {
                const auto& node = palette[marker.node];
                return weapon_muzzle::Transform(marker, node.scale, node.rotation, node.translation, ray);
            });
        // Scope lens at node 0 x the kit glass row (gated at read time by scope_lens_node_anchor).
        (void)lens_anchor::Fill(pending.lens, GameTitle::Halo4, checksum, uint32_t(count), base,
            [&](const weapon_muzzle::Marker& marker, weapon_muzzle::Ray& ray)
            {
                const auto& node = palette[marker.node];
                return weapon_muzzle::Transform(marker, node.scale, node.rotation, node.translation, ray);
            });
        // The lens keeps the contact frame's transform: automated tests show the
        // H4 glass raising correctly on it, so changing it needs the scope glass
        // checks run again.
        pending.lensTransform = tracking.transform;
        pending.lensHasTransform = true;
        // The reticle ray is drawn in OpenXR local space, so it uses the
        // pair's XR-local transform, not the body-relative contact frame (x forward).
        if (g_halo4FloatingPair.xrTrackingValid)
        {
            pending.reticleTransform = g_halo4FloatingPair.xrTrackingTransform;
            pending.reticleHasTransform = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("preparing the Halo 4 muzzle palette faulted");
        pending.palette = {};
        pending.lens = {};
        pending.lensHasTransform = false;
    }
    return pending;
}
void Halo4CommitMuzzlePalette(const Halo4MuzzlePending& pending)
{
    if (!pending.admitted)
        return;
    weapon_muzzle::Palette palette{};
    bool owned = false;
    __try
    {
        uint32_t owner = UINT32_MAX;
        uint8_t slot = 0;
        if (pending.generation == g_halo4Camera.generation.load() && Halo4FloatingPairMatchesCurrent() &&
            Halo4CaptureMuzzleOwner(pending.weapon, owner, slot) && owner == pending.unit && slot == pending.slot)
        {
            palette = pending.palette;
            owned = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("committing the Halo 4 muzzle palette faulted");
    }
    (void)g_halo4BarrelMuzzles.Publish(GameTitle::Halo4, pending.slot, palette);
    // The reticle rides this committed barrel (empty when not owned, which
    // leaves the reticle on the hand ray), projected into XR-local space; the
    // body-relative contact-frame transform (x forward) would draw the reticle
    // 90 deg to the right. No XR transform: empty ray.
    VR_PublishWeaponReticleRay(GameTitle::Halo4, pending.slot,
        pending.reticleHasTransform ? palette.barrels[0] : weapon_muzzle::Receipt{}, pending.reticleTransform);
    if (owned && pending.slot == 0 && pending.lensHasTransform)
        VR_PublishWeaponLensRay(GameTitle::Halo4, 0, pending.lens.barrels[0], pending.lensTransform);
}
