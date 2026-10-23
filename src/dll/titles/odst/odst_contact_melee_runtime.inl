// ODST physical contact melee: the title's native rows, detours and install/report entry points.
// The shared Halo 3-engine runtime does the work; this file holds only what is ODST's own.
#pragma once
// Included once, by src/dll/titles/odst/odst_camera_core.cpp; also compiled by
//   tests/odst_contact_melee_runtime_golden_tests.cpp, which supplies its own versions. Not a
//   standalone header: it uses what the host defines above its include line, namely BoneMatrix,
//   FpInterpolationContext, InstallOdstContactMelee, OdstContactMeleeReady,
//   OdstPublishContactHand, OdstRedirectContactVector, ReportOdstContactMelee,
//   g_odstEngineTlsIndex, g_odstPlayerUnitGetter, g_odstRuntimeGeneration, g_odstUnitInVehicle,
//   g_odstWorldCollision.
// ODST native contact adapter. Included after the title-owned native getters.
// The shared Halo 3-engine runtime is legacy_contact_melee_runtime.inl; this
// file holds ODST's own rows and detours. Each native address below is
// mapped from its ODST kit function to the pinned retail module.
#include "../../core/legacy_contact_melee_runtime.inl"
using OdstContactUpdateFn = LegacyContactUpdateFn;
using OdstContactMeleeFn = LegacyContactMeleeFn;
using OdstContactDamageFn = LegacyContactDamageFn;
using OdstContactRuntime = LegacyContactRuntime;
using OdstContactScope = LegacyContactScope;
LegacyContactRuntime g_odstContact;
thread_local LegacyContactScope g_odstContactScope;

bool InstallOdstMeleeSelection(uintptr_t base, size_t size); // defined in odst_melee_selection.inl
void ReportOdstMeleeSelection();

struct OdstContactTraits
{
    static LegacyContactRuntime& Runtime() { return g_odstContact; }
    static LegacyContactScope& Scope() { return g_odstContactScope; }
    static const uint32_t* TlsIndex() { return g_odstEngineTlsIndex; }
    // ODST's own object table: entry stride, live extent and full salt.
    static constexpr size_t kObjectTableOffset = kOdstTlsObjectTableOffset;
    static constexpr size_t kGlobalsOffset = 0x40;
    static void* WorldCollisionOriginal() { return g_odstWorldCollision.original; }
    static constexpr uintptr_t kRedirectCaller = 0x39FEBD;
    static constexpr uintptr_t kDamageCaller = 0x3A0D04;
    // ODST kit AD4FC0 -> AF2D90/AF1770, retail 3A0B00 -> 3B68A8: ODST impulse
    // vectors are +40/+4C, not the Halo 3 fields.
    static constexpr size_t kImpulseA = 0x40, kImpulseB = 0x4C;
    static constexpr GameTitle kTitle = GameTitle::Halo3ODST;
    static uint32_t RuntimeGeneration() { return g_odstRuntimeGeneration.load(); }
    static bool HasPlayerUnit() { return g_odstPlayerUnitGetter != nullptr; }
    static uint32_t PlayerUnit() { return static_cast<uint32_t>(g_odstPlayerUnitGetter(0)); }
    static bool HasUnitInVehicle() { return g_odstUnitInVehicle != nullptr; }
    static bool UnitInVehicle(int32_t unit) { return g_odstUnitInVehicle(unit); }
    // The striking hand reaches the melee selection, as in Halo 3.
    static constexpr bool kCarriesHand = true;
    static constexpr LegacyContactBinding kBindings[]{
        {0x3DEB98,
            "48 89 5C 24 08 48 89 6C 24 18 56 57 41 54 41 56 41 57 48 83 EC 20 8B 15 68 0F 6B 00 33 DB 65 48 8B 04 25 58 00 00 00 40 8A FB 41 B9 20 00 00 00 44"},
        {0x39FA48,
            "48 8B C4 F3 0F 11 58 20 66 44 89 40 18 89 50 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 F8 FD FF FF 48 81 EC C8 02 00 00"},
        {0x3B68A8,
            "48 8B C4 66 44 89 48 20 66 44 89 40 18 89 50 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 98 FE FF FF 48 81 EC 28 02 00 00 44 0F B7 11 45 33 E4"},
        {0x39FE8B,
            "48 8B 0D 36 E5 DD 01 48 8D 85 A0 00 00 00 48 89 44 24 38 4C 8D 4D 20 44 89 5C 24 30 4C 8D 45 B8 44 89 5C 24 28 41 8A D7 44 89 6C 24 20 E8 4F F9 E8 FF"},
        {0x10B0A8,
            "48 83 EC 28 E8 9F FB FF FF 32 D2 84 C0 74 27 8B 0D 5F 4A 98 00 65 48 8B 04 25 58 00 00 00 41 B8 40 00 00 00 48 8B 04 C8 4A 8B 0C 00 38 91 60 01 00 00 74 02 B2 01 8A C2 48 83 C4 28 C3"}};
    static constexpr LegacyContactEdge kEdges[]{
        {0x3DEC1E, 0x3C2C28}, {0x3DEC39, 0x3CFF78}, {0x39FEB8, 0x22F80C}, {0x3A0CFF, 0x3B68A8}, {0x329502, 0x10B0A8}};
    // The update entry is also proven through its vtable slot.
    static bool ExtraProof(uintptr_t base)
    {
        return *reinterpret_cast<const uintptr_t*>(base + 0x8F6A30) == base + 0x3DEB98;
    }
    static constexpr uintptr_t kFlagsNext = 0x39FE92, kFlagsDisplacement = 0x39FE8E, kFlags = 0x217E3C8;
    static constexpr uintptr_t kMelee = 0x39FA48, kPlayback = 0x10B0A8, kDamage = 0x3B68A8, kUpdate = 0x3DEB98;
    static void AfterInstall(uintptr_t base, size_t size) { (void)InstallOdstMeleeSelection(base, size); }
    static constexpr LegacyContactLogText kLog{
        "ODST contact melee unavailable: native binding +0x%llX missing/ambiguous",
        "ODST contact melee unavailable: native caller verification failed",
        "ODST contact melee unavailable: hook creation failed", "ODST contact melee unavailable: hook enable failed",
        "ODST contact cleanup: disable/range verification failed; retained hooks",
        "ODST contact cleanup: ingress/callback verification failed; retained hooks",
        "ODST contact cleanup: MinHook removal failed; retained bookkeeping",
        "ODST contact melee installed: physical hand/weapon contact, native exact-target damage; headset verification pending",
        "ODST contact melee: enabled=%d fault=%d queries=%llu contacts=%llu submissions(L/R)=%llu/%llu nativeRejected=%llu queueDrops=%llu predictedRequests=%llu"};
};

