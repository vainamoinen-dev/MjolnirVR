// Reserve weapon identity: inventory selection, learning only stable
// handle/identity pairs, and Unknown (0) for anything never seen.
#include "../src/common/reserve_weapon_logic.h"
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

using namespace reserve_weapon;

int main()
{
    const uint32_t A = 0x00120004, B = 0x00340007, C = 0x00560009;
    const auto all = [](uint32_t) { return true; };
    const auto none = [](uint32_t) { return false; };
    {
        const uint32_t inv[4]{A, B, kNoHandle, kNoHandle};
        Check(SelectReserve(inv, 0, 0xFF, all) == B, "reserve is the other owned slot");
        Check(SelectReserve(inv, 1, 0xFF, all) == A, "after a swap the reserve flips");
        Check(SelectReserve(inv, 0, 0xFF, none) == kNoHandle, "unowned handle is never a reserve");
        Check(SelectReserve(inv, 0xFF, 0xFF, all) == kNoHandle, "no primary role -> none");
        Check(SelectReserve(inv, 4, 0xFF, all) == kNoHandle, "invalid primary role -> none");
        Check(SelectReserve(inv, 0, 1, all) == kNoHandle, "dual wielding both -> no reserve");
    }
    {
        const uint32_t inv[4]{A, B, C, kNoHandle};
        Check(SelectReserve(inv, 0, 1, all) == C, "dual wielding: the third weapon is the reserve");
        Check(SelectReserve(inv, 0, 0xFF, all) == kNoHandle, "two candidates are ambiguous -> none");
    }
    {
        const uint32_t inv[4]{A, 0x00000005, kNoHandle, kNoHandle};
        Check(SelectReserve(inv, 0, 0xFF, all) == kNoHandle, "unsalted handle rejected");
    }

    // Frame-rate observation helper: `held` drawn every 16 ms from `from` to
    // `to` inclusive, optionally with an unknown model (the arms) after each.
    const auto draw = [](Tracker& tr, uint32_t primary, uint64_t held, uint64_t from, uint64_t to, bool arms)
    {
        for (uint64_t ms = from; ms <= to; ms += 16)
        {
            tr.Observe(primary, held, ms);
            if (arms)
                tr.Observe(primary, 0, ms + 1);
        }
        if ((to - from) % 16)
            tr.Observe(primary, held, to); // always end exactly at `to`
    };

    Tracker t;
    Check(t.IdentityOf(A) == 0 && t.IdentityOf(kNoHandle) == 0, "nothing learned -> Unknown");
    draw(t, A, 111, 1000, 1000 + kStableMs - 16, false);
    Check(t.IdentityOf(A) == 0, "not learned before stable");
    draw(t, A, 111, 1000 + kStableMs, 1000 + kStableMs, false);
    Check(t.IdentityOf(A) == 111, "learned once stable");

    // Swap to B: the old model is still drawn briefly with B in the primary role.
    draw(t, B, 111, 5000, 5000 + kStableMs / 2, false);
    draw(t, B, 222, 5000 + kStableMs / 2 + 16, 5000 + kStableMs / 2 + 16 + kStableMs, false);
    Check(t.IdentityOf(B) == 222, "transient swap pairing is not learned; new primary learned after it settles");
    Check(t.IdentityOf(A) == 111, "reserve A keeps its identity");

    // An unknown model drawn alongside (Reach's first-person arms) is no
    // evidence: it neither restarts nor completes the window.
    draw(t, C, 333, 10000, 10000 + kStableMs - 16, true);
    Check(t.IdentityOf(C) == 0, "not learned before stable, unknowns in between");
    t.Observe(C, 0, 10000 + kStableMs);
    Check(t.IdentityOf(C) == 0, "an unknown observation never learns");
    t.Observe(C, 333, 10000 + kStableMs);
    Check(t.IdentityOf(C) == 333, "interleaved unknown models do not block learning");

    {
        // Reach pattern: weapon and arms every frame across a swap.
        Tracker r;
        const uint32_t D = 0x00070007, E = 0x00080008;
        draw(r, A, 111, 0, 2 * kStableMs, true);
        Check(r.IdentityOf(A) == 111, "Reach: weapon learned with the arms interleaved");
        const uint64_t swap = 10000;
        draw(r, D, 111, swap, swap, true); // old model drawn once more
        draw(r, D, 444, swap + 16, swap + 16 + 2 * kStableMs, true);
        Check(
            r.IdentityOf(D) == 444 && r.IdentityOf(A) == 111, "Reach: new primary learned, reserve keeps its identity");

        // Swap to a weapon whose model is NOT in the catalog: the old model is
        // drawn once at the swap, then only unknown models - and the stale
        // identity is drawn once more much later. It must never be learned.
        const uint64_t swap2 = 30000;
        draw(r, E, 444, swap2, swap2, true);
        for (uint64_t ms = swap2 + 16; ms <= swap2 + 2 * kStableMs; ms += 16)
            r.Observe(E, 0, ms);
        r.Observe(E, 444, swap2 + 2 * kStableMs + 16);
        Check(r.IdentityOf(E) == 0, "old model drawn once at a swap is never learned for a non-catalog weapon");
        // Even drawn a few times with gaps longer than kMaxGapMs.
        for (int i = 0; i < 10; ++i)
            r.Observe(E, 444, swap2 + 4 * kStableMs + i * (kMaxGapMs + 1));
        Check(r.IdentityOf(E) == 0, "sporadic draws with gaps never complete the window");
    }

    // No primary still breaks the candidate.
    Tracker g;
    draw(g, C, 333, 20000, 20000 + kStableMs / 2, false);
    g.Observe(kNoHandle, 333, 20000 + kStableMs / 2 + 16);
    draw(g, C, 333, 20000 + kStableMs / 2 + 32, 20000 + kStableMs + 32, false);
    Check(g.IdentityOf(C) == 0, "no primary restarts the stable window");
    draw(g, C, 333, 20000 + kStableMs + 48, 20000 + 2 * kStableMs + 48, false);
    Check(g.IdentityOf(C) == 333, "learned after a full window");

    // Clock going backwards restarts rather than learning.
    Tracker u;
    u.Observe(A, 111, 9000);
    draw(u, A, 111, 100, 100 + kStableMs - 16, false);
    Check(u.IdentityOf(A) == 0, "clock regression restarts the window");

    // Bounded memory: the oldest entry falls off.
    Tracker v;
    const uint32_t handles[5]{0x00010001, 0x00020002, 0x00030003, 0x00040004, 0x00050005};
    for (int i = 0; i < 5; ++i)
    {
        const uint64_t at = 1000u * 10 * (i + 1);
        draw(v, handles[i], 100 + i, at, at + kStableMs, false);
    }
    Check(v.IdentityOf(handles[0]) == 0 && v.IdentityOf(handles[4]) == 104 && v.IdentityOf(handles[1]) == 101,
        "four most recent weapons remembered");
    v.Reset();
    Check(v.IdentityOf(handles[4]) == 0, "reset forgets everything");

    // Halo 2 / CE: previously held weapon that the unit still owns.
    {
        PreviousHeld p;
        const auto ownsAB = [&](uint32_t h) { return h == A || h == B; };
        Check(p.Reserve(A, kNoHandle, all) == kNoHandle, "nothing held before: no reserve");
        p.Observe(A);
        Check(p.Reserve(A, kNoHandle, all) == kNoHandle, "first weapon only: no reserve");
        p.Observe(B);
        Check(p.Reserve(B, kNoHandle, ownsAB) == A, "swap: old weapon is the reserve");
        p.Observe(B);
        p.Observe(kNoHandle);
        Check(p.Reserve(B, kNoHandle, ownsAB) == A, "same weapon / empty sample changes nothing");
        p.Observe(A);
        Check(p.Reserve(A, kNoHandle, ownsAB) == B, "swap back");
        p.Observe(C);
        Check(p.Reserve(C, kNoHandle, [&](uint32_t h) { return h == C || h == B; }) == kNoHandle,
            "pickup that dropped the old weapon: not owned, Unknown");
        Check(p.Reserve(C, A, all) == kNoHandle, "held in the other hand: not a reserve");
        p.Reset();
        Check(p.Reserve(C, kNoHandle, all) == kNoHandle, "reset forgets");
    }
    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("reserve_weapon_tests: all passed\n");
    return 0;
}
