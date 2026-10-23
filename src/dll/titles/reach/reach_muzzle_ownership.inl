// Halo Reach barrel-muzzle ownership: the title's native offsets for the player's owned weapons.
// Every field is read only after Reach's object-table reader validates the full salted handle.
#pragma once
// Included once, by src/dll/core/game.cpp; also compiled by
//   tests/reach_muzzle_tests.cpp, which supplies its own versions. Not a standalone header: it
//   uses what the host defines above its include line, namely ReachVehicleObjectData,
//   g_reachCamera.
// HREK D78C90 inventory, DE4290 role bytes, D77E00 controlling parent;
// pinned retail 48B3D0, 4C2A4F/4C31B0, 48375C. Full salted handles are
// validated by Reach's existing native object-table reader before any field.
// The readers are barrel_muzzle_runtime.h's; Reach's offsets stay here.
#include "../../core/barrel_muzzle_runtime.h"
const uint8_t* ReachMuzzleObject(uint32_t handle, uint8_t wantedKind)
{
    uint8_t kind = 0;
    const auto* object = ReachVehicleObjectData(int32_t(handle), kind);
    return object && kind == wantedKind ? object : nullptr;
}
bool ReachBarrelValidObject(uint32_t handle)
{
    uint8_t kind = 0;
    return ReachVehicleObjectData(int32_t(handle), kind) != nullptr;
}
struct ReachMuzzleOwnership
{
    static const uint8_t* Object(uint32_t handle, uint8_t kind) { return ReachMuzzleObject(handle, kind); }
    template <class U>
    static U Read(const uint8_t* object, size_t offset)
    {
        return *reinterpret_cast<const U*>(object + offset);
    }
    static constexpr uint8_t kUnit = 0, kWeapon = 2;
    // HREK reload ownership; retail 4BE0B8 independently matches all three.
    static constexpr size_t kOwner = 0x32C, kHasParent = 0x1A9, kParent = 0x1B4;
    // Proven inventory: roles +34A/+34B, four handles at +350.
    static constexpr size_t kRoles = 0x34A, kHandles = 0x350;
    // HREK DE4290 aligns unit+699 to four bytes; retail 4C2F57 does the same.
    static constexpr size_t kControllingParent = 0x390, kTargetStorage = 0x2B8;
};
bool ReachOwnedWeapon(uint32_t handle, uint32_t owner)
{
    return BarrelMuzzleOwnedWeapon<ReachMuzzleOwnership>(handle, owner);
}
bool ReachReadMuzzleWeapons(uint32_t owner, uint32_t weapons[2], bool* primaryAbsentOut = nullptr)
{
    return BarrelMuzzleReadWeapons<ReachMuzzleOwnership>(owner, weapons, primaryAbsentOut);
}
// Same proven inventory: the primary handle and the single owned weapon
// holding neither role (the stowed one).
bool ReachReadReserveWeapon(uint32_t owner, uint32_t& primary, uint32_t& reserve)
{
    return BarrelMuzzleReadReserveWeapon<ReachMuzzleOwnership>(owner, primary, reserve);
}
void* ReachMuzzleTargetStorage(uint32_t owner)
{
    if (!g_reachCamera.playerUnitByOutputUser || !g_reachCamera.unitInVehicle ||
        owner != uint32_t(g_reachCamera.playerUnitByOutputUser(0)) || g_reachCamera.unitInVehicle(int32_t(owner)))
        return nullptr;
    return BarrelMuzzleTargetStorage<ReachMuzzleOwnership>(owner);
}
