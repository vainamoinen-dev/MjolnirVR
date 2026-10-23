// Name of the per-process event the launcher signals to ask a loaded mod to recover VR.
// Used by launcher/launcher.cpp, dll/core/game.cpp and tests/minhook_lifecycle_tests.cpp.
#pragma once
#include <windows.h>
#include <cwchar>

// PID-scoped and session-local: a launcher can request recovery from an
// already loaded mod without loading a second DLL or creating a remote thread.
inline void ManualVrRecoveryEventName(wchar_t (&name)[96], DWORD pid)
{
    swprintf_s(name, L"Local\\HaloMCCVR.RecoverVR.%lu", pid);
}
