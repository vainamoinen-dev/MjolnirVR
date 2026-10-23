// Tests vehicle_comfort_logic.h with Halo 3 vehicle identities: the turn vignette, its entry delay,
// the rate measured per new pose, and hull roll following.
// Header-only; built as halomccvr_vehicle_comfort_tests. Pure logic: no game process.
#include "../src/common/vehicle_comfort_logic.h"
#include "../src/common/titles/h3/halo3_vehicle_logic.h"
#include <cmath>
#include <cstdio>
#include <limits>

using namespace vehicle_comfort;
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
}

int main()
{
    {
        // 45 deg/s turn sampled on 30 Hz ticks (33 ms) but rendered at 90 Hz.
        YawRate r;
        uint64_t now = 1000;
        float yaw = 0;
        r.Update(now, true, yaw);
        float rate = 0;
        for (int frame = 1; frame <= 200; ++frame)
        {
            now += 11;
            if (frame % 3 == 0)
                yaw += 45.0f / 57.2957795f * 0.033f;
            rate = r.Update(now, true, yaw);
            if (now - 1000 < kActivationDelayMs)
                Check(rate == 0.0f, "no vignette during the entry delay");
        }
        Check(rate > 40.0f && rate < 50.0f, "rate is measured per new 30 Hz pose, not per render frame");
        float later = 0;
        for (int frame = 0; frame < 30; ++frame)
        {
            now += 11;
            later = r.Update(now, true, yaw);
        }
        Check(later == 0.0f, "no new pose for a while: the turn is over");
        Check(r.Update(now + 11, false, yaw) == 0.0f, "not following: no rate");
        Check(r.Update(now + 22, true, std::numeric_limits<float>::quiet_NaN()) == 0.0f, "a bad yaw is ignored");
    }
    Check(VignetteTarget(5, 0.6f) == 0 && VignetteTarget(10, 0.6f) == 0 &&
              std::fabs(VignetteTarget(55, 0.6f) - 0.3f) < 1e-5f &&
              std::fabs(VignetteTarget(500, 0.6f) - 0.6f) < 1e-5f && VignetteTarget(500, 0) == 0,
        "vignette starts above 10 deg/s, full at 100 deg/s, scaled by strength");
    {
        Envelope e;
        uint64_t now = 1000;
        float v = e.Update(now, 0.6f);
        for (int i = 0; i < 15; ++i)
            v = e.Update(now += 11, 0.6f);
        Check(v > 0.35f && v < 0.6f, "the vignette comes in quickly (0.15 s)");
        for (int i = 0; i < 40; ++i)
            v = e.Update(now += 11, 0.6f);
        const float full = v;
        for (int i = 0; i < 15; ++i)
            v = e.Update(now += 11, 0.0f);
        Check(v > full * 0.5f, "and leaves more slowly (0.4 s)");
        for (int i = 0; i < 300; ++i)
            v = e.Update(now += 11, 0.0f);
        Check(v == 0.0f, "then fully clears");
    }
    {
        const float head[3]{0.1f, 1.2f, 0.0f};
        const DashPose d = CaptureDash(head, 0.0f);
        Check(d.valid && std::fabs(d.position[2] + kDashAheadM) < 1e-6f &&
                  std::fabs(d.position[1] - (1.2f - kDashBelowM)) < 1e-6f,
            "dash line sits ahead of and below the head at seat entry");
        const float bad[3]{std::numeric_limits<float>::quiet_NaN(), 0, 0};
        Check(!CaptureDash(bad, 0).valid, "a bad head pose captures nothing");
    }
    std::printf("Vehicle comfort: %u checks, %u failures\n", checks, failures);
    {
        const float seat[3]{0, 1.2f, 0}, near[3]{0.3f, 1.2f, 0}, mid[3]{0.55f, 1.2f, 0}, out[3]{0, 1.2f, 0.7f};
        Check(CabinFadeAlpha(near, seat, 0.5f) == 0.0f, "leaning inside the cabin keeps the view");
        Check(std::fabs(CabinFadeAlpha(mid, seat, 0.5f) - 0.5f) < 1e-4f, "half black 5 cm past the threshold");
        Check(CabinFadeAlpha(out, seat, 0.5f) == 1.0f && CabinFadeAlpha(out, seat, 0.0f) == 0.0f,
            "head out of the cabin is black; threshold 0 is off");
        // Seat-entry eye, sitting down on a real chair, recenter, safety rebase.
        CabinFade fade;
        float standing[3]{0, 1.7f, 0}, sat[3]{0, 1.05f, 0};
        fade.Capture(standing, 1);
        Check(fade.Update(1000, standing, 0.5f, 1) == 0.0f, "no fade at the captured eye");
        Check(fade.Update(1100, sat, 0.5f, 1) == 1.0f, "sitting down 0.65 m fades to black");
        Check(fade.Update(1100 + kCabinRebaseMs + 10, sat, 0.5f, 1) == 0.0f &&
                  fade.Update(1100 + kCabinRebaseMs + 20, sat, 0.5f, 1) == 0.0f,
            "the safety rebase never strands the player in black");
        fade.Capture(standing, 1);
        Check(fade.Update(5000, sat, 0.5f, 2) == 0.0f && fade.Update(5010, sat, 0.5f, 2) == 0.0f,
            "a position recenter re-captures the seated eye");
        fade.Capture(standing, 3);
        fade.Update(9000, sat, 0.5f, 3);
        fade.Update(9500, standing, 0.5f, 3);
        Check(fade.Update(10700, sat, 0.5f, 3) == 1.0f, "a brief lean does not rebase early");
    }
    {
        // On-foot smooth-turn vignette rate; snap turn never darkens.
        Check(SmoothTurnRate(true, 1.0f, 120.0f) == 120.0f && SmoothTurnRate(true, -0.5f, 120.0f) == 60.0f,
            "smooth turn rate follows the stick");
        Check(SmoothTurnRate(true, 0.1f, 120.0f) == 0.0f && SmoothTurnRate(false, 1.0f, 120.0f) == 0.0f &&
                  SmoothTurnRate(true, std::numeric_limits<float>::quiet_NaN(), 120.0f) == 0.0f,
            "dead zone, snap turn and bad input give no turn vignette");
        Check(VignetteTarget(SmoothTurnRate(true, 1.0f, 120.0f), 0.5f) > 0.0f &&
                  VignetteTarget(SmoothTurnRate(true, 1.0f, 120.0f), 0.0f) == 0.0f,
            "turn_vignette 0 is off");
        Check(kSnapTurnOptions[0] == 30.0f && kSnapTurnOptions[3] == 90.0f, "snap options 30/45/60/90");
    }
    {
        // Partial horizon hold: level under the hold, smooth blend, full roll by 2x.
        const float d = 0.01745329252f;
        Check(PartialHorizonRoll(5 * d, 6) == 0.0f && PartialHorizonRoll(-6 * d, 6) == 0.0f, "small tilts stay level");
        Check(std::fabs(PartialHorizonRoll(12 * d, 6) - 12 * d) < 1e-6f &&
                  std::fabs(PartialHorizonRoll(-30 * d, 6) + 30 * d) < 1e-6f,
            "beyond 2x the hold the view follows the roll");
        const float mid = PartialHorizonRoll(9 * d, 6);
        Check(mid > 0.0f && mid < 9 * d, "between hold and 2x it blends");
        bool smooth = true;
        float previous = 0.0f;
        for (int i = 0; i <= 200; ++i)
        {
            const float r = PartialHorizonRoll(i * 0.1f * d, 6);
            smooth &= r >= previous && r - previous < 0.5f * d;
            previous = r;
        }
        Check(smooth, "the blend has no step");
        Check(PartialHorizonRoll(40 * d, 180) == 0.0f && PartialHorizonRoll(3 * d, 0) == 3 * d &&
                  PartialHorizonRoll(std::numeric_limits<float>::quiet_NaN(), 6) == 0.0f,
            "180 never rolls, 0 always, NaN level");
        // Roll read from a hull basis round-trips through the follow composition.
        bool round = true;
        for (float yaw : {0.0f, 1.2f, -2.5f})
            for (float pitch : {0.0f, 0.3f, -0.2f})
                for (float roll : {0.0f, 0.2f, -0.6f})
                {
                    float basis[9]{}, recovered = 0.0f;
                    round &= Halo3ComposeRollStableFollowBasis(yaw, pitch, yaw, 0, 0, 0, basis, roll) &&
                             Halo3RollFromBasis(basis, basis + 6, recovered) && std::fabs(recovered - roll) < 1e-4f;
                }
        Check(round, "hull roll from the seat basis matches the follow basis roll");
        float level[9]{}, defaulted[9]{};
        Halo3ComposeRollStableFollowBasis(0.4f, 0.1f, 0.4f, 0.2f, 0.1f, 0.05f, level, 0.0f);
        Halo3ComposeRollStableFollowBasis(0.4f, 0.1f, 0.4f, 0.2f, 0.1f, 0.05f, defaulted);
        bool same = true;
        for (int i = 0; i < 9; ++i)
            same &= level[i] == defaulted[i];
        Check(same, "zero hull roll is the existing level composition");
    }
    return failures ? 1 : 0;
}
