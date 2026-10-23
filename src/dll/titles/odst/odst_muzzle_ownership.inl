// ODST barrel-muzzle ownership: full-salt object table and inventory readers for the player's owned weapons.
// Also used by melee, finger pose and muzzle publication. Read-only native reads.
#pragma once
// Included once, by src/dll/titles/odst/odst_camera_core.cpp; also compiled by tests/odst_muzzle_tests.cpp,
//   which supplies its own versions. Not a standalone header: it uses what the host defines
//   above its include line, namely g_odstPlayerUnitGetter, g_odstUnitInVehicle.
// ODST-only full-salt object table and inventory proof. The title's existing
// contact validator performs bounds/salt checks; no contact feature need be on.
// Only the target-storage tail is barrel_muzzle_runtime.h's. ODST's owned
// weapon rule (parent flag +155, parent +160) is not the Reach/Halo 4 rule, so
// its readers stay here; they also serve melee, finger pose and the publication.
#include "../../core/barrel_muzzle_runtime.h"
const uint8_t* OdstMuzzleObject(uint32_t handle, uint8_t kind)
{
    if (!OdstContactObject(handle))
        return nullptr;
    const auto* table = *reinterpret_cast<const uint8_t* const*>(OdstContactTls() + kOdstTlsObjectTableOffset);
    const auto* entries = *reinterpret_cast<const uint8_t* const*>(table + 0x48);
    const auto* entry = entries + (handle & 0xFFFF) * 0x18;
    return entry[3] == kind ? *reinterpret_cast<const uint8_t* const*>(entry + 0x10) : nullptr;
}
bool OdstOwnedWeapon(uint32_t handle, uint32_t owner)
{
    const auto* weapon = OdstMuzzleObject(handle, 2);
    return weapon && weapon[0x155] && *reinterpret_cast<const uint32_t*>(weapon + 0x160) == owner;
}
bool OdstReadMuzzleWeapons(uint32_t owner, uint32_t weapons[2], bool* primaryAbsentOut = nullptr)
{
    if (primaryAbsentOut)
        *primaryAbsentOut = false;
    const auto* unit = OdstMuzzleObject(owner, 0);
    if (!unit)
        return false;
    // ODSTEK ADE940 and retail 39AB60: four handles at +27C. The firing
    // data helper proves primary/secondary role bytes +276/+277 separately.
    // A 0xFF primary role is reported as absence before any other refusal.
    return weapon_inventory::ReadRoleWeapons(
        unit[0x276], unit[0x277], false, weapons, primaryAbsentOut,
        [unit](uint8_t i) { return *reinterpret_cast<const uint32_t*>(unit + 0x27C + i * 4); },
        [owner](uint32_t handle) { return OdstOwnedWeapon(handle, owner); });
}
// Same proven inventory (four handles at +27C, roles +276/+277): the primary
// handle and the single owned weapon holding neither role (the stowed one).
bool OdstReadReserveWeapon(uint32_t owner, uint32_t& primary, uint32_t& reserve)
{
    primary = reserve = reserve_weapon::kNoHandle;
    const auto* unit = OdstMuzzleObject(owner, 0);
    if (!unit)
        return false;
    return weapon_inventory::ReadReserveWeapon(
        unit[0x276], unit[0x277], primary, reserve,
        [unit](uint8_t i) { return *reinterpret_cast<const uint32_t*>(unit + 0x27C + i * 4); },
        [owner](uint32_t handle) { return OdstOwnedWeapon(handle, owner); });
}
struct OdstMuzzleOwnership
{
    static const uint8_t* Object(uint32_t handle, uint8_t kind) { return OdstMuzzleObject(handle, kind); }
    template <class U>
    static U Read(const uint8_t* object, size_t offset)
    {
        return *reinterpret_cast<const U*>(object + offset);
    }
    static constexpr uint8_t kUnit = 0;
    // ODSTEK B122B0 follows controlling parent +2B8, targeting +228.
    static constexpr size_t kControllingParent = 0x2B8, kTargetStorage = 0x228;
};
void* OdstMuzzleTargetStorage(uint32_t owner)
{
    if (!g_odstPlayerUnitGetter || !g_odstUnitInVehicle || owner != uint32_t(g_odstPlayerUnitGetter(0)) ||
        g_odstUnitInVehicle(int32_t(owner)))
        return nullptr;
    return BarrelMuzzleTargetStorage<OdstMuzzleOwnership>(owner);
}
