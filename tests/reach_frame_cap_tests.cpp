// Tests reach_frame_cap_logic.h: the frame cap decisions (uncap, restore, drop, refuse) and the shared
// tick that lifts MCC's frame limit for Reach, plus the Halo 2 and Halo 4 limiter rules and F1 statuses.
// Header-only logic; built as halomccvr_reach_frame_cap_tests. No game process: reads and writes go to
// fixture globals.
#include "../src/common/titles/reach/reach_frame_cap_logic.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

// The shared tick (Tick<Ops>), driven with fake MCC globals.
namespace fixture
{
    int32_t cap = 60;
    float target = 1.0f / 60;
    bool readFault = false, writeFault = false, resolves = true;
    int logs = 0;
    char last[256]{};
    struct Ops
    {
        static void Log(const char* format, ...)
        {
            ++logs;
            va_list args;
            va_start(args, format);
            std::vsnprintf(last, sizeof last, format, args);
            va_end(args);
        }
        static bool Read(const reach_frame_cap::Binding& b, int32_t& c, float& t)
        {
            if (readFault)
                return false;
            c = *b.cap;
            t = *b.target;
            return true;
        }
        static bool Write(reach_frame_cap::Binding& b, int32_t c, float t)
        {
            if (writeFault)
                return false;
            *b.target = t;
            *b.cap = c;
            return true;
        }
    };
    bool Resolve(uintptr_t, size_t, volatile int32_t*& c, volatile float*& t)
    {
        c = &cap;
        t = &target;
        return resolves;
    }
} // namespace fixture

static void TickTests()
{
    using namespace reach_frame_cap;
    using fixture::Ops;
    std::atomic<uint8_t> status{uint8_t(Status::NotChecked)};
    Binding b;
    b.title = "Reach";
    b.key = "reach_uncap_framerate";
    b.status = &status;
    auto tick = [&](uintptr_t base, uint32_t generation, bool want)
    {
        fixture::logs = 0;
        Tick<Ops>(b, base, 0x1000, generation, true, fixture::Resolve, want);
    };
    auto state = [&] { return Status(status.load()); };
    // Lift: the resolve line, then MCC's 60 cap set to unlimited.
    tick(0x1000, 1, true);
    Check(fixture::cap == 0 && fixture::target == 0.0f && fixture::logs == 2 &&
              !std::strcmp(fixture::last,
                  "Reach frame cap: MCC limited Reach to 60 fps; set to unlimited (the headset paces each frame)"),
        "R11: proven binding uncaps MCC's 60 with the existing lines");
    Check(state() == Status::Uncapped && b.saved && b.savedCap == 60 && b.savedTarget == 1.0f / 60,
        "R11: the replaced pair is saved; F1 reads uncapped");
    tick(0x1000, 1, true);
    Check(fixture::logs == 0 && state() == Status::Uncapped, "R11: already unlimited: nothing written, nothing logged");
    // Key off: MCC's limit comes back at once (target, then cap), with its line.
    tick(0x1000, 1, false);
    Check(fixture::cap == 60 && fixture::target == 1.0f / 60 && fixture::logs == 1 &&
              !std::strcmp(fixture::last, "Reach frame cap: reach_uncap_framerate off; restored MCC's limit of 60 fps"),
        "R11 N1: key off restores the saved pair with the exact line");
    Check(!b.saved && state() == Status::McLimit, "R11: the save is spent; F1 reads MCC's own limit");
    tick(0x1000, 1, false);
    Check(fixture::logs == 0 && fixture::cap == 60, "R11: key still off: MCC's limit stays, nothing logged");
    // Key on again: uncapped and saved again, with the existing line.
    tick(0x1000, 1, true);
    Check(fixture::cap == 0 && b.saved && fixture::logs == 1 && state() == Status::Uncapped,
        "R11: key on again uncaps and saves");
    // MCC rewrote its limit while uncapped: the save is dropped, MCC's value stands.
    fixture::cap = 120;
    fixture::target = 1.0f / 120;
    tick(0x1000, 1, false);
    Check(fixture::cap == 120 && fixture::target == 1.0f / 120 && fixture::logs == 0 && !b.saved &&
              state() == Status::McLimit,
        "R11: MCC's own rewrite drops the save; nothing written");
    // A new generation on the same module keeps the save (our zeros stay in place).
    tick(0x1000, 1, true);
    Check(fixture::cap == 0 && b.saved && b.savedCap == 120, "R11: uncapped from MCC's 120");
    tick(0x1000, 2, false);
    Check(
        fixture::cap == 120 && fixture::target == 1.0f / 120 && fixture::logs == 2 &&
            !std::strcmp(fixture::last, "Reach frame cap: reach_uncap_framerate off; restored MCC's limit of 120 fps"),
        "R11: the save survives a generation on the same module and is restored");
    // A different module: the save is dropped and nothing is written.
    tick(0x1000, 2, true);
    tick(0x2000, 3, false);
    Check(fixture::cap == 0 && fixture::target == 0.0f && !b.saved && fixture::logs == 1,
        "R11: a save from another module is dropped; the zeros are left");
    // A new generation whose proof fails drops the save and
    // leaves the values alone.
    fixture::cap = 60;
    fixture::target = 1.0f / 60;
    tick(0x2000, 4, true);
    Check(fixture::cap == 0 && b.saved, "R11: uncapped again (module 2)");
    Tick<Ops>(b, 0x2000, 0x1000, 5, false, fixture::Resolve, false);
    Check(state() == Status::NotChecked && b.saved && fixture::cap == 0,
        "R11: a new generation reads not checked until it resolves");
    fixture::resolves = false;
    tick(0x2000, 5, false);
    Check(
        fixture::cap == 0 && fixture::target == 0.0f && !b.saved && fixture::logs == 1 &&
            state() == Status::NotMatched &&
            !std::strcmp(fixture::last, "Reach frame cap: binding NOT proven; it keeps MCC's frame-rate limit (stock)"),
        "R11 condition 1: an unproven generation drops the save and writes nothing");
    fixture::resolves = true;
    // A write fault during the restore leaves the binding unproven (existing line).
    fixture::cap = 60;
    fixture::target = 1.0f / 60;
    tick(0x2000, 6, true);
    fixture::writeFault = true;
    tick(0x2000, 6, false);
    Check(!b.proven && fixture::cap == 0 && !std::strcmp(fixture::last, "Reach frame cap: FAULTED writing; stock") &&
              state() == Status::NotMatched,
        "R11: a faulting restore is unproven, stock line");
    fixture::writeFault = false;
    // Unrecognised values with a save: refused with the existing line, nothing written.
    tick(0x2000, 7, true);
    fixture::cap = 75;
    fixture::target = 1.0f / 75;
    tick(0x2000, 7, false);
    Check(fixture::cap == 75 && fixture::logs == 1 && state() == Status::Unrecognised &&
              !std::strcmp(fixture::last, "Reach frame cap: unexpected values (cap 75, target 0.013333); left stock"),
        "R11: unrecognised values are refused, not restored over");
}

