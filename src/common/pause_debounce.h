#pragma once
// Native pause entry debounce: the pause flag must hold before the 2D pause view starts.
// Used by dll/core/game.cpp and tests/pause_debounce_tests.cpp. Pure logic, no globals.
//
// At the end of a Reach level load the engine pause flag reads "paused" for about
// 100 ms (found in headset testing), which would flip the presentation to head-locked
// 2D and straight back. Entry needs the flag to hold for kEntryHoldMs; exit (or an
// unknown flag) is immediate, so unpausing never lags.
#include <cstdint>

namespace pause_debounce
{
    inline constexpr uint64_t kEntryHoldMs = 150;

    class Entry
    {
    public:
        // raw: the engine flag reads paused (and is known) this tick.
        // Returns the debounced paused state.
        bool Update(bool raw, uint64_t nowMs, uint64_t holdMs = kEntryHoldMs) noexcept
        {
            if (!raw)
            {
                Reset();
                return false;
            }
            if (paused_)
                return true;
            if (!started_ || nowMs < since_)
            {
                started_ = true;
                since_ = nowMs;
            }
            if (nowMs - since_ >= holdMs)
                paused_ = true;
            return paused_;
        }
        bool Paused() const noexcept { return paused_; }
        void Reset() noexcept
        {
            since_ = 0;
            started_ = false;
            paused_ = false;
        }

    private:
        uint64_t since_{};
        bool started_{};
        bool paused_{};
    };
} // namespace pause_debounce
