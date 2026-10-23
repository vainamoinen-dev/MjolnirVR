// halo4_frame_source.cpp: the Halo 4 pieces of the VR frame path that lived in vr.cpp (moved unchanged). vr.cpp
// calls them through halo4_frame_bridge.h; they reach vr.cpp's frame state through vr_internal.h and
// vr_frame.h.
#include <windows.h>
#include <d3d11.h>
#include <openxr/openxr.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../../core/vr_internal.h"
#include "../../core/vr_frame.h"
#include "halo4_frame_bridge.h"
#include "../../core/game.h"
#include "../../core/title_adapter.h"
#include "../../../common/log.h"
#include "../../../common/config.h"
#include "../../../common/titles/h4/halo4_render_logic.h"
#include "../../../common/titles/h4/halo4_cui_reticle_logic.h"
using namespace vr_core;
using namespace halo4_frame_bridge;

namespace
{
    constinit std::atomic<bool> g_halo4AuthoredReticleTargetBound{false};
} // namespace
namespace halo4_frame_bridge
{
    constinit std::atomic<uint64_t> g_halo4EyeSerial[2]{};
} // namespace halo4_frame_bridge
namespace
{
    enum class Halo4XrPairDropReason : uint32_t
    {
        None = 0,
        EyeUploadIncomplete,
        ProjectionIncomplete,
    };
} // namespace
namespace halo4_frame_bridge
{
    constinit std::atomic<uint64_t> g_halo4XrPairsSubmitted{};
    constinit std::atomic<uint64_t> g_halo4XrPairsDropped{};
    constinit std::atomic<uint32_t> g_halo4LastXrPairDropReason{};
    // Halo 4 scene-target discovery state. Two-phase, because deciding at bind
    // time picked an early pass and put unlit geometry in the headset: phase 1
    // watches a whole eye and remembers the LAST qualifying target, phase 2
    // redirects it once two consecutive eyes name the same one. Declared here
    // so both the resize/detach reset and the end-of-eye resolver can reach it.
    constinit ID3D11RenderTargetView* g_h4LastCandidate = nullptr;
    constinit D3D11_TEXTURE2D_DESC g_h4LastCandidateDesc{};
    constinit DXGI_FORMAT g_h4LastCandidateViewFormat = DXGI_FORMAT_UNKNOWN;
} // namespace halo4_frame_bridge
namespace
{
    constinit ID3D11RenderTargetView* g_h4PreviousEyeCandidate = nullptr;
} // namespace
namespace halo4_frame_bridge
{
    constinit unsigned g_h4CandidatesThisEye = 0;
    constinit unsigned g_h4LearningEyes = 0;

