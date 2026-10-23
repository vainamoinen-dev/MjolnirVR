// Halo 2 look tests: MinHook detours on the native action-test builder and readers that hand a
// polled look test the head's turn, then restore the real input so facing and aim never change.
// Installed by the observer core (halo2_observer_6dof.cpp), driven from game.cpp.
// Any missing or ambiguous signature leaves stock behaviour.
#include "halo2_look_tests.h"
#include "../../core/hook_install_check.h" // MinHook create/remove go through the install-time detour check

#include <windows.h>
#include <MinHook.h>
#include <atomic>

#include "../../../common/head_look_test.h"
#include "../../../common/log.h"
#include "../../core/hook_quiescence.h"
#include "../../core/sigscan.h"
#include "../../../common/minhook_lifecycle.h"
#include "../../core/vr.h"
#include "../../core/look_test_feed.h"
#include "../../core/guard_count.h"

namespace
{
    // Builder entry, the five readers the look_relative evaluates call, and
    // the reset: each loads the action-test globals pointer rip-relatively.
    constexpr const char* kBuilderSig = "F6 42 24 04 4C 8B CA 4C 8B 05 ?? ?? ?? ?? 74 05 41 83 48 04 01";
    constexpr size_t kBuilderLoad = 7, kBuilderLoadSize = 7;
    // The builder's look block: +0x14 pitch > 0 -> 0x400 (up) / < 0 -> 0x800,
    // +0x10 yaw > 0 -> 0x1000 (left) / < 0 -> 0x2000.
    constexpr const char* kLookBlockSig = "F3 0F 10 42 14 0F 2F C2 76 0A 41 81 48 04 00 04 00 00 EB 0D 0F 2F D0 "
                                          "76 08 41 81 48 04 00 08 00 00 F3 0F 10 42 10 0F 2F C2 76 0A 41 81 48 "
                                          "04 00 10 00 00 EB 0D 0F 2F D0 76 08 41 81 48 04 00 20 00 00";
    constexpr const char* kResetSig = "48 8B 05 ?? ?? ?? ?? 33 C9 48 89 48 04 C3";
    // Order = head_look_test::Poll (up, down, left, right, all).
    constexpr const char* kReaderSigs[head_look_test::PollCount] = {"48 8B 05 ?? ?? ?? ?? 8B 40 04 C1 E8 0A 24 01 C3",
        "48 8B 05 ?? ?? ?? ?? 8B 40 04 C1 E8 0B 24 01 C3", "48 8B 05 ?? ?? ?? ?? 8B 40 04 C1 E8 0C 24 01 C3",
        "48 8B 05 ?? ?? ?? ?? 8B 40 04 C1 E8 0D 24 01 C3", "48 8B 05 ?? ?? ?? ?? F7 40 04 00 3C 00 00 0F 95 C0 C3"};
    // Halo 2's look bits sit 3 above the shared head_look_test bits (7-10).
    constexpr int kBitShift = 3;

    using BuilderFn = void(__fastcall*)(void*, unsigned char*);
    using ReaderFn = bool(__fastcall*)();

    struct Hook
    {
        void* target{};
        void* original{};
        bool enabled{};
    };
    Hook g_builder{};
    Hook g_readers[head_look_test::PollCount]{};
    head_look_test::PollTracker g_polls;
    std::atomic<bool> g_installed{false}, g_enabled{true};
    std::atomic<uint32_t> g_generation{0}, g_callbacks{0};
    std::atomic<uintptr_t> g_moduleBase{0};
    std::atomic<uint64_t> g_fedBits{0}, g_fedTicks{0};
    uintptr_t g_globals = 0; // address of the action-test globals pointer
    uint32_t g_rejectedGeneration = 0;
    uint64_t g_lastReportMs = 0;

