// Golden fixture: pins the ODST contact-melee runtime (odst_contact_melee_runtime.inl, built on the
// shared Halo 3-engine legacy_contact_melee_runtime.inl): install proofs, hook order and the
// retire-on-failure rules, exact log text, the report line, and the
// native Apply path (25-ray centre redirect, +40/+4C impulse direction,
// exact-target damage) against a fake TLS object table and fake natives.
// Compiled with sigscan.cpp and the golden_hook_fakes.h MinHook fixtures; built as
// odst_contact_melee_runtime_golden_tests. No real hook is installed.
#include "golden_hook_fakes.h"
#include <cmath>
#include "../src/common/runtime_types.h"
#include "../src/common/feedback_cues.h"
#include "../src/common/titles/odst/odst_vehicle_logic.h"
#include "../src/common/contact_melee_drain.h"
#include "hook_checked_forwarders.h" // DLL hook sites call MinHook through the checked names
#include "../src/dll/core/contact_melee_queue.h"
#include "../src/dll/core/native_binding.h"
#include "../src/dll/core/sigscan.h"
#include "../src/dll/core/guard_count.h"

// ---- title context the runtime is included after ----
struct
{
    bool physical_melee = true, melee_fist = true;
    float physical_melee_swing_speed = 5.0f, physical_melee_cooldown_s = 0.06f;
    float gun_scale = 1, left_hand_scale = 1, gun_forward_m = 0, gun_right_m = 0, gun_up_m = 0, left_hand_forward_m = 0,
          gun_pitch_deg = 0, gun_yaw_deg = 0, gun_roll_deg = 0, barrel_pitch_deg = 0, barrel_yaw_deg = 0,
          barrel_roll_deg = 0;
} g_config;
inline float ClampPhysicalMeleeSwingSpeed(float v)
{
    return v;
}
std::atomic<bool> g_enabled{true}, g_baseCamValid{true};
std::atomic<float> g_baseCamX{0}, g_baseCamY{0}, g_baseCamZ{0}, g_worldScale{1};
float g_headYawRef = 0, g_gameYawRef = 0, g_headPosRef[3]{};
float g_fpPaletteCollisionCorrection[2][3]{};
std::atomic<uint32_t> g_odstRuntimeGeneration{7};
uint32_t engineTlsIndex = 3;
uint32_t* g_odstEngineTlsIndex = &engineTlsIndex;
int64_t __fastcall PlayerUnit(int)
{
    return 0x00010001;
}
int64_t(__fastcall* g_odstPlayerUnitGetter)(int) = PlayerUnit;
bool __fastcall NotInVehicle(int32_t)
{
    return false;
}
bool(__fastcall* g_odstUnitInVehicle)(int32_t) = NotInVehicle;
using GameIsPlaybackFn = bool(__fastcall*)();
using LegacyCollisionTestVectorFn = uint8_t(__fastcall*)(
    uint64_t, int32_t, const float*, const float*, int32_t, int32_t, int32_t, void*);
struct
{
    void* original = nullptr;
} g_odstWorldCollision;
namespace exclusive_input
{
    inline bool Active()
    {
        return false;
    }
}
bool VR_IsStereoEnabled()
{
    return true;
}
GameTitle TitleAdapter_GetActiveTitle()
{
    return GameTitle::Halo3ODST;
}
CinematicControlState ReadCinematicControl(int32_t&, int32_t&)
{
    return CinematicControlState::PlayerControlled;
}
unsigned haptics[2]{};
void VR_PulseContactHaptics(bool support, float)
{
    ++haptics[support ? 0 : 1];
}
bool VR_IsBareFist(int)
{
    return false;
}
struct FpInterpolationContext
{
    uint64_t wristDescendants = 0, lWristDescendants = 0;
    int count = 0, slot = 0;
};
struct BoneMatrix
{
    float m[12]{};
};
constexpr int kLegacyCollisionWeaponSamples = 14;
bool LegacyBuildMappedWeaponBounds(
    GameTitle, const FpInterpolationContext&, const BoneMatrix&, const BoneMatrix*, float (*)[3], uint64_t&)
{
    return false;
}
struct VrContactTrackingSnapshot
{
    struct Hand
    {
        bool valid = false;
        float orientation[4]{0, 0, 0, 1};
        float position[3]{};
    } hands[2];
    uint64_t serial = 0, referenceEpoch = 0;
    int64_t timeNs = 0;
    bool twoHandAimActive = false;
};
bool snapshotOk = false;
VrContactTrackingSnapshot snapshotValue{};
bool VR_GetContactTrackingSnapshot(VrContactTrackingSnapshot& out)
{
    out = snapshotValue;
    return snapshotOk;
}

