// vr_telemetry.h: what vr_telemetry.cpp provides to the other VR units (the pacing capture and the prepared-frame and aim telemetry), in namespace vr_core.
// Defined in vr_telemetry.cpp.
#pragma once
#include "vr_internal.h"

namespace vr_core
{
    void CapturePreparedFrameTelemetry(const XrFrameState& frameState, bool upcomingPadFresh, bool upcomingViewsValid,
        bool upcomingHeadValid) noexcept;
    void LogPacingCapture(const FramePacingWorkerState& worker, const char* completion);
    void LogStreamingHosts();
    void ProbeCaptureWeaponTitle(GameTitle title) noexcept;
    void ProbeFsrTargets(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView* const* input);
    void PublishTwoHandLabDiagnostics(
        uint64_t serial, bool valid, const two_hand_lab::Diagnostics& diagnostics) noexcept;
} // namespace vr_core
