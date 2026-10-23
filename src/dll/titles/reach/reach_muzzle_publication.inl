// Halo Reach muzzle publication: captures the rendered gun's barrel and publishes the reticle ray and scope lens anchor.
// Logs each distinct refusal once, so the silent receipt-builder refusals are visible.
#pragma once
// Included once, by src/dll/core/game.cpp; also compiled by
//   tests/reach_muzzle_tests.cpp, which supplies its own versions. Not a standalone header: it
//   uses what the host defines above its include line, namely BoneMatrix,
//   ReachCaptureMuzzleOwner, ReachFpInterpolationContext, ReachMuzzleTrace,
//   ReachPublishMuzzlePalette, ReachReadRenderModelIdentity, g_reachBarrelBindingsReady,
//   g_reachBarrelMuzzles, g_reachCamera, g_reachFpPairScope.
bool ReachCaptureMuzzleOwner(int view, int slot, uint32_t& unit, uint32_t& weapon) noexcept
{
    if (!g_config.gun_barrel_aim || !g_reachBarrelBindingsReady.load(std::memory_order_acquire) || view != 0 ||
        slot < 0 || slot > 1 || !g_reachCamera.playerUnitByOutputUser || !g_reachCamera.unitInVehicle)
        return false;
    __try
    {
        const uint32_t owner = uint32_t(g_reachCamera.playerUnitByOutputUser(0));
        uint32_t weapons[2]{};
        if (!ReachMuzzleTargetStorage(owner) || !ReachReadMuzzleWeapons(owner, weapons) || weapons[slot] == UINT32_MAX)
            return false;
        auto** slots = reinterpret_cast<uint8_t**>(__readgsqword(0x58));
        const auto index = *reinterpret_cast<const uint32_t*>(g_reachCamera.base + kReachEngineTlsIndexRva);
        const auto* tls = slots && index < 0x200 ? slots[index] : nullptr;
        // HREK 8CEED0 / retail 2B1218: TLS6A0, user53A8, slot2978, full weapon3C.
        const auto* users = tls ? *reinterpret_cast<const uint8_t* const*>(tls + 0x6A0) : nullptr;
        if (!users || *reinterpret_cast<const uint32_t*>(users + slot * 0x2978 + 0x3C) != weapons[slot])
            return false;
        unit = owner;
        weapon = weapons[slot];
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("capturing the Reach muzzle owner faulted");
        return false;
    }
}
// One log line per distinct (reason, render model checksum): the receipt builder's silent refusals.
static muzzle_refusal::Once g_reachMuzzleRefusals;
static muzzle_refusal::Once
    g_reachMuzzleGuardRefusals; // its own table: guard combinations must not crowd out the real reasons
