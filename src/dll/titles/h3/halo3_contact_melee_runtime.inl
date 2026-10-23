// Halo 3 physical contact melee: the title's native rows, detours and install/report entry points.
// The shared Halo 3-engine runtime does the work; this file holds only what is Halo 3's own.
#pragma once
// Included once, by src/dll/core/game.cpp; also compiled by
//   tests/halo3_contact_melee_runtime_golden_tests.cpp, which supplies its own versions. Not a
//   standalone header: it uses what the host defines above its include line, namely BoneMatrix,
//   FpInterpolationContext, Halo3ContactMeleeReady, Halo3PublishContactHand,
//   Halo3RedirectContactVector, InstallHalo3ContactMelee, ReportHalo3ContactMelee,
//   ReportHalo3DualAim, g_engineTlsIndex, g_halo3PlayerUnitGetter, g_halo3RuntimeGeneration,
//   g_halo3UnitInVehicle, g_halo3WorldCollision.
// Halo 3 native contact adapter. Included after the title-owned native getters.
// The shared Halo 3-engine runtime is legacy_contact_melee_runtime.inl; this
// file holds Halo 3's own rows and detours. Each native address below is
// mapped from its H3EK function to the pinned retail module.
#include "../../core/legacy_contact_melee_runtime.inl"
using Halo3ContactUpdateFn = LegacyContactUpdateFn;
using Halo3ContactMeleeFn = LegacyContactMeleeFn;
using Halo3ContactDamageFn = LegacyContactDamageFn;
using Halo3ContactRuntime = LegacyContactRuntime;
using Halo3ContactScope = LegacyContactScope;
LegacyContactRuntime g_halo3Contact;
thread_local LegacyContactScope g_halo3ContactScope;

bool InstallHalo3MeleeSelection(uintptr_t base, size_t size);

struct Halo3ContactTraits
{
    static LegacyContactRuntime& Runtime() { return g_halo3Contact; }
    static LegacyContactScope& Scope() { return g_halo3ContactScope; }
    static const uint32_t* TlsIndex() { return g_engineTlsIndex; }
    // H3EK datum_get 364640; retail 830F0..83107 retains these table fields.
    static constexpr size_t kObjectTableOffset = 0x38;
    static constexpr size_t kGlobalsOffset = 0x48;
    static void* WorldCollisionOriginal() { return g_halo3WorldCollision.original; }
    static constexpr uintptr_t kRedirectCaller = 0x35B297;
    static constexpr uintptr_t kDamageCaller = 0x35C102;
    // H3EK A59860 -> A745B0 -> A73390 and retail 35BEFC -> 36FE00 agree:
    // +3C/+48 are the two native impulse vectors.
    static constexpr size_t kImpulseA = 0x3C, kImpulseB = 0x48;
    static constexpr GameTitle kTitle = GameTitle::Halo3;
    static uint32_t RuntimeGeneration() { return g_halo3RuntimeGeneration.load(); }
    static bool HasPlayerUnit() { return g_halo3PlayerUnitGetter != nullptr; }
    static uint32_t PlayerUnit() { return static_cast<uint32_t>(g_halo3PlayerUnitGetter(0)); }
    static bool HasUnitInVehicle() { return g_halo3UnitInVehicle != nullptr; }
    static bool UnitInVehicle(int32_t unit) { return g_halo3UnitInVehicle(unit); }
    // Halo 3's melee selection reads the striking hand from the scope.
    static constexpr bool kCarriesHand = true;
    static constexpr LegacyContactBinding kBindings[]{
        {0x379B30,
            "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 54 41 56 41 57 48 83 EC 20 8B 15 ?? ?? ?? ?? 40 32 FF 65 48 8B 04 25 58 00 00 00 33 F6 44 8B F1 B9 38 00 00 00"},
        {0x35AE34,
            "48 8B C4 F3 0F 11 58 20 66 44 89 40 18 89 50 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 E8 FD FF FF 48 81 EC D8 02 00 00"},
        {0x36FE00,
            "48 8B C4 66 44 89 48 20 66 44 89 40 18 89 50 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 98 FE FF FF 48 81 EC 28 02 00 00 44 0F B7 11 45 33 E4"},
        {0x35B265,
            "48 8B 0D ?? ?? ?? ?? 48 8D 85 B0 00 00 00 48 89 44 24 38 4C 8D 4D 30 44 89 5C 24 30 4C 8D 45 E0 44 89 5C 24 28 41 8A D4 44 89 7C 24 20 E8 ?? ?? ?? ??"},
        {0xF01A0,
            "48 83 EC 28 E8 ?? ?? ?? ?? 32 D2 84 C0 74 27 8B 0D ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 41 B8 48 00 00 00 48 8B 04 C8 4A 8B 0C 00 38 91 60 01 00 00 74 02 B2 01 8A C2 48 83 C4 28 C3"}};
    static constexpr LegacyContactEdge kEdges[]{
        {0x39A5AD, 0x379B30}, {0x35B292, 0x1FD748}, {0x35C0FD, 0x36FE00}, {0x2EDFA1, 0xF01A0}};
    static bool ExtraProof(uintptr_t) { return true; }
    static constexpr uintptr_t kFlagsNext = 0x35B26C, kFlagsDisplacement = 0x35B268, kFlags = 0x2127108;
    static constexpr uintptr_t kMelee = 0x35AE34, kPlayback = 0xF01A0, kDamage = 0x36FE00, kUpdate = 0x379B30;
    static void AfterInstall(uintptr_t base, size_t size) { (void)InstallHalo3MeleeSelection(base, size); }
    static constexpr LegacyContactLogText kLog{
        "Halo 3 contact melee unavailable: native binding +0x%llX missing/ambiguous",
        "Halo 3 contact melee unavailable: native caller verification failed",
        "Halo 3 contact melee unavailable: hook creation failed",
        "Halo 3 contact melee unavailable: hook enable failed",
        "Halo 3 contact cleanup: disable/range verification failed; retained hooks",
        "Halo 3 contact cleanup: ingress/callback verification failed; retained hooks",
        "Halo 3 contact cleanup: MinHook removal failed; retained bookkeeping",
        "Halo 3 contact melee installed: physical hand/weapon contact, native exact-target damage; headset verification pending",
        "Halo 3 contact melee: enabled=%d fault=%d queries=%llu contacts=%llu submissions(L/R)=%llu/%llu nativeRejected=%llu queueDrops=%llu predictedRequests=%llu"};
};