    // Only ever holds raw comparison pointers, never a reference: the latched
    // target is the one that gets AddRef'd, and it lives in g_sceneColorRtv.
    void Halo4ResetSceneTargetDiscovery()
    {
        g_h4LastCandidate = nullptr;
        g_h4LastCandidateDesc = {};
        g_h4LastCandidateViewFormat = DXGI_FORMAT_UNKNOWN;
        g_h4PreviousEyeCandidate = nullptr;
        g_h4CandidatesThisEye = 0;
        g_h4LearningEyes = 0;
    }
} // namespace halo4_frame_bridge
namespace
{
    constexpr uint32_t kHalo4RenderSnapshotWriter = 0x80000000u;
    constexpr uint32_t kHalo4RenderSnapshotInvalid = 0xFFFFFFFFu;
    constinit Halo4VrRenderSnapshot g_halo4RenderSnapshots[2]{};
    constinit std::atomic<uint32_t> g_halo4RenderSnapshotStates[2]{};
    constinit std::atomic<uint32_t> g_halo4RenderSnapshotIndex{kHalo4RenderSnapshotInvalid};
    static_assert(std::is_trivially_copyable_v<Halo4VrRenderSnapshot>);
} // namespace
namespace halo4_frame_bridge
{
    bool PublishHalo4RenderSnapshot(uint64_t preparedSerial, bool padFresh) noexcept
    {
        if (!preparedSerial || g_views.size() != 2)
            return false;

        Halo4VrRenderSnapshot next{};
        next.leftHanded = g_capturedLeftHanded.load(std::memory_order_acquire);
        next.handAlignment = next.leftHanded && g_config.experimental_hand_alignment;
        next.preparedSerial = preparedSerial;
        for (int eye = 0; eye < 2; ++eye)
        {
            if (!VR_GetEyeViewOffset(eye, next.eyes[eye].position, next.eyes[eye].orientation))
            {
                return false;
            }
            // Optional by design: a runtime that cannot report this eye's FOV
            // costs Halo 4 only its native-cover widening, and the camera core
            // renders at Halo 4's stock FOV instead of dropping the frame.
            next.eyes[eye].fovValid = VR_GetEyeFov(eye, next.eyes[eye].fov);
        }
        // Same frame as the eye offsets above. Head tracking degrades to the
        // engine's own camera if this is unavailable; it never fails the pair.
        next.headPoseValid = VR_GetHeadPose(next.headOrientation, next.headPosition);
        // The publisher owns the OpenXR action values for this prepared frame.
        // Copy them here exactly as Reach does, so Halo 4's palette hook never
        // enters the tracking critical section and cannot mix controller poses
        // from two prepared serials.
        const bool rightPoseFresh = padFresh && g_rightAimPoseValid;
        const bool leftPoseFresh = padFresh && g_leftAimPoseValid;
        const AimPoseInputs labAimInputs =
            CurrentFrameStockAimPoseInputs(rightPoseFresh, g_rightAimPose, leftPoseFresh, g_leftAimPose);
        const AimPoseResult aim = ComputeAimPose(labAimInputs);
        next.rightAimValid = aim.valid;
        next.twoHandAimActive = aim.valid && aim.twoHandActive;
        if (aim.valid)
        {
            // Orientation only: the presentation surfaces consume the aim that
            // includes any active grab/release transition correction; the
            // position and the raw two-hand activity flag stay exactly as the
            // solver returned them. The Lab temporal presentation (VS-off
            // only) runs after the VS one; at most one is ever active.
            const XrPosef presentedAim =
                PresentedTwoHandLabTemporalPose(PresentedAimContinuityPose(aim.pose, preparedSerial), preparedSerial,
                    labAimInputs.twoHandLabGeneration);
            next.rightAimOrientation[0] = presentedAim.orientation.x;
            next.rightAimOrientation[1] = presentedAim.orientation.y;
            next.rightAimOrientation[2] = presentedAim.orientation.z;
            next.rightAimOrientation[3] = presentedAim.orientation.w;
            next.rightAimPosition[0] = presentedAim.position.x;
            next.rightAimPosition[1] = presentedAim.position.y;
            next.rightAimPosition[2] = presentedAim.position.z;
        }
        // Owner-safe ordinary one-hand fallback from the SAME prepared
        // controller sample (the solver returns through its calibrated
        // one-hand path whenever two-hand support is disabled). Computed only
        // while the persistent support grip is wired for Halo 4, so with the
        // feature off this adds no solve and the field keeps its falsy
        // default. No new solver and no new mode.
        if (VR_SupportGripWiredForTitle(GameTitle::Halo4))
        {
            AimPoseInputs oneHandInputs =
                CurrentAimPoseInputs(rightPoseFresh, g_rightAimPose, leftPoseFresh, g_leftAimPose);
            oneHandInputs.twoHandEnabled = false;
            const AimPoseResult oneHandAim = ComputeAimPose(oneHandInputs);
            next.oneHandRightAimValid = oneHandAim.valid;
            if (oneHandAim.valid)
            {
                next.oneHandRightAimOrientation[0] = oneHandAim.pose.orientation.x;
                next.oneHandRightAimOrientation[1] = oneHandAim.pose.orientation.y;
                next.oneHandRightAimOrientation[2] = oneHandAim.pose.orientation.z;
                next.oneHandRightAimOrientation[3] = oneHandAim.pose.orientation.w;
            }
        }
        next.leftControllerValid = leftPoseFresh;
        if (leftPoseFresh)
        {
            next.leftControllerOrientation[0] = g_leftAimPose.orientation.x;
            next.leftControllerOrientation[1] = g_leftAimPose.orientation.y;
            next.leftControllerOrientation[2] = g_leftAimPose.orientation.z;
            next.leftControllerOrientation[3] = g_leftAimPose.orientation.w;
            next.leftControllerPosition[0] = g_leftAimPose.position.x;
            next.leftControllerPosition[1] = g_leftAimPose.position.y;
            next.leftControllerPosition[2] = g_leftAimPose.position.z;
        }

        const uint32_t current = g_halo4RenderSnapshotIndex.load(std::memory_order_seq_cst);
        const uint32_t target = current < 2 ? 1u - current : 0u;
        uint32_t expectedState = 0;
        if (!g_halo4RenderSnapshotStates[target].compare_exchange_strong(
                expectedState, kHalo4RenderSnapshotWriter, std::memory_order_seq_cst, std::memory_order_seq_cst))
        {
            // The old slot still has a reader. Never wait in the OpenXR frame
            // path; the exact-serial camera hook will render stock this frame.
            return false;
        }
        g_halo4RenderSnapshots[target] = next;
        g_halo4RenderSnapshotIndex.store(target, std::memory_order_seq_cst);
        g_halo4RenderSnapshotStates[target].store(0, std::memory_order_seq_cst);
        return true;
    }
    // Halo 4: invalidate the prepared pair and count the drop.
    void DropIncompleteHalo4Frame(bool halo4StereoUploadComplete)
    {
        VR_InvalidateHalo4PreparedFrame();
        if (!g_abortFrameForSwapchainFailure)
        {
            g_halo4XrPairsDropped.fetch_add(1, std::memory_order_relaxed);
            g_halo4LastXrPairDropReason.store(
                static_cast<uint32_t>(!halo4StereoUploadComplete ? Halo4XrPairDropReason::EyeUploadIncomplete
                                                                 : Halo4XrPairDropReason::ProjectionIncomplete),
                std::memory_order_relaxed);
        }
    }
    // Halo 4: both eye renders belong to this prepared serial.
    bool Halo4PreparedFrameImagesReady()
    {
        return Halo4PreparedPairMatches(g_preparedFrame.serial, g_halo4EyeSerial[0].load(std::memory_order_acquire),
            g_halo4EyeSerial[1].load(std::memory_order_acquire));
    }
} // namespace halo4_frame_bridge
bool VR_Halo4GetRenderSnapshot(Halo4VrRenderSnapshot& snapshot)
{
    snapshot = {};
    const uint64_t serial = g_preparedSerialPublished.load(std::memory_order_acquire);
    if (!serial || g_preparedViewSerialPublished.load(std::memory_order_acquire) != serial ||
        !g_preparedShouldRender.load(std::memory_order_acquire))
    {
        return false;
    }

    const uint32_t index = g_halo4RenderSnapshotIndex.load(std::memory_order_seq_cst);
    if (index >= 2)
        return false;
    uint32_t state = g_halo4RenderSnapshotStates[index].load(std::memory_order_seq_cst);
    if ((state & kHalo4RenderSnapshotWriter) != 0 || state >= kHalo4RenderSnapshotWriter - 1 ||
        !g_halo4RenderSnapshotStates[index].compare_exchange_strong(
            state, state + 1, std::memory_order_seq_cst, std::memory_order_seq_cst))
    {
        return false;
    }
    if (g_halo4RenderSnapshotIndex.load(std::memory_order_seq_cst) != index)
    {
        g_halo4RenderSnapshotStates[index].fetch_sub(1, std::memory_order_seq_cst);
        return false;
    }
    const Halo4VrRenderSnapshot local = g_halo4RenderSnapshots[index];
    g_halo4RenderSnapshotStates[index].fetch_sub(1, std::memory_order_seq_cst);

    // Close publication rollover on both sides of the copy. A snapshot from a
    // prior or newly prepared frame is never admitted into this transaction.
    if (local.preparedSerial != serial || g_preparedSerialPublished.load(std::memory_order_acquire) != serial ||
        g_preparedViewSerialPublished.load(std::memory_order_acquire) != serial ||
        !g_preparedShouldRender.load(std::memory_order_acquire))
    {
        return false;
    }
    snapshot = local;
    return true;
}
bool VR_CaptureHalo4RenderedEye(int eye, uint64_t preparedSerial)
{
    if (eye < 0 || eye > 1 || !preparedSerial)
        return false;
    if (!Halo4EyeCaptureIsCurrent(
            eye, g_rasterEye.load(std::memory_order_acquire), g_rasterRedirected[eye], g_eyeCache[eye] != nullptr))
    {
        g_halo4EyeSerial[eye].store(0, std::memory_order_release);
        return false;
    }
    g_eyeHasImage[eye] = true;
    g_halo4EyeSerial[eye].store(preparedSerial, std::memory_order_release);
    return true;
}

