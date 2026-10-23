// halo4_frame_bridge.h: what vr.cpp calls or reads in the Halo 4 frame source, in namespace halo4_frame_bridge.
// Defined in titles/h4/halo4_frame_source.cpp. Included by vr.cpp and
// that frame source. The Halo 4 frame source reaches vr.cpp through vr_internal.h and vr_frame.h.
#pragma once
#include "../../core/vr_internal.h"

namespace halo4_frame_bridge
{
    using namespace vr_core;
    inline constexpr WorldSwapchainMessages kHalo4WorldMessages{"Halo 4 world swapchain acquire did not complete",
        "Halo 4 world swapchain wait did not complete", "Halo 4 world swapchain release did not complete"};

    extern std::atomic<uint64_t> g_halo4EyeSerial[2];
    extern std::atomic<uint64_t> g_halo4XrPairsSubmitted;
    extern std::atomic<uint64_t> g_halo4XrPairsDropped;
    extern std::atomic<uint32_t> g_halo4LastXrPairDropReason;
    extern ID3D11RenderTargetView* g_h4LastCandidate;
    extern D3D11_TEXTURE2D_DESC g_h4LastCandidateDesc;
    extern DXGI_FORMAT g_h4LastCandidateViewFormat;
    extern unsigned g_h4CandidatesThisEye;
    extern unsigned g_h4LearningEyes;
    void Halo4ResetSceneTargetDiscovery();
    bool PublishHalo4RenderSnapshot(uint64_t preparedSerial, bool padFresh) noexcept;
    void DropIncompleteHalo4Frame(bool halo4StereoUploadComplete);
    bool Halo4PreparedFrameImagesReady();
    void Halo4ResolveSceneTargetAtEyeEnd();
} // namespace halo4_frame_bridge
