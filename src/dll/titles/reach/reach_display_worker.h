// reach_display_worker.h: the Reach display worker state and calls that vr.cpp uses, in namespace reach_bridge.
// Defined in src/dll/titles/reach (until the move: in vr.cpp). The worker owns Reach's Present-side capture;
// vr.cpp calls it at fixed points (Present, resize, detach) and must not reach into it otherwise.
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <atomic>
#include <cstdint>
#include "../../common/titles/reach/reach_render_logic.h"

namespace reach_bridge
{
    void InvalidateReachPresentAdmission() noexcept;
    void ObserveReachPresentSwapchain(IDXGISwapChain* presentSwapchain) noexcept;
    bool ReachPresentMatchesEngineSwapchain(const ReachModuleEpoch& epoch, IDXGISwapChain* presentSwapchain) noexcept;
    void DiscardReachDisplaySnapshots(bool waitForWriters) noexcept;
    bool ResetReachDisplayCandidateLocked(bool teardown, bool releaseResources) noexcept;

    extern ReachModuleEpoch g_reachDisplayEpoch;
    extern std::atomic<bool> g_reachResizeActive;
    extern ReachDisplaySurfaceProof g_reachCaptureProof;
    extern D3D11_TEXTURE2D_DESC g_reachCaptureDesc;
    extern std::atomic<bool> g_reachCaptureEnabled;
    extern std::atomic<uint32_t> g_reachCaptureUsers;
    extern std::atomic<uint64_t> g_reachEyeSerial[2];
} // namespace reach_bridge
