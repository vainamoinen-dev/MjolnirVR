// Interface of the Halo 2 look tests feature: lets a script's "look up/down/left/right" test see
// the head's turn. Installed and removed by src/dll/titles/h2/halo2_observer_6dof.cpp; enabled
// and reported from src/dll/core/game.cpp. Implemented in halo2_look_tests.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

// Halo 2 look tests (method from the earlier CE VR mod's UpdatePitchYaw hook, also used for
// ODST's drop-pod look). player_action_test_look_relative_up/down/left/right
// read bits 10/11/12/13 of the action-test word at [halo2+0x15E40D0]+4, set by
// the builder (+0x6C0130) from the sign of the input's yaw (+0x10) and pitch
// (+0x14) deltas. While a script polls a look test, the builder alone gets the
// head's turn as that delta for its call; the real input is restored, so
// facing, view and aim never change. Every address is signature-proven to
// resolve to that one global; any refusal leaves stock behaviour.
// Installed and removed by the title's own verified lifecycle (Halo 2:
// the observer's optional feature transactions; Halo 4: the camera core).
// Remove() is false while any hook is retained; call again on the next
// retirement attempt.
bool Halo2LookTests_Install(uintptr_t moduleBase, size_t moduleSize, uint32_t generation) noexcept;
bool Halo2LookTests_Remove() noexcept;
// Wait-thread diagnostics (every 2 s while installed).
void Halo2LookTests_Report() noexcept;

// halo2_tutorial_head_look.
void Halo2LookTests_SetEnabled(bool enabled) noexcept;
