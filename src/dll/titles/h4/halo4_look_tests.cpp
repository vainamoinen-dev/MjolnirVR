// Halo 4 tutorial look tests: hooks the native look-input builder and look-test readers (see halo4_look_tests.h).
// Every address is signature-proven; any refusal leaves the game stock. Hooks still busy at removal stay for a retry.
#include "halo4_look_tests.h"
#include "../../core/hook_install_check.h" // MinHook create/remove go through the install-time detour check

#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstdio>
#include <intrin.h>

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
    // Builder entry (TLS index load at +0x25), the single-bit reader every
    // up/down/left/right evaluate jumps to (bits 15..18; TLS load at +0x05)
    // and the range reader all_directions jumps to (15..18; TLS load at +0x13).
    constexpr const char* kBuilderSig = "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 57 48 83 EC 20 "
                                        "65 48 8B 04 25 58 00 00 00 48 8B FA 44 8B 0D ?? ?? ?? ?? BE 01 00 00 00 "
                                        "BA B0 00 00 00 49 8B D8";
    // The builder's look block: +0x18 pitch > 0 -> bit 15 (up) / < 0 -> 16,
    // +0x14 yaw > 0 -> 17 (left) / < 0 -> 18.
    constexpr const char* kLookBlockSig =
        "F3 0F 10 43 18 0F 57 DB 0F 2F C3 76 08 4B 0F BA 2C DA 0F EB 0B 0F 2F D8 76 06 "
        "4B 0F BA 2C DA 10 F3 0F 10 43 14 0F 2F C3 76 08 4B 0F BA 2C DA 11 EB 0B 0F 2F "
        "D8 76 06 4B 0F BA 2C DA 12";
    constexpr const char* kBitReaderSig = "48 89 5C 24 08 44 8B 15 ?? ?? ?? ?? 32 C0 65 4C 8B 1C 25 58 00 00 00 33 D2 "
                                          "45 33 C0 44 8B C9 BB 38 05 00 00";
    constexpr const char* kRangeReaderSig =
        "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 4C 89 70 20 8B 3D ?? ?? ?? ?? "
        "2B D1 65 48 8B 34 25 58 00 00 00";
    // Evaluates: "mov ecx,bit; jmp reader" (up 15, down 16, left 17, right 18)
    // and "mov edx,18; lea ecx,[rdx-3]; jmp range".
    constexpr uint8_t kEvaluateBits[4] = {0x0F, 0x10, 0x11, 0x12};
    constexpr size_t kAccumulatorMember = 0xB0;
    // Halo 4's look bits sit 8 above the shared head_look_test bits (7-10).
    constexpr int kBitShift = 8;

    using BuilderFn = void(__fastcall*)(int, void*, unsigned char*);
    using BitReaderFn = bool(__fastcall*)(int);
    using RangeReaderFn = bool(__fastcall*)(int, int);

    struct Hook
    {
        void* target{};
        void* original{};
        bool enabled{};
    };
    Hook g_builder{}, g_bitReader{}, g_rangeReader{};
    head_look_test::PollTracker g_polls;
    std::atomic<bool> g_installed{false}, g_enabled{true};
    std::atomic<uint32_t> g_generation{0}, g_callbacks{0};
    std::atomic<uintptr_t> g_moduleBase{0};
    std::atomic<uint64_t> g_fedBits{0}, g_fedTicks{0};
    const uint32_t* g_tlsIndex = nullptr;
    uint32_t g_rejectedGeneration = 0;
    uint64_t g_lastReportMs = 0;

    __declspec(noinline) bool __fastcall BitReaderHook(int bit)
    {
        g_callbacks.fetch_add(1, std::memory_order_acq_rel);
        bool result = false;
        __try
        {
            if (bit >= 15 && bit <= 18)
                g_polls.Note(static_cast<head_look_test::Poll>(bit - 15), GetTickCount64());
            const auto original = reinterpret_cast<BitReaderFn>(g_bitReader.original);
            if (original)
                result = original(bit);
        }
        __finally
        {
            g_callbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
        return result;
    }

    __declspec(noinline) bool __fastcall RangeReaderHook(int first, int last)
    {
        g_callbacks.fetch_add(1, std::memory_order_acq_rel);
        bool result = false;
        __try
        {
            if (first == 15 && last == 18)
                g_polls.Note(head_look_test::PollAll, GetTickCount64());
            const auto original = reinterpret_cast<RangeReaderFn>(g_rangeReader.original);
            if (original)
                result = original(first, last);
        }
        __finally
        {
            g_callbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
        return result;
    }

    // User 0's accumulator in engine TLS (look bits 15..18), under this title's SEH.
    bool ReadLookWord(uint64_t& word) noexcept
    {
        __try
        {
            auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
            auto* tls = slots && g_tlsIndex ? reinterpret_cast<unsigned char*>(slots[*g_tlsIndex]) : nullptr;
            auto* users = tls ? *reinterpret_cast<unsigned char**>(tls + kAccumulatorMember) : nullptr;
            if (!users)
                return false;
            word = *reinterpret_cast<volatile uint64_t*>(users); // user 0
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading Halo 4's look-test word faulted");
            return false;
        }
    }

    uint64_t BitsToFeed() noexcept
    {
        static head_look_test::Anchor anchor;
        return look_test_feed::BitsToFeed(anchor, g_polls, g_installed, g_enabled, kBitShift, ReadLookWord);
    }

    void BuilderBody(int user, void* context, unsigned char* input)
    {
        const auto original = reinterpret_cast<BuilderFn>(g_builder.original);
        if (!original)
            return;
        const uint64_t feed = user == 0 && input ? BitsToFeed() : 0;
        if (!feed)
        {
            original(user, context, input);
            return;
        }
        look_test_feed::FeedIdleAxes(reinterpret_cast<float*>(input + 0x14), reinterpret_cast<float*>(input + 0x18),
            feed, g_fedBits, g_fedTicks, [&] { original(user, context, input); });
    }

    __declspec(noinline) void __fastcall BuilderHook(int user, void* context, unsigned char* input)
    {
        g_callbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            BuilderBody(user, context, input);
        }
        __finally
        {
            g_callbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    bool AnyHookRetained() noexcept;
    Hook* AllHooks(size_t i) noexcept
    {
        return i == 0 ? &g_builder : i == 1 ? &g_bitReader : &g_rangeReader;
    }
    constexpr size_t kHookCount = 3;

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
            reinterpret_cast<const void*>(&BitReaderHook), reinterpret_cast<const void*>(&RangeReaderHook),
            reinterpret_cast<const void*>(&BuilderBody)};
        const void* trampolines[] = {g_builder.original, g_bitReader.original, g_rangeReader.original, nullptr};
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
        g_tlsIndex = nullptr;
        g_moduleBase.store(0, std::memory_order_release);
        g_generation.store(0, std::memory_order_release);
        if (reason)
            LOG("Halo 4 look tests: removed (%s)", reason);
        return true;
    }

    uintptr_t Unique(uintptr_t base, size_t size, const char* pattern) noexcept
    {
        const uintptr_t hit = sig::Find(base, size, pattern);
        return hit && !sig::Find(hit + 1, base + size - hit - 1, pattern) ? hit : 0;
    }

    // True when some match of `pattern` has its rel32 (at +dispOffset, ending
    // at +nextOffset) landing on `target`.
    bool SomeJumpsTo(uintptr_t base, size_t size, const char* pattern, size_t dispOffset, size_t nextOffset,
        uintptr_t target) noexcept
    {
        for (uintptr_t at = sig::Find(base, size, pattern); at; at = sig::Find(at + 1, base + size - at - 1, pattern))
            if (sig::RipTarget(at + dispOffset, at + nextOffset) == target)
                return true;
        return false;
    }

    bool Install(uintptr_t base, size_t size, uint32_t generation) noexcept
    {
        const uintptr_t builder = Unique(base, size, kBuilderSig);
        const uintptr_t look = Unique(base, size, kLookBlockSig);
        const uintptr_t bitReader = Unique(base, size, kBitReaderSig);
        const uintptr_t rangeReader = Unique(base, size, kRangeReaderSig);
        const uintptr_t tls = builder ? sig::RipTarget(builder + 0x28, builder + 0x2C) : 0;
        bool proven = builder && look && bitReader && rangeReader && look > builder && look - builder < 0x600 &&
                      tls >= base && tls + sizeof(uint32_t) <= base + size &&
                      sig::RipTarget(bitReader + 8, bitReader + 12) == tls &&
                      sig::RipTarget(rangeReader + 0x15, rangeReader + 0x19) == tls &&
                      *reinterpret_cast<const uint32_t*>(tls) < 1088;
        // Every look evaluate jumps to these readers.
        for (int i = 0; proven && i < 4; ++i)
        {
            char pattern[32];
            _snprintf_s(
                pattern, sizeof(pattern), _TRUNCATE, "B9 %02X 00 00 00 E9", static_cast<unsigned>(kEvaluateBits[i]));
            proven = SomeJumpsTo(base, size, pattern, 6, 10, bitReader);
        }
        if (proven)
            proven = SomeJumpsTo(base, size, "BA 12 00 00 00 8D 4A FD E9", 9, 13, rangeReader);
        if (!proven)
        {
            LOG("Halo 4 look tests: builder/reader/evaluate proof missing or ambiguous "
                "(builder=+0x%llX); look prompts stay stick-only",
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
            {bitReader, reinterpret_cast<void*>(&BitReaderHook), &g_bitReader},
            {rangeReader, reinterpret_cast<void*>(&RangeReaderHook), &g_rangeReader}};
        g_tlsIndex = reinterpret_cast<const uint32_t*>(tls);
        g_moduleBase.store(base, std::memory_order_release);
        g_generation.store(generation, std::memory_order_release);
        for (const Spec& spec : specs)
        {
            void* original = nullptr;
            const MH_STATUS created =
                MCCVR_CreateHookChecked(reinterpret_cast<void*>(spec.target), spec.detour, &original);
            if (created != MH_OK || !original)
            {
                LOG("Halo 4 look tests WITHHELD: hook create=%d at +0x%llX", static_cast<int>(created),
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
                LOG("Halo 4 look tests WITHHELD: hook enable failed");
                (void)Remove("install rollback");
                g_rejectedGeneration = generation;
                return false;
            }
            spec.hook->enabled = true;
        }
        g_installed.store(true, std::memory_order_release);
        LOG("Halo 4 look tests: builder +0x%llX and look-test readers +0x%llX/+0x%llX "
            "hooked; while a script polls a look test, head look reaches the builder as "
            "look input (halo4_tutorial_head_look=%d)",
            static_cast<unsigned long long>(builder - base), static_cast<unsigned long long>(bitReader - base),
            static_cast<unsigned long long>(rangeReader - base), g_enabled.load() ? 1 : 0);
        return true;
    }

    void Report() noexcept
    {
        look_test_feed::Report(g_lastReportMs, g_fedBits, g_fedTicks, [](const char* letters, unsigned long long ticks)
            { LOG("Halo 4 look tests: head look fed %s on %llu builder tick(s)", letters, ticks); });
    }
}

bool Halo4LookTests_Install(uintptr_t moduleBase, size_t moduleSize, uint32_t generation) noexcept
{
    if (g_installed.load(std::memory_order_acquire))
        return true;
    if (g_builder.target || !moduleBase || !moduleSize)
    {
        if (g_builder.target)
            LOG("Halo 4 look tests: prior hooks retained; not reinstalling");
        return false;
    }
    if (g_rejectedGeneration == generation)
        return false;
    return Install(moduleBase, moduleSize, generation);
}

bool Halo4LookTests_Remove() noexcept
{
    if (!g_builder.target && !AnyHookRetained())
        return true;
    return Remove("title teardown");
}

void Halo4LookTests_Report() noexcept
{
    if (g_installed.load(std::memory_order_acquire))
        Report();
}

void Halo4LookTests_SetEnabled(bool enabled) noexcept
{
    g_enabled.store(enabled, std::memory_order_relaxed);
}
