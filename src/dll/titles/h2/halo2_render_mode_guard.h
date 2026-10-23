// Interface of the Halo 2 render mode guard, which blocks Classic <-> Anniversary switches that
// no real switch input asked for. Polled by src/dll/core/game.cpp; the cold observation reads
// its last-switch text. Implemented in halo2_render_mode_guard.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

// Halo 2's Classic <-> Anniversary switch is applied by
// 0x511E0 from the request dword 0xE21278, which the frame driver 0x515E0
// toggles on the switch input (pad Back / keyboard Tab / SSL) or drives from
// the "forced legacy fading" state. One captured log showed twelve switches in
// eighteen seconds with no button fed by the mod - a flashing headset. This
// guard detours the applier: a request that differs from the applied mode is
// honoured only while a switch input is actually present (keyboard Tab held,
// the physical pad's Back held with halo2_gamepad_graphics_switch = 1, or the
// mod's own deliberate Back from the head-gesture held click); otherwise the
// request is written back to the applied mode and the event is logged with
// the evidence that was read at that instant.
bool Halo2RenderModeGuard_Poll(uintptr_t moduleBase, size_t moduleSize, uint32_t generation, bool activeAndRange,
    bool levelRunning, bool coldPassed) noexcept;

bool Halo2RenderModeGuard_Installed() noexcept;

// For the cold-observation CHANGED line: the evidence the guard saw on the
// most recent honoured switch (or "none" when the guard never saw one).
void Halo2RenderModeGuard_DescribeLastSwitch(char* buffer, size_t bytes) noexcept;
