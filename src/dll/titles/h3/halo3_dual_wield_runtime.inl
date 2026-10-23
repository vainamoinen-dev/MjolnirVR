// Halo 3 dual wielding: reads the player's owned weapons and publishes one room-to-world aim ray per hand.
// The native firing thread reads the published pair and never takes a VR lock.
#pragma once
// Included once, by src/dll/core/game.cpp. Not a standalone header: it uses what the
//   host defines above its include line, namely BasisFromAngles,
//   BuildTrackedGameBasisFromFrame, MultiplyBases, PublishHalo3DualAim, ReadCinematicControl,
//   g_baseCamValid, g_baseCamX, g_baseCamY, g_baseCamZ, g_enabled, g_gameYawRef,
//   g_halo3PlayerUnitGetter, g_halo3RuntimeGeneration, g_halo3UnitInVehicle, g_headPosRef,
//   g_headYawRef, and 2 more.
// H3EK A844A0 weapon_barrel_fire -> firing-data helper; pinned retail
// 3683A0 -> 3524B0 at 368B92. H3's five-argument outer ABI is distinct from
// H2 and ODST.
using Halo3DualFireFn = uint64_t(__fastcall*)(uint32_t, int16_t, void*, int32_t, uint8_t);
using Halo3DualAimFn = void(__fastcall*)(uint32_t, float*, float*, uint64_t, float*, uint8_t, uint8_t);
using Halo3DualQueryFn = void(__fastcall*)(int32_t, uint8_t, float*, int16_t, float*, void*);
using Halo3DualCameraFn = int32_t(__fastcall*)(uint32_t, float*, float*);
struct Halo3DualRuntime
{
    uintptr_t base = 0;
    uint32_t generation = 0;
    uint64_t installedAtMs = 0;
    void* fireTarget = nullptr;
    void* aimTarget = nullptr;
    Halo3DualFireFn fireOriginal = nullptr;
    Halo3DualAimFn aimOriginal = nullptr;
    void* queryTarget = nullptr;
    void* cameraTarget = nullptr;
    Halo3DualQueryFn queryOriginal = nullptr;
    Halo3DualCameraFn cameraOriginal = nullptr;
    std::atomic<bool> enabled{false}, faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> rays[2]{}, refused{0};
    std::atomic<uint64_t> nativeQueries{0}, targetRestoresRefused{0};
    std::atomic<uint64_t> fireEntries{0}, fireReturns{0}, fireUnwinds{0}, fireWithData{0}, firePredicted{0};
    DualWeaponAimPublication aim;
} g_halo3Dual;

// Read-only, full-salt native inventory. H3EK A5DE20/A844A0 and retail
// 35A9A4/3683A0/356388 prove role bytes +262/+263 and four handles +268.
// The object entry and owner fields are independently H3-proven, not H2 copies.
bool Halo3OwnedWeapon(uint32_t handle, uint32_t owner)
{
    const auto* weapon = Halo3MeleeSelectionObject(handle, 2);
    return weapon && weapon[0x15D] && *reinterpret_cast<const uint32_t*>(weapon + 0x168) == owner;
}
bool Halo3ReadOwnedWeapons(uint32_t owner, uint32_t weapons[2], bool requireDual, bool* primaryAbsentOut = nullptr)
{
    if (primaryAbsentOut)
        *primaryAbsentOut = false;
    const auto* unit = Halo3MeleeSelectionObject(owner, 0);
    if (!unit)
        return false;
    // A 0xFF role byte is the native explicit "no weapon in this slot"; every
    // other failure is uncertainty and must never be reported as absence.
    return weapon_inventory::ReadRoleWeapons(
        unit[0x262], unit[0x263], requireDual, weapons, primaryAbsentOut,
        [unit](uint8_t i) { return *reinterpret_cast<const uint32_t*>(unit + 0x268 + i * 4); },
        [owner](uint32_t handle) { return Halo3OwnedWeapon(handle, owner); });
}
// Same proven inventory (four handles at +268, roles +262/+263): the primary
// handle and the single owned weapon holding neither role (the stowed one;
// while dual wielding, the third weapon).
bool Halo3ReadReserveWeapon(uint32_t owner, uint32_t& primary, uint32_t& reserve)
{
    primary = reserve = reserve_weapon::kNoHandle;
    const auto* unit = Halo3MeleeSelectionObject(owner, 0);
    if (!unit)
        return false;
    return weapon_inventory::ReadReserveWeapon(
        unit[0x262], unit[0x263], primary, reserve,
        [unit](uint8_t i) { return *reinterpret_cast<const uint32_t*>(unit + 0x268 + i * 4); },
        [owner](uint32_t handle) { return Halo3OwnedWeapon(handle, owner); });
}
bool Halo3ReadOwnedDualWeapons(uint32_t owner, uint32_t weapons[2])
{
    return Halo3ReadOwnedWeapons(owner, weapons, true);
}

