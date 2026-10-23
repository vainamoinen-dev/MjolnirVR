// Tests for grip-held drop guards, the stow/draw/drop retry, the rig probe script and
// the G1b zones and hand owner
// (src/common/holster_logic.h). Built as halomccvr_holster_logic_tests and run by ctest.
#include "../src/common/holster_logic.h"
#include <cstdio>

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

static holster::GripInput Ready(float grip, uint64_t now)
{
    holster::GripInput in{};
    in.grip = grip;
    in.tracked = in.actionActive = in.focused = in.gateOpen = true;
    in.nowMs = now;
    return in;
}

int main()
{
    using namespace holster;

    // Arming: an open hand never drops a weapon that was not Held first.
    GripHold g;
    Check(!g.Update(Ready(0.0f, 1000)) && !g.Update(Ready(0.0f, 1400)), "never Held: an open hand keeps the gun");
    Check(!g.Update(Ready(0.60f, 1500)) && g.Armed(), "Held at 0.55 or more arms the drop");
    Check(!g.Update(Ready(0.20f, 2000)), "release starts");
    Check(!g.Update(Ready(0.20f, 2149)), "149 ms released: keep");
    Check(g.Update(Ready(0.20f, 2151)), "151 ms released: drop");
    Check(!g.Update(Ready(0.20f, 2400)), "one drop only; the next needs a fresh Held");

    // Hysteresis band: chatter between 0.24 and 0.56 changes nothing.
    GripHold h;
    (void)h.Update(Ready(0.56f, 1000));
    bool any = false;
    for (uint64_t t = 1016; t < 3000; t += 16)
        any |= h.Update(Ready((t / 16) % 2 ? 0.26f : 0.54f, t));
    Check(!any && h.Armed(), "band chatter 0.26..0.54: keep, still armed");
    Check(!h.Update(Ready(0.24f, 3000)) && !h.Update(Ready(0.40f, 3100)) && !h.Update(Ready(0.24f, 3200)),
        "a dip into the band restarts the release timer");
    Check(!h.Update(Ready(0.24f, 3349)) && h.Update(Ready(0.24f, 3351)), "continuous release after the band: drop");

    // Guards: every one keeps the gun, and a release afterwards needs a fresh Held.
    const auto guarded = [](void (*spoil)(GripInput&), const char* what)
    {
        GripHold k;
        (void)k.Update(Ready(0.9f, 1000));
        GripInput in = Ready(0.1f, 1100);
        spoil(in);
        bool dropped = false;
        for (uint64_t t = 1100; t < 2000; t += 16)
        {
            in.nowMs = t;
            dropped |= k.Update(in);
        }
        Check(!dropped, what);
        dropped = false;
        for (uint64_t t = 2000; t < 3000; t += 16)
            dropped |= k.Update(Ready(0.1f, t));
        Check(!dropped, "after a guard, an open hand needs a fresh Held before a drop");
    };
    guarded([](GripInput& in) { in.tracked = false; }, "tracking lost: keep");
    guarded([](GripInput& in) { in.actionActive = false; }, "controller asleep or off: keep");
    guarded([](GripInput& in) { in.focused = false; }, "headset off (session not focused): keep");
    guarded([](GripInput& in) { in.blocked = true; }, "F1, pause, menu, load, cinematic or seat: keep");
    guarded([](GripInput& in) { in.gateOpen = false; }, "single-player gate closed: keep");
    guarded([](GripInput& in) { in.sampleAgeMs = 51; }, "stale grip sample: keep");
    GripHold r;
    (void)r.Update(Ready(0.9f, 1000));
    GripInput lost = Ready(0.9f, 1100);
    lost.tracked = false;
    (void)r.Update(lost);
    (void)r.Update(Ready(0.9f, 1150));
    Check(!r.Armed(), "tracking back for 50 ms: not yet trusted");
    (void)r.Update(Ready(0.9f, 1310));
    Check(r.Armed(), "tracking back for 200 ms and Held: armed again");
    r.NewWeapon();
    Check(!r.Armed(), "a new weapon in the hand must be Held before a drop");

    // Request retry (7.4): issue, retry every 100 ms, land or give up at 500 ms.
    RoleRequest q;
    q.Start(RoleRequest::kUnarmed, 1000);
    Check(q.Update(0, 1000) == Step::Issue, "first update issues the action");
    Check(q.Update(0, 1050) == Step::None, "no re-issue inside 100 ms");
    Check(q.Update(0, 1100) == Step::Issue, "busy unit: re-issued after 100 ms");
    Check(q.Update(-1, 1150) == Step::Landed && !q.Active(), "role reads -1: landed");
    RoleRequest busy;
    busy.Start(1, 2000);
    Step last = Step::None;
    int issues = 0;
    for (uint64_t t = 2000; t <= 2600 && busy.Active(); t += 16)
    {
        last = busy.Update(0, t);
        issues += last == Step::Issue;
    }
    Check(last == Step::Refused && issues == 5, "still busy after 500 ms: refused once, 5 issues");
    Check(busy.Update(0, 3000) == Step::None, "a refused request stays quiet");

    // Probe scripts: each step once, in order, at its time.
    ProbeScript s;
    s.Select(1);
    Check(s.Next(1999) == ProbeAction::None, "script 1: nothing before 2 s");
    Check(s.Next(2000) == ProbeAction::Stow && s.Next(2000) == ProbeAction::None, "script 1: stow once at 2 s");
    Check(s.Next(12000) == ProbeAction::Draw, "script 1: draw at 12 s");
    Check(s.Next(30000) == ProbeAction::Drop && s.Done(), "script 1: then the drop, then done");
    s.Select(2);
    Check(s.Next(2000) == ProbeAction::ThirdOn && s.Next(2000) == ProbeAction::Stow && s.Done(),
        "script 2: limit raised, then stow");
    s.Select(3);
    int steps = 0;
    for (uint64_t t = 0; t <= 20000; t += 100)
        steps += s.Next(t) != ProbeAction::None;
    Check(steps == 6 && s.Done(), "script 3: six stow/draw steps");
    s.Select(0);
    Check(s.Next(100000) == ProbeAction::None && s.Done(), "script 0: probe off");

    // G1b zones: class locations, a clash takes the other one, a pinned weapon keeps its zone.
    {
        const uint32_t slots[4]{0x10, 0x20, 0xFFFFFFFFu, 0xFFFFFFFFu};
        const int8_t hipHip[4]{kHip, kHip, kNoZone, kNoZone};
        const bool none[4]{}, secondPinned[4]{false, true, false, false};
        int8_t z[4];
        AssignZones(slots, hipHip, none, kHip, z);
        Check(z[0] == kHip && z[1] == kShoulder && z[2] == kNoZone, "two pistols: hip, then the shoulder");
        AssignZones(slots, hipHip, secondPinned, kHip, z);
        Check(z[1] == kHip && z[0] == kShoulder, "the holstered weapon keeps its zone over slot order");
        const int8_t unknown[4]{kNoZone, kShoulder, kNoZone, kNoZone};
        AssignZones(slots, unknown, none, kShoulder, z);
        Check(z[0] == kShoulder && z[1] == kHip, "an unknown class takes the fallback location first");
        const uint32_t three[4]{0x10, 0x20, 0x30, 0xFFFFFFFFu};
        const int8_t pref3[4]{kHip, kShoulder, kHip, kNoZone};
        AssignZones(three, pref3, none, kHip, z);
        Check(z[2] == kNoZone, "a third weapon with both zones taken has no zone");
    }

    // What an open hand means (Reviewer PLAN OK 2; Manager: a full holster is felt).
    {
        const bool hipFull[2]{false, true};
        Check(ReleaseAt(false, kNoZone, hipFull) == Release::Keep, "zones unknown: keep");
        Check(ReleaseAt(true, kNoZone, hipFull) == Release::Drop, "outside both zones: drop");
        Check(ReleaseAt(true, kShoulder, hipFull) == Release::Stow, "a free zone: stow");
        Check(ReleaseAt(true, kHip, hipFull) == Release::Occupied, "a full zone: keep, felt");
    }

    // An empty hand's grip close: once per close, a fresh open after a guard.
    {
        GripClose c;
        Check(!c.Update(0.9f, true), "a grip already closed is not a close");
        Check(!c.Update(0.1f, true) && c.Update(0.6f, true), "open, then Held: one close");
        Check(!c.Update(0.9f, true), "still closed: no second close");
        (void)c.Update(0.1f, true);
        Check(!c.Update(0.9f, false), "under a guard: no close");
        Check(!c.Update(0.9f, true), "after the guard a fresh open is needed");
    }

    // The hand owner: the write, the echo, and the switch button (Reviewer PLAN OK 1).
    {
        HandPlan p;
        int8_t w = 7;
        Check(!p.Apply(0, w) && w == 7, "Stock writes nothing");
        p.Take(HandPlan::Mode::Empty, -1);
        Check(p.Apply(0, w) && w == -1, "empty hand: the desired slot is written as -1");
        Check(p.Apply(0, w) && w == -1, "the player's value unchanged: keep writing");
        Check(p.Apply(-1, w) && w == -1, "the game echoing our -1 back is not a switch press");
        Check(p.Holstered(0) && p.Holstered(1), "empty hand: every carried slot is holstered");
        Check(!p.Apply(1, w) && p.Current() == HandPlan::Mode::Stock, "a new value of the player's own: Stock");
        Check(!p.Holstered(0), "Stock holsters nothing");

        p.Take(HandPlan::Mode::Slot, 1);
        Check(p.Apply(0, w) && w == 1, "grab-back: the drawn slot is written");
        Check(!p.Holstered(1) && p.Holstered(0), "the drawn slot is not holstered, the other is");
        Check(p.Apply(1, w) && w == 1, "the player's value moving to our own write is an echo");
        Check(!p.Apply(0, w) && p.Current() == HandPlan::Mode::Stock, "then a switch press: Stock");
    }

    // Slot changes the mod did not ask for (Reviewer CHANGES b06bb0ff).
    {
        const uint32_t none = 0xFFFFFFFFu;
        const uint32_t two[4]{0x10, 0x20, none, none}, three[4]{0x10, 0x20, 0x30, none};
        const uint32_t one[4]{none, 0x20, none, none}, swapped[4]{0x40, 0x20, none, none};
        int slot = 7;
        Check(ClassifySlots(two, three, false, slot) == SlotChange::Pickup && slot == 2,
            "a handle that only appeared: a pickup, into its slot");
        Check(ClassifySlots(two, one, false, slot) == SlotChange::Other && slot == -1,
            "a holstered weapon the game took: hand back to the game");
        Check(ClassifySlots(two, swapped, false, slot) == SlotChange::Other, "a swap (one in, one out): hand back");
        Check(ClassifySlots(two, one, true, slot) == SlotChange::None, "a drop landing during the mod's request");
        Check(ClassifySlots(two, two, false, slot) == SlotChange::None, "no change");

        // The pickup while the hand is empty: the hand follows the new slot, not -1.
        HandPlan p;
        int8_t w = 0;
        p.Take(HandPlan::Mode::Empty, -1);
        (void)p.Apply(0, w);
        p.Take(HandPlan::Mode::Slot, 2);
        Check(p.Apply(0, w) && w == 2 && !p.Holstered(2) && p.Holstered(0), "after a pickup the new slot is drawn");
    }

    // The seat (build 33 rig finding 1): one stale sample never hands the hand back.
    {
        SeatConfirm c;
        Check(c.Update(Seat::OnFoot, 1000) == Seat::OnFoot, "on foot at once");
        Check(c.Update(Seat::Unknown, 1010) == Seat::Unknown && !c.LongUnknown(1010), "a stale sample holds");
        Check(c.Update(Seat::Unknown, 6009) == Seat::Unknown && !c.LongUnknown(6009), "unknown under 5 s holds");
        Check(c.Update(Seat::Unknown, 6010) == Seat::Unknown && c.LongUnknown(6010), "unknown for 5 s hands back");
        Check(c.Update(Seat::OnFoot, 6020) == Seat::OnFoot && !c.LongUnknown(6020), "on foot again clears it");
        Check(c.Update(Seat::Seated, 7000) == Seat::Unknown, "a seat is not confirmed at once");
        Check(c.Update(Seat::Seated, 7199) == Seat::Unknown, "a seat under 200 ms is not confirmed");
        Check(c.Update(Seat::Seated, 7200) == Seat::Seated, "a seat held 200 ms is confirmed");
        Check(c.Update(Seat::OnFoot, 7210) == Seat::OnFoot, "on foot again");
        Check(c.Update(Seat::Seated, 7220) == Seat::Unknown, "a new seat starts its own wait");
        Check(c.Update(Seat::Unknown, 7300) == Seat::Unknown && c.Update(Seat::Seated, 7400) == Seat::Unknown,
            "a stale sample restarts the seat wait");
        Check(!c.LongUnknown(7400), "a seat sample is not unknown");
    }

    // The switch button: the game's next-weapon order.
    {
        const uint32_t none = 0xFFFFFFFFu;
        const uint32_t two[4]{0x10, 0x20, none, none}, gap[4]{0x10, none, 0x30, none}, one[4]{0x10, none, none, none};
        Check(NextCarried(two, 0) == 1 && NextCarried(two, 1) == 0, "two weapons: the other one");
        Check(NextCarried(two, -1) == 0, "an empty hand: the first carried");
        Check(NextCarried(gap, 0) == 2 && NextCarried(gap, 2) == 0, "empty slots are skipped");
        Check(NextCarried(one, 0) == -1 && NextCarried(one, -1) == 0, "one weapon: none other, or draw it");
    }

    // A dropped gun stays on the floor until the hand takes it (walk-over skips it).
    {
        const uint32_t none = 0xFFFFFFFFu;
        DroppedGuns d;
        d.Add(0xE33E00CF);
        Check(d.Has(0xE33E00CF) && !d.Has(0xE34000D1), "the dropped gun is skipped, another gun is not");
        Check(!d.Has(0xE40000CF), "the same index with another salt is another object");
        Check(d.FirstRefusal(0xE33E00CF) && !d.FirstRefusal(0xE33E00CF), "one log line per drop");
        const uint32_t walking[4]{0x10, none, none, none};
        d.Carried(walking);
        Check(d.Has(0xE33E00CF), "carrying other guns keeps it on the floor");
        const uint32_t grabbed[4]{0x10, 0xE33E00CF, none, none};
        d.Carried(grabbed);
        Check(!d.Has(0xE33E00CF), "the hand grab takes it: free again");
        d.Add(0xE33E00CF);
        Check(d.FirstRefusal(0xE33E00CF), "dropped again: a new log line");
        for (uint32_t id = 1; id <= 4; ++id)
            d.Add(id);
        Check(!d.Has(0xE33E00CF) && d.Has(1) && d.Has(4), "the last 4 drops are kept");
        Check(!d.Has(none), "no handle is never a dropped gun");
    }

    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("holster_logic_tests: all passed\n");
    return 0;
}