// TLS and return-address seams (the same seams halo3_weapon_haptics_backend_tests uses).
void* tlsSlots[8]{};
uint64_t MockReadGs(unsigned long)
{
    return reinterpret_cast<uint64_t>(tlsSlots);
}
uintptr_t mockReturnAddress = 0;
#define __readgsqword MockReadGs
#define _ReturnAddress() reinterpret_cast<void*>(mockReturnAddress)
uint64_t clockMs = 1000;
uint64_t MockNow()
{
    return clockMs;
}
#define GetTickCount64 MockNow

#include "../src/dll/titles/odst/odst_contact_melee_runtime.inl"
// The ODST melee selection hook lives in odst_melee_selection.inl (it needs
// the ODST muzzle ownership helpers); its response reader is tested in odst_muzzle_tests.
bool InstallOdstMeleeSelection(uintptr_t, size_t)
{
    return false;
}
void ReportOdstMeleeSelection() {}

#undef __readgsqword
#undef GetTickCount64

namespace
{
    using golden::Op;
    unsigned checks = 0, failures = 0;
    void Check(bool ok, const std::string& what)
    {
        ++checks;
        if (!ok)
        {
            ++failures;
            std::printf("FAIL: %s\n", what.c_str());
        }
    }

    struct Row
    {
        uintptr_t rva;
        const char* pattern;
    };
    const Row kBindings[]{
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
    constexpr uintptr_t kEdges[][2]{
        {0x3DEC1E, 0x3C2C28}, {0x3DEC39, 0x3CFF78}, {0x39FEB8, 0x22F80C}, {0x3A0CFF, 0x3B68A8}, {0x329502, 0x10B0A8}};
    constexpr size_t kSize = 0x2200000;
    constexpr uintptr_t kVtableSlot = 0x8F6A30;
    const char* kInstalled =
        "ODST contact melee installed: physical hand/weapon contact, native exact-target damage; headset verification pending";

    uint8_t worldCollisionStub[1]{};
    void ResetRuntime()
    {
        // What a completed retirement + selection removal leave behind.
        g_odstContact.enabled = false;
        g_odstContact.faulted = false;
        g_odstContact.targets[0] = g_odstContact.targets[1] = nullptr;
        g_odstContact.detours[0] = g_odstContact.detours[1] = nullptr;
        g_odstContact.updateOriginal = nullptr;
        g_odstContact.damageOriginal = nullptr;
        golden::Reset();
    }

    // ---- fake natives for Apply ----
    constexpr uint32_t kOwner = 0x00010001, kTarget = 0x00020002;
    uintptr_t g_base = 0;
    uint8_t lastEvent[0x60]{};
    unsigned damageCalls = 0;
    uint32_t damageTarget = 0;
    bool redirectCentreHits = true;
    uint8_t __fastcall FakeWorldCollision(
        uint64_t, int32_t, const float*, const float*, int32_t, int32_t, int32_t, void* result)
    {
        auto* bytes = static_cast<uint8_t*>(result);
        const uint32_t type = 4;
        std::memcpy(bytes, &type, 4);
        const uint32_t target = redirectCentreHits ? kTarget : 0x00030003;
        std::memcpy(bytes + 0x40, &target, 4);
        return 1;
    }
    void __fastcall FakeDamageOriginal(void* event, uint32_t target, int16_t, int16_t, uint16_t, void*)
    {
        ++damageCalls;
        damageTarget = target;
        std::memcpy(lastEvent, event, sizeof(lastEvent));
    }
    // The native melee: 25 collision rays through the redirect, then the damage call from its own return site.
    void __fastcall FakeMelee(uint32_t owner, int32_t response, int16_t mode, float scale)
    {
        Check(owner == kOwner && response == 0x79 && mode == 0 && scale == 1.0f,
            "native melee called as (owner, 0x79, mode, 1.0)");
        alignas(16) uint8_t result[0x80]{};
        uint8_t centre = 0;
        for (int ray = 0; ray < 25; ++ray)
        {
            uint8_t returned = 0;
            (void)OdstRedirectContactVector(g_base + 0x39FEBD, 0, 0, int32_t(owner), -1, -1, result, returned);
            if (ray == 12)
                centre = returned;
        }
        if (!centre)
            return; // the native builds no damage event without a hit
        alignas(16) uint8_t event[0x60]{};
        std::memcpy(event + 0x18, &owner, 4);
        std::memset(event + 0x3C, 0xAB, 0x24);
        mockReturnAddress = g_base + 0x3A0D04;
        OdstContactDamageDetour(event, kTarget, 0, 0, 0, nullptr);
        mockReturnAddress = 0;
    }

    // Fake engine TLS: object table with the owner (biped) and the target (type 1).
    alignas(16) uint8_t fixtureTls[0x100]{}, fixtureTable[0x60]{}, fixtureEntries[4 * 0x18]{}, fixtureGlobals[0x20]{},
        objectBody[16]{};
    void BuildTls()
    {
        tlsSlots[engineTlsIndex] = fixtureTls;
        void* p = fixtureTable;
        std::memcpy(fixtureTls + kOdstTlsObjectTableOffset, &p, 8);
        p = fixtureGlobals;
        std::memcpy(fixtureTls + 0x40, &p, 8);
        const int32_t stride = 0x18, count = 4;
        std::memcpy(fixtureTable + 0x24, &stride, 4);
        std::memcpy(fixtureTable + 0x3C, &count, 4);
        p = fixtureEntries;
        std::memcpy(fixtureTable + 0x48, &p, 8);
        auto entry = [](uint32_t handle, uint8_t type)
        {
            uint8_t* e = fixtureEntries + (handle & 0xFFFF) * 0x18;
            const uint16_t salt = uint16_t(handle >> 16);
            std::memcpy(e, &salt, 2);
            e[3] = type;
            void* body = objectBody;
            std::memcpy(e + 0x10, &body, 8);
        };
        entry(kOwner, 0);
        entry(kTarget, 1);
        fixtureGlobals[0] = 1;
        fixtureGlobals[0x11] = 0;
    }
}

int main()
{
    golden::Image image(kSize);
    if (!image.bytes)
        return 2;
    for (const auto& b : kBindings)
        image.Pattern(b.rva, b.pattern);
    for (const auto& e : kEdges)
        image.Edge(e[0], e[1]);
    // The update entry is proven through its vtable slot too.
    const uintptr_t vtableValue = image.Base() + 0x3DEB98;
    std::memcpy(image.bytes + kVtableSlot, &vtableValue, 8);
    // RIP reference from the +0x39FE8B binding to the collision flags global.
    const int32_t flagsRel = int32_t(0x217E3C8 - 0x39FE92);
    std::memcpy(image.bytes + 0x39FE8E, &flagsRel, 4);
    const uintptr_t base = image.Base();
    g_base = base;
    g_odstWorldCollision.original = worldCollisionStub;

    // 1. Success: damage hook created+enabled, then the update hook, then melee selection; exact lines.
    ResetRuntime();
    Check(InstallOdstContactMelee(base, kSize, 7), "install succeeds on a matching image");
    const bool order =
        golden::calls.size() == 4 && golden::calls[0].op == Op::Create &&
        golden::calls[0].target == reinterpret_cast<void*>(base + 0x3B68A8) &&
        golden::calls[0].detour == reinterpret_cast<void*>(&OdstContactDamageDetour) &&
        golden::calls[1].op == Op::Create && golden::calls[1].target == reinterpret_cast<void*>(base + 0x3DEB98) &&
        golden::calls[1].detour == reinterpret_cast<void*>(&OdstContactUpdateDetour) &&
        golden::calls[2].op == Op::Enable && golden::calls[2].target == reinterpret_cast<void*>(base + 0x3B68A8) &&
        golden::calls[3].op == Op::Enable && golden::calls[3].target == reinterpret_cast<void*>(base + 0x3DEB98);
    Check(order, "z67 hook order: damage and update created, then both enabled (no melee selection in ODST)");
    Check(golden::logs.size() == 1 && golden::logs[0] == kInstalled, "exact installed line");
    Check(g_odstContact.enabled.load() && g_odstContact.generation == 7 && g_odstContact.base == base &&
              g_odstContact.flags == reinterpret_cast<const uint64_t*>(base + 0x217E3C8) &&
              g_odstContact.melee == reinterpret_cast<OdstContactMeleeFn>(base + 0x39FA48) &&
              g_odstContact.playback == reinterpret_cast<GameIsPlaybackFn>(base + 0x10B0A8),
        "runtime bindings");
    Check(reinterpret_cast<const void*>(&OdstContactDamageDetour) !=
              reinterpret_cast<const void*>(&OdstContactUpdateDetour),
        "distinct detour addresses (no /OPT:ICF folding)");
    // Teardown verification (WaitForNativeDetourQuiescence / ResolveReachDetourCodeRange):
    // every detour must carry unwind data whose range covers its own entry.
    for (const void* detour : {reinterpret_cast<const void*>(&OdstContactUpdateDetour),
             reinterpret_cast<const void*>(&OdstContactDamageDetour)})
    {
        DWORD64 imageBase = 0;
        const auto* entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(detour), &imageBase, nullptr);
        const DWORD64 at = reinterpret_cast<DWORD64>(detour);
        Check(entry && entry->EndAddress > entry->BeginAddress && imageBase + entry->BeginAddress <= at &&
                  at < imageBase + entry->EndAddress,
            "detour has an unwind range covering its entry (teardown quiescence can verify it)");
    }
    golden::Reset();
    Check(!InstallOdstContactMelee(base, kSize, 7) && golden::calls.empty() && !g_odstContact.enabled.load(),
        "quirk: a second install clears enabled and refuses while hooks exist");