static void ReachMuzzleRefused(muzzle_refusal::Reason reason, uint32_t checksum, int a = 0, int b = 0) noexcept
{
    muzzle_refusal::Once& table =
        reason == muzzle_refusal::Reason::Guard ? g_reachMuzzleGuardRefusals : g_reachMuzzleRefusals;
    if (!table.ShouldLog(reason, checksum))
        return;
    LOG("Reach muzzle receipt refused: %s (model %08X, detail %d/%d)", muzzle_refusal::Name(reason), checksum, a, b);
}
// Diagnostic: one line per distinct (stage, tag, detail), at most 24 lines per run. Names how far the builder got for a palette
// (a Reach multiplayer test produced no receipt and no refusal line, so the call itself must be traced).
void ReachMuzzleTrace(const char* stage, uint16_t tag, uint32_t a, uint32_t b = 0, uint32_t c = 0) noexcept
{
    if (!g_config.reach_muzzle_trace)
        return;
    static uint64_t seen[24]{};
    static unsigned used = 0;
    const uint64_t key = (uint64_t(tag) << 48) ^ (uint64_t(a) << 24) ^ uint64_t(b) ^ (uint64_t(stage[0]) << 56) ^
                         (uint64_t(stage[1]) << 40);
    for (unsigned i = 0; i < used; ++i)
        if (seen[i] == key)
            return;
    // 'skip' fires for every non-builder palette: its own small cap keeps it from crowding out the builder stages.
    static unsigned skips = 0;
    const bool isSkip = stage[0] == 's';
    if (used >= 24 || (isSkip && skips >= 8))
        return;
    skips += isSkip ? 1u : 0u;
    seen[used++] = key;
    LOG("Reach muzzle trace: %s tag=%04X a=%08X b=%08X c=%08X", stage, unsigned(tag), a, b, c);
}
void ReachPublishMuzzlePalette(
    uint16_t tag, const ReachFpInterpolationContext& context, const BoneMatrix* destination) noexcept
{
    // Bit set = that guard refused. Logged once per distinct combination (key = 0xFFFF0000|bits).
    const uint32_t guardBits =
        (g_config.gun_barrel_aim ? 0u : 1u) | (g_reachBarrelBindingsReady.load(std::memory_order_acquire) ? 0u : 2u) |
        (context.valid ? 0u : 4u) | (context.interpolationView == 0 && context.interpolationSlot == 0 ? 0u : 8u) |
        (destination ? 0u : 16u) | (g_reachFpPairScope.armed ? 0u : 32u) |
        (context.generation && context.generation == g_reachCamera.generation.load() ? 0u : 64u) |
        (context.preparedSerial == g_reachFpPairScope.preparedSerial ? 0u : 128u);
    ReachMuzzleTrace("enter", tag, guardBits, context.muzzleUnit, context.muzzleWeapon);
    if (guardBits)
    {
        // Not holding a gun in the first-person slot is normal (most calls); only log the combination once.
        ReachMuzzleRefused(muzzle_refusal::Reason::Guard, 0xFFFF0000u | guardBits, int(guardBits), tag);
        return;
    }
    __try
    {
        uint32_t checksum = 0;
        int count = 0;
        if (!ReachReadRenderModelIdentity(tag, checksum, count) || count <= 0 || count > 64)
        {
            ReachMuzzleTrace("identity", tag, checksum, uint32_t(count));
            ReachMuzzleRefused(muzzle_refusal::Reason::NoIdentity, checksum, count, tag);
            return;
        }
        if (!weapon_muzzle::Find(GameTitle::HaloReach, checksum, 0) &&
            !weapon_muzzle::Find(GameTitle::HaloReach, checksum, 1))
        {
            ReachMuzzleRefused(muzzle_refusal::Reason::NoRow, checksum, count, tag);
            return;
        }
        ReachMuzzleTrace("row", tag, checksum, uint32_t(count));
        weapon_muzzle::Palette palette{}, lens{};
        const auto& tracking = context.targets;
        uint32_t unit = UINT32_MAX, weapon = UINT32_MAX;
        const bool ownerOk = ReachCaptureMuzzleOwner(0, 0, unit, weapon);
        const bool ownerSame = ownerOk && unit == context.muzzleUnit && weapon == context.muzzleWeapon;
        ReachMuzzleTrace("owner", tag, (ownerOk ? 1u : 0u) | (ownerSame ? 2u : 0u),
            (unit == context.muzzleUnit ? 1u : 0u) | (weapon == context.muzzleWeapon ? 2u : 0u),
            (tracking.contactSerial ? 1u : 0u) | (tracking.contactReference ? 2u : 0u) |
                (tracking.contactTimeNs > 0 ? 4u : 0u) | (tracking.rightWristValid ? 8u : 0u));
        const bool trackingOk = tracking.contactSerial && tracking.contactReference && tracking.contactEpoch &&
                                tracking.contactTimeNs > 0 && tracking.rightWristValid;
        if (!ownerOk)
            ReachMuzzleRefused(muzzle_refusal::Reason::OwnerCapture, checksum);
        else if (!ownerSame)
            ReachMuzzleRefused(muzzle_refusal::Reason::OwnerMismatch, checksum, unit == context.muzzleUnit,
                weapon == context.muzzleWeapon);
        else if (!trackingOk)
            ReachMuzzleRefused(muzzle_refusal::Reason::TrackingMissing, checksum, tracking.contactSerial != 0,
                tracking.rightWristValid);
        if (ownerSame && trackingOk)
        {
            const auto now = GetTickCount64();
            // Keyed by the raw tracking epoch, the space every reader (the shot, the lens)
            // presents. The hashed contactReference never matches, so keying on it sent every
            // Reach shot back to stock aim (headset logs showed 5394 of 5394 refused).
            const weapon_muzzle::Receipt base{GameTitle::HaloReach, context.generation, unit, weapon, checksum,
                tracking.contactEpoch, tracking.contactSerial, now, tracking.contactTimeNs, 0, 0, g_config.left_handed};
            weapon_muzzle::FillPalette(palette, GameTitle::HaloReach, checksum, uint32_t(count), base,
                [&](const weapon_muzzle::Marker& marker, weapon_muzzle::Ray& ray)
                {
                    const auto& node = destination[marker.node];
                    return weapon_muzzle::Transform(marker, node.scale, node.rotation, node.translation, ray);
                });
            if (!palette.barrels[0].identity && !palette.barrels[1].identity)
                ReachMuzzleRefused(muzzle_refusal::Reason::PaletteEmpty, checksum, count, 0);
            // The lens receipt shares the muzzle's raw tracking epoch (as H3/ODST/H4 publish).
            const weapon_muzzle::Receipt lensBase = base;
            if (g_config.scope_lens_node_anchor &&
                !lens_anchor::Fill(lens, GameTitle::HaloReach, checksum, uint32_t(count), lensBase,
                    [&](const weapon_muzzle::Marker& marker, weapon_muzzle::Ray& ray)
                    {
                        const auto& node = destination[marker.node];
                        return weapon_muzzle::Transform(marker, node.scale, node.rotation, node.translation, ray);
                    }))
            {
                const auto* model = weapon_model::Find(GameTitle::HaloReach, checksum);
                ReachMuzzleRefused(model && weapon_profile::KitLensNodeFor(GameTitle::HaloReach, model->name)
                                       ? muzzle_refusal::Reason::LensTransform
                                       : muzzle_refusal::Reason::LensNoRow,
                    checksum, count, model ? 1 : 0);
            }
        }
        (void)g_reachBarrelMuzzles.Publish(GameTitle::HaloReach, 0, palette);
        // Primary hand is role 1: the same tracking-to-world space the Reach contact frames use.
        VR_PublishWeaponLensRay(GameTitle::HaloReach, 0, lens.barrels[0], tracking.contactSpace[1]);
        // The reticle rides the committed barrel in that same space.
        VR_PublishWeaponReticleRay(GameTitle::HaloReach, 0, palette.barrels[0], tracking.contactSpace[1]);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("publishing the Reach muzzle palette faulted");
        ReachMuzzleTrace("fault", tag, 0);
        (void)g_reachBarrelMuzzles.Publish(GameTitle::HaloReach, 0, {});
    }
}
