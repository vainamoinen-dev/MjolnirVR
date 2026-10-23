// Tests vehicle_wheel_logic.h: the two-handed steering wheel (grab, rotation, throttle and brake) and
// its turn-rate and direct-angle feels, with Halo 3 vehicle identities.
// Header-only logic; built as halomccvr_vehicle_wheel_tests. No game process or headset needed.
#include "../src/common/vehicle_wheel_logic.h"
#include "../src/common/titles/h3/halo3_vehicle_logic.h"
#include <cstdio>
#include <limits>

using namespace vehicle_wheel;
namespace
{
    unsigned checks{}, failures{};
    void Check(bool ok, const char* message)
    {
        ++checks;
        if (!ok)
        {
            ++failures;
            std::printf("FAIL: %s\n", message);
        }
    }

    // Body faces -Z; right is +X. Wheel centre = head + (0,0,-0.40) + (0,-0.35,0).
    struct Rig
    {
        GrabWheel wheel;
        Settings c;
        Input in;
        Rig()
        {
            in.now = 1000;
            in.available = true;
            in.head[0] = 0;
            in.head[1] = 1.2f;
            in.head[2] = 0;
            in.bodyForward[0] = 0;
            in.bodyForward[1] = 0;
            in.bodyForward[2] = -1;
            Place(in.left, -0.18f, 0);
            Place(in.right, 0.18f, 0);
            in.left.valid = in.right.valid = true;
        }
        static void Place(Hand& h, float x, float y)
        {
            h.pos[0] = x;
            h.pos[1] = 0.85f + y;
            h.pos[2] = -0.40f;
        }
        Output Step(unsigned dt = 11)
        {
            in.now += dt;
            return wheel.Update(in, c);
        }
        Output GrabBoth()
        {
            in.left.grip = in.right.grip = 0;
            Step();
            in.left.grip = in.right.grip = 1;
            return Step();
        }
    };
    // Put both hands on the rim at a given counter-clockwise rotation (degrees).
    void Rotate(Rig& r, float degrees)
    {
        const float a = degrees / 57.2957795f, R = 0.18f;
        Rig::Place(r.in.right, R * std::cos(a), R * std::sin(a));
        Rig::Place(r.in.left, -R * std::cos(a), -R * std::sin(a));
    }
}

