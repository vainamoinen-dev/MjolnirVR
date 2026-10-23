// Publishes the muzzle marker transforms of Halo 2's drawn first-person weapons.
// Included once, by src/dll/titles/h2/halo2_observer_6dof.cpp; also compiled by
//   tests/weapon_muzzle_tests.cpp, which supplies its own versions. Not a standalone header: it
//   uses what the host defines above its include line, namely Halo2VisibleConsumerContext,
//   g_generation, kOwnedUser.
#pragma once
// H2 publishes its committed render-model palette, after carrier trimming and
// collision correction. Never substitute the controller or an unclamped node.
weapon_muzzle::Store g_halo2Muzzles;
void Halo2PublishMuzzlePalette(const Halo2VisibleConsumerContext& context, uint8_t slot, uint64_t identity,
    const float* matrices, uint32_t count) noexcept
{
    weapon_muzzle::Palette palette{}, lens{};
    const uint32_t weapon = slot == 0 ? context.weaponObject : context.secondaryWeaponObject;
    if (context.valid && context.user == kOwnedUser && context.muzzleGeneration &&
        context.muzzleGeneration == g_generation.load(std::memory_order_acquire) && context.unitObject != UINT32_MAX &&
        weapon != UINT32_MAX && matrices && count && count <= kHalo2FirstPersonPaletteCapacity && context.muzzleSpace &&
        context.muzzleSerial && context.muzzleTimeNs > 0)
    {
        const uint64_t now = GetTickCount64();
        const weapon_muzzle::Receipt base{GameTitle::Halo2, context.muzzleGeneration, context.unitObject, weapon,
            identity, context.muzzleSpace, context.muzzleSerial, now, context.muzzleTimeNs, slot, 0,
            context.muzzleLeftHanded};
        weapon_muzzle::FillPalette(palette, GameTitle::Halo2, identity, count, base,
            [&](const weapon_muzzle::Marker& marker, weapon_muzzle::Ray& ray)
            {
                Halo2FirstPersonTransform node{};
                return Halo2ReadFirstPersonTransform(matrices + marker.node * kHalo2FirstPersonNodeFloats, node) &&
                       weapon_muzzle::Transform(marker, node.scale, node.rotation, node.translation, ray);
            });
        // Scope lens at node 0 x the kit glass row (gated at read time by scope_lens_node_anchor).
        (void)lens_anchor::Fill(lens, GameTitle::Halo2, identity, count, base,
            [&](const weapon_muzzle::Marker& marker, weapon_muzzle::Ray& ray)
            {
                Halo2FirstPersonTransform node{};
                return Halo2ReadFirstPersonTransform(matrices + marker.node * kHalo2FirstPersonNodeFloats, node) &&
                       weapon_muzzle::Transform(marker, node.scale, node.rotation, node.translation, ray);
            });
    }
    (void)g_halo2Muzzles.Publish(GameTitle::Halo2, slot, palette);
    if (slot < 2 && context.contactFrames[slot == 0 ? 1 : 0].serial == context.muzzleSerial)
        VR_PublishWeaponReticleRay(
            GameTitle::Halo2, slot, palette.barrels[0], context.contactFrames[slot == 0 ? 1 : 0].transform);
    if (slot == 0)
        VR_PublishWeaponLensRay(GameTitle::Halo2, 0, lens.barrels[0], context.contactFrames[1].transform);
}
