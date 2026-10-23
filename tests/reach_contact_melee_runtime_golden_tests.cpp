// Golden fixture: pins the Reach contact-melee install and retire (reach_contact_melee_runtime.inl),
// compiled with sigscan.cpp against fake bindings, a fake image and the golden_hook_fakes.h MinHook
// fixtures; built as reach_contact_melee_runtime_golden_tests. No real hook is installed.
//
// Pinned: silent preconditions, binding/edge refusals and their lines, hook order
// (damage-direction create/enable, then simulation create/enable), one shared line per failed
// step with the hooks already created retired the way the camera teardown retires them,
// and the report line.
// RemoveReachContactMelee is also the normal retire: RemoveReachCameraCore calls it
// before the camera table (tools/check_reach_contact_retire_site.py pins that side),
// and the retire cases below pin it here.
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

// ---- what game.cpp defines above the runtime's include line ----
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
std::atomic<bool> g_enabled{true}, g_reachCinematicLocked{false};
bool VR_IsStereoEnabled()
{
    return true;
}
bool VR_IsBareFist(int)
{
    return false;
}
void VR_PulseContactHaptics(bool, float) {}
inline constexpr uintptr_t kReachEngineTlsIndexRva = 0x00C17B18;
struct
{
    uintptr_t base = 0;
    std::atomic<bool> armed{true};
    std::atomic<uint32_t> generation{7}, activeCallbacks{0};
    bool(__fastcall* unitInVehicle)(int32_t) = nullptr;
} g_reachCamera;
using LegacyCollisionTestVectorFn = uint8_t(__fastcall*)(
    uint64_t, int32_t, const float*, const float*, int32_t, int32_t, int32_t, void*);
