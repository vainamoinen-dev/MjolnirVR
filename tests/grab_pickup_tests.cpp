// Tests for grab-a-gun-off-the-floor timing (src/common/grab_pickup_logic.h).
// Built as halomccvr_grab_pickup_tests and run by ctest. Header-only logic: no game process, no headset.
#include "../src/common/grab_pickup_logic.h"
#include <cmath>
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

int main()
{
    using namespace grab_pickup;
    Input in{};
    in.enabled = true;
    in.handValid = true;
    in.handDropM = 0.8f;
    in.nowMs = 1000;
    Grab g;
    in.grip = 0.1f;
    Check(!g.Update(in).steerSearch, "open hand: the search stays on the body");
    in.grip = 0.4f;
    Output o = g.Update(in);
    Check(o.steerSearch && !o.pressPickup && !o.haptic, "half grip reaching down: search follows the hand");
    in.target = 0x00120007u;
    in.nowMs += 16;
    o = g.Update(in);
    Check(o.haptic && !o.pressPickup, "prompt under the hand: one haptic tick");
    in.nowMs += 200;
    Check(!g.Update(in).haptic, "no repeated tick while the prompt stays");
    in.grip = 0.9f;
    in.nowMs += 16;
    o = g.Update(in);
    Check(o.fired && o.pressPickup && o.steerSearch, "full grip on a prompt: native pick up pressed");
    in.nowMs += 300;
    o = g.Update(in);
    Check(o.pressPickup && !o.fired, "held for the pickup duration");
    in.nowMs += 500;
    o = g.Update(in);
    Check(!o.pressPickup, "press released after the duration");
    in.nowMs += 16;
    Check(!g.Update(in).fired, "holding the grip does not grab again");
    in.grip = 0.2f;
    in.nowMs += 16;
    (void)g.Update(in);
    in.grip = 0.9f;
    in.nowMs += 16;
    Check(!g.Update(in).fired, "cooldown blocks an immediate re-grab");
    in.grip = 0.2f;
    in.nowMs += 1200;
    (void)g.Update(in);
    in.grip = 0.4f;
    (void)g.Update(in);
    in.nowMs += 200;
    (void)g.Update(in);
    in.grip = 0.9f;
    in.nowMs += 16;
    Check(g.Update(in).fired, "a new grab after release and cooldown");

    Grab h;
    Input n = in;
    n.nowMs = 50000;
    n.grip = 0.9f;
    n.target = kNoTarget;
    Check(!h.Update(n).fired, "no prompt under the hand: no press");
    n.target = 0x00120007u;
    n.handDropM = 0.1f;
    Check(!h.Update(n).steerSearch && !h.Update(n).fired, "hand up at the face: not a reach");
    n.handDropM = 0.8f;
    n.handBusy = true;
    Check(!h.Update(n).steerSearch, "a busy support hand (two-hand grip) never grabs");
    n.handBusy = false;
    n.enabled = false;
    Check(!h.Update(n).steerSearch && !h.Update(n).pressPickup, "gate off: nothing");

    Grab k;
    Input m = in;
    m.nowMs = 90000;
    m.grip = 0.4f;
    m.target = 0x00120007u;
    (void)k.Update(m);
    m.grip = 0.9f;
    m.nowMs += 200;
    Check(k.Update(m).pressPickup, "press starts");
    m.enabled = false;
    m.nowMs += 16;
    Check(!k.Update(m).pressPickup, "a gate closing mid-press releases the input");

    // Confirmed-target gate: the press belongs to the weapon it started on.
    Grab c;
    Input t = in;
    t.nowMs = 200000;
    t.grip = 0.4f;
    t.target = 0x00120007u;
    (void)c.Update(t);
    t.grip = 0.9f;
    t.nowMs += 200;
    Check(c.Update(t).pressPickup, "confirmed weapon: press starts");
    t.target = 0x00340009u;
    t.nowMs += 16;
    o = c.Update(t);
    Check(!o.pressPickup && !o.aborted, "the game's pick changes mid-press: released at once, verdict pending");
    t.nowMs += 16;
    Check(!c.Update(t).fired, "no re-press on the new pick without a new grip");
    t.nowMs += 400;
    o = c.Update(t);
    Check(o.aborted && !o.taken, "held weapon unchanged after the wait: an abort");
    Grab v;
    Input u = in;
    u.nowMs = 300000;
    u.grip = 0.4f;
    u.target = 0x00120007u;
    (void)v.Update(u);
    u.grip = 0.9f;
    u.nowMs += 200;
    Check(v.Update(u).pressPickup, "press starts (second weapon)");
    u.target = kNoTarget;
    u.nowMs += 16;
    o = v.Update(u);
    Check(!o.pressPickup && !o.aborted, "pick becomes a vehicle/device/none: released");
    u.nowMs += 450;
    Check(v.Update(u).aborted, "nothing changed in the hand: an abort");
    // A grab with an empty hand (after a grip-held drop): taken once a weapon is in it.
    Grab e;
    Input r = in;
    r.nowMs = 400000;
    r.held = kNoTarget;
    r.grip = 0.4f;
    r.target = 0x00120007u;
    (void)e.Update(r);
    r.grip = 0.9f;
    r.nowMs += 200;
    Check(e.Update(r).fired, "empty hand: the grab fires");
    r.nowMs += 16;
    Check(!e.Update(r).taken, "still empty: not taken yet");
    r.held = 0x00120007u;
    r.nowMs += 16;
    o = e.Update(r);
    Check(o.taken && !o.aborted, "the weapon came to the empty hand: taken");
    Grab w;
    Input x = in;
    x.nowMs = 400000;
    x.grip = 0.4f;
    x.target = 0x00120007u;
    Check(w.Update(x).haptic, "tick on the first weapon");
    x.target = 0x00340009u;
    x.nowMs += 16;
    Check(w.Update(x).haptic, "tick again when the pick moves to another weapon");
    x.grip = 0.4f;
    x.target = kNoTarget;
    x.nowMs += 16;
    Check(!w.Update(x).haptic && !w.Update(x).pressPickup, "no weapon picked: no tick, no press");

    // Stability: the press waits for the same pick to hold ~175 ms; a
    // flickering pick (weapon / vehicle / other weapon) restarts the wait.
    Grab f;
    Input y = in;
    y.nowMs = 500000;
    y.grip = 0.9f;
    y.target = 0x00120007u;
    Check(!f.Update(y).fired, "a brand-new pick never presses at once");
    y.nowMs += 100;
    y.target = kNoTarget;
    Check(!f.Update(y).fired, "pick drops out (vehicle won): no press");
    y.nowMs += 16;
    y.target = 0x00120007u;
    Check(!f.Update(y).fired, "pick back: the wait restarts");
    y.nowMs += 100;
    y.target = 0x00340009u;
    Check(!f.Update(y).fired, "pick flickers to another weapon: still waiting");
    y.nowMs += 100;
    Check(!f.Update(y).fired, "100 ms on the new pick: not yet stable");
    y.nowMs += 100;
    o = f.Update(y);
    Check(o.fired && o.pressPickup, "stable for 200 ms: the press starts on that weapon");

    // A4-18: a successful grab moves the game's pick as the weapon is taken.
    // The held weapon changing makes it "taken", never "aborted".
    Grab a;
    Input z = in;
    z.nowMs = 600000;
    z.grip = 0.4f;
    z.target = 0x00120007u;
    z.held = 0x00560001u;
    (void)a.Update(z);
    z.grip = 0.9f;
    z.nowMs += 200;
    Check(a.Update(z).fired, "A4-18: press starts with a known held weapon");
    z.target = kNoTarget;
    z.nowMs += 16;
    o = a.Update(z);
    Check(!o.pressPickup && !o.aborted && !o.taken, "A4-18: the pick moves first (weapon taken): no verdict yet");
    z.held = 0x00120007u;
    z.nowMs += 33;
    o = a.Update(z);
    Check(o.taken && !o.aborted, "A4-18: the held weapon changed: taken, not aborted");
    z.nowMs += 500;
    Check(!a.Update(z).aborted, "A4-18: no late abort after a success");
    Grab b;
    Input q = z;
    q.nowMs = 700000;
    q.grip = 0.4f;
    q.target = 0x00120007u;
    q.held = 0x00560001u;
    (void)b.Update(q);
    q.grip = 0.9f;
    q.nowMs += 200;
    (void)b.Update(q);
    q.held = 0x00120007u;
    q.nowMs += 100;
    o = b.Update(q);
    Check(o.taken && !o.pressPickup, "held weapon changes while the pick has not moved yet: taken, press released");

    // Single-player gate (GRAB-G1-PLAN.md 7.1).
    Check(SimAllowsGrab(1, -1) && SimAllowsGrab(1, 0), "sim 1 (local) allows the grab");
    Check(SimAllowsGrab(3, 0), "sim 3 alone (solo campaign host) allows the grab");
    Check(!SimAllowsGrab(3, 1) && !SimAllowsGrab(3, -1), "sim 3 with another machine, or unknown: refused");
    Check(!SimAllowsGrab(2, 0) && !SimAllowsGrab(4, 0) && !SimAllowsGrab(5, 0), "sims 2, 4 and 5: refused");
    Check(!SimAllowsGrab(0, 0) && !SimAllowsGrab(-1, 0), "no game or unreadable: refused");
    Check(MachinesOtherThan(0, 1u) == 0 && MachinesOtherThan(2, 4u) == 0, "only this machine in the mask");
    Check(MachinesOtherThan(0, 3u) == 1 && MachinesOtherThan(1, 0x83u) == 2, "other machines counted");
    Check(MachinesOtherThan(-1, 1u) == -1 && MachinesOtherThan(32, 1u) == -1, "unusable index: unknown");

    // Nearest-weapon line: body-frame offset and change-only buckets.
    const float facingForward[4]{0, 0, 0, 1};
    const float delta[3]{0.30f, -0.50f, -1.0f};
    float body[3]{};
    Check(BodyOffset(delta, facingForward, body) && std::fabs(body[0] - 0.30f) < 1e-5f &&
              std::fabs(body[1] + 0.50f) < 1e-5f && std::fabs(body[2] - 1.0f) < 1e-5f,
        "head facing -z: right, up, forward as tracking x, y, -z");
    const float turnedLeft[4]{0, 0.70710678f, 0, 0.70710678f}; // 90 degrees about +y: facing -x
    const float ahead[3]{-1.0f, 0.0f, 0.0f};
    Check(BodyOffset(ahead, turnedLeft, body) && std::fabs(body[2] - 1.0f) < 1e-4f && std::fabs(body[0]) < 1e-4f,
        "head turned left: a point at -x is straight ahead");
    Check(Bucket(0.12f) == 0 && Bucket(0.13f) == 1 && Bucket(-0.30f) == -1, "0.25 m buckets");
    NearestWeaponLog nl;
    const float near1[3]{0.10f, -0.40f, 0.60f}, near2[3]{0.11f, -0.41f, 0.62f}, near3[3]{0.40f, -0.40f, 0.60f};
    Check(nl.Changed(0x00120007u, near1), "first line prints");
    Check(!nl.Changed(0x00120007u, near2), "same weapon, same buckets: quiet");
    Check(nl.Changed(0x00120007u, near3), "a new bucket on one axis prints");
    Check(nl.Changed(0x00340009u, near3), "another weapon prints");
    Check(nl.Changed(kNoTarget, near3) && !nl.Changed(kNoTarget, near1), "none prints once");

    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("grab_pickup_tests: all passed\n");
    return 0;
}