bool OdstContactMeleeReady()
{
    return LegacyContactMeleeReady<OdstContactTraits>();
}
const uint8_t* OdstContactTls()
{
    return LegacyContactTls<OdstContactTraits>();
}
bool OdstContactObject(uint32_t handle, bool requireBiped = false)
{
    return LegacyContactObject<OdstContactTraits>(handle, requireBiped);
}

bool OdstRedirectContactVector(uintptr_t caller, uint64_t flags, int32_t mode, int32_t ignoredA, int32_t ignoredB,
    int32_t ignoredC, void* result, uint8_t& returned)
{
    return LegacyRedirectContactVector<OdstContactTraits>(
        caller, flags, mode, ignoredA, ignoredB, ignoredC, result, returned);
}

// The detours keep their own counter and SEH frame (see LegacyContactDamageBody).
__declspec(noinline) void __fastcall OdstContactDamageDetour(
    void* event, uint32_t target, int16_t region, int16_t node, uint16_t material, void* result)
{
    g_odstContact.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        LegacyContactDamageBody<OdstContactTraits>(
            event, target, region, node, material, result, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    }
    __finally
    {
        g_odstContact.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

using OdstContactBackend = LegacyContactBackend<OdstContactTraits>;

void OdstContactTick(uint32_t unit)
{
    LegacyContactTick<OdstContactTraits>(unit);
}

__declspec(noinline) uint8_t __fastcall OdstContactUpdateDetour(uint32_t unit)
{
    g_odstContact.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint8_t result = 0;
    __try
    {
        if (g_odstContact.updateOriginal)
            result = g_odstContact.updateOriginal(unit);
        OdstContactTick(unit);
    }
    __finally
    {
        g_odstContact.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

bool InstallOdstContactMelee(uintptr_t base, size_t size, uint32_t generation)
{
    return LegacyContactInstall<OdstContactTraits>(base, size, generation,
        reinterpret_cast<void*>(&OdstContactDamageDetour), reinterpret_cast<void*>(&OdstContactUpdateDetour));
}

void OdstPublishContactHand(int hand, const FpInterpolationContext& context, const BoneMatrix& root,
    const BoneMatrix* solved, const float points[][3], int count, int32_t unit, uint32_t generation)
{
    LegacyContactPublishHand<OdstContactTraits>(hand, context, root, solved, points, count, unit, generation);
}

void ReportOdstContactMelee()
{
    if (!g_odstContact.targets[1])
        return;
    LegacyContactReport<OdstContactTraits>();
    ReportOdstMeleeSelection();
}