int main()
{
    using namespace reach_frame_cap;
    // The restore decision shares Decide's plausible pairs.
    Check(DecideRestore(0, 0.0f) == RestoreAction::Write, "R11: our zeros: write the saved pair back");
    Check(DecideRestore(60, 1.0f / 60) == RestoreAction::Drop && DecideRestore(240, 1.0f / 240) == RestoreAction::Drop,
        "R11: a plausible MCC limit: drop the save");
    Check(DecideRestore(75, 1.0f / 75) == RestoreAction::Refuse && DecideRestore(0, 0.01f) == RestoreAction::Refuse &&
              DecideRestore(60, 0.5f) == RestoreAction::Refuse,
        "R11: anything else: refuse");
    // Halo 2's F1 status from its two limiters.
    Check(Halo2Status(true, true, true) == Status::Uncapped && Halo2Status(true, true, false) == Status::McLimit &&
              Halo2Status(true, false, true) == Status::Partial && Halo2Status(false, true, true) == Status::Partial &&
              Halo2Status(false, false, true) == Status::NotMatched &&
              Halo2Status(true, false, false) == Status::McLimit,
        "R11: Halo 2 status: both, one (partly matched), neither; key off = MCC's limit when any is proven");
    TickTests();
    Check(Decide(60, 1.0f / 60, true) == Action::Uncap, "MCC 60 cap: uncap");
    Check(Decide(120, 1.0f / 120, true) == Action::Uncap, "MCC rewrote 120: uncap again");
    Check(Decide(240, 1.0f / 240, true) == Action::Uncap, "MCC 240 cap: uncap");
    Check(Decide(0, 0.0f, true) == Action::None, "already unlimited: nothing to do");
    Check(Decide(60, 1.0f / 60, false) == Action::None, "option off: MCC's cap stays (no write)");
    Check(Decide(75, 1.0f / 75, true) == Action::Refuse, "unexpected cap: not the proven global, refuse");
    Check(Decide(60, 0.5f, true) == Action::Refuse, "target not 1/cap: refuse");
    Check(Decide(0, 0.01f, true) == Action::Refuse, "unlimited with a target: refuse");
    // Halo 2 stores only the cap; Halo 4 uses Decide like Reach.
    Check(DecideCapOnly(60, true) == Action::Uncap && DecideCapOnly(0, true) == Action::None &&
              DecideCapOnly(60, false) == Action::None && DecideCapOnly(90, true) == Action::Refuse,
        "Halo 2 cap-only decision");
    {
        const uint8_t stock[4]{0xF0, 0xB4, 0x78, 0x3C}, patched[4]{0, 0, 0, 0}, other[4]{0xF0, 0, 0x78, 0x3C};
        Check(Halo2CapImmediates(stock) == Halo2Table::Stock && Halo2CapImmediates(patched) == Halo2Table::Patched &&
                  Halo2CapImmediates(other) == Halo2Table::Unknown,
            "Halo 2 option table: stock, already patched (same module, new generation), or unknown (refused)");
    }
    // Halo 2's second limiter pair (frame time + "limited" flag).
    Check(ClassifyFrameTimeFlag(1.0f / 60, 1) == FramePair::Limited &&
              ClassifyFrameTimeFlag(1.0f / 240, 1) == FramePair::Limited &&
              ClassifyFrameTimeFlag(0.0f, 0) == FramePair::Unlimited,
        "Halo 2 second limiter: 60..240 limited, 0/0 = MCC's unlimited");
    Check(ClassifyFrameTimeFlag(1.0f / 60, 0) == FramePair::Unknown &&
              ClassifyFrameTimeFlag(0.0f, 1) == FramePair::Unknown &&
              ClassifyFrameTimeFlag(1.0f / 90, 1) == FramePair::Unknown &&
              ClassifyFrameTimeFlag(1.0f / 60, 2) == FramePair::Unknown,
        "Halo 2 second limiter: a pair its setter never writes is refused");
    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("reach_frame_cap_tests: all passed\n");
    return 0;
}
