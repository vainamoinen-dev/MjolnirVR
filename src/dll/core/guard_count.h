#pragma once
// One counter per exception-guard site. Every __except and catch handler in the
// DLL starts with GUARD_COUNT("reason"), which counts the hit and nothing else:
// no log line, no allocation, no lock, nothing that can fault. A site joins the
// hit list on its first hit, so a session without faults touches no shared
// state. The 10 s status block prints the list as one "guard summary:" line
// (GuardSummaryFormat): once at the first block, even at zero, so every log
// shows the counters ran, then only when the totals change. Nothing prints at
// DLL detach (the loader lock).
// Header-only: production .inl files that hold handlers are also built by tests.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

struct GuardSite
{
    // constexpr, so each site static is constant-initialised: no guard variable,
    // no constructor at run time, legal next to __try (no unwinding).
    constexpr GuardSite(const char* site, const char* why) noexcept : function(site), reason(why) {}
    const char* function;          // the function that holds the guard
    const char* reason;            // why the guard exists, in plain words
    std::atomic<uint32_t> hits{0}; // may wrap; linking does not depend on it
    std::atomic<bool> linked{false};
    GuardSite* next = nullptr; // written once, before the release push
};

// The sites hit so far, newest first. Pushed with release, read with acquire.
inline constinit std::atomic<GuardSite*> g_guardSitesHit{nullptr};

inline void GuardHit(GuardSite& site) noexcept
{
    site.hits.fetch_add(1, std::memory_order_relaxed);
    if (site.linked.load(std::memory_order_relaxed) || site.linked.exchange(true, std::memory_order_relaxed))
        return;
    GuardSite* head = g_guardSitesHit.load(std::memory_order_relaxed);
    do
        site.next = head;
    while (!g_guardSitesHit.compare_exchange_weak(head, &site, std::memory_order_release, std::memory_order_relaxed));
}

// The first statement of every handler body. __FUNCTION__ names the enclosing
// function (a lambda's reason says which lambda); never __FILE__, so no build
// paths end up in the DLL.
#define GUARD_COUNT(why)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        static constinit GuardSite guardSite_{__FUNCTION__, why};                                                      \
        GuardHit(guardSite_);                                                                                          \
    } while (false)

struct GuardTotals
{
    uint32_t sites = 0;
    uint64_t hits = 0;
};

// Writes "<S> sites, <H> hits" and, for at most kGuardSummarySites sites (most
// hit first), "; <function>: <reason> x<n>", then "; +<k> more". Returns the
// totals. Allocates nothing; a too-small buffer truncates the text.
inline constexpr size_t kGuardSummarySites = 12;
inline GuardTotals GuardSummaryFormat(char* out, size_t size) noexcept
{
    GuardTotals totals;
    const GuardSite* top[kGuardSummarySites]{};
    uint32_t topHits[kGuardSummarySites]{};
    size_t used = 0;
    for (const GuardSite* site = g_guardSitesHit.load(std::memory_order_acquire); site; site = site->next)
    {
        const uint32_t hits = site->hits.load(std::memory_order_relaxed);
        ++totals.sites;
        totals.hits += hits;
        size_t at = used < kGuardSummarySites ? used++ : kGuardSummarySites;
        if (at == kGuardSummarySites)
        {
            if (hits <= topHits[kGuardSummarySites - 1])
                continue;
            at = kGuardSummarySites - 1;
        }
        while (at > 0 && topHits[at - 1] < hits)
        {
            top[at] = top[at - 1];
            topHits[at] = topHits[at - 1];
            --at;
        }
        top[at] = site;
        topHits[at] = hits;
    }
    if (!out || !size)
        return totals;
    int n = std::snprintf(out, size, "%u sites, %llu hits", totals.sites, static_cast<unsigned long long>(totals.hits));
    for (size_t i = 0; i < used && n >= 0 && static_cast<size_t>(n) < size; ++i)
        n += std::snprintf(out + n, size - n, "; %s: %s x%u", top[i]->function, top[i]->reason, topHits[i]);
    if (totals.sites > used && n >= 0 && static_cast<size_t>(n) < size)
        std::snprintf(out + n, size - n, "; +%u more", static_cast<unsigned>(totals.sites - used));
    return totals;
}
