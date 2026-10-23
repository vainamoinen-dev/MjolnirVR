// Tests hud_reveal_logic.h: the head touch that toggles the flashlight or reveals the HUD,
// including the hand that also drives the head-gesture D-pad.
// Header-only logic; built as halomccvr_hud_reveal_tests. No game process or headset needed.
#include "../src/common/hud_reveal_logic.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
static void Check(bool v, const char* message)
{
    if (!v)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
// Head touch: flashlight tap vs HUD-reveal dwell, never both from one touch.
static void HeadTouchTests()
{
    for (int mode = 0; mode < 3; ++mode)
    {
        const bool flash = mode != 1, reveal = mode != 0;
        hud_reveal::HeadTouch t;
        uint64_t now = 1000;
        const float r = .22f;
        unsigned flashes = 0;
        bool revealed = false;
        auto step = [&](float d, unsigned dt = 20)
        {
            now += dt;
            const auto o = t.Update(now, 7, true, d, r, flash, reveal);
            flashes += o.flashlight;
            revealed |= o.reveal;
            return o;
        };
        step(.10f);
        Check(!flashes && !revealed, "a hand already at the head when enabled does nothing");
        step(.40f);
        for (int i = 0; i < 15; ++i)
            step(.10f); // short touch: about 300 ms
        step(.40f);
        Check(flashes == (flash ? 1u : 0u), "a short touch toggles the flashlight exactly once");
        Check(revealed == (!flash && reveal), "a 300 ms touch reveals the HUD only when the flashlight gesture is off");
        flashes = 0;
        revealed = false;
        for (int i = 0; i < 50; ++i)
            step(.10f); // long touch: 1 s
        step(.40f);
        Check(revealed == reveal, "a long touch reveals the HUD when that is on");
        Check(flashes == (flash && !reveal ? 1u : 0u), "with the reveal on, a long touch never toggles the flashlight");
    }
    hud_reveal::HeadTouch t;
    uint64_t now = 1000;
    auto o = t.Update(now, 7, true, .4f, .22f, true, false);
    unsigned toggles = 0;
    for (int i = 0; i < 20; ++i)
    {
        now += 20;
        o = t.Update(now, 7, true, .1f, .22f, true, false);
        toggles += o.flashlight;
    }
    Check(toggles == 1, "holding the hand there does not repeat the toggle");
    now += 20;
    o = t.Update(now, 7, false, .1f, .22f, true, false);
    for (int i = 0; i < 10; ++i)
    {
        now += 20;
        o = t.Update(now, 7, true, .1f, .22f, true, false);
        Check(!o.flashlight, "an interruption needs the hand to leave before another toggle");
    }
    now += 20;
    o = t.Update(now, 7, true, .1f, .22f, false, false);
    Check(!o.flashlight && !o.reveal, "both options off does nothing");
}

// The flashlight head touch on the D-pad's hand.
static void DpadSharedHandTests()
{
    auto touch = [](bool useDpad, unsigned holdSteps)
    {
        hud_reveal::HeadTouch t;
        uint64_t now = 1000;
        unsigned flashes = 0;
        t.Update(now, 7, true, .4f, .22f, true, false, true, false);
        for (unsigned i = 0; i < holdSteps; ++i)
        {
            now += 20;
            flashes += t.Update(now, 7, true, .1f, .22f, true, false, true, useDpad && i == 3).flashlight;
        }
        now += 20;
        flashes += t.Update(now, 7, true, .4f, .22f, true, false, true, false).flashlight;
        return flashes;
    };
    Check(touch(false, 8) == 1, "A4-06: a short touch on the D-pad hand toggles the flashlight on release");
    Check(touch(true, 8) == 0, "A4-06: a touch that used the head D-pad (the ODST map) never toggles the flashlight");
    Check(touch(false, 40) == 0, "A4-06: holding the hand at the head (D-pad navigation) never toggles it");
    hud_reveal::HeadTouch t;
    uint64_t now = 1000;
    unsigned early = 0;
    t.Update(now, 7, true, .4f, .22f, true, false, true, false);
    for (int i = 0; i < 8; ++i)
    {
        now += 20;
        early += t.Update(now, 7, true, .1f, .22f, true, false, true, false).flashlight;
    }
    Check(early == 0, "A4-06: on the D-pad hand nothing fires while the hand is still at the head");
}

int main()
{
    HeadTouchTests();
    DpadSharedHandTests();
    for (float radius : {.10f, .22f, .40f})
    {
        hud_reveal::Gesture g;
        uint64_t now = 1000, epoch = 7;
        auto step = [&](float distance, bool ready = true)
        {
            now += 20;
            return g.Update(now, epoch, ready, distance, radius);
        };
        Check(!step(radius * .5f), "already-near hand cannot reveal at enable or title entry");
        Check(!step(radius + .1f), "moving away arms without revealing");
        for (int i = 0; i < 10; ++i)
            Check(!step(radius * .5f), "brief passes cannot trigger a reveal");
        Check(step(radius * .5f), "a deliberate dwell reveals");
        Check(step(radius + .02f), "hysteresis keeps a steady reveal at the edge");
        Check(!step(radius + .05f), "moving away hides without modifying preference");
        for (int i = 0; i < 11; ++i)
            step(radius * .5f);
        Check(!step(radius * .5f, false), "menus, grips, dual wield, reload and loss of tracking cancel");
        for (int i = 0; i < 15; ++i)
            Check(!step(radius * .5f), "blocked hand cannot replay on resume");
        step(radius + .1f);
        for (int i = 0; i < 11; ++i)
            step(radius * .5f);
        ++epoch;
        Check(!step(radius * .5f), "recenter or title generation cancels");
        step(radius + .1f);
        for (int i = 0; i < 11; ++i)
            step(radius * .5f);
        now += 101;
        Check(!step(radius * .5f), "stale input cancels and requires a fresh crossing");
        Check(!step(std::numeric_limits<float>::quiet_NaN()), "invalid pose never reveals");
    }
    hud_reveal::Publication p;
    for (auto title : {GameTitle::HaloCE, GameTitle::Halo2, GameTitle::Halo3, GameTitle::Halo3ODST,
             GameTitle::HaloReach, GameTitle::Halo4})
    {
        for (uint64_t now : {1000ull, (1ull << 29) - 50, (1ull << 32) - 50})
        {
            p.Publish(true, title, 12, now);
            Check(p.Active(title, 12, now + 90), "fresh owned reveal survives clock wrap");
            Check(!p.Active(title, 12, now + 101), "rendering cannot retain a stalled gesture");
            Check(!p.Active(title, 13, now), "foreign generation cannot reveal");
            Check(!p.Active(GameTitle::None, 12, now), "foreign title cannot reveal");
            Check(!p.Active(title, 12, now - 1), "backward clock cannot reveal");
            p.Publish(false, title, 12, now);
            Check(!p.Active(title, 12, now), "cancellation is immediate");
        }
    }
    std::puts("HUD reveal gesture and publication checks passed");
}
