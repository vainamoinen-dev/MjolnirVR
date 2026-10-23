// Tests turret_gyro_logic.h: measuring the hull turn rate and turning it into stick input that keeps
// a turret gun steady. Header-only; built as halomccvr_turret_gyro_tests. Pure logic: no game process.
#include "../src/common/turret_gyro_logic.h"
#include <cstdio>
#include <limits>

using namespace turret_gyro;
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
    bool Near(float a, float b, float eps = 1e-3f)
    {
        return std::fabs(a - b) <= eps;
    }
    constexpr float kDeg = 1.0f / 57.2957795f;

    struct Rig
    {
        Gyro g;
        Settings s;
        Hull h;
        uint64_t now{1000};
        Rig()
        {
            h.valid = true;
            h.generation = 7;
            h.sampleMs = now;
        }
        // One camera frame of 10 ms with the hull turning by (dyaw, dpitch) degrees.
        Output Frame(float dyawDeg, float dpitchDeg, float aimYaw = 0.0f)
        {
            now += 10;
            h.sampleMs = now;
            h.yaw = WrapPi(h.yaw + dyawDeg * kDeg);
            h.pitch += dpitchDeg * kDeg;
            return g.Update(true, h, aimYaw, now, s);
        }
    };
}

int main()
{
    {
        Rig r;
        Output o = r.g.Update(true, r.h, 0.0f, r.now, r.s);
        Check(o.active && o.rx == 0 && o.ry == 0, "first sample: no rate yet");
        o = r.Frame(1.0f, 0.0f); // 100 deg/s left
        Check(Near(o.yawDps, 100.0f, 0.5f), "hull yaw rate measured");
        Check(o.rx > 0.0f && Near(o.rx, (100 * kDeg) / 2.6f, 1e-3f), "hull turns left -> stick right, rate / gun rate");
        o = r.Frame(-1.0f, 0.0f);
        Check(o.rx < 0.0f, "hull turns right -> stick left");
        o = r.Frame(0.0f, 0.5f);
        Check(o.ry < 0.0f && Near(o.ry, -(50 * kDeg) / 2.6f, 1e-3f), "hull nose up -> lower the gun");
        o = r.Frame(0.0f, 0.5f, 90 * kDeg);
        Check(Near(o.ry, 0.0f, 1e-3f), "gun facing sideways gets no pitch share");
        o = r.Frame(0.0f, 0.5f, 180 * kDeg);
        Check(o.ry > 0.0f, "gun facing backwards: hull nose up lowers it, so raise it");
        o = r.Frame(0.01f, 0.0f);
        Check(o.rx == 0.0f, "1 deg/s is inside the deadband");
        // Strength scales; zero switches off.
        r.s.strength = 0.5f;
        o = r.Frame(1.0f, 0.0f);
        Check(Near(o.rx, 0.5f * (100 * kDeg) / 2.6f, 1e-3f), "strength scales the compensation");
        r.s.strength = 0.0f;
        o = r.Frame(1.0f, 0.0f);
        Check(!o.active && o.rx == 0 && o.ry == 0, "strength 0 is off");
        r.s.strength = 1.0f;
        // Clamped to a full stick.
        o = r.Frame(10.0f, 0.0f);
        Check(o.rx == 1.0f, "fast hull turn clamps to full stick");
    }
    {
        // Input polls faster than the camera: the rate is held between samples.
        Rig r;
        r.g.Update(true, r.h, 0, r.now, r.s);
        Output a = r.Frame(1.0f, 0.0f);
        Output b = r.g.Update(true, r.h, 0.0f, r.now + 4, r.s);
        Check(Near(a.rx, b.rx), "same sample re-polled keeps the same compensation");
        // Stale sample: off.
        Output c = r.g.Update(true, r.h, 0.0f, r.now + 150, r.s);
        Check(!c.active && c.rx == 0, "stale hull sample gives nothing");
    }
    {
        Rig r;
        r.g.Update(true, r.h, 0, r.now, r.s);
        Output o = r.Frame(40.0f, 0.0f);
        Check(o.rx == 0.0f, "a 40 deg jump between samples (teleport) gives no rate");
        r.h.generation = 8;
        o = r.Frame(1.0f, 0.0f);
        Check(o.rx == 0.0f, "new seat generation restarts without a rate");
        o = r.Frame(1.0f, 0.0f);
        Check(o.rx > 0.0f, "then compensates again");
        // Across +-pi the short way round.
        r.h.yaw = 179.5f * kDeg;
        o = r.Frame(0.0f, 0.0f);
        o = r.Frame(1.0f, 0.0f);
        Check(o.rx > 0.0f && Near(o.yawDps, 100.0f, 0.5f), "yaw wraps across +-180");
        r.h.pitch = std::numeric_limits<float>::quiet_NaN();
        o = r.Frame(0, 0);
        Check(!o.active, "bad hull pitch: nothing");
        Check(!r.g.Update(false, r.h, 0, r.now, r.s).active, "disabled: nothing");
    }
    {
        const Rates a = ReachRatesFor(ReachVehicleId::WarthogChaingun, 2.0f);
        Check(Near(a.yaw, 5.2f) && Near(a.pitch, 5.2f), "table rate times the config scalar");
        const Rates b = ReachRatesFor(ReachVehicleId::Warthog, 1.0f);
        Check(Near(b.yaw, kDefaultRates.yaw), "unlisted vehicle uses the default rate");
        const Rates c = ReachRatesFor(ReachVehicleId::WraithGunner, 99.0f);
        Check(Near(c.yaw, 2.6f * 4.0f), "scalar is clamped");
        const Rates w = LegacyRatesFor(2, 1.0f);
        Check(Near(w.yaw, 1.047f) && Near(w.pitch, 2.6f), "H3/ODST Warthog gunner: 60 deg/s yaw cap, pitch uncapped");
        const Rates d = LegacyRatesFor(1, 0.5f);
        Check(Near(d.yaw, 1.3f), "other mounted guns: default rate x scalar");
        // A 60 deg/s gun needs a bigger stick for the same hull rate.
        Rig slow;
        slow.s.rateYaw = w.yaw;
        slow.g.Update(true, slow.h, 0, slow.now, slow.s);
        const Output so = slow.Frame(0.3f, 0.0f); // 30 deg/s
        Check(Near(so.rx, (30 * kDeg) / 1.047f, 1e-3f), "compensation scales with the gun's own rate");
    }
    {
        Check(!DirectionLearned(0.0f, 6.0f) && !DirectionLearned(2.0f, 6.0f) && !DirectionLearned(-2.9f, 6.0f),
            "H2: no gyro on an axis before its stick direction is learned");
        Check(DirectionLearned(3.0f, 6.0f) && DirectionLearned(-6.0f, 6.0f),
            "H2: gyro once half the evidence cap is reached");
        Check(!DirectionLearned(std::numeric_limits<float>::quiet_NaN(), 6.0f) && !DirectionLearned(6.0f, 0.0f),
            "H2: bad evidence or cap: no gyro");
    }
    std::printf("Turret gyro: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
