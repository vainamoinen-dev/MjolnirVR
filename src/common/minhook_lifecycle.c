// MinHook retirement extension: disables a hook for retirement without freeing its trampoline,
// plus test-only create/remove fault injection. Declared in minhook_lifecycle.h.
//
// Compile the pinned upstream implementation unchanged in this translation
// unit so the narrow retirement extension can use its lock and hook records.
// Upstream's MH_RemoveHook and MH_CreateHook are compiled under other names so
// the test-only fault injection below can sit in front of them.
#define MH_RemoveHook MCCVR_UpstreamRemoveHook
#define MH_CreateHook MCCVR_UpstreamCreateHook
#include "hook.c"
#undef MH_RemoveHook
#undef MH_CreateHook
#include "minhook_lifecycle.h"

// Test-only: HALOMCCVR_TEST_FAIL_CREATE_ONCE=<module>+<hex rva> (for example
// haloreach.dll+286C6C) makes the first MH_CreateHook at exactly that address
// report MH_ERROR_MEMORY_ALLOC without creating anything, so an install's
// rollback of the hooks it already created runs on the fake headset. Unset, or
// after it has fired once, this is a direct pass-through.
static volatile LONG g_mccvrCreateFaultState = -1; // -1 unread, 0 off/fired, 1 armed
static char g_mccvrCreateFaultModule[64];
static ULONG_PTR g_mccvrCreateFaultRva = 0;

MH_STATUS WINAPI MH_CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal)
{
    if (g_mccvrCreateFaultState < 0)
    {
        char value[96] = {0};
        LONG state = 0;
        const DWORD length = GetEnvironmentVariableA("HALOMCCVR_TEST_FAIL_CREATE_ONCE", value, sizeof(value));
        if (length > 0 && length < sizeof(value))
        {
            char* plus = strchr(value, '+');
            if (plus && plus - value < (int)sizeof(g_mccvrCreateFaultModule))
            {
                *plus = '\0';
                lstrcpynA(g_mccvrCreateFaultModule, value, sizeof(g_mccvrCreateFaultModule));
                g_mccvrCreateFaultRva = (ULONG_PTR)strtoull(plus + 1, NULL, 16);
                state = g_mccvrCreateFaultRva ? 1 : 0;
            }
        }
        InterlockedCompareExchange(&g_mccvrCreateFaultState, state, -1);
    }
    if (g_mccvrCreateFaultState == 1 && pTarget)
    {
        HMODULE module = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCSTR)pTarget, &module) &&
            (ULONG_PTR)pTarget - (ULONG_PTR)module == g_mccvrCreateFaultRva)
        {
            char path[MAX_PATH] = {0};
            const DWORD n = GetModuleFileNameA(module, path, MAX_PATH);
            const char* name = path;
            for (DWORD i = 0; i < n; ++i)
                if (path[i] == '\\' || path[i] == '/')
                    name = path + i + 1;
            if (lstrcmpiA(name, g_mccvrCreateFaultModule) == 0 &&
                InterlockedCompareExchange(&g_mccvrCreateFaultState, 0, 1) == 1)
            {
                OutputDebugStringA("HaloMCCVR test fault: MH_CreateHook refused once\n");
                return MH_ERROR_MEMORY_ALLOC;
            }
        }
    }
    return MCCVR_UpstreamCreateHook(pTarget, pDetour, ppOriginal);
}

// Test-only fault injection, off unless the test harness sets
// HALOMCCVR_TEST_FAIL_REMOVE_ONCE=1 in the game's environment: the FIRST
// removal of each distinct target then reports MH_ERROR_MEMORY_PROTECT without
// touching the hook, and every later attempt goes through. That drives every
// title's "removal not confirmed -> keep ownership -> retry" path on the fake
// headset. Without the variable this is a straight pass-through.
static volatile LONG g_mccvrFaultMode = -1; // -1 unread, 0 off, 1 fail each target once
static LPVOID g_mccvrFaultedTargets[256];
static UINT g_mccvrFaultedCount = 0;

