// Golden tests for the Halo 2 contact-melee lifecycle (src/dll/titles/h2/halo2_contact_melee_runtime.inl).
// Built as halo2_contact_melee_runtime_golden_tests with src/dll/core/sigscan.cpp and run by ctest.
// MinHook, the module image and the log are fakes from golden_hook_fakes.h; no game process.
//
// Written first against the older lifecycle code; the
// conversion onto muzzle_hook_set.h changed only the expectations whose Check
// messages are tagged for it, each with the base behaviour it replaced: create both hooks, then
// enable both; a failed install retires what it created (base: kept, so the
// retry hit "cleanup pending"); shared "hook creation/enable failed" lines;
// removal stops at the first failure (base: tried both).
// Doubles as the Halo 2 contact fixture for the shared contact-melee lifecycle.
#include "golden_hook_fakes.h"
#include <cmath>
#include <cstddef>
#include "../src/common/runtime_types.h"
#include "../src/common/feedback_cues.h"
#include "../src/common/titles/h2/halo2_contact_melee_logic.h"
#include "hook_checked_forwarders.h" // DLL hook sites call MinHook through the checked names
#include "../src/dll/core/contact_melee_queue.h"
#include "../src/common/contact_melee_drain.h"
#include "../src/dll/core/native_binding.h"
#include "../src/dll/core/sigscan.h"
#include "../src/dll/core/guard_count.h"

// ---- what halo2_observer_6dof.cpp defines above the runtime's include line ----
struct
{
    bool physical_melee = true, melee_fist = true;
    float physical_melee_swing_speed = 5.0f, physical_melee_cooldown_s = 0.06f;
    float gun_forward_m = 0, gun_right_m = 0, gun_up_m = 0, left_hand_forward_m = 0, gun_pitch_deg = 0, gun_yaw_deg = 0,
          gun_roll_deg = 0, barrel_pitch_deg = 0, barrel_yaw_deg = 0, barrel_roll_deg = 0,
          halo2_classic_gun_yaw_deg = 0, halo2_classic_gun_pitch_deg = 0;
} g_config;
inline float ClampPhysicalMeleeSwingSpeed(float v)
{
    return v;
}
namespace exclusive_input
{
    inline bool Active()
    {
        return false;
    }
}
std::atomic<uint32_t> g_generation{7};
std::atomic<uintptr_t> g_nativeAimOriginal{0x1234};
std::atomic<bool> g_vehicleSeatVerified{true};
struct Halo2CollisionResult
{
    int32_t type = 0;
    float fraction = 1.0f;
    float position[3]{};
    uint8_t remainderToObject[0x40 - 0x14]{};
    int32_t objectIndex = -1;
    uint8_t remainder[0x60 - 0x44]{};
};
static_assert(sizeof(Halo2CollisionResult) == 0x60);
using Halo2CollisionVectorFn = uint8_t(__fastcall*)(
    uint32_t, const float*, const float*, int32_t, int32_t, Halo2CollisionResult*);
struct
{
    Halo2CollisionVectorFn original = nullptr;
} g_halo2WorldCollision;
constexpr uint32_t kHalo2CollisionFlags = 0x2480000Fu;
// Only the fields the runtime reads (the host's struct carries many more).
struct Halo2VisibleConsumerContext
{
    uint32_t unitObject = UINT32_MAX, weaponObject = UINT32_MAX, secondaryWeaponObject = UINT32_MAX;
    bool twoHandAimActive = false;
    float rightScale = 1.0f, leftScale = 1.0f, worldScale = 1.0f;
    contact_melee::Frame contactFrames[2]{};
};
void* Halo2ObjectFromIndex(uint32_t) noexcept
{
    return nullptr;
}
bool Game_Halo2ControllerAimActive()
{
    return true;
}
bool Halo2Observer6Dof_DirectWeaponAimArmed()
{
    return true;
}
bool VR_IsBareFist(int)
{
    return false;
}
void VR_PulseContactHaptics(bool, float) {}
// Production scans region by region under SEH; the lifecycle only needs its count/first-match contract.
bool CountPatternMatches(uintptr_t base, size_t size, const char* pattern, uintptr_t& first, uint32_t& count) noexcept
{
    first = 0;
    count = 0;
    for (uintptr_t at = base; at < base + size;)
    {
        const uintptr_t hit = sig::Find(at, base + size - at, pattern);
        if (!hit)
            break;
        if (!count)
            first = hit;
        ++count;
        at = hit + 1;
    }
    return true;
}
uintptr_t mockReturnAddress = 0;
#define _ReturnAddress() reinterpret_cast<void*>(mockReturnAddress)

