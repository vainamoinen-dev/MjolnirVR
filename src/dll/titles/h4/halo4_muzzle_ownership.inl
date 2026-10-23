// Halo 4 barrel-muzzle ownership: the title's native offsets for the player's owned weapons and first-person slots.
// The reader logic is shared in barrel_muzzle_runtime.h.
#pragma once
// Included once, by src/dll/titles/h4/halo4_camera_core.cpp; also compiled by
//   tests/halo4_muzzle_tests.cpp, which supplies its own versions. Not a standalone header: it
//   uses what the host defines above its include line, namely g_halo4EngineTlsIndex.
// The readers are barrel_muzzle_runtime.h's; Halo 4's offsets stay here.
#include "../../core/barrel_muzzle_runtime.h"
weapon_muzzle::Store g_halo4BarrelMuzzles;
std::atomic<bool> g_halo4BarrelBindingsReady{false};
const uint8_t* Halo4MuzzleTls()
{
    if (!g_halo4EngineTlsIndex || *g_halo4EngineTlsIndex >= 1088)
        return nullptr;
    auto** slots = reinterpret_cast<const uint8_t**>(__readgsqword(0x58));
    return slots ? slots[*g_halo4EngineTlsIndex] : nullptr;
}
const uint8_t* Halo4MuzzleObject(uint32_t handle, uint32_t kinds)
{
    const auto* tls = Halo4MuzzleTls();
    return tls ? Halo4VehicleObject(Halo4VehicleRead<const uint8_t*>(tls, 0x18), handle, kinds) : nullptr;
}
bool Halo4BarrelValidObject(uint32_t handle)
{
    return Halo4MuzzleObject(handle, UINT32_MAX) != nullptr;
}
struct Halo4MuzzleOwnership
{
    static const uint8_t* Object(uint32_t handle, uint32_t kinds) { return Halo4MuzzleObject(handle, kinds); }
    template <class U>
    static U Read(const uint8_t* object, size_t offset)
    {
        return Halo4VehicleRead<U>(object, offset);
    }
    static constexpr uint32_t kUnit = 1, kWeapon = 4;
    static constexpr size_t kOwner = 0x624, kHasParent = 0x471, kParent = 0x480;
    // H4EK E67B60: four inventory entries at +640, roles +63A/+63B; retail 5F9E20 / 5FA1C4.
    static constexpr size_t kRoles = 0x63A, kHandles = 0x640;
    // H4EK E80C00/E977E0, retail 5F1ABC/6176B8: controlling parent +694, target +5A8.
    static constexpr size_t kControllingParent = 0x694, kTargetStorage = 0x5A8;
};
bool Halo4OwnedWeapon(uint32_t handle, uint32_t owner)
{
    return BarrelMuzzleOwnedWeapon<Halo4MuzzleOwnership>(handle, owner);
}
// A 0xFF primary role is a real KnownAbsent state, reported before the
// validation guards so it is never an unexplained refusal.
bool Halo4ReadMuzzleWeapons(uint32_t owner, uint32_t weapons[2], bool* primaryAbsentOut = nullptr)
{
    return BarrelMuzzleReadWeapons<Halo4MuzzleOwnership>(owner, weapons, primaryAbsentOut);
}
// Same proven inventory: the primary handle and the single owned weapon
// holding neither role (the stowed one).
bool Halo4ReadReserveWeapon(uint32_t owner, uint32_t& primary, uint32_t& reserve)
{
    return BarrelMuzzleReadReserveWeapon<Halo4MuzzleOwnership>(owner, primary, reserve);
}
void* Halo4MuzzleTargetStorage(uint32_t owner)
{
    Halo4VehicleInputState seat{};
    if (!Halo4ReadVehicleInput(seat) || seat.unit != owner || seat.seated)
        return nullptr;
    return BarrelMuzzleTargetStorage<Halo4MuzzleOwnership>(owner);
}
bool Halo4CaptureMuzzleOwner(uint32_t weapon, uint32_t& owner, uint8_t& slot)
{
    if (!g_config.gun_barrel_aim || !g_halo4BarrelBindingsReady.load(std::memory_order_acquire))
        return false;
    Halo4VehicleInputState seat{};
    uint32_t weapons[2]{};
    if (!Halo4ReadVehicleInput(seat) || seat.seated || !Halo4MuzzleTargetStorage(seat.unit) ||
        !Halo4ReadMuzzleWeapons(seat.unit, weapons))
        return false;
    const int selected = ResolveEquippedWeaponSlot(weapon, weapons[0], weapons[1], true, weapons[1] != UINT32_MAX);
    if (selected < 0)
        return false;
    const auto* tls = Halo4MuzzleTls();
    const auto* fp = tls ? Halo4VehicleRead<const uint8_t*>(tls, 0x6A0) : nullptr;
    // Own FP producer 92A1F0 -> 3B1B4C: user5F48, slot2EC8,
    // unit+4, held weapon+6C, held render tag+74.
    if (!fp || !(Halo4VehicleRead<uint32_t>(fp, 0) & 2) || Halo4VehicleRead<uint32_t>(fp, 4) != seat.unit ||
        Halo4VehicleRead<uint32_t>(fp, selected * 0x2EC8 + 0x6C) != weapon)
        return false;
    owner = seat.unit;
    slot = uint8_t(selected);
    return true;
}
