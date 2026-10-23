// CE network input interface (haloce_network_input.cpp): controller aim in the outgoing player
// action for networked games. Called by game.cpp and haloce_unit_control.cpp.
// Optional: a failed install leaves the native network actions unchanged.
#pragma once
#include <cstddef>
#include <cstdint>
bool HaloCENetworkInput_Poll(uintptr_t base, size_t size, uint32_t generation, bool active) noexcept;
// Network actions must not subsequently be overwritten by local-only body or
// movement adapters. False for local campaigns, films and unknown bindings.
bool HaloCENetworkInput_UsesNativeSimulation() noexcept;
