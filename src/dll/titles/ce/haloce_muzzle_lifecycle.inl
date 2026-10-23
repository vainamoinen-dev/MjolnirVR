#pragma once
// Included once, by src/dll/titles/ce/haloce_first_person.cpp; also compiled by
//   tests/haloce_muzzle_lifecycle_tests.cpp, which supplies its own versions. Not a standalone
//   header: it uses what the host defines above its include line, namely Hook.
// The install/retire order is muzzle_hook_set.h's, shared with the
// other titles' barrel sets: create every hook, then enable every hook; a failure
// retires what was created; retirement disables all, waits for the detours and
// trampolines to drain, then removes each hook, stopping at the first removal that
// fails and keeping that hook and the ones after it for the next retry. The CE
// contract proof (VerifyNativeFeatureBindings) and the log text stay CE's own.
// The host includes muzzle_hook_set.h at file scope (this file sits in a namespace).
bool RemoveMuzzle() noexcept;
MCCVR_MUZZLE_HOOK_OPS(CeMuzzleHookOps, RemoveMuzzle, false);
// The targets the shared helper owns; the originals stay in the Hook records the
// detours call through.
void* muzzleTargets[4]{};
constexpr HookSetLogText kCeMuzzleText{"CE barrel StockFallback: create failed",
    "CE barrel StockFallback: enable failed", "CE barrel CleanupRequired: disable failed",
    "CE barrel CleanupRequired: callbacks or ingress busy", "CE barrel CleanupRequired: removal failed"};
bool RemoveMuzzle() noexcept
{
    muzzleInstalled = false;
    muzzleRetiring = true;
    Hook* hooks[]{&muzzleFireHook, &muzzleMarkersHook, &muzzleQueryHook, &muzzleDirectQueryHook};
    void** originals[]{&hooks[0]->original, &hooks[1]->original, &hooks[2]->original, &hooks[3]->original};
    const void* functions[]{reinterpret_cast<const void*>(&MuzzleFireHook),
        reinterpret_cast<const void*>(&MuzzleMarkersHook), reinterpret_cast<const void*>(&MuzzleQueryHook),
        reinterpret_cast<const void*>(&MuzzleDirectQueryHook)};
    const bool retired =
        RetireMuzzleHooks<CeMuzzleHookOps>(muzzleTargets, originals, 4, functions, 4, callbacks, kCeMuzzleText);
    for (int i = 0; i < 4; ++i)
    {
        hooks[i]->target = muzzleTargets[i];
        if (!muzzleTargets[i])
            *hooks[i] = {};
    }
    if (retired)
        muzzleRetiring = false;
    return retired;
}
bool InstallMuzzle(uintptr_t base, size_t size, uint32_t gen) noexcept
{
    for (void* target : muzzleTargets)
        if (target)
            return false;
    if (!installed.load() || !aimInstalled.load() || !targetInstalled.load())
        return false;
    const char* failure{};
    const NativeContractSet contracts{contract::muzzle::entries, contract::muzzle::witnesses,
        contract::muzzle::relatives, contract::muzzle::pointers};
    if (!VerifyNativeFeatureBindings(base, size, gen, contracts, failure))
    {
        LOG("CE barrel StockFallback: binding verification failed: %s", failure ? failure : "unknown");
        return false;
    }
    const MuzzleHook hooks[]{
        {contract::muzzle::muzzle_fire, reinterpret_cast<void*>(&MuzzleFireHook), &muzzleFireHook.original},
        {contract::muzzle::muzzle_markers, reinterpret_cast<void*>(&MuzzleMarkersHook), &muzzleMarkersHook.original},
        {contract::muzzle::muzzle_query, reinterpret_cast<void*>(&MuzzleQueryHook), &muzzleQueryHook.original},
        {contract::muzzle::muzzle_direct_query, reinterpret_cast<void*>(&MuzzleDirectQueryHook),
            &muzzleDirectQueryHook.original}};
    if (!InstallMuzzleHooks<CeMuzzleHookOps>(base, hooks, 4, muzzleTargets, kCeMuzzleText))
        return false;
    Hook* records[]{&muzzleFireHook, &muzzleMarkersHook, &muzzleQueryHook, &muzzleDirectQueryHook};
    for (int i = 0; i < 4; ++i)
    {
        records[i]->target = muzzleTargets[i];
        records[i]->enabled = true;
    }
    muzzleInstalledAt = GetTickCount64();
    muzzleFaulted = false;
    muzzleInstalled = true;
    LOG("CE barrel Installed: optional authored committed muzzle, native acquisition and obstruction");
    return true;
}
