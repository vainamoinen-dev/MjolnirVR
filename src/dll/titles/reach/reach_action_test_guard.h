// Halo Reach action-test builder guard: the worker-facing install/remove tick and a live check.
// Used by src/dll/core/game.cpp and src/dll/core/input.cpp; implemented in reach_action_test_guard.cpp.
#pragma once
#include <cstddef>
#include <cstdint>

// Worker (ReachCameraCore_Poll, every tick): installs the Reach action-test
// builder guard once a level runs, removes it when Reach goes away or the
// switch turns off, and logs the first skipped call.
// True while the builder guard is installed (any thread).
bool ReachActionTestGuard_Live() noexcept;
void ReachActionTestGuard_Tick(
    uintptr_t base, size_t size, uint32_t generation, bool soleReachTitle, bool levelRunning);
