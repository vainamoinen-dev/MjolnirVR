// CE comfort hooks: the Anniversary camera motion blur and lens flares follow the mod's settings.
// HaloCEComfort_Poll is called by the CE title tick in src/dll/core/game.cpp.
// Optional: a failed install leaves the native effects stock and never touches CE stereo.
#pragma once
#include <cstddef>
#include <cstdint>

// Optional native camera-motion-blur setting. Failure leaves this effect stock.
bool HaloCEComfort_Poll(uintptr_t base, size_t size, uint32_t generation, bool active) noexcept;
