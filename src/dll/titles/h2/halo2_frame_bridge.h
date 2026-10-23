// halo2_frame_bridge.h: what vr.cpp calls or reads in the Halo 2 frame source, in namespace halo2_frame_bridge.
// Defined in titles/h2/halo2_frame_source.cpp. Included by vr.cpp and
// that frame source. The Halo 2 frame source reaches vr.cpp through vr_internal.h and vr_frame.h.
#pragma once
#include "../../core/vr_internal.h"
#include "../../../common/titles/h2/halo2_render_logic.h"
#include "halo2_stereo_core.h"

namespace halo2_frame_bridge
{
    using namespace vr_core;
    inline constexpr uint64_t kHalo2SynchronousReservationBit = uint64_t{1} << 63;
    struct Halo2SynchronousEyeScope
    {
        bool active = false;
        bool completed = false;
        uint32_t generation = 0;
        uint32_t resourceEpoch = 0;
        uint64_t preparedSerial = 0;
        int eye = -1;
        uint32_t captures = 0;
        ID3D11RenderTargetView* finalRtv = nullptr;
        ID3D11RenderTargetView* sceneRtv = nullptr;
        // Every DISTINCT slot-0 target the engine bound while this
        // eye's scope was open, oldest first (pointers only; recorded in the
        // OM hot hook). The classic per-eye image is in one of these, not in
        // either of the two slots the capture used to choose between.
        ID3D11RenderTargetView* lastBoundRtv = nullptr;
        ID3D11RenderTargetView* bound[kHalo2EyeBoundRtvSlots]{};
        int boundCount = 0;
        uint32_t binds = 0;
    };
    inline constexpr WorldSwapchainMessages kHalo2WorldMessages{
        "Halo 2 synchronous world swapchain acquire did not complete",
        "Halo 2 synchronous world swapchain wait did not complete",
        "Halo 2 synchronous world swapchain release did not complete"};
    // Halo 2 Anniversary half of SubmitPreparedFrame: decides whether this prepared
    // frame presents the synchronous stereo pair, repeats the last pair, falls back
    // to the shared default or drops, re-poses the projection views to the pair it
    // will show, and requests quarantine/reports exactly as the inline code did.
    struct Halo2FrameDecision
    {
        bool title = false;
        uint32_t generation = 0;
        float halfX[2]{}, halfY[2]{};
        bool liveExactPair = false;
        bool images = true;
        bool strictStockScreen = false;
        bool stereoWorldFrame = false;
        bool allowStockScreenFrame = true;
    };

    extern std::atomic<uint64_t> g_halo2SynchronousPairToken;
    extern std::atomic<uint32_t> g_halo2SynchronousPairGeneration;
    extern std::atomic<uint32_t> g_halo2SynchronousPairResourceEpoch;
    extern std::atomic<uint64_t> g_halo2SynchronousTargetSerial;
    extern std::atomic<ID3D11RenderTargetView*> g_halo2SynchronousFinalRtv;
    extern std::atomic<ID3D11RenderTargetView*> g_halo2SynchronousSceneRtv;
    extern std::atomic<uint64_t> g_halo2XrPairsRepeated;
    extern std::atomic<uint64_t> g_halo2SynchronousCaptureSerial[2];
    extern std::atomic<uint32_t> g_halo2SynchronousCaptureCount[2];
    extern std::atomic<uint32_t> g_halo2SynchronousResourceEpoch;
    extern std::atomic<uint64_t> g_halo2XrPairsSubmitted;
    extern std::atomic<uint64_t> g_halo2XrPairsDropped;
    extern std::atomic<uint32_t> g_halo2LastDropReason;
    extern thread_local Halo2SynchronousEyeScope g_halo2SynchronousEyeScope;
    extern ID3D11Texture2D* g_halo2ScopeSource;
    extern D3D11_TEXTURE2D_DESC g_halo2ScopeSourceDesc;
    extern bool g_halo2ScopeCopyActive;
    extern bool g_halo2ScopeSourceBound;
    extern std::atomic<uint64_t> g_halo2PreparedCadenceSerial;
    constexpr bool Halo2SynchronousSerialIsReserved(uint64_t value) noexcept
    {
        return (value & kHalo2SynchronousReservationBit) != 0;
    }
    constexpr uint64_t Halo2ReservedSynchronousSerial(uint64_t serial) noexcept
    {
        return serial | kHalo2SynchronousReservationBit;
    }
    void ValidateHalo2EyePairPeriodic();
    void HandleHalo2StockScreenFailure(const char* failureReason, Halo2StockScreenXrOwnership ownership) noexcept;
    bool PublishHalo2RenderSnapshot(uint64_t preparedSerial, bool padFresh) noexcept;
    void PublishHalo2PreparedCadence(
        uint64_t preparedSerial, XrDuration targetPeriod, XrDuration predictedDisplayDelta) noexcept;
    void SubmitHalo2StrictStockScreen(ID3D11Texture2D* backbuffer, const D3D11_TEXTURE2D_DESC& bd,
        bool pausedPresentation, XrCompositionLayerQuad& screenQuad, std::vector<XrCompositionLayerBaseHeader*>& layers,
        bool& halo2StrictStockScreenQueued);
    bool QuarantineHalo2PreparedFrame(Halo2StereoQuarantineReason reason);
    void FinishHalo2Submission(bool halo2StrictStockScreenQueued, bool halo2ProjectionQueued, XrResult r);
    void RememberHalo2Pair(uint32_t generation, const std::vector<XrCompositionLayerProjectionView>& projectionViews,
        const float halfX[2], const float halfY[2]);
    void DropIncompleteHalo2Frame(uint32_t halo2Generation, bool halo2StereoUploadComplete);
    Halo2FrameDecision DecideHalo2Frame(bool stereo, bool pausedPresentation, bool projectionReady,
        std::vector<XrCompositionLayerProjectionView>& projectionViews);
    bool Halo2SynchronousContextCurrent(uint32_t generation, uint64_t preparedSerial, uint32_t resourceEpoch) noexcept;
    bool Halo2CopyFinishedEyeFrame(const Halo2SynchronousEyeScope& scope);
} // namespace halo2_frame_bridge
