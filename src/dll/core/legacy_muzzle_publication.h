#pragma once
// The barrel-muzzle publication shared by Halo 3 and ODST. Each title keeps its traits (engine
// TLS index, FP users offset and slot stride, readers, globals), its comments
// on the native evidence, and its two functions under their old names as
// wrappers (h3/halo3_muzzle_publication.inl, odst/odst_muzzle_publication.inl).
// Not a standalone header: like barrel_muzzle_runtime.h, it uses what the host
// defines above its include line (g_config, g_fpStereoSolveScope, BoneMatrix,
// FpInterpolationContext, LegacyAnatomicalRenderNodeCount, weapon_muzzle,
// lens_anchor), in game.cpp or a title's muzzle test.
//
// Hidden cross-title dependency: ODST's traits reach the
// lens and reticle publishers through LegacyPublishLensAnchor and
// LegacyPublishReticleRay, which live in the Halo 3 file
// (h3/halo3_muzzle_publication.inl), as does LegacyTrackingToWorld. ODST
// reaches them through core/halo3_family_internal.h (game.cpp exports them). Those
// three helpers belong in core.
#include "guard_count.h"

// Traits T: kTitle, kUsersOffset, kSlotStride, BindingsReady(), TlsIndex(),
// ReadersPresent(), PlayerUnit(), InVehicle(owner), ReadWeapons(owner,weapons),
// RuntimeGeneration(), Muzzles(), PublishLens(title,slot,lens),
// PublishReticle(title,slot,palette).

// The title's own FP users/slots and weapon-handle member (+3C). Capture before
// AND after interpolation, so a later inventory lookup cannot attach an old
// palette to a replacement gun.
template <class T>
bool LegacyCaptureMuzzleOwner(int player, int slot, uint32_t& unit, uint32_t& weapon) noexcept
{
    if (!g_config.gun_barrel_aim || !T::BindingsReady().load(std::memory_order_acquire) || player != 0 || slot < 0 ||
        slot > 1 || !T::ReadersPresent())
        return false;
    __try
    {
        auto** slots = reinterpret_cast<uint8_t**>(__readgsqword(0x58));
        const auto* tls = slots ? slots[*T::TlsIndex()] : nullptr;
        const auto* users = tls ? *reinterpret_cast<const uint8_t* const*>(tls + T::kUsersOffset) : nullptr;
        if (!users)
            return false;
        const uint32_t owner = T::PlayerUnit();
        uint32_t weapons[2]{};
        if (owner == UINT32_MAX || T::InVehicle(int32_t(owner)) || !T::ReadWeapons(owner, weapons) ||
            weapons[slot] == UINT32_MAX ||
            *reinterpret_cast<const uint32_t*>(users + slot * T::kSlotStride + 0x3C) != weapons[slot])
            return false;
        unit = owner;
        weapon = weapons[slot];
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("reading the muzzle owner and weapon faulted");
        return false;
    }
}

// One committed palette per FP interpolation. Past the gate, the palette, the
// lens and the reticle are always published, in that order; they are empty
// when the owner capture or tracking fails. A fault publishes one empty palette.
template <class T>
void LegacyPublishMuzzlePalette(
    uint16_t tag, const FpInterpolationContext& context, const BoneMatrix* destination, uint32_t generation) noexcept
{
    constexpr GameTitle title = T::kTitle;
    if (!g_config.gun_barrel_aim || !T::BindingsReady().load(std::memory_order_acquire) || !context.valid ||
        context.player != 0 || context.slot < 0 || context.slot > 1 || !generation ||
        context.generation != generation || generation != T::RuntimeGeneration() || !destination ||
        !g_fpStereoSolveScope.armed)
        return;
    __try
    {
        uint32_t checksum = 0;
        const int count = LegacyAnatomicalRenderNodeCount(title, tag, &checksum);
        if (count <= 0 || count > 64 ||
            (!weapon_muzzle::Find(title, checksum, 0) && !weapon_muzzle::Find(title, checksum, 1)))
            return;
        weapon_muzzle::Palette palette{}, lens{};
        const auto& tracking = g_fpStereoSolveScope.anatomicalTracking;
        uint32_t unit = UINT32_MAX, weapon = UINT32_MAX;
        if (LegacyCaptureMuzzleOwner<T>(context.player, context.slot, unit, weapon) && unit == context.muzzleUnit &&
            weapon == context.muzzleWeapon && tracking.serial && tracking.referenceEpoch && tracking.timeNs > 0 &&
            tracking.hands[context.slot == 0 ? 1 : 0].valid)
        {
            const uint64_t now = GetTickCount64();
            const weapon_muzzle::Receipt base{title, generation, unit, weapon, checksum, tracking.referenceEpoch,
                tracking.serial, now, tracking.timeNs, uint8_t(context.slot), 0, g_config.left_handed};
            weapon_muzzle::FillPalette(palette, title, checksum, uint32_t(count), base,
                [&](const weapon_muzzle::Marker& marker, weapon_muzzle::Ray& ray)
                {
                    const auto& node = destination[marker.node];
                    return weapon_muzzle::Transform(marker, node.scale, node.rotation, node.translation, ray);
                });
            if (g_config.scope_lens_node_anchor)
                (void)lens_anchor::Fill(lens, title, checksum, uint32_t(count), base,
                    [&](const weapon_muzzle::Marker& marker, weapon_muzzle::Ray& ray)
                    {
                        const auto& node = destination[marker.node];
                        return weapon_muzzle::Transform(marker, node.scale, node.rotation, node.translation, ray);
                    });
        }
        (void)T::Muzzles().Publish(title, uint8_t(context.slot), palette);
        T::PublishLens(title, uint8_t(context.slot), lens);
        T::PublishReticle(title, uint8_t(context.slot), palette);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("publishing the muzzle palette faulted");
        (void)T::Muzzles().Publish(title, uint8_t(context.slot), {});
    }
}
