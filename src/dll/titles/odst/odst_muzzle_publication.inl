// ODST muzzle publication: captures the rendered gun's barrel and publishes the reticle ray and scope lens anchor.
// Holds title data only; the capture and publish steps are shared with Halo 3.
#pragma once
// Included once, by src/dll/titles/odst/odst_camera_core.cpp; also compiled by tests/odst_muzzle_tests.cpp,
//   which supplies its own versions. Not a standalone header: it uses what the host defines
//   above its include line, namely BoneMatrix, FpInterpolationContext, OdstCaptureMuzzleOwner,
//   OdstPublishMuzzlePalette, g_fpStereoSolveScope, g_odstEngineTlsIndex,
//   g_odstMuzzleBindingsReady, g_odstMuzzles, g_odstPlayerUnitGetter, g_odstRuntimeGeneration,
//   g_odstUnitInVehicle, OdstReadMuzzleWeapons, and the Halo 3 file's LegacyPublishLensAnchor and
//   LegacyPublishReticleRay (through its traits; declared in core/halo3_family_internal.h).
// The capture and publish steps are legacy_muzzle_publication.h's;
// the title data stays here.
#include "../../core/legacy_muzzle_publication.h"
// ODST native FP users/slots: TLS598, user4F38, slot2740, weapon3C;
// retail 2E9E65/2E9E8E, independently established
// in native reload-tail evidence.
struct OdstMuzzlePublicationTraits
{
    static constexpr GameTitle kTitle = GameTitle::Halo3ODST;
    static constexpr int kUsersOffset = 0x598, kSlotStride = 0x2740;
    static std::atomic<bool>& BindingsReady() noexcept { return g_odstMuzzleBindingsReady; }
    static uint32_t* TlsIndex() noexcept { return g_odstEngineTlsIndex; }
    static bool ReadersPresent() noexcept
    {
        return g_odstEngineTlsIndex && g_odstPlayerUnitGetter && g_odstUnitInVehicle;
    }
    static uint32_t PlayerUnit() { return uint32_t(g_odstPlayerUnitGetter(0)); }
    static bool InVehicle(int32_t owner) { return g_odstUnitInVehicle(owner); }
    static bool ReadWeapons(uint32_t owner, uint32_t (&weapons)[2]) { return OdstReadMuzzleWeapons(owner, weapons); }
    static uint32_t RuntimeGeneration() noexcept { return g_odstRuntimeGeneration.load(); }
    static auto& Muzzles() noexcept { return g_odstMuzzles; }
    // The two Legacy* publishers live in the Halo 3 file (see the header's note).
    static void PublishLens(GameTitle title, uint8_t slot, const weapon_muzzle::Palette& lens) noexcept
    {
        LegacyPublishLensAnchor(title, slot, lens);
    }
    // ODST's reticle rides the committed barrel too, as Halo 3's does.
    static void PublishReticle(GameTitle title, uint8_t slot, const weapon_muzzle::Palette& palette) noexcept
    {
        LegacyPublishReticleRay(title, slot, palette);
    }
};
bool OdstCaptureMuzzleOwner(int player, int slot, uint32_t& unit, uint32_t& weapon) noexcept
{
    return LegacyCaptureMuzzleOwner<OdstMuzzlePublicationTraits>(player, slot, unit, weapon);
}

void OdstPublishMuzzlePalette(
    uint16_t tag, const FpInterpolationContext& context, const BoneMatrix* destination, uint32_t generation) noexcept
{
    LegacyPublishMuzzlePalette<OdstMuzzlePublicationTraits>(tag, context, destination, generation);
}
