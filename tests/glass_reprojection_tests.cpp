// Tests for scope glass rotational reprojection: the CE Anniversary gun-camera picture is held and
// shifted between borrowed frames (src/common/glass_reprojection_logic.h).
// Built as halomccvr_glass_reprojection_tests and run by ctest. Pure logic: no D3D, no game process.
#include "../src/common/glass_reprojection_logic.h"
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

using namespace glass_reprojection;

int main()
{
    const V3 f{1, 0, 0}, up{0, 0, 1};
    const float tanV = 0.0736f, tanH = tanV * 1.387f;
    Shift s{};
    Check(Compute(f, up, f, tanH, tanV, s) && s.du == 0 && s.dv == 0 && s.degrees < 0.01f, "same aim: no shift");
    // Halo: +y is left. Turning right (toward -y) by a small angle moves the centre right (u+).
    const float a = 0.5f * 3.14159265f / 180.0f;
    Check(Compute(f, up, {std::cos(a), -std::sin(a), 0}, tanH, tanV, s) && s.du > 0 && std::fabs(s.dv) < 1e-6f &&
              std::fabs(s.du - std::tan(a) / (2 * tanH)) < 1e-4f && std::fabs(s.degrees - 0.5f) < 0.01f,
        "turning right shifts the sample right by tan/(2 tanH)");
    Check(Compute(f, up, {std::cos(a), 0, std::sin(a)}, tanH, tanV, s) && s.dv < 0 && std::fabs(s.du) < 1e-6f,
        "raising the aim shifts the sample up (v-)");
    // A rolled render camera: its own right/up axes are used.
    Check(Compute(f, {0, -1, 0}, {std::cos(a), 0, std::sin(a)}, tanH, tanV, s) && std::fabs(s.du) > 1e-3f &&
              std::fabs(s.dv) < 1e-6f,
        "a camera rolled 90 deg sees the raise as a sideways shift");
    Check(!Compute(f, up, {0, 1, 0}, tanH, tanV, s) && !Compute(f, up, {NAN, 0, 0}, tanH, tanV, s) &&
              !Compute(f, f, f, tanH, tanV, s),
        "no shift for a 90 deg turn, NaN, or a degenerate up");
    {
        // CE sniper glass 3:1 on a 1.387 picture, cap 0.25: the crop has horizontal room
        // (1 - 1/1.167)/2 = 0.071 and far more vertical room.
        const Room r = CropRoom(3.0f, 1.387f, 0.25f);
        Check(std::fabs(r.u - 0.0714f) < 0.002f && r.v > 0.25f, "sniper glass crop room");
        bool clamped = false;
        const Shift c = Clamp({0.2f, -0.01f, 0}, r, clamped);
        Check(clamped && std::fabs(c.du - r.u) < 1e-6f && c.dv == -0.01f,
            "a shift past the room stops at the picture edge");
        Clamp({0.01f, 0.01f, 0}, r, clamped);
        Check(!clamped, "a small shift is not clamped");
        const Room sq = CropRoom(1.0f, 1.387f, 0.25f);
        Check(std::fabs(sq.u - 0.5f * (1 - 1 / 1.387f)) < 1e-4f && sq.v == 0.0f, "square glass: side room only");
    }
    {
        // The CE sniper at 8x (unmargined horizontal tangent ~0.0875) gets 2.4 deg each side.
        const float tanH0 = 0.0875f, room = std::tan(kRoomDegrees * 3.14159265f / 180.0f);
        const float cap = CapForRoom(3.0f, 1.387f, 0.25f, tanH0, room);
        const float m = scope_window::GlassShiftMargin(3.0f, 1.387f, cap);
        const Room r = CropRoom(3.0f, 1.387f, cap);
        // uv room x picture width (2 tanH0 m) = tangent room each side.
        Check(cap > 0.25f && cap < 1.0f && r.u * 2.0f * tanH0 * m >= room - 1e-4f && std::fabs(m - 1.479f) < 0.01f,
            "z48: 8x sniper glass rendered ~1.48x wide leaves 2.4 deg of headroom");
        Check(
            CapForRoom(3.0f, 1.387f, 0.25f, 0.35f, room) == 0.25f, "z48: at 2x the parallax margin already covers it");
        Check(CapForRoom(1.0f, 1.387f, 0.25f, tanH0, room) == 0.25f, "z48: a square glass keeps its cap");
    }
    {
        // Borrow cadence from the gun's rate (90 Hz).
        const float t = 1.0f / 90.0f;
        Check(BorrowEvery(0.0f, t, 6) == 6 && BorrowEvery(5.0f, t, 6) == 6 && BorrowEvery(5.0f, t, 12) == 12,
            "z53: a still or slow gun keeps the setting (1 in 6 raised, 12 hip)");
        Check(BorrowEvery(20.0f, t, 6) == 4 && BorrowEvery(30.0f, t, 6) == 2 && BorrowEvery(90.0f, t, 12) == 2,
            "z53: 20 deg/s borrows 1 in 4, 30 deg/s and faster 1 in 2");
        const unsigned every = BorrowEvery(30.0f, t, 12);
        Check((every + kBorrowLatencyFrames) * t * 30.0f < kRoomDegrees,
            "z53: at 30 deg/s the oldest shown picture stays inside the 2.4 deg room");
        Check(
            BorrowEvery(std::nanf(""), t, 6) == 6 && BorrowEvery(30.0f, 0.0f, 6) == 6 && BorrowEvery(30.0f, t, 1) == 2,
            "z53: bad input keeps the setting; never below 1 in 2");
        {
            // A receipt that moves 1 deg every third 90 Hz frame (a 30 Hz tick) is 30 deg/s, not 90.
            GunRateMeter meter;
            RateHold held;
            float peak = 0.0f;
            for (int frame = 0; frame <= 30; ++frame)
            {
                const float aInner = static_cast<float>(frame / 3) * 3.14159265f / 180.0f;
                const float r = meter.Update({std::cos(aInner), std::sin(aInner), 0.0f}, frame * t);
                peak = held.Update(r, static_cast<uint64_t>(frame * t * 1000.0));
            }
            Check(peak > 28.0f && peak < 32.0f, "z53: tick-rate receipt changes read the true 30 deg/s");
            GunRateMeter still;
            float stillRate = 0.0f;
            for (int frame = 0; frame < 10; ++frame)
                stillRate += still.Update({1, 0, 0}, frame * t);
            Check(stillRate == 0.0f, "z53: a still gun reads 0");
        }
        RateHold hold;
        Check(hold.Update(40.0f, 1000) == 40.0f && hold.Update(5.0f, 1100) == 40.0f && hold.Update(5.0f, 1300) == 5.0f,
            "z53: the peak rate holds 250 ms, then follows the gun");
    }
    {
        // A refused pass keeps the last image; a half-drawn one is dropped.
        Check(
            ImageAfterPass(true, false, false) && ImageAfterPass(false, true, true) && ImageAfterPass(true, true, true),
            "A2-02: a pass that never drew keeps the image; a captured pass has one");
        Check(!ImageAfterPass(true, true, false) && !ImageAfterPass(false, false, false),
            "A2-02: a pass that drew but captured nothing drops the half-drawn image");
        Check(HeldImageFresh(false, 450) && !HeldImageFresh(false, 600) && HeldImageFresh(true, 5000),
            "A2-02: a held image older than 0.5 s is dropped (CE Anniversary borrows are exempt)");
        // A failed creation retries after 5 s instead of being off for the process.
        RetryLatch latch;
        Check(!latch.Blocked(1000), "A2-05: nothing failed yet");
        latch.Fail(1000);
        Check(latch.Blocked(1000) && latch.Blocked(5999) && !latch.Blocked(6000) && !latch.Blocked(500),
            "A2-05: blocked for 5 s, then retried (a clock step back is not a block)");
    }
    // The held picture is shown on every frame between borrows.
    Check(HeldImagePresent(true, false, false) && HeldImagePresent(false, true, true),
        "z52: a new borrow, or the held image of this CE module, keeps the glass picture");
    Check(!HeldImagePresent(false, false, true) && !HeldImagePresent(false, true, false),
        "z52: no image yet, or one from another title/module, is not shown");
    {
        // Half a texel in at both ends of the filled subrect, and the
        // high end is the last texel the pass actually filled (lround(s * size)).
        bool symmetric = true;
        for (unsigned size : {512u, 1024u, 1536u, 2048u})
            for (float sInner : {0.25f, 0.5f, 0.6667f, 0.75f, 0.8333f, 1.0f})
            {
                const auto b = SubrectClamp(sInner, size);
                const long filled = std::lround(sInner * size);
                symmetric = symmetric && std::fabs(b.low * size - 0.5f) < 1e-3f &&
                            std::fabs(b.high * size - (filled - 0.5f)) < 1e-3f &&
                            std::fabs((filled - b.high * size) - b.low * size) < 1e-3f;
            }
        Check(symmetric, "z67: the subrect clamp is half a texel in at both ends of the filled texels");
    }
    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("glass reprojection tests passed\n");
    return 0;
}
