#pragma once
// The DLL's hook sites call MinHook through the install-time detour check by
// name (src/dll/core/hook_install_check.h). A fixture that fakes MinHook under
// its own names gets the two checked names routed to those fakes here; a
// fixture that renames MH_CreateHook / MH_RemoveHook with a macro renames the
// checked name the same way.
#include "../src/dll/core/hook_install_check.h"
inline MH_STATUS MCCVR_CreateHookChecked(LPVOID target, LPVOID detour, LPVOID* original)
{
    return MH_CreateHook(target, detour, original);
}
inline MH_STATUS MCCVR_RemoveHookChecked(LPVOID target)
{
    return MH_RemoveHook(target);
}
