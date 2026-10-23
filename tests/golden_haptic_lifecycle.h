#pragma once
// Shared golden driver for the native weapon-haptic install/remove/report lifecycles,
// included by the Halo 4, ODST and Reach *_weapon_haptics_lifecycle_golden_tests.cpp.
// Each title fixture fills a Spec with its CURRENT bindings, edges, hook order and exact log text;
// quirks are explicit fields so a fold that changes one fails here. Test-only: never in the DLL.
#include "golden_hook_fakes.h"
#include <algorithm>
#include <functional>

namespace golden
{
    struct Binding
    {
        uintptr_t rva;
        const char* pattern;
    };
    struct EdgeRow
    {
        uintptr_t site, target;
    };
    // A further proof (witness or extra binding) with the exact line it logs when broken.
    struct ExtraProof
    {
        uintptr_t rva;
        const char* pattern;
        std::string log;
    };

    struct HapticSpec
    {
        const char* name;
        size_t imageSize;
        size_t smallSize;         // an image size the title refuses
        const char* smallSizeLog; // nullptr: refused silently (before the binding proofs)
        bool rejectsZeroGeneration;
        std::vector<Binding> bindings;
        const char* bindingLogFormat; // printf format taking the RVA
        std::vector<EdgeRow> edges;
        const char* edgeLog; // printf format; may take the edge site
        std::vector<ExtraProof> extras;
        std::vector<uintptr_t> hookRvas;  // create/enable order
        std::vector<const void*> detours; // in hook order (also the quiescence list)
        Op retireOp;
        bool logAfterCleanup;   // the create/enable failure line follows the cleanup's own lines
        bool quiescesWhenEmpty; // removal waits for the detours even with no hook created
        const char* installedLog;
        const char* createFailLog;
        const char* enableFailLog;
        const char* disableFailLog;
        const char* busyLog;
        const char* removeFailLog;
        const char* reportHealthy; // exact line for enabled=1 fault=0 and zero counters
        const char* reportFaulted; // exact line for enabled=1 fault=1 and zero counters
        std::function<bool(uintptr_t, size_t, uint32_t)> install;
        std::function<bool()> remove;
        std::function<void()> report;
        std::function<bool()> anyTarget, enabled;
        std::function<std::atomic<uint32_t>&()> callbacks;
        std::function<std::atomic<bool>&()> faulted;
    };

    inline size_t PatternLength(const char* pattern)
    {
        size_t n = 0;
        for (const char* p = pattern; *p; ++p)
            if (*p != ' ' && (p == pattern || p[-1] == ' '))
                ++n;
        return n;
    }

    // The exact line the install logs when the byte at `rva` is corrupted: the
    // first proof, in the title's own check order, that covers that byte.
    inline std::string ExpectedFor(const struct HapticSpec& s, uintptr_t rva);

    inline unsigned g_checks = 0, g_failures = 0;
    inline void Check(bool ok, const std::string& what)
    {
        ++g_checks;
        if (!ok)
        {
            ++g_failures;
            std::printf("FAIL: %s\n", what.c_str());
        }
    }

    inline std::string ExpectedFor(const HapticSpec& s, uintptr_t rva)
    {
        char text[256];
        for (const auto& b : s.bindings)
            if (rva >= b.rva && rva < b.rva + PatternLength(b.pattern))
            {
                snprintf(text, sizeof(text), s.bindingLogFormat, static_cast<unsigned long long>(b.rva));
                return text;
            }
        for (const auto& e : s.edges)
            if (rva >= e.site && rva < e.site + 5)
            {
                snprintf(text, sizeof(text), s.edgeLog, static_cast<unsigned long long>(e.site));
                return text;
            }
        for (const auto& x : s.extras)
            if (rva >= x.rva && rva < x.rva + PatternLength(x.pattern))
                return x.log;
        return "(no proof covers this byte)";
    }