MH_STATUS WINAPI MH_RemoveHook(LPVOID pTarget)
{
    if (g_mccvrFaultMode < 0)
    {
        char value[4] = {0};
        const DWORD length = GetEnvironmentVariableA("HALOMCCVR_TEST_FAIL_REMOVE_ONCE", value, sizeof(value));
        InterlockedExchange(&g_mccvrFaultMode, (length > 0 && length < sizeof(value) && value[0] == '1') ? 1 : 0);
    }
    if (g_mccvrFaultMode == 1 && pTarget != MH_ALL_HOOKS)
    {
        BOOL inject = FALSE;
        UINT i;
        EnterSpinLock();
        for (i = 0; i < g_mccvrFaultedCount; ++i)
            if (g_mccvrFaultedTargets[i] == pTarget)
                break;
        if (i == g_mccvrFaultedCount &&
            g_mccvrFaultedCount < sizeof(g_mccvrFaultedTargets) / sizeof(g_mccvrFaultedTargets[0]))
        {
            g_mccvrFaultedTargets[g_mccvrFaultedCount++] = pTarget;
            inject = TRUE;
        }
        LeaveSpinLock();
        if (inject)
        {
            OutputDebugStringA("HaloMCCVR test fault: MH_RemoveHook first attempt refused\n");
            return MH_ERROR_MEMORY_PROTECT;
        }
    }
    return MCCVR_UpstreamRemoveHook(pTarget);
}

MH_STATUS WINAPI MCCVR_DisableHookForRetirement(LPVOID target)
{
    MH_STATUS status = MH_ERROR_NOT_INITIALIZED;
    EnterSpinLock();
    if (g_hHeap != NULL && target != MH_ALL_HOOKS)
    {
        UINT pos = FindHookEntry(target);
        status = MH_ERROR_NOT_CREATED;
        if (pos != INVALID_HOOK_POS)
        {
            PHOOK_ENTRY hook = &g_hooks.pItems[pos];
            LPBYTE patch = (LPBYTE)target;
            SIZE_T size = sizeof(JMP_REL);
            MEMORY_BASIC_INFORMATION memory;
            if (hook->patchAbove)
            {
                patch -= sizeof(JMP_REL);
                size += sizeof(JMP_REL_SHORT);
            }
            status = MH_ERROR_MEMORY_PROTECT;
            // Do not make an unreadable mapping writable just to restore
            // bytes from a previous module instance.
            if (VirtualQuery(patch, &memory, sizeof(memory)) == sizeof(memory))
            {
                BOOL absent = memory.State != MEM_COMMIT;
                BOOL restored = FALSE;
                BOOL owned = FALSE;
                if (!absent && !(memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                    (SIZE_T)(patch - (LPBYTE)memory.BaseAddress) + size <= memory.RegionSize)
                {
                    __try
                    {
                        JMP_REL jump;
                        jump.opcode = 0xE9;
                        jump.operand = (UINT32)((LPBYTE)hook->pDetour - (patch + sizeof(JMP_REL)));
                        restored = memcmp(patch, hook->backup, size) == 0;
                        owned =
                            memcmp(patch, &jump, sizeof(jump)) == 0 &&
                            (!hook->patchAbove || (((LPBYTE)target)[0] == 0xEB && ((LPBYTE)target)[1] == (UINT8)-7));
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                    }
                }
                if (absent || restored)
                {
                    hook->isEnabled = FALSE;
                    hook->queueEnable = FALSE;
                    status = MH_ERROR_DISABLED;
                }
                else if (owned && hook->isEnabled)
                {
                    FROZEN_THREADS threads;
                    status = Freeze(&threads, pos, ACTION_DISABLE);
                    if (status == MH_OK)
                    {
                        // The loader may remove a mapping after VirtualQuery.
                        __try
                        {
                            status = EnableHookLL(pos, FALSE);
                        }
                        __except (EXCEPTION_EXECUTE_HANDLER)
                        {
                            status = MH_ERROR_MEMORY_PROTECT;
                        }
                        Unfreeze(&threads);
                    }
                }
                // Foreign bytes are never overwritten, even if upstream's
                // bookkeeping says this entry is already disabled.
            }
        }
    }
    LeaveSpinLock();
    return status;
}