struct
{
    void* original = nullptr;
} g_reachWorldCollision;
struct
{
    bool secondaryWeaponPresent = false;
} g_reachFpPairScope;
const void* ReachVehicleObjectData(int32_t, uint8_t& kind)
{
    kind = 0;
    return nullptr;
}
int32_t LegacyCollisionIgnoredObject(GameTitle)
{
    return -1;
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
struct FpExplicitPoseTargets
{
    int64_t contactTimeNs = 0;
    uint64_t contactSerial = 0, contactReference = 0;
    decltype(contact_melee::Frame::transform) contactSpace[2]{};
    decltype(contact_melee::Frame::controllerPose) contactController[2]{};
    float collisionCorrection[2][3]{};
};
constexpr int kLegacyCollisionWeaponSamples = 14;
bool LegacyBuildMappedWeaponBounds(
    GameTitle, const FpInterpolationContext&, const BoneMatrix&, const BoneMatrix*, float (*)[3], uint64_t&)
{
    return false;
}
bool ReachVerifyRel32Call(uintptr_t base, uintptr_t siteRva, uintptr_t expectedTargetRva)
{
    const uint8_t* p = reinterpret_cast<const uint8_t*>(base + siteRva);
    if (p[0] != 0xE8)
        return false;
    int32_t rel = 0;
    std::memcpy(&rel, p + 1, sizeof(rel));
    return base + siteRva + 5 + static_cast<uintptr_t>(static_cast<intptr_t>(rel)) == base + expectedTargetRva;
}

#include "../src/dll/titles/reach/reach_contact_melee_runtime.inl"

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
        {0x49F138,
            "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 54 41 56 41 57 48 83 EC 20 8B 15 ?? ?? ?? ?? 40 32 FF"},
        {0x4919D4,
            "48 8B C4 48 89 58 20 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 A8 FE FF FF 48 81 EC 20 02 00 00 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 98 44 0F 29 48 88 44 0F 29 90 78 FF FF FF 44 0F 29 98 68 FF FF FF 44 0F 29 A0 58 FF FF FF 44 0F 29 A8 48 FF FF FF 44 0F 29 B0 38 FF FF FF 44 0F 29 B8 28 FF FF FF 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 78 65 48 8B 04 25 58 00 00 00 49 8B D8"},
        {0x4924F0,
            "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 F1 48 81 EC C0 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 07"},
        {0x58EEC,
            "48 83 EC 28 E8 ?? ?? ?? ?? 32 D2 84 C0 74 27 8B 0D ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 41 B8 48 00 00 00 48 8B 04 C8 4A 8B 0C 00 38 91 DA 01 00 00 74 02 B2 01 8A C2 48 83 C4 28 C3"},
        {0x492D68,
            "48 8B C4 48 89 58 10 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 E8 FD FF FF 48 81 EC E0 02 00 00 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 98 44 0F 29 48 88"},
        {0x492234,
            "F6 45 FD 08 8B 45 EC 89 43 14 8B 45 B0 F2 0F 11 43 34 F2 0F 10 45 CC 89 43 3C 8B 45 D4 F2 0F 11 43 40 89 43 48"},
        {0x492268, "0F B6 45 02 89 43 18 0F B6 45 FE 89 43 1C 8B 45 DC 89 43 28 8B 45 F4 89 43 2C"},
        {0x4931D8,
            "66 44 3B 6B 20 74 26 8B 43 28 44 0F BF 4B 22 44 0F BF 43 20 8B 53 24 8B 4B 18 89 44 24 28 48 8D 44 24 70 48 89 44 24 20"}};
    constexpr uintptr_t kEdges[][2]{{0x4A04D0, 0x49F138}, {0x50BA00, 0x4919D4}, {0x50BCA7, 0x4924F0},
        {0x491E89, 0x12969C}, {0x58EF0, 0x588E0}, {0x492860, 0x491100}, {0x491865, 0x492D68}, {0x493200, 0x15AF28}};
    constexpr uintptr_t kDamage = 0x492D68, kSimulation = 0x49F138;
    constexpr size_t kSize = 0x600000;
    const char* kInstalled =
        "Reach contact melee installed: per-hand mesh/weapon sweeps, native exact-target melee damage, independent world-collision toggle; headset verification pending";

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
        snprintf(t, sizeof(t), "Reach contact melee unavailable: native binding +0x%llX missing/ambiguous",
            (unsigned long long)rva);
        return t;
    }
    // What DisableAndRemoveReachHooks leaves after a completed removal.
    void Clear()
    {
        g_reachContact.enabled = false;
        g_reachContact.faulted = false;
        g_reachContact.targets[0] = g_reachContact.targets[1] = nullptr;
        g_reachContact.callbacks = 0;
        g_reachContact.original = nullptr;
        g_reachContact.damageOriginal = nullptr;
        g_reachContact.build = nullptr;
        g_reachContact.consume = nullptr;
        g_reachContact.playback = nullptr;
        golden::Reset();
    }
    bool Target(size_t i, Op op, uintptr_t base, uintptr_t rva)
    {
        return i < golden::calls.size() && golden::calls[i].op == op &&
               golden::calls[i].target == reinterpret_cast<void*>(base + rva);
    }
    uint8_t worldCollisionStub[1]{};
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
    const uintptr_t base = image.Base();
    g_reachCamera.base = base;
    g_reachWorldCollision.original = worldCollisionStub;

    // 1. Success: damage-direction create+enable, then simulation create+enable; one line.
    Clear();
    Check(InstallReachContactMelee(base, kSize, 7), "install succeeds on a matching image");
    Check(golden::calls.size() == 4 && Target(0, Op::Create, base, kDamage) &&
              golden::calls[0].detour == reinterpret_cast<void*>(&ReachContactDamageDetour) &&
              Target(1, Op::Create, base, kSimulation) &&
              golden::calls[1].detour == reinterpret_cast<void*>(&ReachContactUpdateDetour) &&
              Target(2, Op::Enable, base, kDamage) && Target(3, Op::Enable, base, kSimulation),
        "z69 order: both created, then both enabled (base: damage-direction create/enable, then simulation create/enable)");
    Check(golden::logs.size() == 1 && golden::logs[0] == kInstalled, "exact installed line");
    Check(g_reachContact.enabled.load() && g_reachContact.generation == 7 &&
              g_reachContact.build == reinterpret_cast<ReachContactBuildFn>(base + 0x4919D4) &&
              g_reachContact.consume == reinterpret_cast<ReachContactConsumeFn>(base + 0x4924F0) &&
              g_reachContact.playback == reinterpret_cast<uint8_t(__fastcall*)()>(base + 0x58EEC) &&
              g_reachContact.targets[0] == reinterpret_cast<void*>(base + kDamage) &&
              g_reachContact.targets[1] == reinterpret_cast<void*>(base + kSimulation) &&
              g_reachContact.damageOriginal && g_reachContact.original,
        "runtime bindings and trampolines");
    for (const void* f : {reinterpret_cast<const void*>(&ReachContactUpdateDetour),
             reinterpret_cast<const void*>(&ReachContactDamageDetour)})
        Check(golden::HasUnwindRange(f), "detour has an unwind range (the camera teardown's quiescence can verify it)");
    golden::Reset();
    Check(!InstallReachContactMelee(base, kSize, 7) && golden::logs.empty() && golden::calls.empty() &&
              !g_reachContact.enabled.load(),
        "a second install clears enabled and refuses silently while hooks exist");

    // 2. Report: exact line, counters reset; nothing when the simulation hook is not set.
    Clear();
    (void)InstallReachContactMelee(base, kSize, 7);
    g_reachContact.queries = 5;
    g_reachContact.submitted[1] = 2;
    golden::Reset();
    ReportReachContactMelee();
    Check(
        golden::logs.size() == 1 &&
            golden::logs[0] ==
                "Reach contact melee: enabled=1 fault=0 queries=5 contacts=0 submissions(L/R)=0/2 nativeRejected=0 queueDrops=0 predictedRequests=0",
        "report line");
    golden::Reset();
    ReportReachContactMelee();
    Check(golden::logs.size() == 1 && golden::logs[0].find("queries=0") != std::string::npos, "report counters reset");
    Clear();
    ReportReachContactMelee();
    Check(golden::logs.empty(), "report with no simulation hook: no line");

    // 3. Precondition: silent.
    Clear();
    g_reachWorldCollision.original = nullptr;
    Check(!InstallReachContactMelee(base, kSize, 7) && golden::logs.empty() && golden::calls.empty(),
        "no world collision: silent");
    g_reachWorldCollision.original = worldCollisionStub;

    // 4. Bindings and edges.
    for (const auto& b : kBindings)
    {
        Clear();
        const uint8_t saved = image.bytes[b.rva];
        image.bytes[b.rva] ^= 0xFF;
        Check(!InstallReachContactMelee(base, kSize, 7) && golden::logs.size() == 1 &&
                  golden::logs[0] == BindingLine(b.rva) && golden::calls.empty(),
            "binding refused with its line");
        image.bytes[b.rva] = saved;
    }
    for (const auto& e : kEdges)
    {
        Clear();
        std::string expected = "Reach contact melee unavailable: native caller verification failed";
        for (const auto& b : kBindings)
            if (e[0] >= b.rva && e[0] < b.rva + Length(b.pattern))
                expected = BindingLine(b.rva);
        const uint8_t saved = image.bytes[e[0]];
        image.bytes[e[0]] = 0x90;
        Check(!InstallReachContactMelee(base, kSize, 7) && golden::logs.size() == 1 && golden::logs[0] == expected &&
                  golden::calls.empty(),
            "edge refused with its line");
        image.bytes[e[0]] = saved;
    }

    // 5. Hook failures at each position: one shared line per step, and the
    //    hooks already created are retired with the camera teardown's calls
    //    (retirement disable, drain, MH_RemoveHook), so a retry installs at once.
    struct Failure
    {
        int createAt, enableAt;
        const char* line;
        unsigned creates, enables, removes;
    };
    const Failure kFailures[]{{0, -1, "Reach contact melee unavailable: hook creation failed", 1, 0, 0},
        {1, -1, "Reach contact melee unavailable: hook creation failed", 2, 0, 1},
        {-1, 0, "Reach contact melee unavailable: hook enable failed", 2, 1, 2},
        {-1, 1, "Reach contact melee unavailable: hook enable failed", 2, 2, 2}};
    for (const auto& f : kFailures)
    {
        Clear();
        golden::failCreateAt = f.createAt;
        golden::failEnableAt = f.enableAt;
        Check(!InstallReachContactMelee(base, kSize, 7) && golden::logs.size() == 1 && golden::logs[0] == f.line &&
                  !g_reachContact.enabled.load(),
            std::string("z69 failure line: ") + f.line);
        Check(golden::Count(Op::Create) == f.creates && golden::Count(Op::Enable) == f.enables &&
                  golden::Count(Op::Remove) == f.removes && golden::Count(Op::DisableRetire) == f.removes,
            "z69 failure: the hooks already created are disabled and removed");
        Check(!g_reachContact.targets[0] && !g_reachContact.targets[1] && !g_reachContact.damageOriginal &&
                  !g_reachContact.original,
            "z69 failure: nothing is left for the camera teardown");
        if (f.removes)
            Check(golden::quiescenceFunctions.size() == 1 && golden::quiescenceFunctions[0].size() == 2 &&
                      golden::quiescenceFunctions[0][0] == reinterpret_cast<const void*>(&ReachContactDamageDetour) &&
                      golden::quiescenceFunctions[0][1] == reinterpret_cast<const void*>(&ReachContactUpdateDetour),
                "z69 failure: the two contact detours drain before removal");
        golden::Reset();
        Check(InstallReachContactMelee(base, kSize, 7) && golden::Logged(kInstalled),
            "z69 retry: installs straight away");
    }
    {
        // A hook the failure path cannot remove stays set for the camera teardown.
        Clear();
        golden::failEnableAt = 1;
        golden::removeStatus = MH_ERROR_MEMORY_PROTECT;
        Check(!InstallReachContactMelee(base, kSize, 7) && golden::logs.size() == 2 &&
                  golden::logs[0] == "Reach contact melee unavailable: hook enable failed" &&
                  golden::logs[1] == "Reach contact melee cleanup: removal failed; retained bookkeeping" &&
                  g_reachContact.targets[0] && g_reachContact.damageOriginal && g_reachContact.targets[1] &&
                  g_reachContact.original,
            "z69 failure with a failed removal: both kept (target and trampoline) for the teardown");
        golden::Reset();
        Check(!InstallReachContactMelee(base, kSize, 7) && golden::logs.empty() && golden::calls.empty(),
            "kept hooks: the retry is refused silently");
        // MH_ERROR_NOT_CREATED counts as removed, as in the camera table.
        Clear();
        golden::failEnableAt = 1;
        golden::removeStatus = MH_ERROR_NOT_CREATED;
        Check(!InstallReachContactMelee(base, kSize, 7) && golden::logs.size() == 1 && !g_reachContact.targets[0] &&
                  !g_reachContact.targets[1],
            "z69 failure: NOT_CREATED on removal counts as removed");
        // Busy contact detours: kept for the teardown, with the line.
        Clear();
        golden::failEnableAt = 1;
        golden::quiescent = false;
        Check(!InstallReachContactMelee(base, kSize, 7) && golden::logs.size() == 2 &&
                  golden::logs[1] == "Reach contact melee cleanup: callbacks busy; retained hooks" &&
                  g_reachContact.targets[0] && g_reachContact.targets[1] && golden::Count(Op::Remove) == 0,
            "z69 failure with busy detours: both kept");
    }

    {
        // A normal retire of an installed set (not a failed install):
        // retirement disable of both, one drain over both detours, then removal of
        // both, clearing both targets and both trampolines, with no line.
        Clear();
        (void)InstallReachContactMelee(base, kSize, 7);
        golden::Reset();
        const bool retired = RemoveReachContactMelee();
        Check(retired && golden::logs.empty() && !g_reachContact.enabled.load() &&
                  golden::Count(Op::DisableRetire) == 2 && golden::Count(Op::Remove) == 2 &&
                  Target(0, Op::DisableRetire, base, kDamage) && Target(1, Op::DisableRetire, base, kSimulation) &&
                  Target(2, Op::Remove, base, kDamage) && Target(3, Op::Remove, base, kSimulation) &&
                  golden::quiescenceFunctions.size() == 1 && golden::quiescenceFunctions[0].size() == 2,
            "M1: normal retire disables both, drains once, removes both, silently");
        Check(!g_reachContact.targets[0] && !g_reachContact.targets[1] && !g_reachContact.damageOriginal &&
                  !g_reachContact.original,
            "M1: normal retire clears both targets and both trampolines");
        Check(!g_reachContact.build && !g_reachContact.consume && !g_reachContact.playback,
            "M1: normal retire clears build, consume and playback (the camera table row did)");
        golden::Reset();
        Check(InstallReachContactMelee(base, kSize, 7) && golden::Logged(kInstalled),
            "M1: install again after a normal retire");
    }
    {
        // The early exit: a busy retire returns false and keeps
        // everything, so RemoveReachCameraCore returns before the camera table;
        // the next attempt, once drained, succeeds.
        Clear();
        (void)InstallReachContactMelee(base, kSize, 7);
        golden::Reset();
        g_reachContact.callbacks = 1;
        const bool busy = RemoveReachContactMelee();
        Check(!busy && golden::logs.size() == 1 &&
                  golden::logs[0] == "Reach contact melee cleanup: callbacks busy; retained hooks" &&
                  golden::Count(Op::Remove) == 0 && !g_reachContact.enabled.load(),
            "M1: busy retire returns false with the busy line and removes nothing");
        Check(g_reachContact.targets[0] && g_reachContact.targets[1] && g_reachContact.damageOriginal &&
                  g_reachContact.original && g_reachContact.build && g_reachContact.consume && g_reachContact.playback,
            "M1: busy retire keeps targets, trampolines and helpers");
        g_reachContact.callbacks = 0;
        golden::Reset();
        const bool retried = RemoveReachContactMelee();
        Check(retried && golden::logs.empty() && golden::Count(Op::Remove) == 2 && !g_reachContact.targets[0] &&
                  !g_reachContact.targets[1] && !g_reachContact.damageOriginal && !g_reachContact.original &&
                  !g_reachContact.build && !g_reachContact.consume && !g_reachContact.playback,
            "M1: the retry after the drain succeeds and clears everything");
    }

    std::printf("Reach contact melee runtime golden: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