void VR_InvalidateHalo4PreparedFrame()
{
    g_halo4EyeSerial[0].store(0, std::memory_order_release);
    g_halo4EyeSerial[1].store(0, std::memory_order_release);
    g_eyeHasImage[0] = false;
    g_eyeHasImage[1] = false;
}
namespace halo4_frame_bridge
{
    // Decides Halo 4's scene target at the END of an eye, when the whole bind
    // sequence has been seen. Called only from VR_EndRasterEye.
    void Halo4ResolveSceneTargetAtEyeEnd()
    {
        if (g_sceneColorRtv)
            return;
        ID3D11RenderTargetView* const candidate = g_h4LastCandidate;
        const unsigned seen = g_h4CandidatesThisEye;
        g_h4LastCandidate = nullptr;
        g_h4CandidatesThisEye = 0;
        if (!candidate)
            return;
        ++g_h4LearningEyes;
        if (candidate != g_h4PreviousEyeCandidate)
        {
            // First sighting, or the sequence is still settling. Report the shape
            // so an unstable pipeline is visible rather than silent.
            g_h4PreviousEyeCandidate = candidate;
            LOG("SCENEPROBE: Halo 4 learning eye %u ended; %u qualifying "
                "full-size colour target(s) bound, last = RTV %p (%ux%u fmt=%u "
                "viewfmt=%u). Waiting for a second eye to agree before latching",
                g_h4LearningEyes, seen, candidate, g_h4LastCandidateDesc.Width, g_h4LastCandidateDesc.Height,
                static_cast<UINT>(g_h4LastCandidateDesc.Format), static_cast<UINT>(g_h4LastCandidateViewFormat));
            return;
        }

        D3D11_TEXTURE2D_DESC eyeDesc = g_h4LastCandidateDesc;
        eyeDesc.Format = g_h4LastCandidateViewFormat;
        if (!EnsureEyeCaches(eyeDesc))
        {
            LOG("SCENEPROBE: Halo 4 scene target RTV %p agreed twice but the eye "
                "caches could not be created for viewfmt=%u; not latching",
                candidate, static_cast<UINT>(g_h4LastCandidateViewFormat));
            return;
        }
        candidate->AddRef();
        g_sceneColorRtv = candidate;
        LOG("raster: learned Halo 4 scene-color RTV %p as the LAST qualifying "
            "target of the eye (%ux%u fmt=%u viewfmt=%u), agreed by two "
            "consecutive eyes after %u learning eye(s); steady-state redirect is "
            "pointer-only",
            g_sceneColorRtv, g_h4LastCandidateDesc.Width, g_h4LastCandidateDesc.Height,
            static_cast<UINT>(g_h4LastCandidateDesc.Format), static_cast<UINT>(g_h4LastCandidateViewFormat),
            g_h4LearningEyes);
    }
} // namespace halo4_frame_bridge
void VR_Halo4NoteBoundRenderTargets(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView* const* rtvs)
{
    if (context != g_context)
        return;
    if (!g_reticleCaptureState.active || !g_reticleCaptureState.framingCaptured)
    {
        g_halo4AuthoredReticleTargetBound.store(false, std::memory_order_release);
        return;
    }
    const uintptr_t boundSlot0 = count && rtvs ? reinterpret_cast<uintptr_t>(rtvs[0]) : 0;
    // D3D11 immediate-context calls are ordered on one render thread. The D3D
    // hooks publish this only after the real state mutation and invalidate it
    // on the alternate RTV/UAV setter and ClearState as well. This retains
    // exact slot-0, mode-selected target gate without putting an
    // OMGetRenderTargets AddRef/Release pair in every admitted Draw.
    g_halo4AuthoredReticleTargetBound.store(
        VrHalo4AuthoredReticleDrawTargetMatches(TitleAdapter_GetActiveTitle() == GameTitle::Halo4,
            g_reticleCaptureState.active, g_reticleCaptureState.framingCaptured,
            g_reticleCaptureState.publishesAuthored, boundSlot0, reinterpret_cast<uintptr_t>(g_authoredReticleRtv),
            reinterpret_cast<uintptr_t>(g_authoredReticleDiscardRtv)),
        std::memory_order_release);
}

