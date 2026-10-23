// Golden tests for the Halo 3 contact-melee runtime (src/dll/titles/h3/halo3_contact_melee_runtime.inl).
// Built as halo3_contact_melee_runtime_golden_tests with src/dll/core/sigscan.cpp and run by ctest.
// MinHook, the module image and the log are fakes from golden_hook_fakes.h; no game process.
//
// Pins the CURRENT Halo 3 contact-melee
// runtime (halo3_contact_melee_runtime.inl) before the H3/ODST
// LegacyContactMelee fold: install proofs, hook order and its
// retain-on-failure semantics, exact log text, the report line, and the
// native Apply path (25-ray centre redirect, +3C/+48 impulse direction,
// exact-target damage) against a fake TLS object table and fake natives.
#include "golden_hook_fakes.h"
#include <cmath>
#include "../src/common/runtime_types.h"
#include "../src/common/feedback_cues.h"
#include "../src/common/titles/h3/halo3_melee_selection_logic.h"
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
std::atomic<uint32_t> g_halo3RuntimeGeneration{7};
uint32_t engineTlsIndex = 3;
uint32_t* g_engineTlsIndex = &engineTlsIndex;
int64_t __fastcall PlayerUnit(int)
{
    return 0x00010001;
}
int64_t(__fastcall* g_halo3PlayerUnitGetter)(int) = PlayerUnit;
bool __fastcall NotInVehicle(int32_t)
{
    return false;
}
bool(__fastcall* g_halo3UnitInVehicle)(int32_t) = NotInVehicle;
using GameIsPlaybackFn = bool(__fastcall*)();
using LegacyCollisionTestVectorFn = uint8_t(__fastcall*)(
    uint64_t, int32_t, const float*, const float*, int32_t, int32_t, int32_t, void*);