#include "halo3_muzzle_publication.inl"

// Runs at the existing admitted stereo boundary. The native firing thread
// receives one coherent pair of room-to-world rays and never takes a VR lock.
void PublishHalo3DualAimBody()
{
    auto& feature = g_halo3Dual;
    if (!feature.enabled.load(std::memory_order_acquire) || feature.faulted.load(std::memory_order_acquire) ||
        !g_vrAim.load() || !g_config.independent_dual_aim || !g_enabled.load() || !g_baseCamValid.load() ||
        feature.generation != g_halo3RuntimeGeneration.load())
        return;
    __try
    {
        int32_t scene = -1, shot = -1;
        if (ReadCinematicControl(scene, shot) != CinematicControlState::PlayerControlled)
            return;
        if (!g_halo3PlayerUnitGetter || !g_halo3UnitInVehicle)
            return;
        DualWeaponAimSnapshot sample{};
        sample.unit = static_cast<uint32_t>(g_halo3PlayerUnitGetter(0));
        if (!Halo3ReadOwnedDualWeapons(sample.unit, sample.weapons) ||
            g_halo3UnitInVehicle(static_cast<int32_t>(sample.unit)))
            return;
        VrContactTrackingSnapshot tracking{};
        if (!VR_GetContactTrackingSnapshot(tracking) || !tracking.referenceEpoch || !tracking.hands[0].valid ||
            !tracking.hands[1].valid)
            return;
        sample.generation = feature.generation;
        sample.trackingEpoch = tracking.referenceEpoch;
        sample.timeNs = tracking.timeNs;
        sample.sampleMs = GetTickCount64();
        const float scale = g_worldScale.load();
        if (!std::isfinite(scale) || scale <= 0)
            return;
        const float sh = sinf(g_headYawRef), ch = cosf(g_headYawRef);
        const float cg = cosf(g_gameYawRef), sg = sinf(g_gameYawRef);
        const float base[3]{g_baseCamX.load(), g_baseCamY.load(), g_baseCamZ.load()};
        for (int slot = 0; slot < 2; ++slot)
        {
            const auto& hand = tracking.hands[slot == 0 ? 1 : 0];
            float basis[9]{};
            BuildTrackedGameBasisFromFrame(hand.orientation, false, false, 0, 0, basis);
            if (slot == 1)
            {
                // Same mirrored support-role trajectory trim as H3's hand.
                float mount[9]{}, trimmed[9]{};
                BasisFromAngles(-g_config.gun_yaw_deg * 0.01745329252f, g_config.gun_pitch_deg * 0.01745329252f,
                    -g_config.gun_roll_deg * 0.01745329252f, mount);
                MultiplyBases(basis, mount, trimmed);
                memcpy(basis, trimmed, sizeof(basis));
            }
            const float dx = hand.position[0] - g_headPosRef[0];
            const float dy = hand.position[1] - g_headPosRef[1];
            const float dz = hand.position[2] - g_headPosRef[2];
            const float forward = dx * sh - dz * ch;
            const float right = dx * ch + dz * sh;
            sample.positions[slot][0] = base[0] + (cg * forward + sg * right) * scale;
            sample.positions[slot][1] = base[1] + (sg * forward - cg * right) * scale;
            sample.positions[slot][2] = base[2] + dy * scale;
            memcpy(sample.directions[slot], basis, sizeof(sample.directions[slot]));
        }
        (void)feature.aim.Publish(sample);
        VR_ObserveSecondaryWeaponPresentation(GameTitle::Halo3, feature.generation);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("publishing the Halo 3 dual aim faulted");
        feature.faulted.store(true, std::memory_order_release);
    }
}

__declspec(noinline) void PublishHalo3DualAim()
{
    g_halo3Dual.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        PublishHalo3DualAimBody();
    }
    __finally
    {
        g_halo3Dual.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

#include "halo3_independent_shots.inl"

#include "halo3_muzzle_lifecycle.inl"
#include "halo3_dual_lifecycle.inl"
