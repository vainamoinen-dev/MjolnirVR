// Tests turret_handles_logic.h: grabbing the turret handles with both grips and the aim ray and
// look rotation they produce. Header-only; built as halomccvr_turret_handles_tests.
// Pure logic: no game process.
#include "../src/common/turret_handles_logic.h"
#include <cstdio>
#include <limits>

using namespace turret_handles;
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

    float YawDeg(const float q[4]) // of the -Z axis
    {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        const float fx = -2.0f * (x * z + w * y), fz = -(1.0f - 2.0f * (x * x + y * y));
        return std::atan2(-fx, -fz) * 57.2957795f;
    }
    float PitchDeg(const float q[4])
    {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        const float fy = -2.0f * (y * z - w * x);
        return std::asin(std::clamp(fy, -1.0f, 1.0f)) * 57.2957795f;
    }

    struct Rig
    {
        Handles handles;
        Settings c;
        Input in;
        Rig()
        {
            in.now = 1000;
            in.available = true;
            in.head[1] = 1.2f;
            in.bodyForward[2] = -1;
            Place(0, 0);
            in.left.valid = in.right.valid = true;
        }
        // Handles at rest offset by (dx, dy) metres.
        void Place(float dx, float dy)
        {
            in.left.pos[0] = -0.12f + dx;
            in.left.pos[1] = 0.90f + dy;
            in.left.pos[2] = -0.40f;
            in.right.pos[0] = 0.12f + dx;
            in.right.pos[1] = 0.90f + dy;
            in.right.pos[2] = -0.40f;
        }
        Output Step(unsigned dt = 11)
        {
            in.now += dt;
            return handles.Update(in, c);
        }
        Output Grab()
        {
            in.left.grip = in.right.grip = 0;
            Step();
            in.left.grip = in.right.grip = 1;
            return Step();
        }
        Output Settle()
        {
            Output o{};
            for (int i = 0; i < 150; ++i)
                o = Step();
            return o;
        }
    };
}

int main()
{
    {
        float q[4];
        const float fwd[3]{0, 0, -1};
        Check(LookRotation(fwd, q) && std::fabs(YawDeg(q)) < 1e-3f && std::fabs(PitchDeg(q)) < 1e-3f,
            "look rotation: straight ahead");
        const float left[3]{-1, 0, -1};
        LookRotation(left, q);
        Check(std::fabs(YawDeg(q) - 45.0f) < 0.1f, "look rotation: 45 deg left is +45 yaw");
    }
    {
        Rig r;
        const Output g = r.Grab();
        Check(g.grabbed && g.aiming && g.consumeLeft && g.consumeRight,
            "both grips on the handles grab and hide the grips");
        Check(std::fabs(g.origin[1] - 0.90f) < 1e-5f && std::fabs(g.origin[2] + 0.40f) < 1e-5f,
            "the aim ray starts at the handle-bar centre");
        const Output centred = r.Settle();
        Check(std::fabs(YawDeg(centred.orientation)) < 0.5f && std::fabs(PitchDeg(centred.orientation)) < 0.5f,
            "handles at rest aim straight ahead");
        r.Place(-0.10f, 0);
        const Output lever = r.Settle();
        Check(YawDeg(lever.orientation) < -10.0f, "lever: pushing the handles left swings the gun right");
        r.Place(0, 0.08f);
        const Output down = r.Settle();
        Check(PitchDeg(down.orientation) < -8.0f, "lever: lifting the handles points the gun down");
        r.in.left.grip = r.in.right.grip = 0.2f;
        const Output let = r.Step();
        Check(let.released && !let.aiming, "letting go hands the aim back to the controller");
    }
    {
        Rig r;
        r.c.lever = false;
        r.Grab();
        r.Place(-0.10f, 0.08f);
        const Output point = r.Settle();
        Check(YawDeg(point.orientation) > 10.0f && PitchDeg(point.orientation) > 8.0f,
            "point mode: the gun aims where the handles point");
    }
    {
        // PR153: blend from the controller's aim over 1 s, capped at 360 deg/s.
        Rig r;
        const float h = 0.7071068f;
        r.in.controller[0] = 0;
        r.in.controller[1] = h;
        r.in.controller[2] = 0;
        r.in.controller[3] = h; // 90 deg left
        const Output g = r.Grab();
        Check(std::fabs(YawDeg(g.orientation) - 90.0f) < 1.0f, "on grab the aim starts where the controller pointed");
        Output mid{};
        for (int i = 0; i < 45; ++i)
            mid = r.Step(); // ~0.5 s
        Check(YawDeg(mid.orientation) > 10.0f && YawDeg(mid.orientation) < 80.0f, "half way through the 1 s blend");
        const Output done = r.Settle();
        Check(std::fabs(YawDeg(done.orientation)) < 0.5f, "after the blend the handles own the aim");
        r.Place(0.30f, 0); // a huge lever throw in one frame
        float prev[4];
        for (int i = 0; i < 4; ++i)
            prev[i] = done.orientation[i];
        const Output step = r.Step(11);
        Check(AngleBetween(prev, step.orientation) * 57.2957795f <= 360.0f * 0.011f + 0.05f,
            "never faster than 360 deg/s");
    }
    {
        Rig r;
        r.in.left.grip = r.in.right.grip = 1;
        r.Place(-0.6f, 0);
        r.Step();
        r.Place(0, 0);
        Check(!r.Step().aiming, "arriving with the grips already squeezed does not grab");
        Rig one;
        one.in.right.grip = 0;
        one.Step();
        one.in.right.grip = 1;
        Check(one.Step().aiming, "one hand can take the handles");
        Rig off;
        off.Grab();
        off.in.available = false;
        Check(off.Step().released, "leaving the seat drops the handles");
        Rig bad;
        bad.in.right.pos[0] = std::numeric_limits<float>::quiet_NaN();
        bad.in.left.grip = 0;
        bad.Step();
        bad.in.left.grip = 1;
        Check(bad.Step().aiming, "a lost controller does not block the other hand");
    }
    {
        // Seat axis: captured at entry; a head turned 30 deg with hands centred still aims down the seat.
        SeatAxis axis;
        float f[3]{}, headEntry[3]{0, 0, -1};
        Check(!axis.Update(false, headEntry, f), "no axis outside a turret seat");
        Check(axis.Update(true, headEntry, f) && std::fabs(f[0]) < 1e-5f && std::fabs(f[2] + 1.0f) < 1e-5f,
            "axis captured from the entry facing");
        const float turned[3]{-std::sin(0.5235988f), 0, -std::cos(0.5235988f)}; // head 30 deg left
        Check(axis.Update(true, turned, f) && std::fabs(f[0]) < 1e-5f && std::fabs(f[2] + 1.0f) < 1e-5f,
            "turning the head does not move the axis");
        Rig r;
        for (int i = 0; i < 3; ++i)
            r.in.bodyForward[i] = f[i];
        const Output g = r.Grab();
        (void)g;
        const Output done = r.Settle();
        Check(std::fabs(YawDeg(done.orientation)) < 0.5f,
            "head turned 30 deg, hands centred: aim stays on the seat axis");
        axis.Update(false, turned, f);
        Check(axis.Update(true, turned, f) && std::fabs(f[0] + std::sin(0.5235988f)) < 1e-4f,
            "a new seat takes a fresh axis");
    }
    std::printf("Turret handles: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