    // 2. Report: the contact line only, counters reset (exchange).
    g_odstContact.enabled = true;
    g_odstContact.queries = 3;
    golden::Reset();
    ReportOdstContactMelee();
    Check(
        golden::logs.size() == 1 &&
            golden::logs[0] ==
                "ODST contact melee: enabled=1 fault=0 queries=3 contacts=0 submissions(L/R)=0/0 nativeRejected=0 queueDrops=0 predictedRequests=0",
        "report: exact contact line");
    golden::Reset();
    ReportOdstContactMelee();
    Check(golden::logs.size() == 1 && golden::logs[0].find("queries=0") != std::string::npos, "report counters reset");

    // 3. Apply through the native path: 25 rays, centre redirected, +3C/+48 impulse, exact target.
    BuildTls();
    g_odstContact.damageOriginal = FakeDamageOriginal;
    g_odstContact.melee = FakeMelee;
    g_odstWorldCollision.original = reinterpret_cast<void*>(&FakeWorldCollision);
    OdstContactBackend backend{};
    backend.owner = kOwner;
    contact_melee::Hit hit{};
    hit.unit = kTarget;
    hit.object = true;
    hit.kind = contact_melee::HitKind::Object;
    hit.fraction = 0.5f;
    contact_melee::Sweep sweep{};
    sweep.start = {0, 0, 0};
    sweep.end = {3, 0, 4};
    Check(backend.Apply(kOwner, hit, sweep), "Apply submits for the exact target");
    float d0[3], d1[3];
    std::memcpy(d0, lastEvent + 0x40, 12);
    std::memcpy(d1, lastEvent + 0x4C, 12);
    Check(damageCalls == 1 && damageTarget == kTarget && std::fabs(d0[0] - 0.6f) < 1e-6f &&
              std::fabs(d0[2] - 0.8f) < 1e-6f && std::memcmp(d0, d1, 12) == 0 && lastEvent[0x3C] == 0xAB &&
              lastEvent[0x58] == 0xAB,
        "impulse: unit sweep direction written at +40 and +4C only (not the H3 fields)");
    Check(g_odstContactScope.rays == 25 && !g_odstContactScope.active, "25 native rays, scope closed");
    redirectCentreHits = false;
    damageCalls = 0;
    Check(!backend.Apply(kOwner, hit, sweep) && damageCalls == 0,
        "a changed centre-ray target is rejected before damage");
    redirectCentreHits = true;
    fixtureGlobals[0] = 0;
    fixtureGlobals[1] = 0;
    Check(!backend.Apply(kOwner, hit, sweep), "no game globals: refused before the native call");
    fixtureGlobals[0] = 1;
    g_odstWorldCollision.original = worldCollisionStub;

