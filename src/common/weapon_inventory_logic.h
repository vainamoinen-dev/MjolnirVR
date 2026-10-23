#pragma once
// Weapon inventory decision shared by Reach, Halo 3, ODST and Halo 4: primary/secondary roles and handles.
// Used by src/dll/core/game.cpp; tested by tests/weapon_inventory_tests.cpp and the per-title muzzle tests.
// Pure logic: no globals, no locks, no logging; each title reads its own memory and passes it in.
//
// Shared reading of a unit's weapon inventory (Reach, Halo 3, ODST, Halo 4).
// Each title proves its own layout (two role bytes, four weapon handles) and
// its own "this weapon object is owned by this unit" check; this file only
// owns the common decision:
//   roles: primary role < 4; secondary role < 4 or 0xFF (no dual weapon,
//   which Halo 3's dual reader refuses); the two roles differ.
//   A 0xFF primary role is reported as the native explicit empty slot before
//   any other check, so it is never lost to an unrelated refusal.
// Handles are read lazily through `handleAt(index)` in the same order the
// titles always read them. Pure logic: no globals, no locks, no logging.
#include <cstdint>

#include "reserve_weapon_logic.h"

namespace weapon_inventory
{
    inline constexpr uint8_t kNoRole = 0xFF;

    // Weapons in the primary and secondary roles (UINT32_MAX for an empty
    // secondary). False on any unproven role or an unowned/duplicate handle.
    template <class HandleAt, class Owned>
    bool ReadRoleWeapons(uint8_t primaryRole, uint8_t secondaryRole, bool requireSecondary, uint32_t weapons[2],
        bool* primaryAbsentOut, HandleAt handleAt, Owned owned)
    {
        if (primaryAbsentOut)
            *primaryAbsentOut = primaryRole == kNoRole;
        if (primaryRole >= 4 || (secondaryRole >= 4 && (requireSecondary || secondaryRole != kNoRole)) ||
            primaryRole == secondaryRole)
            return false;
        const uint8_t roles[2]{primaryRole, secondaryRole};
        for (int slot = 0; slot < 2; ++slot)
        {
            if (roles[slot] == kNoRole)
            {
                weapons[slot] = UINT32_MAX;
                continue;
            }
            weapons[slot] = handleAt(roles[slot]);
            if (!owned(weapons[slot]))
                return false;
        }
        return weapons[0] != weapons[1];
    }

    // The primary handle and the single owned weapon holding neither role (the
    // stowed one; while dual wielding, the third). Reserve is kNoHandle when there
    // is none or it is ambiguous.
    template <class HandleAt, class Owned>
    bool ReadReserveWeapon(uint8_t primaryRole, uint8_t secondaryRole, uint32_t& primary, uint32_t& reserve,
        HandleAt handleAt, Owned owned)
    {
        primary = reserve = reserve_weapon::kNoHandle;
        if (primaryRole >= 4 || (secondaryRole >= 4 && secondaryRole != kNoRole) || primaryRole == secondaryRole)
            return false;
        uint32_t inventory[reserve_weapon::kInventorySlots]{};
        for (int i = 0; i < reserve_weapon::kInventorySlots; ++i)
            inventory[i] = handleAt(uint8_t(i));
        if (!owned(inventory[primaryRole]))
            return false;
        primary = inventory[primaryRole];
        reserve = reserve_weapon::SelectReserve(inventory, primaryRole, secondaryRole, owned);
        return true;
    }
} // namespace weapon_inventory