    inline int RunHapticLifecycle(const HapticSpec& s)
    {
        Image image(s.imageSize);
        if (!image.bytes)
            return 2;
        for (const auto& b : s.bindings)
            image.Pattern(b.rva, b.pattern);
        for (const auto& e : s.edges)
            image.Edge(e.site, e.target);
        for (const auto& x : s.extras)
            image.Pattern(x.rva, x.pattern);
        const uintptr_t base = image.Base();
        const std::string n = s.name;
        const size_t hooks = s.hookRvas.size();
        auto clean = [&]
        {
            Reset();
            if (s.anyTarget())
                (void)s.remove();
            Reset();
            s.callbacks() = 0;
            s.faulted() = false;
        };

        // Success: create in order with per-hook detours, then enable in the same order.
        clean();
        Check(s.install(base, s.imageSize, 7), n + ": install succeeds on a matching image");
        bool order = calls.size() == 2 * hooks;
        for (size_t i = 0; order && i < hooks; ++i)
            order = calls[i].op == Op::Create && calls[i].target == reinterpret_cast<void*>(base + s.hookRvas[i]) &&
                    calls[i].detour == s.detours[i] && calls[hooks + i].op == Op::Enable &&
                    calls[hooks + i].target == reinterpret_cast<void*>(base + s.hookRvas[i]);
        Check(order, n + ": create all in order with per-hook detours, then enable all in the same order");
        Check(s.enabled() && logs.size() == 1 && logs[0] == s.installedLog,
            n + ": enabled with the exact installed line");
        bool distinct = true;
        for (size_t i = 0; i < hooks; ++i)
            for (size_t j = i + 1; j < hooks; ++j)
                distinct = distinct && s.detours[i] != s.detours[j];
        Check(distinct, n + ": distinct detour addresses (no /OPT:ICF folding)");
        bool unwind = true;
        for (const void* detour : s.detours)
            unwind = unwind && HasUnwindRange(detour);
        Check(unwind, n + ": every detour has an unwind range (teardown quiescence can verify it)");

        Reset();
        Check(!s.install(base, s.imageSize, 7) && calls.empty(), n + ": install refused while installed");
        Reset();
        s.report();
        Check(logs.size() == 1 && logs[0] == s.reportHealthy && Count(Op::Remove) == 0,
            n + ": healthy report line, hooks kept");

        // Remove: retire kind, quiescence on the detours in hook order, remove each.
        Reset();
        Check(s.remove(), n + ": remove succeeds");
        const Op otherDisable = s.retireOp == Op::DisableRetire ? Op::DisableMh : Op::DisableRetire;
        Check(Count(s.retireOp) == hooks && Count(otherDisable) == 0 && Count(Op::Remove) == hooks,
            n + ": remove disables with its own retire kind, then removes each hook");
        Check(quiescenceFunctions.size() == 1 && quiescenceFunctions[0] == s.detours,
            n + ": quiescence on the detours in hook order");
        Check(!s.enabled() && !s.anyTarget() && logs.empty(), n + ": runtime cleared, nothing logged");

        clean();
        s.install(base, s.imageSize, 7);
        Reset();
        s.callbacks() = 1;
        Check(!s.remove() && Logged(s.busyLog) && Count(Op::Remove) == 0 && s.anyTarget(),
            n + ": busy callbacks keep the hooks");
        s.callbacks() = 0;
        Reset();
        disableStatus = MH_ERROR_MEMORY_PROTECT;
        Check(!s.remove() && Logged(s.disableFailLog) && quiescenceFunctions.empty(),
            n + ": disable failure line, no quiescence");
        disableStatus = MH_OK;
        Reset();
        removeStatus = MH_ERROR_MEMORY_PROTECT;
        Check(!s.remove() && Logged(s.removeFailLog), n + ": removal failure line");
        removeStatus = MH_OK;

        for (size_t failAt = 0; failAt < hooks; ++failAt)
        {
            clean();
            failCreateAt = int(failAt);
            Check(!s.install(base, s.imageSize, 7) && Logged(s.createFailLog) && Count(Op::Create) == failAt + 1 &&
                      Count(Op::Remove) == failAt && Count(Op::Enable) == 0 && !s.anyTarget() && !s.enabled(),
                n + ": create failure at hook " + std::to_string(failAt) + " removes the earlier hooks");
        }
        {
            // Ordering quirk: a create failure whose cleanup also fails.
            clean();
            failCreateAt = 1;
            removeStatus = MH_ERROR_MEMORY_PROTECT;
            (void)s.install(base, s.imageSize, 7);
            const bool after = logs.size() == 2 && logs[0] == s.removeFailLog && logs[1] == s.createFailLog;
            const bool before = logs.size() == 2 && logs[0] == s.createFailLog && logs[1] == s.removeFailLog;
            Check(s.logAfterCleanup ? after : before, n + ": failure line order relative to its cleanup");
            removeStatus = MH_OK;
            Reset();
            (void)s.remove();
            // Empty-set quiescence quirk: a create failure at the first hook.
            clean();
            failCreateAt = 0;
            (void)s.install(base, s.imageSize, 7);
            Check(quiescenceFunctions.size() == (s.quiescesWhenEmpty ? 1u : 0u), n + ": empty-set quiescence quirk");
        }
        for (size_t failAt = 0; failAt < hooks; ++failAt)
        {
            clean();
            failEnableAt = int(failAt);
            Check(!s.install(base, s.imageSize, 7) && Logged(s.enableFailLog) && Count(Op::Remove) == hooks &&
                      !s.enabled(),
                n + ": enable failure at hook " + std::to_string(failAt) + " removes every hook");
        }

        for (const auto& b : s.bindings)
        {
            clean();
            const uint8_t saved = image.bytes[b.rva];
            image.bytes[b.rva] ^= 0xFF;
            Check(!s.install(base, s.imageSize, 7) && Logged(ExpectedFor(s, b.rva).c_str()) && calls.empty(),
                n + ": missing binding +" + std::to_string(b.rva) + " refused with its exact line");
            image.bytes[b.rva] = saved;
        }
        {
            clean();
            const uintptr_t spare = s.imageSize - 0x200;
            image.Pattern(spare, s.bindings[0].pattern);
            char expected[256];
            snprintf(
                expected, sizeof(expected), s.bindingLogFormat, static_cast<unsigned long long>(s.bindings[0].rva));
            Check(!s.install(base, s.imageSize, 7) && Logged(expected) && calls.empty(),
                n + ": ambiguous binding refused");
            std::memset(image.bytes + spare, 0, 0x200);
        }
        for (const auto& e : s.edges)
        {
            clean();
            const std::string expected = ExpectedFor(s, e.site);
            const uint8_t saved = image.bytes[e.site];
            image.bytes[e.site] = 0x90;
            Check(!s.install(base, s.imageSize, 7) && Logged(expected.c_str()) && calls.empty(),
                n + ": each call edge is proven");
            image.bytes[e.site] = saved;
        }

        for (const auto& x : s.extras)
        {
            clean();
            const uint8_t saved = image.bytes[x.rva];
            image.bytes[x.rva] ^= 0xFF;
            Check(!s.install(base, s.imageSize, 7) && Logged(ExpectedFor(s, x.rva).c_str()) && calls.empty(),
                n + ": extra proof +" + std::to_string(x.rva) + " refused with its exact line");
            image.bytes[x.rva] = saved;
        }

        clean();
        const bool small = s.install(base, s.smallSize, 7);
        Check(!small && calls.empty() && (s.smallSizeLog ? Logged(s.smallSizeLog) && logs.size() == 1 : logs.empty()),
            n + ": undersized image refused (exact log quirk)");
        clean();
        const bool zero = s.install(base, s.imageSize, 0);
        Check(zero == !s.rejectsZeroGeneration, n + ": zero-generation quirk preserved");

        clean();
        s.install(base, s.imageSize, 7);
        Reset();
        s.faulted() = true;
        s.report();
        Check(logs.size() == 1 && logs[0] == s.reportFaulted && Count(Op::Remove) == hooks && !s.anyTarget(),
            n + ": faulted report logs then removes");
        Reset();
        s.report();
        Check(logs.empty() && calls.empty(), n + ": report with nothing installed is silent");

        clean();
        std::printf("%s haptic lifecycle golden: %u checks, %u failures\n", s.name, g_checks, g_failures);
        return g_failures ? 1 : 0;
    }
} // namespace golden