int main()
{
    Check(ModeFor(SteerClass::Car, Wheel, Stick) == Wheel && ModeFor(SteerClass::Bike, Wheel, Stick) == Stick &&
              ModeFor(SteerClass::None, Wheel, Wheel) == Point && ModeFor(SteerClass::Car, 7, 0) == Point,
        "steering mode per class; aircraft/tanks and bad values stay Point");
    Check(!EffectiveFollow(false, true, SteerClass::Car, Point) &&
              EffectiveFollow(false, true, SteerClass::Car, Wheel) &&
              EffectiveFollow(false, true, SteerClass::Bike, Stick) &&
              !EffectiveFollow(false, false, SteerClass::Car, Wheel) &&
              EffectiveFollow(true, false, SteerClass::None, Point),
        "Wheel/Stick make the driver seat follow the hull; Point and passengers keep the switch");
    {
        Rig r;
        const Output o = r.GrabBoth();
        Check(o.grabbed && o.driving && o.consumeLeft && o.consumeRight && o.steer == 0.0f,
            "squeezing both grips on the rim grabs the wheel, centred, and hides the grips");
        Rotate(r, 40);
        Output t{};
        for (int i = 0; i < 8; ++i)
        {
            Rotate(r, 5.0f * (i + 1));
            t = r.Step();
        }
        Check(
            t.steer < -0.45f && t.steer > -0.55f, "turning the wheel 40 deg counter-clockwise steers about half left");
        for (int i = 0; i < 20; ++i)
        {
            Rotate(r, -5.0f * i);
            t = r.Step();
        }
        Check(t.steer > 0.99f, "past the 80 deg lock the steer is full right");
        r.in.left.grip = r.in.right.grip = 0.2f;
        const Output let = r.Step();
        Check(let.released && !let.driving && let.steer == 0.0f, "letting go returns steering to the stick");
    }
    {
        Rig r;
        Rig::Place(r.in.left, -0.70f, -0.30f);
        Rig::Place(r.in.right, 0.70f, -0.30f);
        r.in.left.grip = r.in.right.grip = 1;
        r.Step();
        r.Step();
        Rig::Place(r.in.left, -0.18f, 0);
        Rig::Place(r.in.right, 0.18f, 0);
        Check(!r.Step().driving, "arriving with the grips already squeezed does not grab");
        r.in.left.grip = r.in.right.grip = 0.5f;
        r.Step();
        Check(!r.Step().driving, "grip between release and grab thresholds still does not grab");
        r.in.left.grip = r.in.right.grip = 0.2f;
        r.Step();
        r.in.left.grip = r.in.right.grip = 0.7f;
        Check(r.Step().driving, "a re-squeeze inside the zone grabs");
        r.in.left.grip = r.in.right.grip = 0.4f;
        Check(r.Step().driving, "a grip above 0.30 keeps holding (hysteresis)");
    }
    {
        Rig r;
        Rig::Place(r.in.left, -0.50f, 0.40f);
        Rig::Place(r.in.right, 0.18f, 0);
        r.in.right.grip = 0;
        r.Step();
        r.in.right.grip = 1;
        Output o = r.Step();
        Check(o.grabRight && !o.grabLeft && o.driving, "one hand can take the wheel");
        Output t{};
        for (int i = 1; i <= 6; ++i)
        {
            const float a = 5.0f * i / 57.2957795f;
            Rig::Place(r.in.right, 0.18f * std::cos(a), 0.18f * std::sin(a));
            t = r.Step();
        }
        Check(t.steer < -0.30f && t.steer > -0.45f, "one hand: centre-to-hand angle steers (30 deg ~ 0.37 left)");
        Rig::Place(r.in.right, 0.02f, 0.02f);
        Check(std::fabs(r.Step().steer - t.steer) < 1e-6f, "a hand near the centre is ignored");
    }
    {
        Rig r;
        r.GrabBoth();
        Output t{};
        for (int i = 1; i <= 4; ++i)
        {
            Rotate(r, 5.0f * i);
            t = r.Step();
        }
        const float before = t.steer;
        Rotate(r, 90.0f);
        Check(std::fabs(r.Step().steer - before) < 1e-6f, "a jump over 45 deg in one frame is ignored");
    }
    {
        Rig r;
        r.GrabBoth();
        for (int i = 1; i <= 4; ++i)
        {
            Rotate(r, 5.0f * i);
            r.Step();
        }
        const float held = r.Step().steer;
        r.in.left.grip = 0.1f;
        const Output one = r.Step();
        Check(one.released && one.driving && std::fabs(one.steer - held) < 0.02f,
            "dropping to one hand keeps the wheel where it was (no snap)");
    }
    {
        Rig r;
        Rig::Place(r.in.right, 0.60f, 0.30f);
        r.in.right.grip = 0;
        r.Step();
        r.in.right.grip = 1;
        Output o = r.Step();
        Check(!o.driving && !o.exitRequested && o.consumeRight,
            "a grip squeezed away from the wheel starts the exit hold");
        bool exited = false;
        for (int i = 0; i < 80; ++i)
            exited |= r.Step(11).exitRequested;
        Check(exited, "holding it 0.8 s out at the side exits");
        bool again = false;
        for (int i = 0; i < 60; ++i)
            again |= r.Step().exitRequested;
        Check(!again, "one exit per squeeze");
        r.in.right.grip = 0;
        r.Step();
        r.in.right.grip = 1;
        r.Step(300);
        r.in.right.grip = 0;
        bool shortTap = false;
        for (int i = 0; i < 60; ++i)
            shortTap |= r.Step().exitRequested;
        Check(!shortTap, "a short squeeze does not exit");
        Rig p;
        p.c.gripExit = false;
        Rig::Place(p.in.right, 0.60f, 0.30f);
        p.in.right.grip = 0;
        p.Step();
        p.in.right.grip = 1;
        bool pointExit = false, pointConsume = false;
        for (int i = 0; i < 60; ++i)
        {
            const Output q = p.Step();
            pointExit |= q.exitRequested;
            pointConsume |= q.consumeRight;
        }
        Check(!pointExit && !pointConsume, "Point mode leaves a lone grip alone (old grip dismount still works)");
    }
    {
        Rig r;
        r.GrabBoth();
        r.in.available = false;
        const Output off = r.Step();
        Check(off.released && !off.driving && off.steer == 0.0f, "leaving the seat drops the wheel");
        r.in.available = true;
        r.in.right.pos[0] = std::numeric_limits<float>::quiet_NaN();
        Check(!r.Step().driving, "a lost controller cannot grab");
    }
    {
        // A missed grab just past the rim, held, never exits (only out at the side).
        Rig r;
        Rig::Place(r.in.right, 0.45f, 0.0f);
        r.in.right.grip = 0;
        r.Step();
        r.in.right.grip = 1;
        bool exited = false;
        for (int i = 0; i < 120; ++i)
            exited |= r.Step().exitRequested;
        Check(!exited, "a grip held just outside the rim does not exit");
        Rig s2;
        s2.c.allowGrab = false;
        s2.GrabBoth();
        Check(!s2.Step().driving && !s2.Step().consumeLeft, "Stick mode never grabs or hides a grip at the wheel");
    }
    {
        // Point mode runs the old toggle: a single squeeze (one grip), even at
        // the wheel position, neither engages nor withholds the grip, so the
        // lone-grip dismount still works.
        Check(UsesGrabWheel(Wheel) && !UsesGrabWheel(Point) && !UsesGrabWheel(Stick),
            "only Wheel mode uses the grab wheel");
        Halo3Wheel legacy{};
        uint64_t now = 1000;
        Halo3UpdateWheelToggle(legacy, true, 0.0f, 0.0f, now);
        Halo3UpdateWheelToggle(legacy, true, 0.0f, 1.0f, now += 20);
        Check(!legacy.engaged && !Halo3WheelSwallowsGrips(legacy), "Point: a lone squeeze dismounts as before");
        Halo3UpdateWheelToggle(legacy, true, 0.0f, 0.0f, now += 20);
        Check(!legacy.engaged, "Point: the wheel still needs the old double-click");
    }
    Check(TriggerThrottle(1, 0) == 1.0f && TriggerThrottle(0, 1) == -1.0f && TriggerThrottle(0.04f, 0) == 0.0f &&
              std::fabs(TriggerThrottle(0.525f, 0) - 0.5f) < 1e-4f && TriggerThrottle(1, 1) == 0.0f,
        "right trigger throttles, left trigger brakes/reverses");
    {
        // Direct angle: the wheel holds the look a fixed angle off the hull.
        const float deg = 1.0f / 57.2957795f;
        Check(DirectAngleStick(0, 1.0f, 1.0f, 45) == 0.0f, "centred wheel, look on the nose: no turn");
        Check(DirectAngleStick(0, 1.0f, 1.0f + 10 * deg, 45) > 0.0f,
            "centred wheel, look left of the nose: turn back right");
        Check(DirectAngleStick(0, 1.0f, 1.0f + 2 * deg, 45) == 0.0f, "inside the 3 deg deadband: no turn");
        Check(DirectAngleStick(0.5f, 1.0f, 1.0f, 45) == 1.0f, "half right lock from straight: turn right hard");
        Check(DirectAngleStick(0.5f, 1.0f, 1.0f - 22.5f * deg, 45) == 0.0f,
            "half right lock holds the look 22.5 deg right");
        Check(DirectAngleStick(-1.0f, 0.0f, 0.0f, 45) == -1.0f, "full left lock: turn left");
        Check(DirectAngleStick(0, 3.1f, -3.1f, 45) > 0.0f && DirectAngleStick(0, 3.1f, -3.1f, 45) < 0.3f,
            "across +-pi the short way round");
        Check(DirectAngleStick(0, std::numeric_limits<float>::quiet_NaN(), 0, 45) == 0.0f, "bad hull yaw: no turn");
    }
    {
        // Wheel v2: rotation about the wheel axis (-Z here) steers; position does not.
        // Driver looks along -Z: turning the wheel clockwise (right) = rotation
        // about +Z by a negative angle.
        const auto AboutZ = [](float deg, float q[4])
        {
            const float h = deg / 57.2957795f * 0.5f;
            q[0] = 0;
            q[1] = 0;
            q[2] = std::sin(h);
            q[3] = std::cos(h);
        };
        const auto Set = [&](Hand& hand, float deg) { AboutZ(deg, hand.q); };
        Rig r;
        r.c.rotation = true;
        r.GrabBoth();
        Set(r.in.left, -40);
        Set(r.in.right, -40);
        Output o = r.Step();
        Check(o.driving && o.steer > 0.4f, "v2: both hands roll clockwise -> steer right");
        Set(r.in.left, 0);
        Set(r.in.right, 0);
        r.Step(); // under the 45 deg jump guard
        Set(r.in.left, 40);
        Set(r.in.right, 40);
        o = r.Step();
        Check(o.steer < -0.4f, "v2: counter-clockwise -> steer left");
        // Hands slide across the screen with no rotation: no steering change.
        Set(r.in.left, 0);
        Set(r.in.right, 0);
        r.Step();
        r.Step();
        Rig::Place(r.in.right, -0.10f, 0.05f);
        Rig::Place(r.in.left, -0.30f, 0.05f);
        o = r.Step();
        Check(std::fabs(o.steer) < 0.05f, "v2: moving the hands sideways does not steer (v1 screen-side bug)");
        r.in.head[0] = 0.3f;
        r.in.bodyForward[0] = 0.5f;
        r.in.bodyForward[2] = -0.866f;
        o = r.Step();
        Check(std::fabs(o.steer) < 0.05f, "v2: moving and turning the head does not steer");

        // One hand, grabbed with an arbitrary wrist pose: no snap; its own twist steers.
        Rig one;
        one.c.rotation = true;
        AboutZ(25, one.in.right.q); // any grab orientation
        one.in.right.grip = 0;
        one.Step();
        one.in.right.grip = 1;
        o = one.Step();
        Check(o.driving && std::fabs(o.steer) < 0.05f, "v2: grab never snaps the wheel");
        AboutZ(25 - 30, one.in.right.q); // 30 deg clockwise from the grab
        o = one.Step();
        Check(o.steer > 0.25f, "v2: one-hand clockwise twist steers right");
        // Pitching the wrist (rotation about X) is not wheel rotation.
        const float h = 30 / 57.2957795f * 0.5f;
        const float pitch[4]{std::sin(h), 0, 0, std::cos(h)};
        float base[4];
        AboutZ(25, base);
        // q = pitch * base
        one.in.right.q[0] = pitch[3] * base[0] + pitch[0] * base[3] + pitch[1] * base[2] - pitch[2] * base[1];
        one.in.right.q[1] = pitch[3] * base[1] - pitch[0] * base[2] + pitch[1] * base[3] + pitch[2] * base[0];
        one.in.right.q[2] = pitch[3] * base[2] + pitch[0] * base[1] - pitch[1] * base[0] + pitch[2] * base[3];
        one.in.right.q[3] = pitch[3] * base[3] - pitch[0] * base[0] - pitch[1] * base[1] - pitch[2] * base[2];
        o = one.Step();
        o = one.Step();
        Check(std::fabs(o.steer) < 0.05f, "v2: wrist pitch alone does not steer");
        // Return to centre: letting go drops the wheel angle; next grab starts at 0.
        one.in.right.grip = 0;
        o = one.Step();
        Check(!o.driving && o.steer == 0.0f && o.released, "v2: let go returns steering to centre (stick)");
        AboutZ(-60, one.in.right.q);
        one.Step();
        one.in.right.grip = 1;
        o = one.Step();
        Check(std::fabs(o.steer) < 0.05f, "v2: regrab at any wrist angle starts centred");
        // Bad quaternion: keep the last angle.
        AboutZ(-90, one.in.right.q);
        o = one.Step();
        const float held = o.steer;
        one.in.right.q[3] = std::numeric_limits<float>::quiet_NaN();
        o = one.Step();
        Check(o.steer == held, "v2: a bad hand orientation keeps the last angle");
        // v1 unchanged by default.
        Check(!Settings{}.rotation, "v1 measurement stays the logic default; config picks v2");
        const float forward[3]{0, 0, -1};
        Check(std::fabs(TwistAbout(base, base, forward)) < 1e-5f, "zero twist for no rotation");
    }
    std::printf("Vehicle wheel: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
