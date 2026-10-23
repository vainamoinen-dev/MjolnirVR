#pragma once
// Two-Hand Lab runtime API: declarations only, implemented in
// src/dll/core/vr.cpp. Minimal surface for the Lab menu UI: whole-
// settings get/set/reset plus the last frame-published diagnostics. The store
// is runtime-only: no Config keys, no ConfigSave, no persistence, and it
// resets to two_hand_lab::DefaultSettings() (disabled) on process restart.
#include "../../common/two_hand_lab_logic.h"

namespace two_hand_lab_runtime
{
    void SetSettings(const two_hand_lab::Settings& settings) noexcept;
    two_hand_lab::Settings GetSettings() noexcept;
    void ResetSettings() noexcept;
    // Last AdvanceTwoHandLabForPreparedFrame publication. False when no Lab-active
    // solve has published yet, including every frame while the Lab is
    // stored-disabled or Virtual Stock resolves on.
    bool GetLastDiagnostics(two_hand_lab::Diagnostics& out) noexcept;
} // namespace two_hand_lab_runtime
