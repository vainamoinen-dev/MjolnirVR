// Tests for the virtual flight stick that maps hand tilt to pitch and yaw (src/common/flight_stick_logic.h).
// Built as halomccvr_flight_stick_tests and run by ctest. Header-only logic: no game process, no headset.
#include "../src/common/flight_stick_logic.h"
#include <cstdio>
#include <limits>

using namespace flight_stick;
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

    // Quaternion for a rotation of `deg` about a unit axis.
    void AxisAngle(float x, float y, float z, float deg, float q[4])
    {
        const float h = deg / 57.2957795f * 0.5f, s = std::sin(h);
        q[0] = x * s;
        q[1] = y * s;
        q[2] = z * s;
        q[3] = std::cos(h);
    }
    void Mul(const float a[4], const float b[4], float o[4])
    {
        o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
        o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
        o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
        o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    }

    struct Rig
    {
        Stick stick;
        Settings c;
        Input in;
        float neutral[4];
        Rig()
        {
            in.now = 1000;
            in.available = true;
            in.handValid = true;
            in.head[1] = 1.2f;
            in.bodyForward[2] = -1;
            in.hand[0] = 0;
            in.hand[1] = 0.75f;
            in.hand[2] = -0.30f;
            // An arbitrary comfortable wrist pose at the grab.
            AxisAngle(1, 0, 0, -35.0f, neutral);
            for (int i = 0; i < 4; ++i)
                in.handQ[i] = neutral[i];
        }
        Output Step()
        {
            in.now += 11;
            return stick.Update(in, c);
        }
        Output Grab()
        {
            in.grip = 0;
            Step();
            in.grip = 1;
            return Step();
        }
        // Apply a world-frame rotation on top of the neutral pose.
        void Deflect(float x, float y, float z, float deg)
        {
            float r[4];
            AxisAngle(x, y, z, deg, r);
            Mul(r, neutral, in.handQ);
        }
    };
}

int main()
{
    {
        Rig r;
        const Output g = r.Grab();
        Check(g.grabbed && g.flying && g.consumeGrip && std::fabs(g.rx) < 1e-4f && std::fabs(g.ry) < 1e-4f,
            "grabbing at any wrist angle is neutral: no lurch");
        r.Deflect(1, 0, 0, -15.0f); // tip the top forward
        const Output fwd = r.Step();
        Check(fwd.ry < -0.40f && fwd.ry > -0.55f && std::fabs(fwd.rx) < 0.05f,
            "push forward: nose down, half stick at 15 deg");
        r.Deflect(1, 0, 0, 40.0f);
        Check(r.Step().ry == 1.0f, "pull back past full deflection: full nose up");
        r.Deflect(0.0f, -0.4226f, -0.9063f, 15.0f); // tip right about the 25-deg leaning shaft
        const Output side = r.Step();
        Check(side.rx > 0.40f && std::fabs(side.ry) < 0.05f, "tip right: yaw right");
        r.Deflect(0.0f, 0.9063f, -0.4226f, 20.0f); // twist left about the shaft
        Check(r.Step().rx < -0.20f, "twisting the wrist left adds left yaw");
        r.Deflect(1, 0, 0, -1.0f);
        const Output small = r.Step();
        Check(small.ry == 0.0f, "inside the 5 % deadzone the stick is centred");
        r.in.grip = 0.2f;
        const Output let = r.Step();
        Check(let.released && !let.flying, "letting go returns to hand-point flying");
    }
    {
        Rig r;
        r.in.grip = 1;
        r.in.hand[0] = 0.6f;
        r.Step();
        r.in.hand[0] = 0;
        Check(!r.Step().flying, "arriving with the grip already squeezed does not grab");
        Rig far;
        far.in.hand[0] = 0.5f;
        far.Grab();
        Check(!far.Step().flying, "a squeeze away from the stick does not grab");
        Rig off;
        off.Grab();
        off.in.available = false;
        Check(off.Step().released, "leaving the seat drops the stick");
        Rig lost;
        lost.Grab();
        lost.in.handQ[0] = std::numeric_limits<float>::quiet_NaN();
        lost.in.grip = 1;
        Check(!lost.Step().flying, "a lost hand pose drops out (the servo takes over)");
    }
    std::printf("Flight stick: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
