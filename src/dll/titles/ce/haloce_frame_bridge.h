// haloce_frame_bridge.h: what vr.cpp calls or reads in the Halo CE frame source, in namespace haloce_frame_bridge.
// Defined in titles/ce/haloce_frame_source.cpp. Included by vr.cpp and
// that frame source. The Halo CE frame source reaches vr.cpp through vr_internal.h and vr_frame.h.
// MirrorCeDesktopAndReport stays in vr.cpp: it calls haloce_desktop_mirror.inl, whose functions are
// anonymous and which kept vr.cpp code also uses.
#pragma once
#include "../../core/vr_internal.h"
#include "../../../common/scope_logic.h"
#include "haloce_eye_cache.h"
#include "haloce_stereo_core.h"

namespace haloce_frame_bridge
{
    using namespace vr_core;
    inline constexpr WorldSwapchainMessages kCeWorldMessages{"CE world swapchain acquire did not complete",
        "CE world swapchain wait did not complete", "CE world swapchain release did not complete"};

    extern std::atomic<float> g_ceVisorAchievedZoom;
    extern std::atomic<float> g_ceScopeAspect;
    extern std::atomic<bool> g_ceAnnivGlass;
    extern ScopeRefreshScheduler g_ceScopeRequestScheduler;
    extern std::atomic<unsigned> g_ceRepeatedPairs;
    extern unsigned g_ceLensFrames[2];
    extern unsigned g_ceLensSubmitted[2];
    extern std::atomic<float> g_ceRepeatPoseDelta;
    bool CeVisorFullResActive() noexcept;
    void ReportCeCrosshairPresentation(bool reticleQuadSubmitted, bool haveAim, bool titleCapturesArt);
    XrFovf CeTrackedEyeFov(const halo_ce::EyeCache::Completed& pair, uint32_t index);
    void MirrorCeDesktopAndReport(
        const halo_ce::EyeCache::Completed& pair, ID3D11Texture2D* backbuffer, const D3D11_TEXTURE2D_DESC& bd);
    void NoteCeSubmittedPair(const halo_ce::EyeCache::Completed& pair, const XrCompositionLayerProjectionView* views);
    XrPosef CeTrackedEyePose(const halo_ce::EyeCache::Completed& pair, uint32_t index);
    void CeRequestHullCamera(halo_ce::Tracking& ce);
    void CeFillGunCameraSource(halo_ce::Tracking& ce);
    void CeAnniversaryMonitorFrame(halo_ce::Tracking& ce);
    void CeVisorCrop(const halo_ce::EyeCache::Completed& pair, const float halfX[2], const float halfY[2]);
} // namespace haloce_frame_bridge
