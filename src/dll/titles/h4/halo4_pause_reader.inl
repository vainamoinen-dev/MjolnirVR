// Halo 4 pause reader: asks Halo's own pause getter whether the game is paused.
// Missing native thread state reads as unknown, never as resumed.
#pragma once
// Included once, by src/dll/titles/h4/halo4_camera_core.cpp; also compiled by
//   tests/halo4_pause_lifetime_tests.cpp, which supplies its own versions. Not a standalone
//   header: it uses what the host defines above its include line, namely g_halo4EngineTlsIndex,
//   g_halo4Restoration.
// Included by halo4_camera_core.cpp and the production pause-lifetime fixture.
bool Halo4ReadNativePaused(bool& paused)
{
    paused = false;
    if (!g_halo4Restoration.pauseProven.load(std::memory_order_acquire) || !g_halo4Restoration.pauseReason)
        return false;
    __try
    {
        // The proven getter immediately dereferences *(TLS+0x90). During
        // title retirement the Present thread can outlive that native
        // pause block (a crash reported at halo4+A0B03). Missing thread
        // state means unknown, not resumed; do not call a native routine
        // whose own precondition is already known to be absent.
        if (!g_halo4EngineTlsIndex || *g_halo4EngineTlsIndex >= 0x200)
            return false;
        const auto* const* slots = reinterpret_cast<const uint8_t* const*>(__readgsqword(0x58));
        const auto* tls = slots ? slots[*g_halo4EngineTlsIndex] : nullptr;
        if (!tls || !*reinterpret_cast<const void* const*>(tls + 0x90))
            return false;
        paused = g_halo4Restoration.pauseReason(3);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("reading Halo 4's pause flag faulted");
        return false;
    }
}