    template <int Poll>
    __declspec(noinline) bool __fastcall ReaderHook()
    {
        g_callbacks.fetch_add(1, std::memory_order_acq_rel);
        bool result = false;
        __try
        {
            g_polls.Note(static_cast<head_look_test::Poll>(Poll), GetTickCount64());
            const auto original = reinterpret_cast<ReaderFn>(g_readers[Poll].original);
            if (original)
                result = original();
        }
        __finally
        {
            g_callbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
        return result;
    }

    // The action-test globals word (look bits 10..13), under this title's SEH.
    bool ReadLookWord(uint64_t& word) noexcept
    {
        __try
        {
            auto* globals = *reinterpret_cast<unsigned char* volatile*>(g_globals);
            if (!globals)
                return false;
            word = *reinterpret_cast<volatile uint32_t*>(globals + 4);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading Halo 2's look-test word faulted");
            return false;
        }
    }

    uint64_t BitsToFeed() noexcept
    {
        static head_look_test::Anchor anchor;
        return look_test_feed::BitsToFeed(anchor, g_polls, g_installed, g_enabled, kBitShift, ReadLookWord);
    }

    void BuilderBody(void* context, unsigned char* input)
    {
        const auto original = reinterpret_cast<BuilderFn>(g_builder.original);
        if (!original)
            return;
        const uint64_t feed = input ? BitsToFeed() : 0;
        if (!feed)
        {
            original(context, input);
            return;
        }
        // The builder clamps these and may abs/negate pitch (+0x6C0360..).
        look_test_feed::FeedIdleAxes(reinterpret_cast<float*>(input + 0x10), reinterpret_cast<float*>(input + 0x14),
            feed, g_fedBits, g_fedTicks, [&] { original(context, input); });
    }

    __declspec(noinline) void __fastcall BuilderHook(void* context, unsigned char* input)
    {
        g_callbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            BuilderBody(context, input);
        }
        __finally
        {
            g_callbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    bool AnyHookRetained() noexcept;
    Hook* AllHooks(size_t i) noexcept
    {
        return i == 0 ? &g_builder : &g_readers[i - 1];
    }
    constexpr size_t kHookCount = 1 + head_look_test::PollCount;

    bool AnyHookRetained() noexcept
    {
        for (size_t i = 0; i < kHookCount; ++i)
            if (AllHooks(i)->target)
                return true;
        return false;
    }

    bool Remove(const char* reason) noexcept
    {
        g_installed.store(false, std::memory_order_release);
        for (size_t i = 0; i < kHookCount; ++i)
        {
            Hook* hook = AllHooks(i);
            if (!hook->target || !hook->enabled)
                continue;
            const MH_STATUS status = MCCVR_DisableHookForRetirement(hook->target);
            if (status != MH_OK && status != MH_ERROR_DISABLED && status != MH_ERROR_NOT_CREATED)
                return false; // retained; retried on the next retirement
            hook->enabled = false;
        }
        const void* functions[] = {reinterpret_cast<const void*>(&BuilderHook),
            reinterpret_cast<const void*>(&ReaderHook<0>), reinterpret_cast<const void*>(&ReaderHook<1>),
            reinterpret_cast<const void*>(&ReaderHook<2>), reinterpret_cast<const void*>(&ReaderHook<3>),
            reinterpret_cast<const void*>(&ReaderHook<4>), reinterpret_cast<const void*>(&BuilderBody)};
        const void* trampolines[] = {g_builder.original, g_readers[0].original, g_readers[1].original,
            g_readers[2].original, g_readers[3].original, g_readers[4].original, nullptr};
        static_assert(std::size(functions) == std::size(trampolines));
        if (!WaitForNativeDetourQuiescence(functions, trampolines, std::size(functions), g_callbacks))
            return false;
        for (size_t i = 0; i < kHookCount; ++i)
        {
            Hook* hook = AllHooks(i);
            if (!hook->target)
                continue;
            const MH_STATUS status = MCCVR_RemoveHookChecked(hook->target);
            if (status != MH_OK && status != MH_ERROR_NOT_CREATED)
                return false; // this and later hooks retained for the next attempt
            *hook = {};
        }
        g_polls.Clear();
        g_globals = 0;
        g_moduleBase.store(0, std::memory_order_release);
        g_generation.store(0, std::memory_order_release);
        if (reason)
            LOG("Halo 2 look tests: removed (%s)", reason);
        return true;
    }

    // The unique match of `pattern` whose rip-relative load (at loadOffset,
    // 7 bytes) resolves to `globals`; 0 when missing or ambiguous.
    uintptr_t FindLoading(
        uintptr_t base, size_t size, const char* pattern, size_t loadOffset, uintptr_t globals) noexcept
    {
        uintptr_t found = 0;
        for (uintptr_t at = sig::Find(base, size, pattern); at; at = sig::Find(at + 1, base + size - at - 1, pattern))
        {
            const uintptr_t target = sig::RipTarget(at + loadOffset + 3, at + loadOffset + 7);
            if (globals && target != globals)
                continue;
            if (found)
                return 0;
            found = at;
        }
        return found;
    }

    bool Install(uintptr_t base, size_t size, uint32_t generation) noexcept
    {
        const uintptr_t builder = sig::Find(base, size, kBuilderSig);
        const bool builderUnique = builder && !sig::Find(builder + 1, base + size - builder - 1, kBuilderSig);
        const uintptr_t globals =
            builderUnique ? sig::RipTarget(builder + kBuilderLoad + 3, builder + kBuilderLoad + kBuilderLoadSize) : 0;
        const uintptr_t look = sig::Find(base, size, kLookBlockSig);
        bool proven = builderUnique && globals >= base && globals < base + size && look && look > builder &&
                      look - builder < 0x100 && !sig::Find(look + 1, base + size - look - 1, kLookBlockSig) &&
                      FindLoading(base, size, kResetSig, 0, globals);
        uintptr_t readers[head_look_test::PollCount]{};
        for (int i = 0; proven && i < head_look_test::PollCount; ++i)
        {
            readers[i] = FindLoading(base, size, kReaderSigs[i], 0, globals);
            proven = readers[i] != 0;
        }
        if (!proven)
        {
            LOG("Halo 2 look tests: builder/reader/reset proof missing or ambiguous "
                "(builder=+0x%llX); look tutorials stay stick-only",
                static_cast<unsigned long long>(builder ? builder - base : 0));
            g_rejectedGeneration = generation;
            return false;
        }
        struct Spec
        {
            uintptr_t target;
            void* detour;
            Hook* hook;
        };
        const Spec specs[kHookCount] = {{builder, reinterpret_cast<void*>(&BuilderHook), &g_builder},
            {readers[0], reinterpret_cast<void*>(&ReaderHook<0>), &g_readers[0]},
            {readers[1], reinterpret_cast<void*>(&ReaderHook<1>), &g_readers[1]},
            {readers[2], reinterpret_cast<void*>(&ReaderHook<2>), &g_readers[2]},
            {readers[3], reinterpret_cast<void*>(&ReaderHook<3>), &g_readers[3]},
            {readers[4], reinterpret_cast<void*>(&ReaderHook<4>), &g_readers[4]}};
        g_globals = globals;
        g_moduleBase.store(base, std::memory_order_release);
        g_generation.store(generation, std::memory_order_release);
        for (const Spec& spec : specs)
        {
            void* original = nullptr;
            const MH_STATUS created =
                MCCVR_CreateHookChecked(reinterpret_cast<void*>(spec.target), spec.detour, &original);
            if (created != MH_OK || !original)
            {
                LOG("Halo 2 look tests WITHHELD: hook create=%d at +0x%llX", static_cast<int>(created),
                    static_cast<unsigned long long>(spec.target - base));
                if (created == MH_OK)
                    (void)MCCVR_RemoveHookChecked(reinterpret_cast<void*>(spec.target));
                (void)Remove(nullptr);
                g_rejectedGeneration = generation;
                return false;
            }
            spec.hook->target = reinterpret_cast<void*>(spec.target);
            spec.hook->original = original;
        }
        for (const Spec& spec : specs)
        {
            if (MH_EnableHook(spec.hook->target) != MH_OK)
            {
                LOG("Halo 2 look tests WITHHELD: hook enable failed");
                (void)Remove("install rollback");
                g_rejectedGeneration = generation;
                return false;
            }
            spec.hook->enabled = true;
        }
        g_installed.store(true, std::memory_order_release);
        LOG("Halo 2 look tests: builder +0x%llX and the five look-test readers hooked; "
            "while a script polls a look test, head look reaches the builder as look "
            "input (halo2_tutorial_head_look=%d)",
            static_cast<unsigned long long>(builder - base), g_enabled.load() ? 1 : 0);
        return true;
    }

    void Report() noexcept
    {
        look_test_feed::Report(g_lastReportMs, g_fedBits, g_fedTicks, [](const char* letters, unsigned long long ticks)
            { LOG("Halo 2 look tests: head look fed %s on %llu builder tick(s)", letters, ticks); });
    }
}

bool Halo2LookTests_Install(uintptr_t moduleBase, size_t moduleSize, uint32_t generation) noexcept
{
    if (g_installed.load(std::memory_order_acquire))
        return true;
    if (g_builder.target || !moduleBase || !moduleSize)
    {
        if (g_builder.target)
            LOG("Halo 2 look tests: prior hooks retained; not reinstalling");
        return false;
    }
    if (g_rejectedGeneration == generation)
        return false;
    return Install(moduleBase, moduleSize, generation);
}

bool Halo2LookTests_Remove() noexcept
{
    if (!g_builder.target && !AnyHookRetained())
        return true;
    return Remove("title teardown");
}

void Halo2LookTests_Report() noexcept
{
    if (g_installed.load(std::memory_order_acquire))
        Report();
}

void Halo2LookTests_SetEnabled(bool enabled) noexcept
{
    g_enabled.store(enabled, std::memory_order_relaxed);
}