    // 4. Install failures, with Halo 3's retain-on-failure semantics.
    ResetRuntime();
    g_odstWorldCollision.original = nullptr;
    Check(!InstallOdstContactMelee(base, kSize, 7) && golden::logs.empty() && golden::calls.empty(),
        "no world collision: silent");
    g_odstWorldCollision.original = worldCollisionStub;
    for (const auto& b : kBindings)
    {
        ResetRuntime();
        const uint8_t saved = image.bytes[b.rva];
        image.bytes[b.rva] ^= 0xFF;
        char expected[128];
        snprintf(expected, sizeof(expected), "ODST contact melee unavailable: native binding +0x%llX missing/ambiguous",
            static_cast<unsigned long long>(b.rva));
        Check(!InstallOdstContactMelee(base, kSize, 7) && golden::Logged(expected) && golden::calls.empty(),
            "binding refused with its line");
        image.bytes[b.rva] = saved;
    }
    for (const auto& e : kEdges)
    {
        ResetRuntime();
        // An edge inside a binding pattern is caught by that binding first.
        std::string expected = "ODST contact melee unavailable: native caller verification failed";
        for (const auto& b : kBindings)
        {
            size_t length = 0;
            for (const char* q = b.pattern; *q; ++q)
                if (*q != ' ' && (q == b.pattern || q[-1] == ' '))
                    ++length;
            if (e[0] >= b.rva && e[0] < b.rva + length)
            {
                char text[128];
                snprintf(text, sizeof(text), "ODST contact melee unavailable: native binding +0x%llX missing/ambiguous",
                    static_cast<unsigned long long>(b.rva));
                expected = text;
            }
        }
        const uint8_t saved = image.bytes[e[0]];
        image.bytes[e[0]] = 0x90;
        Check(!InstallOdstContactMelee(base, kSize, 7) && golden::Logged(expected.c_str()) && golden::calls.empty(),
            "edge refused with its line");
        image.bytes[e[0]] = saved;
    }
    ResetRuntime();
    const int32_t wrong = flagsRel + 8;
    std::memcpy(image.bytes + 0x39FE8E, &wrong, 4);
    // ODST's flags displacement is literal in the +0x39FE8B binding, so a moved reference trips that binding.
    Check(!InstallOdstContactMelee(base, kSize, 7) &&
              golden::Logged("ODST contact melee unavailable: native binding +0x39FE8B missing/ambiguous") &&
              golden::calls.empty(),
        "flags reference mismatch: caught by its binding");
    std::memcpy(image.bytes + 0x39FE8E, &flagsRel, 4);

