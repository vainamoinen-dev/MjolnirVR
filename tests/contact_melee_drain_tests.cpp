// Shared contact packet drain: same refusal rules, read limits, reset points
// and result reporting the five titles' inline loops had.
// Built as halomccvr_contact_melee_drain_tests (src/common/contact_melee_drain.h) and run by ctest. No game process.
#include "../src/common/contact_melee_drain.h"
#include <cstdio>
#include <deque>
#include <vector>

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

struct Frame
{
    uint32_t unit = 0;
    int id = 0;
};
struct Packet
{
    Frame frame{};
    uint64_t publishedAtMs = 0;
    uint32_t generation = 0;
};
struct Queue
{
    std::deque<Packet> items;
    bool Pop(Packet& out)
    {
        if (items.empty())
            return false;
        out = items.front();
        items.pop_front();
        return true;
    }
};
struct Backend
{
    int hand = -1;
};
enum class Result
{
    None,
    Applied,
    Rejected
};
struct Hand
{
    int resets = 0;
    std::vector<int> processed;
    float lastSpeed = 0;
    int lastBackendHand = -2;
    int motionResets = 0;
    void Reset() { ++resets; }
    void ResetMotion() { ++motionResets; }
    Result Process(const Frame& frame, float speed, Backend& backend, int64_t = 0)
    {
        processed.push_back(frame.id);
        lastSpeed = speed;
        lastBackendHand = backend.hand;
        return frame.id % 2 ? Result::Applied : Result::Rejected;
    }
};

static Packet P(int id, uint64_t at, uint32_t gen = 7, uint32_t unit = 42)
{
    return {{unit, id}, at, gen};
}

int main()
{
    using contact_melee::DrainHands;
    using contact_melee::DrainPolicy;
    const auto make = [](int hand)
    {
        Backend b{};
        b.hand = hand;
        return b;
    };

    // Fresh packets processed in order; results reported per hand.
    {
        Queue q[2];
        Hand h[2];
        q[0].items = {P(1, 1000), P(2, 1050)};
        q[1].items = {P(3, 1100)};
        std::vector<std::pair<int, Result>> results;
        DrainHands<Packet>(q, h, true, 7, 42, 1100, 2.5f, DrainPolicy{}, make,
            [&](int hand, Result r) { results.push_back({hand, r}); });
        Check((h[0].processed == std::vector<int>{1, 2}) && (h[1].processed == std::vector<int>{3}),
            "fresh packets processed");
        Check(h[0].lastSpeed == 2.5f && h[0].lastBackendHand == 0 && h[1].lastBackendHand == 1,
            "speed and per-hand backend");
        Check(results.size() == 3 && results[0] == std::pair{0, Result::Applied} &&
                  results[1] == std::pair{0, Result::Rejected} && results[2] == std::pair{1, Result::Applied},
            "every result reported");
        Check(h[0].resets == 0 && h[1].resets == 0, "admitted fresh drain never resets");
    }
    // Refusals reset the hand and skip; 100 ms inclusive; future rejected.
    {
        Queue q[2];
        Hand h[2];
        q[0].items = {P(1, 999), P(2, 1000), P(3, 1101), P(4, 1000, 8), P(5, 1000, 7, 43)};
        DrainHands<Packet>(q, h, true, 7, 42, 1100, 1, DrainPolicy{}, make, [](int, Result) {});
        Check((h[0].processed == std::vector<int>{2}) && h[0].resets == 2 && h[0].motionResets == 2,
            "R5 z58: age >100 and future restart the swing only (latch kept); other generation / unit reset; exactly 100 ms accepted");
    }
    // At most 8 packets per hand per tick.
    {
        Queue q[2];
        Hand h[2];
        for (int i = 0; i < 10; ++i)
            q[0].items.push_back(P(i, 1000));
        DrainHands<Packet>(q, h, true, 7, 42, 1000, 1, DrainPolicy{}, make, [](int, Result) {});
        Check(h[0].processed.size() == 8 && q[0].items.size() == 2, "eight packets per hand per tick");
    }
    // Not admitted: every popped packet resets, plus one reset after the drain
    // (also for an empty queue); Halo 4's policy skips the trailing reset.
    {
        Queue q[2];
        Hand h[2];
        q[0].items = {P(1, 1000), P(2, 1000)};
        DrainHands<Packet>(q, h, false, 7, 42, 1000, 1, DrainPolicy{}, make, [](int, Result) {});
        Check(h[0].processed.empty() && h[0].resets == 3 && h[1].resets == 1, "refused tick drains and resets");
        Queue r[2];
        Hand k[2];
        r[0].items = {P(1, 1000)};
        DrainPolicy keep{};
        keep.resetWhenRefused = false;
        DrainHands<Packet>(r, k, false, 7, 42, 1000, 1, keep, make, [](int, Result) {});
        Check(k[0].resets == 1 && k[1].resets == 0 && r[0].items.empty(), "no trailing reset when policy says so");
    }

    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("contact_melee_drain_tests: all passed\n");
    return 0;
}
