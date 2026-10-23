// Halo 3 muzzle publication: captures the rendered gun's barrel and publishes the reticle ray and scope lens anchor.
// Holds title data only; the capture and publish steps are shared with ODST.
#pragma once
// Included once, by src/dll/titles/h3/halo3_dual_wield_runtime.inl; also compiled by
//   tests/halo3_muzzle_publication_tests.cpp, which supplies its own versions. Not a standalone
//   header: it uses what the host defines above its include line, namely Halo3ReadOwnedWeapons.
// The capture and publish steps are legacy_muzzle_publication.h's;
// the title data stays here.
#include "../../core/legacy_muzzle_publication.h"
// Defined below; ODST's traits use them too (see the header's note).
void LegacyPublishLensAnchor(GameTitle title, uint8_t slot, const weapon_muzzle::Palette& lens) noexcept;
void LegacyPublishReticleRay(GameTitle title, uint8_t slot, const weapon_muzzle::Palette& palette) noexcept;
// H3's own FP users/slots and weapon-handle member, independently established
// in native reload-tail evidence: engine TLS +568 holds the FP users, one
// 0x11BC slot per hand, the weapon handle at +3C.
struct Halo3MuzzlePublicationTraits
{
    static constexpr GameTitle kTitle = GameTitle::Halo3;
    static constexpr int kUsersOffset = 0x568, kSlotStride = 0x11BC;
    static std::atomic<bool>& BindingsReady() noexcept { return g_halo3MuzzleBindingsReady; }
    static uint32_t* TlsIndex() noexcept { return g_engineTlsIndex; }
    static bool ReadersPresent() noexcept
    {
        return g_engineTlsIndex && g_halo3PlayerUnitGetter && g_halo3UnitInVehicle;
    }
    static uint32_t PlayerUnit() { return uint32_t(g_halo3PlayerUnitGetter(0)); }
    static bool InVehicle(int32_t owner) { return g_halo3UnitInVehicle(owner); }
    static bool ReadWeapons(uint32_t owner, uint32_t (&weapons)[2])
    {
        return Halo3ReadOwnedWeapons(owner, weapons, false);
    }
    static uint32_t RuntimeGeneration() noexcept { return g_halo3RuntimeGeneration.load(); }
    static auto& Muzzles() noexcept { return g_halo3Muzzles; }
    static void PublishLens(GameTitle title, uint8_t slot, const weapon_muzzle::Palette& lens) noexcept
    {
        LegacyPublishLensAnchor(title, slot, lens);
    }
    static void PublishReticle(GameTitle title, uint8_t slot, const weapon_muzzle::Palette& palette) noexcept
    {
        LegacyPublishReticleRay(title, slot, palette);
    }
};
bool Halo3CaptureMuzzleOwner(int player, int slot, uint32_t& unit, uint32_t& weapon) noexcept
{
    return LegacyCaptureMuzzleOwner<Halo3MuzzlePublicationTraits>(player, slot, unit, weapon);
}

// Scope lens anchored to the rendered gun's node 0 (lens_anchor.h): projected with the same accepted
// XR-to-world mapping as the reticle ray, published once the palette has committed.
// The accepted XR-to-world mapping of the Halo 3-engine titles (Halo 3, ODST):
// the visible carrier, reload receiver, lens and reticle rays all use it.
bool LegacyTrackingToWorld(contact_melee::TrackingToWorld& transform) noexcept
{
    if (!g_baseCamValid.load())
        return false;
    transform = {};
    const float sh = sinf(g_headYawRef), ch = cosf(g_headYawRef);
    const float cg = cosf(g_gameYawRef), sg = sinf(g_gameYawRef);
    transform.unitsPerMetre = g_worldScale.load();
    transform.axis[0] = {cg * sh + sg * ch, sg * sh - cg * ch, 0};
    transform.axis[1] = {0, 0, 1};
    transform.axis[2] = {-cg * ch + sg * sh, -sg * ch - cg * sh, 0};
    const auto reference = transform.World({g_headPosRef[0], g_headPosRef[1], g_headPosRef[2]});
    transform.origin = {
        g_baseCamX.load() - reference.x, g_baseCamY.load() - reference.y, g_baseCamZ.load() - reference.z};
    return true;
}
void LegacyPublishLensAnchor(GameTitle title, uint8_t slot, const weapon_muzzle::Palette& lens) noexcept
{
    contact_melee::TrackingToWorld transform{};
    if (!LegacyTrackingToWorld(transform))
    {
        VR_PublishWeaponLensRay(title, slot, {}, {});
        return;
    }
    VR_PublishWeaponLensRay(title, slot, lens.barrels[0], transform);
}
// The reticle rides this committed barrel; project only after the palette has
// committed. Without a camera base nothing is published (the old ray goes stale).
void LegacyPublishReticleRay(GameTitle title, uint8_t slot, const weapon_muzzle::Palette& palette) noexcept
{
    contact_melee::TrackingToWorld transform{};
    if (LegacyTrackingToWorld(transform))
        VR_PublishWeaponReticleRay(title, slot, palette.barrels[0], transform);
}

void Halo3PublishMuzzlePalette(
    uint16_t tag, const FpInterpolationContext& context, const BoneMatrix* destination, uint32_t generation) noexcept
{
    LegacyPublishMuzzlePalette<Halo3MuzzlePublicationTraits>(tag, context, destination, generation);
}