void VR_Halo4PrepareAuthoredReticleDraw(ID3D11DeviceContext* context)
{
    if (context != g_context || !g_reticleCaptureState.active || !g_reticleCaptureState.framingCaptured ||
        !g_halo4AuthoredReticleTargetBound.load(std::memory_order_acquire) ||
        TitleAdapter_GetActiveTitle() != GameTitle::Halo4)
    {
        return;
    }

    const D3D11_VIEWPORT& base = g_reticleCaptureState.captureViewport;
    const Halo4CuiCaptureViewport input{
        base.TopLeftX, base.TopLeftY, base.Width, base.Height, base.MinDepth, base.MaxDepth};
    Halo4CuiCaptureViewport framed{};
    const uint32_t backbufferWidth = g_gameBackbufferDescValid ? g_gameBackbufferDesc.Width : 0;
    const uint32_t backbufferHeight = g_gameBackbufferDescValid ? g_gameBackbufferDesc.Height : 0;
    // 3CX fold-in: the vertical bias and the presentation zoom follow the
    // LIVE layout the visible-pass hide recorded; before any hide runs the
    // helpers return the calibrated 3BR constants.
    float liveBaseY = 0.0f;
    float liveHideShift = 0.0f;
    (void)Game_Halo4LiveCuiCanvas(liveBaseY, liveHideShift);
    if (!Halo4BuildCuiCaptureDrawViewport(input, backbufferWidth, backbufferHeight,
            Halo4CuiLiveVerticalBiasRatio(liveBaseY), Halo4CuiLiveCaptureScale(liveHideShift), framed))
    {
        return;
    }

    const D3D11_VIEWPORT viewport{
        framed.topLeftX, framed.topLeftY, framed.width, framed.height, framed.minDepth, framed.maxDepth};
    context->RSSetViewports(1, &viewport);
    context->RSSetScissorRects(1, &g_reticleCaptureState.captureScissor);
    g_authoredReticleFramingReasserts.fetch_add(1, std::memory_order_relaxed);
}

bool VR_Halo4AuthoredReticleFeatureHealthy()
{
    // InstallHalo4CuiReticle arms only after every capture/suppression resource
    // is prepared. The only permanent runtime invalidation is this atomic
    // latch; checking it from the CUI hook is sufficient and avoids racing on
    // the render thread's raw D3D/OpenXR resource pointers.
    return !g_reticleChainFailed.load(std::memory_order_acquire);
}