#include "../src/dll/titles/h2/halo2_contact_melee_runtime.inl"

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
        {0x8F3C80,
            "48 8B C4 44 88 40 18 89 50 10 89 48 08 55 53 41 57 48 8D A8 B8 FE FF FF 48 81 EC 30 02 00 00 48 89 70 20 45 33 FF"},
        {0x90F4B0,
            "48 83 EC 38 B8 FF FF FF FF 48 C7 44 24 28 00 00 00 00 44 8B C8 66 89 44 24 20 44 8B C0 E8 ?? ?? ?? ?? 48 83 C4 38 C3"},
        {0x8BD810,
            "40 55 57 41 54 41 55 41 56 48 83 EC 60 45 8B E1 4D 8B F0 48 63 EA 44 8B E9 E8 ?? ?? ?? ?? 48 8B F8 44 8B 50 10 41 83 EA 05"},
        {0x67AFEC,
            "89 7D 20 48 89 45 50 48 8B 44 24 58 89 75 24 C7 45 2C 40 74 40 64 48 C7 45 48 00 00 00 00 C6 45 29 00"},
        {0x8DD512, "E8 ?? ?? ?? ?? BA 00 00 10 00 48 89 05 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 32 DB E8 ?? ?? ?? ??"}};
    constexpr uintptr_t kEdges[][2]{{0x936995, 0x8F3C80}, {0x8F401F, 0x75B850}, {0x8F44CC, 0x8F4690},
        {0x8F4825, 0x90F4B0}, {0x90F4CD, 0x90EB80}, {0x8858B0, 0x8BD810}, {0x8DD512, 0x67B1D0}};
    constexpr uintptr_t kDamage = 0x90F4B0, kEvent = 0x8BD810;
    constexpr size_t kSize = 0xA00000;
    const char* kInstalled =
        "Halo 2 contact melee installed for Classic/Anniversary: physical segments, independent hands, native exact-target damage; headset verification pending";
    const char* kPending = "Halo 2 contact melee StockFallback: native dependencies unavailable or cleanup pending";

    size_t Length(const char* pattern)
    {
        size_t n = 0;
        for (const char* p = pattern; *p; ++p)
            if (*p != ' ' && (p == pattern || p[-1] == ' '))
                ++n;
        return n;
    }
    std::string BindingLine(uintptr_t rva)
    {
        char text[128];
        snprintf(text, sizeof(text), "Halo 2 contact melee StockFallback: binding +0x%llX missing/ambiguous",
            (unsigned long long)rva);
        return text;
    }
    // The production state after a completed removal (what a fresh process has).
    void Clear()
    {
        g_halo2Contact.enabled = false;
        g_halo2Contact.faulted = false;
        g_halo2Contact.callbacks = 0;
        g_halo2Contact.targets[0] = g_halo2Contact.targets[1] = nullptr;
        g_halo2Contact.damageOriginal = nullptr;
        g_halo2Contact.eventOriginal = nullptr;
        golden::Reset();
    }
    bool Target(size_t i, Op op, uintptr_t base, uintptr_t rva)
    {
        return i < golden::calls.size() && golden::calls[i].op == op &&
               golden::calls[i].target == reinterpret_cast<void*>(base + rva);
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
    // Data edge: the objects-array pointer referenced from the +0x8DD512 binding.
    const int32_t objectsRel = int32_t(intptr_t(kHalo2ObjectsDataArrayPointerRva) - intptr_t(0x8DD523));
    std::memcpy(image.bytes + 0x8DD51F, &objectsRel, 4);
    const uintptr_t base = image.Base();
    g_halo2WorldCollision.original = reinterpret_cast<Halo2CollisionVectorFn>(0x1);

    // 1. Success: damage create+enable, then native event create+enable; one line.
    Clear();
    Check(InstallHalo2ContactMelee(base, kSize, 7), "install succeeds on a matching image");
    Check(golden::calls.size() == 4 && Target(0, Op::Create, base, kDamage) &&
              golden::calls[0].detour == reinterpret_cast<void*>(&Halo2ContactDamageDetour) &&
              Target(1, Op::Create, base, kEvent) &&
              golden::calls[1].detour == reinterpret_cast<void*>(&Halo2ContactEventDetour) &&
              Target(2, Op::Enable, base, kDamage) && Target(3, Op::Enable, base, kEvent),
        "z69 order: both created, then both enabled (base: damage create/enable, then event create/enable)");
    Check(golden::logs.size() == 1 && golden::logs[0] == kInstalled, "exact installed line");
    Check(g_halo2Contact.enabled.load() && g_halo2Contact.generation == 7 && g_halo2Contact.base == base &&
              g_halo2Contact.melee == reinterpret_cast<Halo2ContactMeleeFn>(base + 0x8F3C80) &&
              g_halo2Contact.targets[0] == reinterpret_cast<void*>(base + kDamage) &&
              g_halo2Contact.targets[1] == reinterpret_cast<void*>(base + kEvent) && g_halo2Contact.damageOriginal &&
              g_halo2Contact.eventOriginal,
        "runtime bindings and trampolines");
    for (const void* f : {reinterpret_cast<const void*>(&Halo2ContactDamageDetour),
             reinterpret_cast<const void*>(&Halo2ContactEventDetour), reinterpret_cast<const void*>(&Halo2ContactTick)})
        Check(golden::HasUnwindRange(f), "detour/tick has an unwind range (quiescence can verify it)");
    golden::Reset();
    Check(!InstallHalo2ContactMelee(base, kSize, 7) && golden::Logged(kPending) && golden::calls.empty() &&
              !g_halo2Contact.enabled.load(),
        "a second install clears enabled and refuses with the cleanup-pending line");

    // 2. Report: exact line, counters reset (exchange).
    Clear();
    (void)InstallHalo2ContactMelee(base, kSize, 7);
    g_halo2Contact.queries = 3;
    g_halo2Contact.contacts = 2;
    g_halo2Contact.applied[1] = 1;
    golden::Reset();
    ReportHalo2ContactMelee();
    Check(
        golden::logs.size() == 1 &&
            golden::logs[0] ==
                "Halo 2 contact melee: enabled=1 fault=0 queries=3 contacts=2 nativeDamage(L/R)=0/1 nativeRequests(L/R)=0/0 nativeRejected=0 queueDrops=0",
        "report line");
    golden::Reset();
    ReportHalo2ContactMelee();
    Check(golden::logs.size() == 1 && golden::logs[0].find("queries=0 contacts=0") != std::string::npos,
        "report counters reset");

    // 3. Dependencies: each missing one gives the same line and no hook call.
    {
        Clear();
        g_halo2WorldCollision.original = nullptr;
        Check(!InstallHalo2ContactMelee(base, kSize, 7) && golden::Logged(kPending) && golden::calls.empty(),
            "no world collision: pending line");
        g_halo2WorldCollision.original = reinterpret_cast<Halo2CollisionVectorFn>(0x1);
        Clear();
        g_nativeAimOriginal = 0;
        Check(!InstallHalo2ContactMelee(base, kSize, 7) && golden::Logged(kPending) && golden::calls.empty(),
            "no native aim: pending line");
        g_nativeAimOriginal = 0x1234;
        Clear();
        g_vehicleSeatVerified = false;
        Check(!InstallHalo2ContactMelee(base, kSize, 7) && golden::Logged(kPending) && golden::calls.empty(),
            "seat not verified: pending line");
        g_vehicleSeatVerified = true;
    }

    // 4. Bindings and edges, each with its line and no hook call.
    for (const auto& b : kBindings)
    {
        Clear();
        const uint8_t saved = image.bytes[b.rva];
        image.bytes[b.rva] ^= 0xFF;
        Check(!InstallHalo2ContactMelee(base, kSize, 7) && golden::Logged(BindingLine(b.rva).c_str()) &&
                  golden::calls.empty(),
            "binding refused with its line");
        image.bytes[b.rva] = saved;
    }
    for (const auto& e : kEdges)
    {
        Clear();
        // An edge inside a binding pattern is caught by that binding first.
        std::string expected = "Halo 2 contact melee StockFallback: native call/data edge mismatch";
        for (const auto& b : kBindings)
            if (e[0] >= b.rva && e[0] < b.rva + Length(b.pattern))
                expected = BindingLine(b.rva);
        const uint8_t saved = image.bytes[e[0]];
        image.bytes[e[0]] = 0x90;
        Check(!InstallHalo2ContactMelee(base, kSize, 7) && golden::Logged(expected.c_str()) && golden::calls.empty(),
            "edge refused with its line");
        image.bytes[e[0]] = saved;
    }
    {
        Clear();
        const int32_t wrong = objectsRel + 8;
        std::memcpy(image.bytes + 0x8DD51F, &wrong, 4);
        Check(!InstallHalo2ContactMelee(base, kSize, 7) &&
                  golden::Logged("Halo 2 contact melee StockFallback: native call/data edge mismatch") &&
                  golden::calls.empty(),
            "objects-array data edge refused with the edge line");
        std::memcpy(image.bytes + 0x8DD51F, &objectsRel, 4);
    }

    // 5. Hook failures at each position. Now: one shared line per step, and the
    //    hooks already created are retired (base: kept "cleanup retained", so the
    //    retry was refused with the cleanup-pending line until a removal ran).
    struct Failure
    {
        int createAt, enableAt;
        const char* line;
        unsigned creates, enables, removes;
    };
    const Failure kFailures[]{{0, -1, "Halo 2 contact melee StockFallback: hook creation failed", 1, 0, 0},
        {1, -1, "Halo 2 contact melee StockFallback: hook creation failed", 2, 0, 1},
        {-1, 0, "Halo 2 contact melee StockFallback: hook enable failed", 2, 1, 2},
        {-1, 1, "Halo 2 contact melee StockFallback: hook enable failed", 2, 2, 2}};
    for (const auto& f : kFailures)
    {
        Clear();
        golden::failCreateAt = f.createAt;
        golden::failEnableAt = f.enableAt;
        Check(!InstallHalo2ContactMelee(base, kSize, 7) && golden::logs.size() == 1 && golden::logs[0] == f.line &&
                  !g_halo2Contact.enabled.load(),
            std::string("z69 failure line: ") + f.line);
        Check(golden::Count(Op::Create) == f.creates && golden::Count(Op::Enable) == f.enables &&
                  golden::Count(Op::Remove) == f.removes,
            "z69 failure: the hooks already created are removed");
        Check(!g_halo2Contact.targets[0] && !g_halo2Contact.targets[1] && !g_halo2Contact.damageOriginal &&
                  !g_halo2Contact.eventOriginal,
            "z69 failure: nothing is kept");
        golden::Reset();
        Check(InstallHalo2ContactMelee(base, kSize, 7) && golden::Logged(kInstalled),
            "z69 retry: installs straight away");
    }
    {
        // A failure whose clean-up cannot remove a hook keeps it (and the next) for the retry.
        Clear();
        golden::failEnableAt = 1;
        golden::removeStatus = MH_ERROR_MEMORY_PROTECT;
        Check(!InstallHalo2ContactMelee(base, kSize, 7) && golden::logs.size() == 2 &&
                  golden::logs[0] == "Halo 2 contact melee StockFallback: hook enable failed" &&
                  golden::logs[1] == "Halo 2 contact cleanup: removal failed; retained bookkeeping" &&
                  g_halo2Contact.targets[0] && g_halo2Contact.targets[1],
            "z69 failure with a failed removal: both kept, both lines");
        golden::Reset();
        Check(!InstallHalo2ContactMelee(base, kSize, 7) && golden::Logged(kPending) && golden::calls.empty(),
            "kept hooks: the retry is refused (pending)");
        golden::Reset();
        Check(RemoveHalo2ContactMelee() && !g_halo2Contact.targets[0] && !g_halo2Contact.targets[1],
            "the next removal clears them");
    }

    // 6. Retirement (RemoveHalo2ContactMelee).
    // Installed, with the recorder cleared so a case sees only its own calls and lines.
    const auto installed = [&]
    {
        Clear();
        const bool ok = InstallHalo2ContactMelee(base, kSize, 7);
        golden::Reset();
        return ok;
    };
    Check(installed(), "retire fixture: installed");
    golden::Reset();
    Check(RemoveHalo2ContactMelee() && !g_halo2Contact.enabled.load() && !g_halo2Contact.targets[0] &&
              !g_halo2Contact.targets[1] && !g_halo2Contact.damageOriginal && !g_halo2Contact.eventOriginal &&
              golden::logs.empty(),
        "retire: clean removal, no line");
    Check(golden::calls.size() == 4 && Target(0, Op::DisableRetire, base, kDamage) &&
              Target(1, Op::DisableRetire, base, kEvent) && Target(2, Op::Remove, base, kDamage) &&
              Target(3, Op::Remove, base, kEvent),
        "retire order: disable damage, event; remove damage, event");
    Check(golden::quiescenceFunctions.size() == 1 && golden::quiescenceFunctions[0].size() == 3 &&
              golden::quiescenceFunctions[0][0] == reinterpret_cast<const void*>(&Halo2ContactDamageDetour) &&
              golden::quiescenceFunctions[0][1] == reinterpret_cast<const void*>(&Halo2ContactEventDetour) &&
              golden::quiescenceFunctions[0][2] == reinterpret_cast<const void*>(&Halo2ContactTick),
        "retire: quiescence over both detours and the tick");
    golden::Reset();
    Check(RemoveHalo2ContactMelee() && golden::calls.empty() && golden::quiescenceFunctions.empty(),
        "retire with nothing installed: true, no calls");

    Check(installed(), "retire fixture: installed for MH_ERROR_DISABLED");
    golden::disableStatus = MH_ERROR_DISABLED;
    Check(RemoveHalo2ContactMelee() && !g_halo2Contact.targets[0] && !g_halo2Contact.targets[1],
        "retire: already-disabled counts as disabled");
    Check(installed(), "retire fixture: installed for MH_ERROR_NOT_CREATED");
    golden::disableStatus = MH_ERROR_NOT_CREATED;
    Check(RemoveHalo2ContactMelee() && !g_halo2Contact.targets[0] && !g_halo2Contact.targets[1],
        "z69 retire: an unmapped module (NOT_CREATED) counts as disabled (base: a failure)");

    Check(installed(), "retire fixture: installed for a disable failure");
    golden::disableStatus = MH_ERROR_NOT_EXECUTABLE;
    Check(!RemoveHalo2ContactMelee() && golden::logs.size() == 1 &&
              golden::logs[0] == "Halo 2 contact cleanup: disable/range verification failed; retained hooks" &&
              g_halo2Contact.targets[0] && g_halo2Contact.targets[1] && golden::Count(Op::Remove) == 0 &&
              golden::Count(Op::DisableRetire) == 1,
        "retire: a disable failure stops at the first hook and keeps both");
    golden::Reset();
    Check(RemoveHalo2ContactMelee() && !g_halo2Contact.targets[0] && !g_halo2Contact.targets[1],
        "retire: the retry removes both");

    Check(installed(), "retire fixture: installed for busy detours");
    golden::quiescent = false;
    Check(!RemoveHalo2ContactMelee() && golden::logs.size() == 1 &&
              golden::logs[0] == "Halo 2 contact cleanup: callbacks/ingress busy; retained hooks" &&
              g_halo2Contact.targets[0] && g_halo2Contact.targets[1] && golden::Count(Op::Remove) == 0,
        "retire: busy detours keep both hooks");
    golden::Reset();
    Check(RemoveHalo2ContactMelee() && !g_halo2Contact.targets[0] && !g_halo2Contact.targets[1],
        "retire: the retry removes both");

    Check(installed(), "retire fixture: installed for a removal failure");
    golden::removeStatus = MH_ERROR_MEMORY_PROTECT;
    Check(!RemoveHalo2ContactMelee() && golden::logs.size() == 1 &&
              golden::logs[0] == "Halo 2 contact cleanup: removal failed; retained bookkeeping" &&
              golden::Count(Op::Remove) == 1 && g_halo2Contact.targets[0] && g_halo2Contact.damageOriginal &&
              g_halo2Contact.targets[1] && g_halo2Contact.eventOriginal,
        "z69 retire: removal stops at the first failure and keeps both (base: tried both)");
    golden::Reset();
    Check(RemoveHalo2ContactMelee() && !g_halo2Contact.targets[0] && !g_halo2Contact.targets[1],
        "retire: the retry removes both");
    golden::Reset();
    Check(InstallHalo2ContactMelee(base, kSize, 7) && golden::Logged(kInstalled), "install again after the retries");

    std::printf("Halo 2 contact melee runtime golden: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
