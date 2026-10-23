// Halo 4 tutorial look tests: lets head turns satisfy the "look up/down/left/right" tutorial checks.
// Used by src/dll/core/game.cpp. Never changes the player's real facing, view or aim.
#pragma once

#include <cstddef>
#include <cstdint>

// Halo 4 look tests (method from the earlier CE VR mod's UpdatePitchYaw hook).
// player_action_test_look_relative_up/down/left/right
// test bits 15/16/17/18 of the per-user accumulator [engine TLS + 0xB0] +
// user * 0x48, set by the builder (+0xAB7A8) from the sign of the input's
// pitch (+0x18) and yaw (+0x14) deltas. While a script polls a look test, the
// builder alone gets the head's turn as that delta for an idle axis; the real
// input is restored, so facing, view and aim never change. Every address is
// signature-proven to the same TLS index; any refusal leaves stock behaviour.
// Installed and removed by the title's own verified lifecycle (Halo 2:
// the observer's optional feature transactions; Halo 4: the camera core).
// Remove() is false while any hook is retained; call again on the next
// retirement attempt.
bool Halo4LookTests_Install(uintptr_t moduleBase, size_t moduleSize, uint32_t generation) noexcept;
bool Halo4LookTests_Remove() noexcept;
// Wait-thread diagnostics (every 2 s while installed).
void Halo4LookTests_Report() noexcept;

// halo4_tutorial_head_look.
void Halo4LookTests_SetEnabled(bool enabled) noexcept;
