// Tests scope_render_scale_logic.h: the adaptive scope pass resolution (drop on missed frames,
// held recovery, backoff), driven with synthetic frame times.
// Header-only logic; built as halomccvr_scope_render_scale_tests. No game process or GPU needed.
#include "../src/common/scope_render_scale_logic.h"
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

using namespace scope_render_scale;

// Feeds n scoped frames, one every `ms`, every `missEvery`th one missed (0 = none); returns the last change.
static Change Run(Controller& c, uint64_t& now, int n, int missEvery, float budget = 1000.0f / 90.0f)
{
    Change last{};
    for (int i = 1; i <= n; ++i)
    {
        const bool miss = missEvery && i % missEvery == 0;
        const float ms = miss ? 2.0f * budget : budget;
        now += static_cast<uint64_t>(ms);
        const Change ch = c.Update(ms, true, budget, now);
        if (ch.changed)
            last = ch;
    }
    return last;
}

int main()
{
    Check(BudgetMs(90.0f) > 11.1f && BudgetMs(90.0f) < 11.12f && BudgetMs(0.0f) == BudgetMs(90.0f) &&
              BudgetMs(120.0f) < 8.4f,
        "budget follows the headset rate, 90 Hz when unknown");
    {
        Controller c;
        uint64_t now = 1000;
        Check(!Run(c, now, 2000, 0).changed && c.Scale() == 1.0f, "frames on budget stay full size");
        Check(!Run(c, now, 2000, 30).changed && c.Scale() == 1.0f,
            "a miss every 30 frames (2 per window) stays full size");
    }
    {
        Controller c;
        uint64_t now = 1000;
        Check(!Run(c, now, 44, 10).changed && Run(c, now, 1, 0).changed,
            "4 misses drop only once the 45-frame window is full");
        Controller e;
        uint64_t t = 1000;
        const Change d2 = Run(e, t, 60, 4);
        Check(d2.changed && d2.from == 1.0f && d2.to == kLowScale && e.Scale() == kLowScale &&
                  d2.misses >= kDropMisses && d2.frames == kWindowFrames && d2.holdMs == kHoldMs && d2.maxMs > d2.p50,
            "every 4th frame missed drops to half size");
        // Held: on budget for 9 s does not recover; the clean frames counted during the hold
        // let it recover the moment the 10 s hold ends (the counting window is inside the hold).
        Check(!Run(e, t, 800, 0).changed && e.Scale() == kLowScale, "held at half size during the hold");
        const Change r = Run(e, t, 400, 0);
        Check(r.changed && r.to == 1.0f && r.from == kLowScale && r.frames == kRecoverFrames && e.Scale() == 1.0f,
            "recovers to full size after the hold and 3 s on budget");
        Check(r.lowMs >= kHoldMs && r.lowMs < kHoldMs + 20, "z46: a clean hold recovers at its end (10 s, not 13 s)");
        // Relapse soon after: the hold doubles.
        const Change d3 = Run(e, t, 60, 4);
        Check(d3.changed && d3.holdMs == 2 * kHoldMs, "a relapse within 30 s doubles the hold");
        Check(!Run(e, t, 1500, 0).changed && e.Scale() == kLowScale, "the doubled hold is kept (~17 s)");
        Check(Run(e, t, 800, 0).changed && e.Scale() == 1.0f, "then recovers");
        // A long clean spell resets the hold.
        Run(e, t, 3600, 0);
        const Change d4 = Run(e, t, 60, 4);
        Check(d4.changed && d4.holdMs == kHoldMs, "a drop long after the last recovery starts at the first hold");
        uint64_t hold = 0;
        for (int i = 0; i < 8; ++i)
        {
            while (e.Scale() < 1.0f)
                Run(e, t, 1, 0);
            hold = Run(e, t, 60, 4).holdMs;
        }
        Check(hold == kMaxHoldMs, "repeated relapses cap the hold at 2 minutes");
    }
    {
        // Misses during the hold keep it low; the recovery window needs <= 1 miss in 270 frames.
        Controller c;
        uint64_t now = 1000;
        Run(c, now, 60, 4);
        Check(!Run(c, now, 1500, 100).changed && c.Scale() == kLowScale, "a miss every 100 frames never recovers");
    }
    {
        // Frames without a scope pass do not count and restart the window; the scale is kept.
        Controller c;
        uint64_t now = 1000;
        Run(c, now, 40, 4);
        c.Update(30.0f, false, 11.1f, now += 30);
        Check(!Run(c, now, 40, 0).changed && c.Scale() == 1.0f, "an unscoped frame restarts the window");
        Run(c, now, 60, 4);
        for (int i = 0; i < 2000; ++i)
            c.Update(11.1f, false, 11.1f, now += 11);
        Check(c.Scale() == kLowScale, "unscoped frames keep the half size (no change with the scope away)");
        c.Reset();
        Check(c.Scale() == 1.0f, "reset: full size");
    }
    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("scope render scale tests passed\n");
    return 0;
}
