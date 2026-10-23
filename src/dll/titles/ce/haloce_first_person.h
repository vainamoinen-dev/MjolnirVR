// CE first-person interface (haloce_first_person.cpp): poll, armed state and the last committed
// weapon graph. Used by game.cpp, vr.cpp and haloce_hud.cpp.
// Optional: a false result never revokes CE stereo ownership.
#pragma once
#include <cstddef>
#include <cstdint>
#include "haloce_controls.h"

// Independent optional transaction; false never revokes CE stereo ownership.
bool HaloCEFirstPerson_Poll(uintptr_t base, size_t size, uint32_t generation, bool active) noexcept;
bool HaloCEFirstPerson_Armed() noexcept;
bool HaloCEFirstPerson_AimArmed() noexcept;

// Narrow native ownership facts, not an invented all-purpose gameplay state.
// Perspective zero is proven to admit CE's own first-person prepare branch;
// other perspective values remain unnamed until separately established.
bool HaloCEFirstPerson_GetLocalPlayerState(HaloCELocalPlayerState& state) noexcept;
// Last committed graph, bounded by title/generation/space and 150 ms age.
uint64_t HaloCEFirstPerson_WeaponGraph(uint32_t generation, uint64_t space, uint64_t now) noexcept;
// The same, for the held gun's profile on the render thread: the
// eye passes' commit stands through the Classic scope pass that cleared it.
uint64_t HaloCEFirstPerson_ProfileWeaponGraph(uint32_t generation, uint64_t space, uint64_t now) noexcept;