struct
{
    void* original = nullptr;
} g_halo3WorldCollision;
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
    return GameTitle::Halo3;
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
void ReportHalo3DualAim() {}
uint8_t* Halo3LoadedTagDefinition(uint32_t)
{
    return nullptr;
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

#include "../src/dll/titles/h3/halo3_contact_melee_runtime.inl"

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
    constexpr uintptr_t kEdges[][2]{
        {0x39A5AD, 0x379B30}, {0x35B292, 0x1FD748}, {0x35C0FD, 0x36FE00}, {0x2EDFA1, 0xF01A0}};
    const char* kSelectionPattern =
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 20 44 8B 15 D5 F5 6D 00 49 8B D9 65 48";
    constexpr size_t kSize = 0x2200000;
    const char* kInstalled =
        "Halo 3 contact melee installed: physical hand/weapon contact, native exact-target damage; headset verification pending";
    const char* kSelectionInstalled =
        "Halo 3 physical melee selection Installed: secondary weapon response or authored unarmed unit damage; main weapon and manual melee retain stock selection";

    uint8_t worldCollisionStub[1]{};
    void ResetRuntime()
    {
        // What a completed retirement + selection removal leave behind.
        g_halo3Contact.enabled = false;
        g_halo3Contact.faulted = false;
        g_halo3Contact.targets[0] = g_halo3Contact.targets[1] = nullptr;
        g_halo3Contact.detours[0] = g_halo3Contact.detours[1] = nullptr;
        g_halo3Contact.updateOriginal = nullptr;
        g_halo3Contact.damageOriginal = nullptr;
        g_halo3MeleeSelection.target = nullptr;
        g_halo3MeleeSelection.original = nullptr;
        g_halo3MeleeSelection.enabled = false;
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
            (void)Halo3RedirectContactVector(g_base + 0x35B297, 0, 0, int32_t(owner), -1, -1, result, returned);
            if (ray == 12)
                centre = returned;
        }
        if (!centre)
            return; // the native builds no damage event without a hit
        alignas(16) uint8_t event[0x60]{};
        std::memcpy(event + 0x18, &owner, 4);
        std::memset(event + 0x3C, 0xAB, 0x24);
        mockReturnAddress = g_base + 0x35C102;
        Halo3ContactDamageDetour(event, kTarget, 0, 0, 0, nullptr);
        mockReturnAddress = 0;
    }

    // Fake engine TLS: object table with the owner (biped) and the target (type 1).
    alignas(16) uint8_t fixtureTls[0x100]{}, fixtureTable[0x60]{}, fixtureEntries[4 * 0x18]{}, fixtureGlobals[0x20]{},
        objectBody[16]{};
    void BuildTls()
    {
        tlsSlots[engineTlsIndex] = fixtureTls;
        void* p = fixtureTable;
        std::memcpy(fixtureTls + 0x38, &p, 8);
        p = fixtureGlobals;
        std::memcpy(fixtureTls + 0x48, &p, 8);
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
    image.Pattern(0x35A9A4, kSelectionPattern);
    image.Edge(0x35B74C, 0x35A9A4);
    image.Edge(0x35E87E, 0x35A9A4);
    // RIP reference from the +0x35B265 binding to the collision flags global.
    const int32_t flagsRel = int32_t(0x2127108 - 0x35B26C);
    std::memcpy(image.bytes + 0x35B268, &flagsRel, 4);
    const uintptr_t base = image.Base();
    g_base = base;
    g_halo3WorldCollision.original = worldCollisionStub;

    // 1. Success: damage hook created+enabled, then the update hook, then melee selection; exact lines.
    ResetRuntime();
    Check(InstallHalo3ContactMelee(base, kSize, 7), "install succeeds on a matching image");
    const bool order =
        golden::calls.size() == 6 && golden::calls[0].op == Op::Create &&
        golden::calls[0].target == reinterpret_cast<void*>(base + 0x36FE00) &&
        golden::calls[0].detour == reinterpret_cast<void*>(&Halo3ContactDamageDetour) &&
        golden::calls[1].op == Op::Create && golden::calls[1].target == reinterpret_cast<void*>(base + 0x379B30) &&
        golden::calls[1].detour == reinterpret_cast<void*>(&Halo3ContactUpdateDetour) &&
        golden::calls[2].op == Op::Enable && golden::calls[2].target == reinterpret_cast<void*>(base + 0x36FE00) &&
        golden::calls[3].op == Op::Enable && golden::calls[3].target == reinterpret_cast<void*>(base + 0x379B30) &&
        golden::calls[4].op == Op::Create && golden::calls[4].target == reinterpret_cast<void*>(base + 0x35A9A4) &&
        golden::calls[4].detour == reinterpret_cast<void*>(&Halo3MeleeSelectionDetour) &&
        golden::calls[5].op == Op::Enable;
    Check(order, "z67 hook order: damage and update created, then both enabled, then melee selection create/enable");
    Check(golden::logs.size() == 2 && golden::logs[0] == kSelectionInstalled && golden::logs[1] == kInstalled,
        "exact lines: selection installed, then contact installed");
    Check(g_halo3Contact.enabled.load() && g_halo3Contact.generation == 7 && g_halo3Contact.base == base &&
              g_halo3Contact.flags == reinterpret_cast<const uint64_t*>(base + 0x2127108) &&
              g_halo3Contact.melee == reinterpret_cast<Halo3ContactMeleeFn>(base + 0x35AE34) &&
              g_halo3Contact.playback == reinterpret_cast<GameIsPlaybackFn>(base + 0xF01A0),
        "runtime bindings");
    Check(reinterpret_cast<const void*>(&Halo3ContactDamageDetour) !=
              reinterpret_cast<const void*>(&Halo3ContactUpdateDetour),
        "distinct detour addresses (no /OPT:ICF folding)");
    // Teardown verification (WaitForNativeDetourQuiescence / ResolveReachDetourCodeRange):
    // every detour must carry unwind data whose range covers its own entry.
    for (const void* detour : {reinterpret_cast<const void*>(&Halo3ContactUpdateDetour),
             reinterpret_cast<const void*>(&Halo3ContactDamageDetour)})
    {
        DWORD64 imageBase = 0;
        const auto* entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(detour), &imageBase, nullptr);
        const DWORD64 at = reinterpret_cast<DWORD64>(detour);
        Check(entry && entry->EndAddress > entry->BeginAddress && imageBase + entry->BeginAddress <= at &&
                  at < imageBase + entry->EndAddress,
            "detour has an unwind range covering its entry (teardown quiescence can verify it)");
    }
    golden::Reset();
    Check(!InstallHalo3ContactMelee(base, kSize, 7) && golden::calls.empty() && !g_halo3Contact.enabled.load(),
        "quirk: a second install clears enabled and refuses while hooks exist");

    // 2. Report: selection line then the contact line, counters reset (exchange).
    g_halo3Contact.enabled = true;
    g_halo3Contact.queries = 3;
    golden::Reset();
    ReportHalo3ContactMelee();
    Check(
        golden::logs.size() == 2 &&
            golden::logs[0] ==
                "Halo 3 physical melee selection: enabled=1 fault=0 secondary=0 unarmed=0 StockFallback=0" &&
            golden::logs[1] ==
                "Halo 3 contact melee: enabled=1 fault=0 queries=3 contacts=0 submissions(L/R)=0/0 nativeRejected=0 queueDrops=0 predictedRequests=0",
        "report: selection line, then contact line");
    golden::Reset();
    ReportHalo3ContactMelee();
    Check(golden::logs.size() == 2 && golden::logs[1].find("queries=0") != std::string::npos, "report counters reset");

    // 3. Apply through the native path: 25 rays, centre redirected, +3C/+48 impulse, exact target.
    BuildTls();
    g_halo3Contact.damageOriginal = FakeDamageOriginal;
    g_halo3Contact.melee = FakeMelee;
    g_halo3WorldCollision.original = reinterpret_cast<void*>(&FakeWorldCollision);
    Halo3ContactBackend backend{};
    backend.owner = kOwner;
    backend.hand = 0;
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
    std::memcpy(d0, lastEvent + 0x3C, 12);
    std::memcpy(d1, lastEvent + 0x48, 12);
    Check(damageCalls == 1 && damageTarget == kTarget && std::fabs(d0[0] - 0.6f) < 1e-6f &&
              std::fabs(d0[2] - 0.8f) < 1e-6f && std::memcmp(d0, d1, 12) == 0 && lastEvent[0x54] == 0xAB &&
              lastEvent[0x3B] == 0,
        "impulse: unit sweep direction written at +3C and +48 only");
    Check(g_halo3ContactScope.rays == 25 && !g_halo3ContactScope.active && g_halo3ContactScope.hand == 0,
        "25 native rays, scope closed, striking hand carried");
    redirectCentreHits = false;
    damageCalls = 0;
    Check(!backend.Apply(kOwner, hit, sweep) && damageCalls == 0,
        "a changed centre-ray target is rejected before damage");
    redirectCentreHits = true;
    fixtureGlobals[0] = 0;
    fixtureGlobals[1] = 0;
    Check(!backend.Apply(kOwner, hit, sweep), "no game globals: refused before the native call");
    fixtureGlobals[0] = 1;
    g_halo3WorldCollision.original = worldCollisionStub;

    // 4. Install failures, with Halo 3's retain-on-failure semantics.
    ResetRuntime();
    g_halo3WorldCollision.original = nullptr;
    Check(!InstallHalo3ContactMelee(base, kSize, 7) && golden::logs.empty() && golden::calls.empty(),
        "no world collision: silent");
    g_halo3WorldCollision.original = worldCollisionStub;
    for (const auto& b : kBindings)
    {
        ResetRuntime();
        const uint8_t saved = image.bytes[b.rva];
        image.bytes[b.rva] ^= 0xFF;
        char expected[128];
        snprintf(expected, sizeof(expected),
            "Halo 3 contact melee unavailable: native binding +0x%llX missing/ambiguous",
            static_cast<unsigned long long>(b.rva));
        Check(!InstallHalo3ContactMelee(base, kSize, 7) && golden::Logged(expected) && golden::calls.empty(),
            "binding refused with its line");
        image.bytes[b.rva] = saved;
    }
    for (const auto& e : kEdges)
    {
        ResetRuntime();
        // An edge inside a binding pattern is caught by that binding first.
        std::string expected = "Halo 3 contact melee unavailable: native caller verification failed";
        for (const auto& b : kBindings)
        {
            size_t length = 0;
            for (const char* q = b.pattern; *q; ++q)
                if (*q != ' ' && (q == b.pattern || q[-1] == ' '))
                    ++length;
            if (e[0] >= b.rva && e[0] < b.rva + length)
            {
                char text[128];
                snprintf(text, sizeof(text),
                    "Halo 3 contact melee unavailable: native binding +0x%llX missing/ambiguous",
                    static_cast<unsigned long long>(b.rva));
                expected = text;
            }
        }
        const uint8_t saved = image.bytes[e[0]];
        image.bytes[e[0]] = 0x90;
        Check(!InstallHalo3ContactMelee(base, kSize, 7) && golden::Logged(expected.c_str()) && golden::calls.empty(),
            "edge refused with its line");
        image.bytes[e[0]] = saved;
    }
    ResetRuntime();
    const int32_t wrong = flagsRel + 8;
    std::memcpy(image.bytes + 0x35B268, &wrong, 4);
    Check(!InstallHalo3ContactMelee(base, kSize, 7) && golden::logs.empty() && golden::calls.empty(),
        "flags reference mismatch: silent");
    std::memcpy(image.bytes + 0x35B268, &flagsRel, 4);

    // muzzle_hook_set.h order. Both hooks are created, then both
    // enabled; any failure logs one line and retires what was created.
    ResetRuntime();
    golden::failCreateAt = 0;
    Check(!InstallHalo3ContactMelee(base, kSize, 7) &&
              golden::Logged("Halo 3 contact melee unavailable: hook creation failed") && !g_halo3Contact.targets[0] &&
              !g_halo3Contact.targets[1] && golden::Count(Op::Remove) == 0,
        "z67: damage create failure: nothing created, nothing to remove");
    ResetRuntime();
    golden::failCreateAt = 1;
    Check(!InstallHalo3ContactMelee(base, kSize, 7) &&
              golden::Logged("Halo 3 contact melee unavailable: hook creation failed") && !g_halo3Contact.targets[0] &&
              !g_halo3Contact.targets[1] && golden::Count(Op::Remove) == 1 && !g_halo3Contact.damageOriginal,
        "z67: update create failure retires the damage hook (was: kept)");
    ResetRuntime();
    golden::failEnableAt = 0;
    Check(!InstallHalo3ContactMelee(base, kSize, 7) &&
              golden::Logged("Halo 3 contact melee unavailable: hook enable failed") &&
              golden::Count(Op::Create) == 2 && !g_halo3Contact.targets[0] && !g_halo3Contact.targets[1] &&
              golden::Count(Op::Remove) == 2,
        "z67: an enable failure retires both created hooks (was: kept for cleanup)");
    ResetRuntime();
    golden::failEnableAt = 1;
    Check(!InstallHalo3ContactMelee(base, kSize, 7) && !g_halo3Contact.enabled.load() && !g_halo3Contact.targets[0] &&
              !g_halo3Contact.targets[1] && golden::Count(Op::Remove) == 2,
        "z67: an update enable failure retires both hooks");
    {
        // The real retirement (LegacyContactRetire), not a re-implementation.
        const auto installed = [&]
        {
            ResetRuntime();
            return InstallHalo3ContactMelee(base, kSize, 7);
        };
        Check(installed(), "retire fixture: installed");
        golden::disableStatus = MH_ERROR_NOT_EXECUTABLE;
        Check(!LegacyContactRetire<Halo3ContactTraits>() &&
                  golden::Logged("Halo 3 contact cleanup: disable/range verification failed; retained hooks") &&
                  g_halo3Contact.targets[0] && g_halo3Contact.targets[1] && golden::Count(Op::Remove) == 0,
            "retire: a disable failure keeps both hooks");
        golden::disableStatus = MH_ERROR_NOT_CREATED;
        Check(LegacyContactRetire<Halo3ContactTraits>() && !g_halo3Contact.targets[0] && !g_halo3Contact.targets[1] &&
                  !g_halo3Contact.damageOriginal && !g_halo3Contact.updateOriginal,
            "retire: NOT_CREATED (module gone) counts as disabled; both removed");
        Check(installed(), "retire fixture: installed again");
        golden::quiescent = false;
        Check(!LegacyContactRetire<Halo3ContactTraits>() &&
                  golden::Logged("Halo 3 contact cleanup: ingress/callback verification failed; retained hooks") &&
                  g_halo3Contact.targets[0] && g_halo3Contact.targets[1] && golden::Count(Op::Remove) == 0,
            "retire: busy detours keep both hooks");
        golden::quiescent = true;
        Check(golden::quiescenceFunctions.back().size() == 2, "retire: both detours drained together");
        Check(installed(), "retire fixture: installed for the removal failure");
        golden::removeStatus = MH_ERROR_MEMORY_PROTECT;
        Check(!LegacyContactRetire<Halo3ContactTraits>() &&
                  golden::Logged("Halo 3 contact cleanup: MinHook removal failed; retained bookkeeping") &&
                  g_halo3Contact.targets[0] && g_halo3Contact.damageOriginal && g_halo3Contact.targets[1] &&
                  g_halo3Contact.updateOriginal && golden::Count(Op::Remove) == 1,
            "retire: removal stops at the first failure; that hook and the next keep target and trampoline");
        golden::removeStatus = MH_OK;
        Check(LegacyContactRetire<Halo3ContactTraits>() && !g_halo3Contact.targets[0] && !g_halo3Contact.targets[1] &&
                  !g_halo3Contact.damageOriginal && !g_halo3Contact.updateOriginal,
            "retire: the retry removes the rest");
    }
    ResetRuntime();
    golden::failCreateAt = 2;
    Check(InstallHalo3ContactMelee(base, kSize, 7) &&
              golden::Logged("Halo 3 melee selection StockFallback: hook creation failed") &&
              golden::Logged(kInstalled),
        "selection failure is optional: contact still installs");

    // 5. Publish: the packet a support-hand frame produces (pinned on the pre-fold code).
    ResetRuntime();
    InstallHalo3ContactMelee(base, kSize, 7);
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
        g_halo3Contact.queue[0].Reset();
        Halo3PublishContactHand(0, context, root, solved, points, 3, int32_t(kOwner), 7);
        ContactMeleePacket packet{};
        const bool got = g_halo3Contact.queue[0].Pop(packet);
        const auto& f = packet.frame;
        Check(got && f.serial == 41 && f.timeNs == 1234567 && f.unit == kOwner && f.count == 3 &&
                  f.shape == 0x087F3F0000006B0Dull && f.referenceEpoch == 0x261A8B72389999FBull &&
                  f.bareHandPoints == 3 && std::fabs(f.points[0].x + 0.2f) < 1e-6f &&
                  std::fabs(f.points[0].y - 0.3f) < 1e-6f && std::fabs(f.points[0].z + 0.1f) < 1e-6f &&
                  packet.generation == 7,
            "publish: support-hand packet identical to the pre-fold code (shape, epoch, points)");
    }
    snapshotOk = false;

    std::printf("Halo 3 contact melee runtime golden: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