bool Halo3ContactMeleeReady()
{
    return LegacyContactMeleeReady<Halo3ContactTraits>();
}
const uint8_t* Halo3ContactTls()
{
    return LegacyContactTls<Halo3ContactTraits>();
}
bool Halo3ContactObject(uint32_t handle, bool requireBiped = false)
{
    return LegacyContactObject<Halo3ContactTraits>(handle, requireBiped);
}

#include "halo3_melee_selection.inl"

bool Halo3RedirectContactVector(uintptr_t caller, uint64_t flags, int32_t mode, int32_t ignoredA, int32_t ignoredB,
    int32_t ignoredC, void* result, uint8_t& returned)
{
    return LegacyRedirectContactVector<Halo3ContactTraits>(
        caller, flags, mode, ignoredA, ignoredB, ignoredC, result, returned);
}

// The detours keep their own counter and SEH frame (see LegacyContactDamageBody).
__declspec(noinline) void __fastcall Halo3ContactDamageDetour(
    void* event, uint32_t target, int16_t region, int16_t node, uint16_t material, void* result)
{
    g_halo3Contact.callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        LegacyContactDamageBody<Halo3ContactTraits>(
            event, target, region, node, material, result, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    }
    __finally
    {
        g_halo3Contact.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}

using Halo3ContactBackend = LegacyContactBackend<Halo3ContactTraits>;

void Halo3ContactTick(uint32_t unit)
{
    LegacyContactTick<Halo3ContactTraits>(unit);
}

__declspec(noinline) uint8_t __fastcall Halo3ContactUpdateDetour(uint32_t unit)
{
    g_halo3Contact.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint8_t result = 0;
    __try
    {
        if (g_halo3Contact.updateOriginal)
            result = g_halo3Contact.updateOriginal(unit);
        Halo3ContactTick(unit);
    }
    __finally
    {
        g_halo3Contact.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

bool InstallHalo3ContactMelee(uintptr_t base, size_t size, uint32_t generation)
{
    return LegacyContactInstall<Halo3ContactTraits>(base, size, generation,
        reinterpret_cast<void*>(&Halo3ContactDamageDetour), reinterpret_cast<void*>(&Halo3ContactUpdateDetour));
}

void Halo3PublishContactHand(int hand, const FpInterpolationContext& context, const BoneMatrix& root,
    const BoneMatrix* solved, const float points[][3], int count, int32_t unit, uint32_t generation)
{
    LegacyContactPublishHand<Halo3ContactTraits>(hand, context, root, solved, points, count, unit, generation);
}

void ReportHalo3ContactMelee()
{
    ReportHalo3DualAim();
    if (!g_halo3Contact.targets[1])
        return;
    ReportHalo3MeleeSelection();
    LegacyContactReport<Halo3ContactTraits>();
}
