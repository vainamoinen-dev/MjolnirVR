// ODST melee response reader: the striking role's weapon or unarmed melee response, from ODST's own tag offsets.
// Reads only; split out so the offsets can be unit-tested.
#pragma once
// Included once, by src/dll/titles/odst/odst_melee_selection.inl; also compiled by
//   tests/odst_muzzle_tests.cpp, which supplies its own versions. Not a standalone header: it
//   uses what the host defines above its include line (headers only).
// The striking role's melee response in ODST (offsets proven against the
// retail module and the ODST kit). Split from
// odst_melee_selection.inl so the offsets are unit-tested (odst_muzzle_tests):
// needs OdstMuzzleObject / OdstOwnedWeapon (odst_muzzle_ownership.inl),
// OdstLoadedTagDefinition and halo3_melee_selection_logic.h.
bool OdstReadPhysicalMeleeResponse(uint32_t owner, int hand, Halo3MeleeResponse& result, bool& armed)
{
    const auto* unit = OdstMuzzleObject(owner, 0);
    if (!unit || hand < 0 || hand > 1)
        return false;
    const auto* unitTag = OdstLoadedTagDefinition(*reinterpret_cast<const uint32_t*>(unit));
    if (!unitTag)
        return false;
    const uint32_t unitDamage = *reinterpret_cast<const uint32_t*>(unitTag + 0x1C4);
    const uint8_t index = unit[hand == 1 ? 0x276 : 0x277];
    if (index != 0xff && index >= 4)
        return false;
    const uint32_t weapon =
        index == 0xff ? UINT32_MAX : *reinterpret_cast<const uint32_t*>(unit + 0x27C + size_t(index) * 4);
    armed = weapon != UINT32_MAX;
    if (!armed)
    {
        result = Halo3SelectPhysicalMeleeResponse(unitDamage, false, 3, UINT32_MAX, UINT32_MAX, {}, false);
        return true;
    }
    const auto* object = OdstMuzzleObject(weapon, 2);
    if (!object || !OdstOwnedWeapon(weapon, owner))
        return false;
    const auto* tag = OdstLoadedTagDefinition(*reinterpret_cast<const uint32_t*>(object));
    if (!tag)
        return false;
    const float power = *reinterpret_cast<const float*>(object + 0x184);
    if (!std::isfinite(power))
        return false;
    const bool charged = (*reinterpret_cast<const uint32_t*>(tag + 0x19C) & (1u << 14)) && power >= 1.0f;
    const size_t response = charged ? 0x2DC : 0x25C;
    Halo3MeleeResponse authored{};
    authored.damage = *reinterpret_cast<const uint32_t*>(tag + response + 0xC);
    authored.effect = *reinterpret_cast<const uint32_t*>(tag + response + 0x1C);
    if (!charged)
    {
        authored.clashDamage = *reinterpret_cast<const uint32_t*>(tag + 0x2FC + 0xC);
        authored.clashEffect = *reinterpret_cast<const uint32_t*>(tag + 0x2FC + 0x1C);
    }
    result =
        Halo3SelectPhysicalMeleeResponse(unitDamage, true, tag[0x32C], *reinterpret_cast<const uint32_t*>(tag + 0x23C),
            *reinterpret_cast<const uint32_t*>(tag + 0x24C), authored, charged);
    return true;
}