    // muzzle_hook_set.h order. Both hooks are created, then both
    // enabled; any failure logs one line and retires what was created.
    ResetRuntime();
    golden::failCreateAt = 0;
    Check(!InstallOdstContactMelee(base, kSize, 7) &&
              golden::Logged("ODST contact melee unavailable: hook creation failed") && !g_odstContact.targets[0] &&
              !g_odstContact.targets[1] && golden::Count(Op::Remove) == 0,
        "z67: damage create failure: nothing created, nothing to remove");
    ResetRuntime();
    golden::failCreateAt = 1;
    Check(!InstallOdstContactMelee(base, kSize, 7) &&
              golden::Logged("ODST contact melee unavailable: hook creation failed") && !g_odstContact.targets[0] &&
              !g_odstContact.targets[1] && golden::Count(Op::Remove) == 1 && !g_odstContact.damageOriginal,
        "z67: update create failure retires the damage hook (was: kept)");
    ResetRuntime();
    golden::failEnableAt = 0;
    Check(!InstallOdstContactMelee(base, kSize, 7) &&
              golden::Logged("ODST contact melee unavailable: hook enable failed") && golden::Count(Op::Create) == 2 &&
              !g_odstContact.targets[0] && !g_odstContact.targets[1] && golden::Count(Op::Remove) == 2,
        "z67: an enable failure retires both created hooks (was: kept for cleanup)");
    ResetRuntime();
    golden::failEnableAt = 1;
    Check(!InstallOdstContactMelee(base, kSize, 7) && !g_odstContact.enabled.load() && !g_odstContact.targets[0] &&
              !g_odstContact.targets[1] && golden::Count(Op::Remove) == 2,
        "z67: an update enable failure retires both hooks");
    {
        // The real retirement (LegacyContactRetire), not a re-implementation.
        const auto installed = [&]
        {
            ResetRuntime();
            return InstallOdstContactMelee(base, kSize, 7);
        };
        Check(installed(), "retire fixture: installed");
        golden::disableStatus = MH_ERROR_NOT_EXECUTABLE;
        Check(!LegacyContactRetire<OdstContactTraits>() &&
                  golden::Logged("ODST contact cleanup: disable/range verification failed; retained hooks") &&
                  g_odstContact.targets[0] && g_odstContact.targets[1] && golden::Count(Op::Remove) == 0,
            "retire: a disable failure keeps both hooks");
        golden::disableStatus = MH_ERROR_NOT_CREATED;
        Check(LegacyContactRetire<OdstContactTraits>() && !g_odstContact.targets[0] && !g_odstContact.targets[1] &&
                  !g_odstContact.damageOriginal && !g_odstContact.updateOriginal,
            "retire: NOT_CREATED (module gone) counts as disabled; both removed");
        Check(installed(), "retire fixture: installed again");
        golden::quiescent = false;
        Check(!LegacyContactRetire<OdstContactTraits>() &&
                  golden::Logged("ODST contact cleanup: ingress/callback verification failed; retained hooks") &&
                  g_odstContact.targets[0] && g_odstContact.targets[1] && golden::Count(Op::Remove) == 0,
            "retire: busy detours keep both hooks");
        golden::quiescent = true;
        Check(golden::quiescenceFunctions.back().size() == 2, "retire: both detours drained together");
        Check(installed(), "retire fixture: installed for the removal failure");
        golden::removeStatus = MH_ERROR_MEMORY_PROTECT;
        Check(!LegacyContactRetire<OdstContactTraits>() &&
                  golden::Logged("ODST contact cleanup: MinHook removal failed; retained bookkeeping") &&
                  g_odstContact.targets[0] && g_odstContact.damageOriginal && g_odstContact.targets[1] &&
                  g_odstContact.updateOriginal && golden::Count(Op::Remove) == 1,
            "retire: removal stops at the first failure; that hook and the next keep target and trampoline");
        golden::removeStatus = MH_OK;
        Check(LegacyContactRetire<OdstContactTraits>() && !g_odstContact.targets[0] && !g_odstContact.targets[1] &&
                  !g_odstContact.damageOriginal && !g_odstContact.updateOriginal,
            "retire: the retry removes the rest");
    }
    ResetRuntime();
    const uintptr_t badVtable = 0;
    std::memcpy(image.bytes + kVtableSlot, &badVtable, 8);
    Check(!InstallOdstContactMelee(base, kSize, 7) &&
              golden::Logged("ODST contact melee unavailable: native caller verification failed") &&
              golden::calls.empty(),
        "update vtable slot is proven");
    std::memcpy(image.bytes + kVtableSlot, &vtableValue, 8);

