#pragma once
// Shared install/retire steps for the optional per-title native hook sets:
// muzzle (Halo 3, Halo 2, ODST, Reach, Halo 4) and weapon haptics (ODST,
// Halo 4). Each title keeps its own rows (RVA +
// signature bindings, call edges, hooks), its own preconditions, runtime state
// and exact log text; this file only owns the common order:
//   verify every binding is the unique match at its pinned RVA, verify every
//   E8 call edge, create every hook, then enable every hook (each detour
//   forwards stock until the whole set is enabled);
//   retire: disable all, wait for detours and trampolines to drain, remove all.
// Any failure retires what was created; a retirement that cannot finish keeps
// every target and original for the next verified retry.
//
// The native primitives come from an Ops type the title declares with
// MCCVR_MUZZLE_HOOK_OPS at its use site, so a title's tests can substitute
// MinHook, the signature scanner and the quiescence wait exactly as before.
#include <MinHook.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>

#include "../../common/gated_hook.h"
#include "native_binding.h"

struct MuzzleBinding
{
    uint32_t rva;
    const char* pattern;
};
struct MuzzleEdge
{
    uint32_t call, target;
};
struct MuzzleHook
{
    uint32_t rva;
    void* detour;
    void** original;
};
// Exact per-title log lines. binding/edge receive the RVA (%X) when they print it.
// The disable line takes the MinHook status (%d) when disableTakesStatus
// is set (Halo 2 and Halo 3 muzzle); every other title keeps its text.
struct MuzzleLogText
{
    const char* binding;
    const char* edge;
    const char* create;
    const char* enable;
    const char* disable;
    const char* busy;
    const char* removal;
    bool disableTakesStatus = false;
};
// The install/retire lines alone, for hook sets that prove their own bindings
// (weapon haptics, contact melee). None takes an argument, except that the
// disable line takes the MinHook status (%d) when disableTakesStatus is set
// (Halo 4 contact cleanup keeps its "(status %d)").
struct HookSetLogText
{
    const char* create;
    const char* enable;
    const char* disable;
    const char* busy;
    const char* removal;
    bool disableTakesStatus = false;
};

template <class Ops>
bool VerifyMuzzleNative(uintptr_t base, size_t size, const MuzzleBinding* bindings, size_t bindingCount,
    const MuzzleEdge* edges, size_t edgeCount, const MuzzleLogText& text)
{
    for (size_t i = 0; i < bindingCount; ++i)
        if (!Ops::Bound(base, size, bindings[i]))
        {
            Ops::Log(text.binding, bindings[i].rva);
            return false;
        }
    for (size_t i = 0; i < edgeCount; ++i)
        if (!NativeCallEdge(base, edges[i].call, edges[i].target))
        {
            Ops::Log(text.edge, edges[i].call);
            return false;
        }
    return true;
}

// kLogAfterRetire keeps a title whose failure line follows its cleanup
// (ODST/Halo 4 weapon haptics); the muzzle sets log first.
template <class Ops, bool kLogAfterRetire = false, class Text>
bool InstallMuzzleHooks(uintptr_t base, const MuzzleHook* hooks, size_t count, void** targets, const Text& text)
{
    const auto fail = [](const char* line)
    {
        if constexpr (kLogAfterRetire)
        {
            (void)Ops::Retire();
            Ops::Log(line);
        }
        else
        {
            Ops::Log(line);
            (void)Ops::Retire();
        }
        return false;
    };
    for (size_t i = 0; i < count; ++i)
    {
        auto* target = reinterpret_cast<void*>(base + hooks[i].rva);
        if (Ops::Create(target, hooks[i].detour, hooks[i].original) != MH_OK)
            return fail(text.create);
        targets[i] = target;
    }
    // Each detour forwards stock until the complete set is enabled.
    for (size_t i = 0; i < count; ++i)
        if (Ops::Enable(targets[i]) != MH_OK)
            return fail(text.enable);
    return true;
}

