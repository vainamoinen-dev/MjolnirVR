// C interface to the MinHook retirement extension compiled in minhook_lifecycle.c.
// Used by dll/core and the per-title hook code when a title's hooks are retired.
#pragma once
#include <MinHook.h>

#ifdef __cplusplus
extern "C"
{
#endif
    // Worker only. Disables an exact live MinHook patch normally, or marks an
    // absent mapping / already restored entry disabled WITHOUT writing it.
    // This never frees a trampoline. Callers must prove detour quiescence before
    // MH_RemoveHook, including when this returns MH_ERROR_DISABLED.
    MH_STATUS WINAPI MCCVR_DisableHookForRetirement(LPVOID target);
#ifdef __cplusplus
}
#endif