    // 5. Publish: the packet a support-hand frame produces (pinned before ODST moved onto the shared runtime).
    ResetRuntime();
    InstallOdstContactMelee(base, kSize, 7);
    snapshotOk = true;
    snapshotValue.serial = 41;
    snapshotValue.referenceEpoch = 9;
    snapshotValue.timeNs = 1234567;
    snapshotValue.hands[0].valid = true;
    snapshotValue.hands[0].position[0] = 0.25f;
    {
        FpInterpolationContext context{};
        context.lWristDescendants = 0x3F;
        context.count = 5;
        context.slot = 0;
        BoneMatrix root{}, solved[2]{};
        const float points[3][3]{{0.1f, 0.2f, 0.3f}, {0.4f, 0.5f, 0.6f}, {-0.1f, 0.0f, 0.9f}};
        g_odstContact.queue[0].Reset();
        OdstPublishContactHand(0, context, root, solved, points, 3, int32_t(kOwner), 7);
        ContactMeleePacket packet{};
        const bool got = g_odstContact.queue[0].Pop(packet);
        const auto& f = packet.frame;
        Check(got && f.serial == 41 && f.timeNs == 1234567 && f.unit == kOwner && f.count == 3 &&
                  f.shape == 0x087F3F0000006B0Dull && f.referenceEpoch == 0x261A8B72389999FBull &&
                  f.bareHandPoints == 3 && std::fabs(f.points[0].x + 0.2f) < 1e-6f &&
                  std::fabs(f.points[0].y - 0.3f) < 1e-6f && std::fabs(f.points[0].z + 0.1f) < 1e-6f &&
                  packet.generation == 7,
            "publish: support-hand packet identical to the pre-fold code (shape, epoch, points)");
    }
    snapshotOk = false;

    std::printf("ODST contact melee runtime golden: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