// `functions` may list more entries than hooks (a gated body without a
// MinHook entry); their trampolines are null. kQuiesceWhenEmpty keeps a title
// that waits for its detours even when no hook was created (weapon haptics).
template <class Ops, bool kQuiesceWhenEmpty = false, class Text>
bool RetireMuzzleHooks(void** targets, void** const* originals, size_t count, const void* const* functions,
    size_t functionCount, const std::atomic<uint32_t>& callbacks, const Text& text)
{
    constexpr size_t kMaxFunctions = 8;
    if (count > functionCount || functionCount > kMaxFunctions)
        return false;
    bool any = false;
    for (size_t i = 0; i < count; ++i)
    {
        if (!targets[i])
            continue;
        any = true;
        const MH_STATUS status = Ops::Disable(targets[i]);
        if (!HookDisableStatusIsSafe(status))
        {
            if constexpr (requires { text.disableTakesStatus; })
            {
                if (text.disableTakesStatus)
                {
                    Ops::Log(text.disable, static_cast<int>(status));
                    return false;
                }
            }
            Ops::Log(text.disable);
            return false;
        }
    }
    if (!any && !kQuiesceWhenEmpty)
        return true;
    const void* trampolines[kMaxFunctions]{};
    for (size_t i = 0; i < count; ++i)
        trampolines[i] = *originals[i];
    if (!Ops::Quiesce(functions, trampolines, functionCount, callbacks))
    {
        Ops::Log(text.busy);
        return false;
    }
    for (size_t i = 0; i < count; ++i)
    {
        if (!targets[i])
            continue;
        const auto status = Ops::Remove(targets[i]);
        if (status != MH_OK && status != MH_ERROR_NOT_CREATED)
        {
            Ops::Log(text.removal);
            return false;
        }
        targets[i] = nullptr;
        *originals[i] = nullptr;
    }
    return true;
}

// One reporting line for a title's barrel counters (Reach, ODST, Halo 4).
template <class Ops, class Feature>
void ReportMuzzleTrajectory(const char* format, Feature& feature, bool option)
{
    if (!feature.targets[0])
        return;
    Ops::Log(format, feature.enabled.load() ? 1 : 0, feature.faulted.load() ? 1 : 0, feature.applied.exchange(0),
        feature.refused.exchange(0), feature.queries.exchange(0), feature.restoreRefused.exchange(0), option ? 1 : 0);
}

template <class Find>
bool MuzzleUniqueBinding(uintptr_t base, size_t size, const MuzzleBinding& binding, Find find)
{
    return UniqueSignatureAt(base, size, binding.rva, binding.pattern, find);
}

// Declares a title's Ops at its use site. Expanded there, so MinHook, sig::Find,
// MCCVR_DisableHookForRetirement, WaitForNativeDetourQuiescence and LOG resolve
// to whatever that translation unit (or test fixture) provides. BOUND is an
// expression over (base, size, binding).
#define MCCVR_MUZZLE_HOOK_OPS(NAME, RETIRE, BOUND)                                                                     \
    struct NAME                                                                                                        \
    {                                                                                                                  \
        static bool Bound(uintptr_t base, size_t size, const MuzzleBinding& binding)                                   \
        {                                                                                                              \
            (void)base;                                                                                                \
            (void)size;                                                                                                \
            (void)binding;                                                                                             \
            return BOUND;                                                                                              \
        }                                                                                                              \
        static MH_STATUS Create(void* t, void* d, void** o)                                                            \
        {                                                                                                              \
            return MCCVR_CreateHookChecked(t, d, o);                                                                   \
        }                                                                                                              \
        static MH_STATUS Enable(void* t)                                                                               \
        {                                                                                                              \
            return MH_EnableHook(t);                                                                                   \
        }                                                                                                              \
        static MH_STATUS Disable(void* t)                                                                              \
        {                                                                                                              \
            return MCCVR_DisableHookForRetirement(t);                                                                  \
        }                                                                                                              \
        static MH_STATUS Remove(void* t)                                                                               \
        {                                                                                                              \
            return MCCVR_RemoveHookChecked(t);                                                                         \
        }                                                                                                              \
        static bool Quiesce(const void* const* f, const void* const* o, size_t n, const std::atomic<uint32_t>& c)      \
        {                                                                                                              \
            return WaitForNativeDetourQuiescence(f, o, n, c);                                                          \
        }                                                                                                              \
        static bool Retire()                                                                                           \
        {                                                                                                              \
            return RETIRE();                                                                                           \
        }                                                                                                              \
        template <class... Args>                                                                                       \
        static void Log(const char* format, Args... args)                                                              \
        {                                                                                                              \
            (void)format;                                                                                              \
            ((void)args, ...);                                                                                         \
            LOG(format, args...);                                                                                      \
        }                                                                                                              \
    }
#define MCCVR_MUZZLE_SIGNATURE_BOUND MuzzleUniqueBinding(base, size, binding, MCCVR_SIG_FIND)
