// Tests the exception-guard counters (src/dll/core/guard_count.h): GUARD_COUNT inside a real __except handler
// (the constant-initialised site static next to __try), one list entry per site under 8 threads, a wrapped
// counter that never links a site twice, and the "guard summary:" text with its 12-site cap.
// Built as halomccvr_guard_count_tests. No game process or headset needed.
#include <Windows.h>
#include "../src/dll/core/guard_count.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

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

    size_t ListedTimes(const GuardSite* wanted)
    {
        size_t n = 0;
        for (const GuardSite* s = g_guardSitesHit.load(std::memory_order_acquire); s && n < 1000; s = s->next)
            n += s == wanted;
        return n;
    }

    size_t ListLength()
    {
        size_t n = 0;
        for (const GuardSite* s = g_guardSitesHit.load(std::memory_order_acquire); s && n < 1000; s = s->next)
            ++n;
        return n;
    }

    // The production shape: a site static in a handler of a function that uses __try.
    int ReadOrMinusOne(const volatile int* p)
    {
        __try
        {
            return *p;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("the test read through a null pointer");
            return -1;
        }
    }

    void HitShared()
    {
        GUARD_COUNT("eight threads hit this site");
    }
}

int main()
{
    // Nothing hit yet: the summary is the zero line.
    char text[1536];
    GuardTotals totals = GuardSummaryFormat(text, sizeof(text));
    Check(totals.sites == 0 && totals.hits == 0 && std::strcmp(text, "0 sites, 0 hits") == 0,
        "no hits gives the zero summary");

    // A real fault in a __try function is counted once, and its site is listed once.
    const int value = 7;
    Check(ReadOrMinusOne(&value) == 7, "a good read returns the value and counts nothing");
    Check(ListLength() == 0, "a fault-free call links no site");
    Check(ReadOrMinusOne(nullptr) == -1 && ReadOrMinusOne(nullptr) == -1, "a faulting read takes the handler");
    totals = GuardSummaryFormat(text, sizeof(text));
    Check(totals.sites == 1 && totals.hits == 2, "two faults at one site: 1 site, 2 hits");
    Check(std::strstr(text, ": the test read through a null pointer x2") != nullptr,
        "the summary names the site's reason and count");
    Check(std::strncmp(text, "1 sites, 2 hits; ", 17) == 0, "the summary starts with the totals");

    // Eight threads hitting one site link it once and count every hit.
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t)
        threads.emplace_back(
            []
            {
                for (int i = 0; i < 10000; ++i)
                    HitShared();
            });
    for (auto& t : threads)
        t.join();
    totals = GuardSummaryFormat(text, sizeof(text));
    Check(ListLength() == 2 && totals.hits == 2 + 80000, "8 threads: one new list entry, every hit counted");

    // A wrapped counter must not link its site again (the list would form a cycle).
    static constinit GuardSite wrapped{"WrapTest", "the counter wraps"};
    wrapped.hits.store(UINT32_MAX, std::memory_order_relaxed);
    GuardHit(wrapped); // first hit: links, and the counter wraps to 0
    GuardHit(wrapped); // counter 1
    GuardHit(wrapped); // counter 2
    Check(ListedTimes(&wrapped) == 1 && ListLength() == 3, "a wrapped counter keeps its site listed once");
    Check(wrapped.hits.load() == 2, "the counter wraps instead of linking again");

    // The summary lists at most 12 sites, most hit first, then "+<k> more".
    static constinit GuardSite many[12]{{"S0", "r"}, {"S1", "r"}, {"S2", "r"}, {"S3", "r"}, {"S4", "r"}, {"S5", "r"},
        {"S6", "r"}, {"S7", "r"}, {"S8", "r"}, {"S9", "r"}, {"S10", "r"}, {"S11", "r"}};
    for (int i = 0; i < 12; ++i)
        for (int k = 0; k <= 100 + i; ++k)
            GuardHit(many[i]);
    totals = GuardSummaryFormat(text, sizeof(text));
    Check(totals.sites == 15, "15 sites hit in all");
    Check(std::strstr(text, "; +3 more") != nullptr, "beyond 12 sites the rest are counted, not listed");
    const char* first = std::strstr(text, "HitShared");
    const char* s11 = std::strstr(text, "S11:");
    const char* s1 = std::strstr(text, "S1:");
    Check(first && s11 && s1 && first < s11 && s11 < s1, "most-hit sites come first");
    // 13 sites have more than 2 hits; the 13th (S0, 101 hits) and the two 2-hit sites are left out.
    Check(!std::strstr(text, "S0:") && !std::strstr(text, "WrapTest") && !std::strstr(text, "null pointer"),
        "the least-hit sites are the ones left out");

    // A short buffer truncates without overrunning.
    char small[24];
    std::memset(small, 'Z', sizeof(small));
    GuardSummaryFormat(small, 16);
    Check(std::memchr(small, '\0', 16) != nullptr && small[16] == 'Z', "a short buffer is truncated, not overrun");

    std::printf("guard count tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
