#pragma once
// The barrel-muzzle lifecycle steps, kept apart from
// barrel_muzzle_runtime.h (which includes this file) so that Halo 2 and Halo 3
// can use them without that file's shot templates: their shot transaction is
// their dual-wield runtime's, not the barrel runtime's.
// Not a standalone header: it uses what the host defines above its include
// line, namely muzzle_hook_set.h's VerifyMuzzleNative, InstallMuzzleHooks and
// RetireMuzzleHooks. Halo 2 includes it inside the observer's anonymous
// namespace, so it includes nothing itself.

// A second install is refused while any hook of the set is still held.
template <class Feature>
bool BarrelMuzzleHeld(const Feature& feature)
{
    for (auto* target : feature.targets)
        if (target)
            return true;
    return false;
}
// After the title's own preconditions: verify every binding and call edge,
// stamp the feature, create then enable every hook, then publish. The caller
// prints its own Installed line on success.
template <class Ops, class Feature, size_t kBindings, size_t kEdges, size_t kHooks>
bool InstallBarrelMuzzle(Feature& feature, std::atomic<bool>& bindingsReady, uintptr_t base, size_t size,
    uint32_t generation, const MuzzleBinding (&bindings)[kBindings], const MuzzleEdge (&edges)[kEdges],
    const MuzzleHook (&hooks)[kHooks], const MuzzleLogText& text)
{
    static_assert(kHooks == std::extent_v<decltype(Feature::targets)>, "one hook per target slot");
    if (!VerifyMuzzleNative<Ops>(base, size, bindings, kBindings, edges, kEdges, text))
        return false;
    feature.base = base;
    feature.generation = generation;
    feature.installedAtMs = GetTickCount64();
    feature.faulted.store(false);
    if (!InstallMuzzleHooks<Ops>(base, hooks, kHooks, feature.targets, text))
        return false;
    feature.enabled.store(true, std::memory_order_release);
    bindingsReady.store(true, std::memory_order_release);
    return true;
}
// Withdraw publication first, then disable, drain and remove every hook.
template <class Ops, class Feature, size_t kHooks>
bool RemoveBarrelMuzzle(Feature& feature, std::atomic<bool>& bindingsReady, const void* const (&functions)[kHooks],
    void** const (&originals)[kHooks], const MuzzleLogText& text)
{
    static_assert(kHooks == std::extent_v<decltype(Feature::targets)>, "one detour per target slot");
    feature.enabled.store(false, std::memory_order_release);
    bindingsReady.store(false, std::memory_order_release);
    return RetireMuzzleHooks<Ops>(feature.targets, originals, kHooks, functions, kHooks, feature.callbacks, text);
}
// Halo 2 has no bindings flag (its publication does not use one). These
// overloads are the same steps without it; the flag versions above, used by
// Reach, Halo 4, ODST and Halo 3, are unchanged.
template <class Ops, class Feature, size_t kBindings, size_t kEdges, size_t kHooks>
bool InstallBarrelMuzzle(Feature& feature, uintptr_t base, size_t size, uint32_t generation,
    const MuzzleBinding (&bindings)[kBindings], const MuzzleEdge (&edges)[kEdges], const MuzzleHook (&hooks)[kHooks],
    const MuzzleLogText& text)
{
    static_assert(kHooks == std::extent_v<decltype(Feature::targets)>, "one hook per target slot");
    if (!VerifyMuzzleNative<Ops>(base, size, bindings, kBindings, edges, kEdges, text))
        return false;
    feature.base = base;
    feature.generation = generation;
    feature.installedAtMs = GetTickCount64();
    feature.faulted.store(false);
    if (!InstallMuzzleHooks<Ops>(base, hooks, kHooks, feature.targets, text))
        return false;
    feature.enabled.store(true, std::memory_order_release);
    return true;
}
template <class Ops, class Feature, size_t kHooks>
bool RemoveBarrelMuzzle(Feature& feature, const void* const (&functions)[kHooks], void** const (&originals)[kHooks],
    const MuzzleLogText& text)
{
    static_assert(kHooks == std::extent_v<decltype(Feature::targets)>, "one detour per target slot");
    feature.enabled.store(false, std::memory_order_release);
    return RetireMuzzleHooks<Ops>(feature.targets, originals, kHooks, functions, kHooks, feature.callbacks, text);
}
