// ODST physical-strike melee response selection: picks the striking hand's own weapon or unarmed response.
// The main hand keeps the native result byte for byte; never swaps inventory.
#pragma once
// Included once, by src/dll/titles/odst/odst_camera_core.cpp. Not a standalone header: it uses what the
//   host defines above its include line, namely OdstContactMeleeReady.
// ODST physical-strike response selection, the port of
// halo3_melee_selection.inl. A support-hand strike uses that hand's own weapon
// response, or the unit's authored unarmed damage when the hand is empty, instead
// of the primary weapon's melee (a bare fist is not an energy sword). The main
// hand keeps the native result byte for byte. Every value below is proven
// against the retail module and ODSTEK:
//   selector 39F5AC (H3 35A9A4), callers 3A037F / 3A3937 (H3 35B74C / 35E87E),
//   unit tag unarmed damage +1C4 (H3 +1B4), weapon tag fields +0x10 each
//   (flags 19C, response 25C, charged 2DC, clash 2FC, material 32C, fallback
//   23C/24C), weapon power +184 (H3 +18C). The unit roles +276/+277, inventory
//   +27C and the weapon owner checks (+155 active, +160 owner) are the ones
//   odst_muzzle_ownership.inl already proves and uses (OdstMuzzleObject /
//   OdstOwnedWeapon).
using OdstMeleeSelectFn = void(__fastcall*)(uint32_t, int32_t, uint8_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t*);
struct OdstMeleeSelectionRuntime
{
    void* target = nullptr;
    OdstMeleeSelectFn original = nullptr;
    std::atomic<bool> enabled{false}, faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> secondary{0}, unarmed{0}, fallbacks{0};
} g_odstMeleeSelection;

#include "odst_melee_response.inl"

__declspec(noinline) void __fastcall OdstMeleeSelectionDetour(uint32_t unit, int32_t state, uint8_t* material,
    uint32_t* damage, uint32_t* effect, uint32_t* clashDamage, uint32_t* clashEffect)
{
    auto& feature = g_odstMeleeSelection;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (!feature.original)
            __leave;
        feature.original(unit, state, material, damage, effect, clashDamage, clashEffect);
        const auto& scope = g_odstContactScope;
        const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        if (!scope.active || !OdstContactMeleeReady() || !feature.enabled.load() || feature.faulted.load() ||
            state != 0x79 || uint16_t(unit) != uint16_t(scope.owner) ||
            (caller != g_odstContact.base + 0x3A0384 && caller != g_odstContact.base + 0x3A393C))
            __leave;
        __try
        {
            Halo3MeleeResponse candidate{};
            bool armed = false;
            if (!material || !damage || !effect || !clashDamage || !clashEffect ||
                !OdstReadPhysicalMeleeResponse(scope.owner, scope.hand, candidate, armed))
            {
                feature.fallbacks.fetch_add(1);
                __leave;
            }
            // The main hand keeps the native selection byte for byte.
            if (scope.hand == 1 && armed)
                __leave;
            if (candidate.damage == UINT32_MAX)
            {
                feature.fallbacks.fetch_add(1);
                __leave;
            }
            *material = candidate.material;
            *damage = candidate.damage;
            *effect = candidate.effect;
            *clashDamage = candidate.clashDamage;
            *clashEffect = candidate.clashEffect;
            (armed ? feature.secondary : feature.unarmed).fetch_add(1);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("selecting the ODST melee response faulted");
            feature.faulted.store(true);
        }
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

bool RemoveOdstMeleeSelection()
{
    auto& feature = g_odstMeleeSelection;
    feature.enabled.store(false, std::memory_order_release);
    if (!feature.target)
        return true;
    const auto disabled = MCCVR_DisableHookForRetirement(feature.target);
    if (disabled != MH_OK && disabled != MH_ERROR_DISABLED)
    {
        LOG("ODST melee selection CleanupRequired: disable failed");
        return false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&OdstMeleeSelectionDetour)};
    const void* originals[]{reinterpret_cast<const void*>(feature.original)};
    if (!WaitForNativeDetourQuiescence(functions, originals, 1, feature.callbacks) ||
        MCCVR_RemoveHookChecked(feature.target) != MH_OK)
    {
        LOG("ODST melee selection CleanupRequired: callbacks/ingress or removal pending");
        return false;
    }
    feature.target = nullptr;
    feature.original = nullptr;
    return true;
}

bool InstallOdstMeleeSelection(uintptr_t base, size_t size)
{
    auto& feature = g_odstMeleeSelection;
    if (feature.target)
        return false;
    // Unique in retail with the RIP displacement wildcarded (H3's literal differs).
    constexpr char pattern[] =
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 20 44 8B 15 ?? ?? ?? ?? 49 8B D9 65 48";
    if (!ExactCodeSignatureAt(base, size, 0x39F5AC, pattern, MCCVR_SIG_FIND, MCCVR_SIG_IS_CODE))
    {
        LOG("ODST melee selection StockFallback: selector binding missing/ambiguous");
        return false;
    }
    const uintptr_t hit = base + 0x39F5AC;
    for (uintptr_t caller : {uintptr_t(0x3A037F), uintptr_t(0x3A3937)})
        if (!NativeCallEdge(base, caller, 0x39F5AC))
        {
            LOG("ODST melee selection StockFallback: native caller mismatch");
            return false;
        }
    if (MCCVR_CreateHookChecked(reinterpret_cast<void*>(hit), reinterpret_cast<void*>(&OdstMeleeSelectionDetour),
            reinterpret_cast<void**>(&feature.original)) != MH_OK)
    {
        LOG("ODST melee selection StockFallback: hook creation failed");
        return false;
    }
    feature.target = reinterpret_cast<void*>(hit);
    if (MH_EnableHook(feature.target) != MH_OK)
    {
        LOG("ODST melee selection StockFallback: hook enable failed");
        (void)RemoveOdstMeleeSelection();
        return false;
    }
    feature.faulted.store(false);
    feature.enabled.store(true, std::memory_order_release);
    LOG("ODST physical melee selection Installed: secondary weapon response or authored unarmed unit damage; main weapon and manual melee retain stock selection");
    return true;
}

void ReportOdstMeleeSelection()
{
    const auto& feature = g_odstMeleeSelection;
    if (!feature.target)
        return;
    LOG("ODST physical melee selection: enabled=%d fault=%d secondary=%llu unarmed=%llu StockFallback=%llu",
        feature.enabled.load() ? 1 : 0, feature.faulted.load() ? 1 : 0, feature.secondary.load(),
        feature.unarmed.load(), feature.fallbacks.load());
}
