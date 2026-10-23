#pragma once
// Install-or-back-out policy for a single MinHook detour.
// Used by dll/core (game, input, muzzle_hook_set.h), the Reach action-test guard and
// tests/hook_registry_tests.cpp.
//
// One optional or proof-gated MinHook detour, installed at an address the
// caller has already proven: create it, enable it, and on any failure take it
// back out again. A hook MinHook cannot confirm it disabled and removed is
// retained instead (target and original kept), so the title's verified
// teardown still owns it - a trampoline that may be executing is never freed.
// Logging stays with the caller, which knows what the hook is for.
//
// The MinHook calls are passed in so the policy is testable on its own.
#include <MinHook.h>

// Disable results after which the hook can no longer be entered: done now,
// already disabled, or never created.
inline bool HookDisableStatusIsSafe(MH_STATUS status)
{
    return status == MH_OK || status == MH_ERROR_DISABLED || status == MH_ERROR_NOT_CREATED;
}

struct GatedHook
{
    bool enabled = false;
    MH_STATUS created = MH_UNKNOWN;
    bool retained = false;
};

template <class Create, class Enable, class Disable, class Remove>
GatedHook InstallGatedHookWith(Create create, Enable enable, Disable disable, Remove remove, void*& target,
    void* address, void* detour, void** original)
{
    GatedHook result;
    target = address;
    result.created = create(target, detour, original);
    result.enabled = result.created == MH_OK && enable(target) == MH_OK;
    if (result.enabled)
        return result;
    result.retained = result.created == MH_OK;
    if (result.retained && HookDisableStatusIsSafe(disable(target)))
    {
        const MH_STATUS removed = remove(target);
        result.retained = removed != MH_OK && removed != MH_ERROR_NOT_CREATED;
    }
    if (!result.retained)
    {
        target = nullptr;
        *original = nullptr;
    }
    return result;
}
