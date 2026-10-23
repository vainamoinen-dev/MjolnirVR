// Golden tests for the Halo 4 contact-melee lifecycle (src/dll/titles/h4/halo4_contact_melee_runtime.inl).
// Built as halo4_contact_melee_runtime_golden_tests with src/dll/core/sigscan.cpp and run by ctest.
// MinHook, the module image and the log are fakes from golden_hook_fakes.h; no game process.
//
// Written first against the older lifecycle code; the
// conversion onto muzzle_hook_set.h changed only the expectations whose Check
// messages are tagged for it, each with the base behaviour it replaced.
// Base description: install preconditions (silent), binding/edge refusals
// and their lines, the flags-reference check (silent), hook order (damage
// create/enable, then simulation create/enable, then the BSP glass
// supplement), today's retain-on-failure, the silent refusal on retry while a
// hook is kept, RemoveHalo4ContactMelee (disable with "(status %d)",
// three-function quiescence, removal stopping at the first failure, all
// silent except the disable line), and the two report lines.
// Doubles as the Halo 4 contact fixture for the shared contact-melee lifecycle.
#include "golden_hook_fakes.h"
#include <cmath>
#include <cstddef>
#include <intrin.h>
#include "../src/common/runtime_types.h"
#include "../src/common/feedback_cues.h"
#include "hook_checked_forwarders.h" // DLL hook sites call MinHook through the checked names
#include "../src/dll/core/contact_melee_queue.h"
#include "../src/common/contact_melee_drain.h"
#include "../src/dll/core/native_binding.h"
#include "../src/dll/core/sigscan.h"
#include "../src/dll/core/guard_count.h"

