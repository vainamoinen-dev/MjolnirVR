#pragma once
// Install-time self-check for every MinHook detour in the DLL. Every hook site
// in src/dll calls MCCVR_CreateHookChecked / MCCVR_RemoveHookChecked by name
// (tools/check_raw_minhook_calls.py fails the build on a raw MH_CreateHook or
// MH_RemoveHook call); test fixtures route these two names to their own fakes.
//
// Retirement (WaitForNativeDetourQuiescence) needs each detour's unwind range
// (RtlLookupFunctionEntry) to prove no thread is still inside it, and it keys
// detours by address. So before a hook is created we verify that the detour
// has an unwind entry covering its first byte, and that the same detour
// address is not already live for a different target (identical-code folding
// would merge two detours into one). A failure refuses that hook with a
// MinHook error, so the caller's existing stock fallback applies; it is logged
// and counted per target module ("detour unwind check: <module> N ok, M missing").
#ifdef __cplusplus
#include <MinHook.h>
#include <cstdint>
MH_STATUS MCCVR_CreateHookChecked(LPVOID target, LPVOID detour, LPVOID* original);
MH_STATUS MCCVR_RemoveHookChecked(LPVOID target);
// Worker: one line per module whose counts changed since the last report.
// "ok/missing/folded" are cumulative install attempts; "live" is hooks still
// created right now (MCCVR_RemoveHookChecked clears them).
void HookInstallCheck_Report();
// Fault-time (vectored handler): no lock, no allocation. Live hook targets in
// [begin, end): returns the total and copies up to `capacity` of them.
unsigned HookInstallCheck_LiveTargets(uintptr_t begin, uintptr_t end, uintptr_t* out, unsigned capacity) noexcept;
#endif
