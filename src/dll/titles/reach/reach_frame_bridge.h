// reach_frame_bridge.h: what vr.cpp calls or reads in the Reach frame source, in namespace reach_frame_bridge.
// Defined in titles/reach/reach_frame_source.cpp. Included by vr.cpp and
// that frame source. The Reach frame source reaches vr.cpp through vr_internal.h and vr_frame.h.
#pragma once
#include "../../core/vr_internal.h"

namespace reach_frame_bridge
{
    using namespace vr_core;
    inline constexpr WorldSwapchainMessages kReachWorldMessages{"Reach world swapchain acquire did not complete",
        "Reach world swapchain wait did not complete", "Reach world swapchain release did not complete"};
    // Reach half of SubmitPreparedFrame's eye-image admission: opens render access
    // for this prepared serial when both eye captures belong to it.
    struct ReachFrameAccess
    {
        ReachVrRenderAccess access{};
        bool title = false;
        bool images = false;
        uint32_t generation = 0;
    };

    bool PublishReachRenderSnapshot(uint64_t preparedSerial, bool padFresh) noexcept;
    void ReportReachReticleGate(bool reticleQuadSubmitted, bool reticleOwnerAdmitted, bool reachProjectionAdmitted,
        bool haveAim, bool reticleChainAdmitted, bool authoredReticleThisFrame);
    void DropIncompleteReachFrame(bool reachStereoUploadComplete);
    ReachFrameAccess BeginReachFrameAccess();
} // namespace reach_frame_bridge