// ---- what halo4_camera_core.cpp defines above the runtime's include line ----
struct
{
    bool physical_melee = true, melee_fist = true;
    float physical_melee_swing_speed = 5.0f, physical_melee_cooldown_s = 0.06f;
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
std::atomic<bool> g_enabled{true};
bool VR_IsStereoEnabled()
{
    return true;
}
GameTitle TitleAdapter_GetActiveTitle()
{
    return GameTitle::Halo4;
}
CinematicControlState ReadHalo4CinematicControl() noexcept
{
    return CinematicControlState::PlayerControlled;
}
bool VR_IsBareFist(int)
{
    return false;
}
void VR_PulseContactHaptics(bool, float) {}
struct
{
    std::atomic<bool> armed{true}, teardownRequested{false};
    std::atomic<uint32_t> generation{7};
} g_halo4Camera;
struct
{
    uint32_t generation = 7;
    contact_melee::Frame contactFrames[2]{};
    bool rightContactPublished = false;
} g_halo4FloatingPair;
uint32_t engineTlsIndex = 3;
uint32_t* g_halo4EngineTlsIndex = &engineTlsIndex;
#pragma pack(push, 1)
struct Halo4PhysicsRayCastInput
{
    uint32_t profile = 0, collisionFlags = 0, additionalFlags = 0;
    float start[3]{}, end[3]{};
    int32_t ignoredObjects[64]{};
    uint8_t ignoredObjectCount = 0, padding[3]{}, resultOptions[4]{};
};
struct Halo4PhysicsRayCastResult
{
    uint32_t type = 0;
    float fraction = 0.0f, position[3]{}, normal[3]{};
    uint8_t unknown20[8]{};
    int32_t objectIndex = -1;
    uint8_t remaining[0x70 - 0x2C]{};
};
#pragma pack(pop)
static_assert(sizeof(Halo4PhysicsRayCastInput) == 0x12C && sizeof(Halo4PhysicsRayCastResult) == 0x70);
struct
{
    bool(__fastcall* originalRayCast)(Halo4PhysicsRayCastInput*, Halo4PhysicsRayCastResult*) = nullptr;
} g_halo4WorldCollision;
bool __fastcall StubRayCast(Halo4PhysicsRayCastInput*, Halo4PhysicsRayCastResult*)
{
    return false;
}

#include "../src/dll/titles/h4/halo4_contact_melee_runtime.inl"

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
    // Contact bindings and edges (InstallHalo4ContactMelee).
    const Row kBindings[]{
        {0x5ECCB4,
            "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC C0 00 00 00 48 8B 05 32 E3 84 00 48 33 C4 48 89 45 27 48 8B F1 48 89 4D D7 E8 57 02 04 00 8B 5E F4 44 8A E0"},
        {0x601B1C,
            "48 8B C4 48 89 58 20 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 38 FD FF FF 48 81 EC 90 03 00 00 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 98 44 0F 29 48 88 44 0F 29 90 78 FF FF FF 44 0F 29 98 68 FF"},
        {0x60296C,
            "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 F1 48 81 EC D0 00 00 00 48 8B 05 81 86 83 00 48 33 C4 48 89 45 07 48 8B 45 7F 45 33 FF 48 8B 5D 77"},
        {0x603330,
            "48 8B C4 48 89 58 10 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 D8 FD FF FF 48 81 EC F0 02 00 00 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 98 44 0F 29 48 88 48 8B 05"},
        {0x5DA400,
            "48 83 EC 28 44 8B 05 0D CE A7 00 44 8B DA 65 48 8B 04 25 58 00 00 00 8B D1 41 B9 18 00 00 00 45 33 D2 4A 8B 04 C0 49 8B 0C 01 E8 A9 96 A6 FF 48 8B C8 48 85 C0 74 0E 0F B6 40 04 41 0F A3 C3 73 04 4C 8B 51 10 49 8B C2"},
        {0x43AD8,
            "45 33 C0 83 FA FF 74 25 0F B7 C2 3B 41 44 7D 1D 4C 8B 49 20 4C 0F AF C8 4C 03 49 50 66 45 39 01 74 0B C1 FA 10 66 41 39 11 4D 0F 44 C1 49 8B C0 C3"},
        {0x9C62C,
            "48 83 EC 28 E8 CF F7 FF FF 33 C9 84 C0 74 22 8B 0D D7 AB FB 00 65 48 8B 04 25 58 00 00 00 BA 40 00 00 00 48 8B 04 C8 48 8B 0C 10 8B 89 00 CE 01 00 8B C1 48 83 C4 28 C3"},
        {0x9C5A0,
            "48 83 EC 28 E8 5B F8 FF FF 33 C9 84 C0 74 1F 8B 0D 63 AC FB 00 65 48 8B 04 25 58 00 00 00 BA 40 00 00 00 48 8B 04 C8 48 8B 0C 10 8B 49 1C 8B C1 48 83 C4 28 C3"},
        {0x601EE4, "48 8B 05 AD 91 9F 02 41 BE FE FF FF FF F3 0F 10 25 63 35 79 00 F3 44 0F"},
        {0x602580,
            "F2 0F 10 45 E8 8B 45 00 89 43 14 8B 45 F0 F2 41 0F 11 07 F2 0F 10 45 F4 41 89 47 08 8B 45 FC F2 0F 11 43 40 89 43 48 0F B7 45 18 66 89 43 20"},
        {0x603740,
            "66 44 3B 43 20 74 26 8B 43 28 44 0F BF 4B 22 44 0F BF 43 20 8B 53 24 8B 4B 18 89 44 24 28 48 8D 44 24 70 48 89 44 24 20"}};
    constexpr uintptr_t kEdges[][2]{{0x6020E5, 0x1C1D4C}, {0x603768, 0x21E908}, {0x6026D4, 0x1C1D4C},
        {0x602DD3, 0x601120}, {0x602E10, 0x601120}, {0x6012ED, 0x603330}, {0x601310, 0x603330}, {0x6013C4, 0x603330},
        {0x6013F8, 0x603330}, {0x601930, 0x603330}, {0x5DA42A, 0x43AD8}, {0x9C630, 0x9BE04}, {0x9C5A4, 0x9BE04},
        {0x5ECCEC, 0x62CF48}};
    // BSP glass supplement bindings and edges (InstallHalo4ContactBsp).
    const Row kBspBindings[]{
        {0x21F7B4,
            "40 53 48 83 EC 20 48 8B 05 6F 82 74 04 4C 8D 15 B8 A9 74 04 48 8B D9 44 8B 80 A8 00 00 00 48 63 C2 48 6B D0 54 49 03 D0 49 C1 E8 1C 4B 8B 04 C2"},
        {0x2DE9FC,
            "40 55 48 8B EC 48 83 EC 30 F3 0F 10 6D 50 0F 57 E4 0F 29 74 24 20 4C 8B D1 0F 28 F5 0F 29 7C 24 10 F3 0F 5C F4 0F 2F F4 72 05 0F 28 FD EB 03 0F"},
        {0x2DEF10,
            "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC C0 00 00 00 0F 29 70 C8 4C 8D 15 42 B2 68 04 8B C2"},
        {0x2DF698,
            "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC C0 00 00 00 33 FF 0F 29 70 C8 4C 8D 15 B8 AA 68 04"},
        {0x2DEC78,
            "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57 48 8D 68 B1 48 81 EC A0 00 00 00 4C 8B 7D 7F 41 8B F1 0F 29 70 C8 8B FA 4C 8B F1 41 BA 20 00 00 00 49 8B 86 E8 00 00 00 4C 8D 0D C1 B4 68 04 48 63 CF 48 C1 E1 05 8B 50"},
        {0x2DF400,
            "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57 48 8D 68 B1 48 81 EC A0 00 00 00 4C 8B 7D 7F 41 8B F1 0F 29 70 C8 8B FA 4C 8B F1 41 BA 20 00 00 00 49 8B 86 E8 00 00 00 4C 8D 0D 39 AD 68 04 48 63 CF 48 C1 E1 05 8B 50"},
        {0x21DFF8,
            "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 41 8B F9 8B D9 49 63 F0 E8 80 FC FF FF 45 33 D2 4C 8B D8 48 85 C0 74 31 4C 6B C6 38 4D 03 D8 74 28"},
        {0xDEA60,
            "48 83 EC 28 4C 8B D9 48 8D 8A 88 05 00 00 33 D2 39 51 1C 74 06 83 79 0C FF EB 02 39 11 0F 95 C0 84 C0 74 12 E8 9B 59 F6 FF 48 8B D0 49 8B CB E8"}};
    constexpr uintptr_t kBspEdges[][2]{{0x21F814, 0xDEA60}, {0x24F631, 0x2DE9FC}, {0x24F675, 0x2DE9FC},
        {0x24F657, 0x2DEC78}, {0x24F663, 0x2DEF10}, {0x24F69E, 0x2DF400}, {0x24F6AA, 0x2DF698}, {0x21E95A, 0x21DFF8}};
    constexpr uintptr_t kDamage = 0x603330, kUpdate = 0x5ECCB4, kFlags = 0x601EE7;
    constexpr size_t kSize = 0x3000000; // the flags global is at +0x2FFB098
    const char* kInstalled =
        "Halo 4 contact melee installed: independent physical hand/weapon sweeps, exact-target native damage; headset verification pending";
    const char* kBspInstalled =
        "Halo 4 BSP glass supplement installed: exact native surface/physical segment, authored validity and damage; headset verification pending";

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
        char t[128];
        snprintf(t, sizeof(t), "Halo 4 contact melee unavailable: native binding +0x%llX missing/ambiguous",
            (unsigned long long)rva);
        return t;
    }
    std::string BspBindingLine(uintptr_t rva)
    {
        char t[160];
        snprintf(t, sizeof(t),
            "Halo 4 BSP glass supplement unavailable: own binding +0x%llX missing/ambiguous; material contact retained",
            (unsigned long long)rva);
        return t;
    }
    void Clear()
    {
        g_halo4Contact.enabled = false;
        g_halo4Contact.faulted = false;
        g_halo4Contact.callbacks = 0;
        g_halo4Contact.targets[0] = g_halo4Contact.targets[1] = nullptr;
        g_halo4Contact.updateOriginal = nullptr;
        g_halo4Contact.damageOriginal = nullptr;
        g_halo4ContactBsp.enabled = false;
        g_halo4ContactBsp.faulted = false;
        golden::Reset();
    }
    bool Target(size_t i, Op op, uintptr_t base, uintptr_t rva)
    {
        return i < golden::calls.size() && golden::calls[i].op == op &&
               golden::calls[i].target == reinterpret_cast<void*>(base + rva);
    }
    std::string DisableLine(MH_STATUS s)
    {
        char t[96];
        snprintf(
            t, sizeof(t), "Halo 4 contact cleanup: disable failed (status %d); retained hooks", static_cast<int>(s));
        return t;
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
    for (const auto& b : kBspBindings)
        image.Pattern(b.rva, b.pattern);
    for (const auto& e : kBspEdges)
        image.Edge(e[0], e[1]);
    const uintptr_t base = image.Base();
    g_halo4WorldCollision.originalRayCast = StubRayCast;

    // 1. Success: damage create+enable, simulation create+enable, then the BSP
    //    supplement; BSP line, then the contact line.
    Clear();
    Check(InstallHalo4ContactMelee(base, kSize, 7), "install succeeds on a matching image");
    Check(golden::calls.size() == 4 && Target(0, Op::Create, base, kDamage) &&
              golden::calls[0].detour == reinterpret_cast<void*>(&Halo4ContactDamageDetour) &&
              Target(1, Op::Create, base, kUpdate) &&
              golden::calls[1].detour == reinterpret_cast<void*>(&Halo4ContactUpdateDetour) &&
              Target(2, Op::Enable, base, kDamage) && Target(3, Op::Enable, base, kUpdate),
        "z69 order: both created, then both enabled (base: damage create/enable, then simulation create/enable)");
    Check(golden::logs.size() == 2 && golden::logs[0] == kBspInstalled && golden::logs[1] == kInstalled,
        "exact lines: BSP installed, then contact installed");
    Check(g_halo4Contact.enabled.load() && g_halo4ContactBsp.enabled.load() && g_halo4Contact.generation == 7 &&
              g_halo4Contact.base == base &&
              g_halo4Contact.flags == reinterpret_cast<const uint64_t*>(base + 0x2FFB098) &&
              g_halo4Contact.build == reinterpret_cast<Halo4ContactBuildFn>(base + 0x601B1C) &&
              g_halo4Contact.consume == reinterpret_cast<Halo4ContactConsumeFn>(base + 0x60296C) &&
              g_halo4Contact.object == reinterpret_cast<Halo4ContactObjectFn>(base + 0x5DA400) &&
              g_halo4Contact.targets[0] == reinterpret_cast<void*>(base + kDamage) &&
              g_halo4Contact.targets[1] == reinterpret_cast<void*>(base + kUpdate) && g_halo4Contact.damageOriginal &&
              g_halo4Contact.updateOriginal && g_halo4Contact.owner.load() == UINT32_MAX,
        "runtime bindings");
    for (const void* f :
        {reinterpret_cast<const void*>(&Halo4ContactUpdateDetour),
            reinterpret_cast<const void*>(&Halo4ContactDamageDetour), reinterpret_cast<const void*>(&Halo4ContactTick)})
        Check(golden::HasUnwindRange(f), "detour/tick has an unwind range (quiescence can verify it)");
    golden::Reset();
    Check(!InstallHalo4ContactMelee(base, kSize, 7) && golden::logs.empty() && golden::calls.empty() &&
              !g_halo4Contact.enabled.load(),
        "a second install clears enabled and refuses silently while hooks exist");

    // 2. Report: BSP line, then the contact line, counters reset; nothing when no hook is set.
    Clear();
    (void)InstallHalo4ContactMelee(base, kSize, 7);
    g_halo4Contact.queries = 4;
    g_halo4Contact.submitted[0] = 1;
    golden::Reset();
    ReportHalo4ContactMelee();
    Check(
        golden::logs.size() == 2 && golden::logs[0] == "Halo 4 BSP glass supplement: enabled=1 fault=0" &&
            golden::logs[1] ==
                "Halo 4 contact melee: enabled=1 fault=0 queries=4 contacts=0 submissions(L/R)=1/0 nativeRejected=0 queueDrops=0 predictedAttempts=0",
        "report: BSP line, then the contact line");
    golden::Reset();
    ReportHalo4ContactMelee();
    Check(golden::logs.size() == 2 && golden::logs[1].find("queries=0") != std::string::npos, "report counters reset");
    Clear();
    ReportHalo4ContactMelee();
    Check(golden::logs.empty(), "report with no hook: no lines");

    // 3. Preconditions: silent.
    Clear();
    g_halo4WorldCollision.originalRayCast = nullptr;
    Check(!InstallHalo4ContactMelee(base, kSize, 7) && golden::logs.empty() && golden::calls.empty(),
        "no world collision: silent");
    g_halo4WorldCollision.originalRayCast = StubRayCast;
    Clear();
    g_halo4EngineTlsIndex = nullptr;
    Check(!InstallHalo4ContactMelee(base, kSize, 7) && golden::logs.empty() && golden::calls.empty(),
        "no engine TLS index: silent");
    g_halo4EngineTlsIndex = &engineTlsIndex;

    // 4. Bindings, edges and the flags reference.
    for (const auto& b : kBindings)
    {
        Clear();
        const uint8_t saved = image.bytes[b.rva];
        image.bytes[b.rva] ^= 0xFF;
        Check(!InstallHalo4ContactMelee(base, kSize, 7) && golden::logs.size() == 1 &&
                  golden::logs[0] == BindingLine(b.rva) && golden::calls.empty(),
            "binding refused with its line");
        image.bytes[b.rva] = saved;
    }
    for (const auto& e : kEdges)
    {
        Clear();
        std::string expected = "Halo 4 contact melee unavailable: native call-edge verification failed";
        for (const auto& b : kBindings)
            if (e[0] >= b.rva && e[0] < b.rva + Length(b.pattern))
                expected = BindingLine(b.rva);
        const uint8_t saved = image.bytes[e[0]];
        image.bytes[e[0]] = 0x90;
        Check(!InstallHalo4ContactMelee(base, kSize, 7) && golden::logs.size() == 1 && golden::logs[0] == expected &&
                  golden::calls.empty(),
            "edge refused with its line");
        image.bytes[e[0]] = saved;
    }
    {
        // The flags RIP reference sits inside the +0x601EE4 binding; with a
        // wildcard-free pattern a changed reference fails the binding first.
        Clear();
        const uint8_t saved = image.bytes[kFlags];
        image.bytes[kFlags] ^= 0x08;
        Check(!InstallHalo4ContactMelee(base, kSize, 7) && golden::logs.size() == 1 &&
                  golden::logs[0] == BindingLine(0x601EE4) && golden::calls.empty(),
            "changed flags reference: refused by its binding");
        image.bytes[kFlags] = saved;
    }

    // 5. BSP supplement failures are optional: the contact still installs.
    for (const auto& b : kBspBindings)
    {
        Clear();
        const uint8_t saved = image.bytes[b.rva];
        image.bytes[b.rva] ^= 0xFF;
        Check(InstallHalo4ContactMelee(base, kSize, 7) && golden::logs.size() == 2 &&
                  golden::logs[0] == BspBindingLine(b.rva) && golden::logs[1] == kInstalled &&
                  !g_halo4ContactBsp.enabled.load(),
            "BSP binding failure: own line, contact still installs");
        image.bytes[b.rva] = saved;
    }
    for (const auto& e : kBspEdges)
    {
        Clear();
        std::string expected =
            "Halo 4 BSP glass supplement unavailable: own call-edge proof failed; material contact retained";
        for (const auto& b : kBspBindings)
            if (e[0] >= b.rva && e[0] < b.rva + Length(b.pattern))
                expected = BspBindingLine(b.rva);
        const uint8_t saved = image.bytes[e[0]];
        image.bytes[e[0]] = 0x90;
        Check(InstallHalo4ContactMelee(base, kSize, 7) && golden::logs.size() == 2 && golden::logs[0] == expected &&
                  golden::logs[1] == kInstalled && !g_halo4ContactBsp.enabled.load(),
            "BSP edge failure: own line, contact still installs");
        image.bytes[e[0]] = saved;
    }

    // 6. Hook failures at each position. Now: one shared line per step, and the
    //    hooks already created are retired (base: kept "cleanup retained", and the
    //    retry was refused silently until a removal ran).
    struct Failure
    {
        int createAt, enableAt;
        const char* line;
        unsigned creates, enables, removes;
    };
    const Failure kFailures[]{{0, -1, "Halo 4 contact melee unavailable: hook creation failed", 1, 0, 0},
        {1, -1, "Halo 4 contact melee unavailable: hook creation failed", 2, 0, 1},
        {-1, 0, "Halo 4 contact melee unavailable: hook enable failed", 2, 1, 2},
        {-1, 1, "Halo 4 contact melee unavailable: hook enable failed", 2, 2, 2}};
    for (const auto& f : kFailures)
    {
        Clear();
        golden::failCreateAt = f.createAt;
        golden::failEnableAt = f.enableAt;
        Check(!InstallHalo4ContactMelee(base, kSize, 7) && golden::logs.size() == 1 && golden::logs[0] == f.line &&
                  !g_halo4Contact.enabled.load() && !g_halo4ContactBsp.enabled.load(),
            std::string("z69 failure line: ") + f.line);
        Check(golden::Count(Op::Create) == f.creates && golden::Count(Op::Enable) == f.enables &&
                  golden::Count(Op::Remove) == f.removes,
            "z69 failure: the hooks already created are removed, no BSP supplement");
        Check(!g_halo4Contact.targets[0] && !g_halo4Contact.targets[1] && !g_halo4Contact.damageOriginal &&
                  !g_halo4Contact.updateOriginal,
            "z69 failure: nothing is kept");
        golden::Reset();
        Check(InstallHalo4ContactMelee(base, kSize, 7) && golden::Logged(kInstalled),
            "z69 retry: installs straight away");
    }
    {
        // A failure whose clean-up cannot disable a hook keeps both, with the status line.
        Clear();
        golden::failEnableAt = 1;
        golden::disableStatus = MH_ERROR_NOT_EXECUTABLE;
        Check(!InstallHalo4ContactMelee(base, kSize, 7) && golden::logs.size() == 2 &&
                  golden::logs[0] == "Halo 4 contact melee unavailable: hook enable failed" &&
                  golden::logs[1] == DisableLine(MH_ERROR_NOT_EXECUTABLE) && g_halo4Contact.targets[0] &&
                  g_halo4Contact.targets[1],
            "z69 failure with a failed disable: both kept, the status line follows");
        golden::Reset();
        Check(!InstallHalo4ContactMelee(base, kSize, 7) && golden::logs.empty() && golden::calls.empty(),
            "kept hooks: the retry is refused silently");
        golden::Reset();
        Check(RemoveHalo4ContactMelee() && !g_halo4Contact.targets[0] && !g_halo4Contact.targets[1],
            "the next removal clears them");
    }

    // 7. Retirement (RemoveHalo4ContactMelee).
    // Installed, with the recorder cleared so a case sees only its own calls and lines.
    const auto installed = [&]
    {
        Clear();
        const bool ok = InstallHalo4ContactMelee(base, kSize, 7);
        golden::Reset();
        return ok;
    };
    Check(installed(), "retire fixture: installed");
    g_halo4Contact.owner = 0x12340001;
    golden::Reset();
    Check(RemoveHalo4ContactMelee() && !g_halo4Contact.enabled.load() && !g_halo4ContactBsp.enabled.load() &&
              !g_halo4Contact.targets[0] && !g_halo4Contact.targets[1] && !g_halo4Contact.damageOriginal &&
              !g_halo4Contact.updateOriginal && g_halo4Contact.owner.load() == UINT32_MAX && golden::logs.empty(),
        "retire: clean removal, no line, owner reset");
    Check(golden::calls.size() == 4 && Target(0, Op::DisableRetire, base, kDamage) &&
              Target(1, Op::DisableRetire, base, kUpdate) && Target(2, Op::Remove, base, kDamage) &&
              Target(3, Op::Remove, base, kUpdate),
        "z69 retire order: install order, damage first (base: simulation first)");
    Check(golden::quiescenceFunctions.size() == 1 && golden::quiescenceFunctions[0].size() == 3 &&
              golden::quiescenceFunctions[0][0] == reinterpret_cast<const void*>(&Halo4ContactDamageDetour) &&
              golden::quiescenceFunctions[0][1] == reinterpret_cast<const void*>(&Halo4ContactUpdateDetour) &&
              golden::quiescenceFunctions[0][2] == reinterpret_cast<const void*>(&Halo4ContactTick),
        "retire: quiescence over both detours and the tick");
    golden::Reset();
    Check(RemoveHalo4ContactMelee() && golden::calls.empty() && golden::quiescenceFunctions.empty(),
        "retire with nothing installed: true, no calls");

    for (MH_STATUS ok : {MH_ERROR_DISABLED, MH_ERROR_NOT_CREATED})
    {
        Check(installed(), "retire fixture: installed for an accepted disable status");
        golden::disableStatus = ok;
        Check(RemoveHalo4ContactMelee() && !g_halo4Contact.targets[0] && !g_halo4Contact.targets[1],
            "retire: DISABLED / NOT_CREATED count as disabled");
    }

    Check(installed(), "retire fixture: installed for a disable failure");
    golden::disableStatus = MH_ERROR_NOT_EXECUTABLE;
    Check(!RemoveHalo4ContactMelee() && golden::logs.size() == 1 &&
              golden::logs[0] == DisableLine(MH_ERROR_NOT_EXECUTABLE) && g_halo4Contact.targets[0] &&
              g_halo4Contact.targets[1] && golden::Count(Op::Remove) == 0 && golden::Count(Op::DisableRetire) == 1,
        "retire: a disable failure logs its status, stops at the first hook and keeps both");
    golden::Reset();
    Check(RemoveHalo4ContactMelee() && !g_halo4Contact.targets[0] && !g_halo4Contact.targets[1],
        "retire: the retry removes both");

    Check(installed(), "retire fixture: installed for busy detours");
    golden::quiescent = false;
    Check(!RemoveHalo4ContactMelee() && golden::logs.size() == 1 &&
              golden::logs[0] == "Halo 4 contact cleanup: callbacks/ingress busy; retained hooks" &&
              g_halo4Contact.targets[0] && g_halo4Contact.targets[1] && golden::Count(Op::Remove) == 0,
        "z69 retire: busy detours keep both hooks, with a line (base: silent)");
    golden::Reset();
    Check(RemoveHalo4ContactMelee() && !g_halo4Contact.targets[0] && !g_halo4Contact.targets[1],
        "retire: the retry removes both");

    Check(installed(), "retire fixture: installed for a removal failure");
    golden::removeStatus = MH_ERROR_MEMORY_PROTECT;
    Check(!RemoveHalo4ContactMelee() && golden::logs.size() == 1 &&
              golden::logs[0] == "Halo 4 contact cleanup: MinHook removal failed; retained bookkeeping" &&
              golden::Count(Op::Remove) == 1 && g_halo4Contact.targets[1] && g_halo4Contact.updateOriginal &&
              g_halo4Contact.targets[0] && g_halo4Contact.damageOriginal,
        "z69 retire: removal stops at the first failure with a line (base: silent); both hooks keep target and trampoline");
    golden::Reset();
    Check(RemoveHalo4ContactMelee() && !g_halo4Contact.targets[0] && !g_halo4Contact.targets[1],
        "retire: the retry removes both");
    Check(installed(), "retire fixture: installed for NOT_CREATED on removal");
    golden::removeStatus = MH_ERROR_NOT_CREATED;
    Check(RemoveHalo4ContactMelee() && !g_halo4Contact.targets[0] && !g_halo4Contact.targets[1],
        "retire: NOT_CREATED on removal counts as removed");
    golden::Reset();
    Check(InstallHalo4ContactMelee(base, kSize, 7) && golden::Logged(kInstalled), "install again after the retries");

    std::printf("Halo 4 contact melee runtime golden: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
