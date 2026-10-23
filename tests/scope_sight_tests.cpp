// Look-to-scope (thresholds from headset testing): gun level within 15 deg, scope 0.15-0.30 m
// from the eye; release below 0.12 m, beyond 0.35 m or past 25 deg.
#include "../src/common/scope_sight_logic.h"
#include <cmath>
#include <cstdio>
#include <limits>

using namespace scope_sight;
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
    const float kEye[3]{0.032f, 1.6f, 0.0f};
    // Lens straight ahead of the eye at `distance`, barrel pitched by `pitchDeg`.
    bool Run(Detector& d, uint64_t& now, float distance, float pitchDeg, float side = 0, int ticks = 6)
    {
        const float r = pitchDeg / 57.2957795f;
        const float forward[3]{0, std::sin(r), -std::cos(r)};
        const float lens[3]{kEye[0] + side, kEye[1], kEye[2] - distance};
        bool s = false;
        for (int i = 0; i < ticks; ++i)
            s = d.Update(now += 20, true, kEye, lens, forward, Settings{});
        return s;
    }
}

int main()
{
    {
        Detector d;
        uint64_t now = 1000;
        const float forward[3]{0, 0, -1};
        const float lens[3]{0.032f, 1.6f, -0.20f};
        Check(!d.Update(now, true, kEye, lens, forward, Settings{}), "no instant activation");
        Check(Run(d, now, 0.20f, 0), "spec pose: level gun, scope 0.20 m (8 in) from the eye engages after the dwell");
    }
    {
        Detector d;
        uint64_t now = 1000;
        Check(Run(d, now, 0.15f, 0), "6 in still engages");
    }
    {
        Detector d;
        uint64_t now = 1000;
        Check(Run(d, now, 0.29f, 14), "11+ in (0.29 m), pitched 14 deg up engages");
        Check(std::fabs(d.LastDistance() - 0.29f) < 0.005f && std::fabs(d.LastPitchDeg() - 14.0f) < 0.5f,
            "edge log values: last distance/pitch are what the gate measured");
    }
    {
        Detector d;
        uint64_t now = 1000;
        Check(Run(d, now, 0.20f, -14), "pitched 14 deg down engages");
    }
    {
        Detector d;
        uint64_t now = 1000;
        Check(Run(d, now, 0.20f, 0, 0.12f), "no yaw/side test: an offset scope at 0.2 m still counts");
    }
    {
        Detector d;
        uint64_t now = 1000;
        Check(!Run(d, now, 0.20f, 20), "gun pitched 20 deg is not level: no engage");
    }
    {
        Detector d;
        uint64_t now = 1000;
        Check(!Run(d, now, 0.10f, 0), "scope against the face (0.10 m): no engage");
    }
    {
        Detector d;
        uint64_t now = 1000;
        Check(!Run(d, now, 0.45f, 0), "level gun at the hip (0.45 m): no engage");
    }
    {
        Detector d;
        uint64_t now = 1000;
        Run(d, now, 0.20f, 0);
        Check(Run(d, now, 0.34f, 22, 0, 2), "hysteresis: 0.34 m / 22 deg keeps it engaged");
        Check(Run(d, now, 0.13f, 0, 0, 2), "hysteresis: 0.13 m keeps it engaged");
        Check(!Run(d, now, 0.20f, 26, 0, 1), "past 25 deg it releases");
    }
    {
        Detector d;
        uint64_t now = 1000;
        Run(d, now, 0.20f, 0);
        Check(!Run(d, now, 0.36f, 0, 0, 1), "beyond 0.35 m it releases");
    }
    {
        Detector d;
        uint64_t now = 1000;
        Run(d, now, 0.20f, 0);
        Check(!Run(d, now, 0.11f, 0, 0, 1), "below 0.12 m it releases");
    }
    {
        Detector d;
        uint64_t now = 1000;
        Run(d, now, 0.20f, 0);
        const float f[3]{0, 0, -1};
        const float lens[3]{0.032f, 1.6f, -0.2f};
        Check(!d.Update(now += 20, false, kEye, lens, f, Settings{}), "lost tracking ends sighting");
        Run(d, now, 0.20f, 0);
        Check(!d.Update(now += 500, true, kEye, lens, f, Settings{}), "a frame gap resets and needs a fresh dwell");
        const float bad[3]{std::numeric_limits<float>::quiet_NaN(), 0, 0};
        Check(!d.Update(now += 20, true, bad, lens, f, Settings{}), "a non-finite eye never sights");
    }
    Check(EyeSide(0, false) == 1 && EyeSide(0, true) == -1 && EyeSide(1, false) == -1 && EyeSide(2, true) == 1,
        "sighting eye: weapon side by default, or forced left/right");
    Check(Active(0, true, false) && !Active(0, false, true) && Active(1, false, true) && !Active(1, true, false) &&
              Active(2, true, false) && Active(2, false, true),
        "activation modes");
    {
        const GlassState hip = Resolve(true, true, 2, true, false);
        Check(hip.live && !hip.engaged,
            "S1: a held glass gun at the hip is live but not engaged (stick, smoothing untouched)");
        const GlassState raised = Resolve(true, true, 0, false, true);
        Check(raised.live && raised.engaged, "S1: raised to the eye engages even in button-only mode");
        const GlassState other = Resolve(true, false, 0, true, false);
        Check(other.live && other.engaged, "S1: helmet zoom keeps its button");
        const GlassState off = Resolve(false, true, 2, true, true);
        Check(!off.live && !off.engaged, "S1: scope disabled: nothing");
    }
    if (failures)
    {
        std::printf("%u failure(s)\n", failures);
        return 1;
    }
    std::printf("scope_sight_tests: %u checks passed\n", checks);
    return 0;
}
