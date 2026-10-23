// reach_frame_source.cpp: the Reach pieces of the VR frame path that lived in vr.cpp (moved unchanged). vr.cpp
// calls them through reach_frame_bridge.h; they reach vr.cpp's frame state through vr_internal.h and
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
#include "reach_frame_bridge.h"
#include "../../core/title_adapter.h"
#include "../../../common/log.h"
#include "../../../common/config.h"
#include "reach_display_worker.h"
using namespace vr_core;
using namespace reach_frame_bridge;
using namespace reach_bridge;

constinit std::atomic<unsigned> g_reachScopeStage{0}; // diagnostic (vr.h)
namespace vr_core
{
    constinit SRWLOCK g_reachDisplayResourceLock = SRWLOCK_INIT;
} // namespace vr_core
namespace
{
    // Two fixed publication slots carry a coherent head/pad/eye sample from
    // PrepareNextFrame into Reach's later render transaction. The high state
    // bit is an exclusive writer claim; the remaining bits count pinned
    // readers. This closes the stale-reader-before-pin race that a bare
    // two-slot reader counter would leave.
    constexpr uint32_t kReachRenderSnapshotWriter = 0x80000000u;
    constexpr uint32_t kReachRenderSnapshotInvalid = 0xFFFFFFFFu;
    constinit ReachVrRenderSnapshot g_reachRenderSnapshots[2]{};
    constinit std::atomic<uint32_t> g_reachRenderSnapshotStates[2]{};
    constinit std::atomic<uint32_t> g_reachRenderSnapshotIndex{kReachRenderSnapshotInvalid};
    static_assert(std::is_trivially_copyable_v<ReachVrRenderSnapshot>);
} // namespace
namespace reach_frame_bridge
{
    bool PublishReachRenderSnapshot(uint64_t preparedSerial, bool padFresh) noexcept
    {
        if (!preparedSerial || !g_headPoseValid || g_views.size() != 2)
            return false;

        ReachVrRenderSnapshot next{};
        next.leftHanded = g_capturedLeftHanded.load(std::memory_order_acquire);
        next.handAlignment = next.leftHanded && g_config.experimental_hand_alignment;
        next.preparedSerial = preparedSerial;
        auto physicalInputs = CurrentAimPoseInputs(
            padFresh && g_rightAimPoseValid, g_rightAimPose, padFresh && g_leftAimPoseValid, g_leftAimPose);
        physicalInputs.twoHandEnabled = false;
        const AimPoseResult physical = ComputeAimPose(physicalInputs);
        next.rightPhysicalValid = physical.valid;
        if (physical.valid)
        {
            next.rightPhysicalOrientation[0] = physical.pose.orientation.x;
            next.rightPhysicalOrientation[1] = physical.pose.orientation.y;
            next.rightPhysicalOrientation[2] = physical.pose.orientation.z;
            next.rightPhysicalOrientation[3] = physical.pose.orientation.w;
        }
        next.predictedDisplayTimeNs = g_preparedFrame.state.predictedDisplayTime;
        const int64_t spaceChangeAt = g_contactSpaceChangeAtNs.load(std::memory_order_acquire);
        if (!spaceChangeAt || next.predictedDisplayTimeNs >= spaceChangeAt)
        {
            next.trackingSpaceEpoch = g_contactSpaceEpoch.load(std::memory_order_acquire);
            if (spaceChangeAt)
                g_contactSpaceChangeAtNs.store(0, std::memory_order_release);
        }
        next.headOrientation[0] = g_headPose.orientation.x;
        next.headOrientation[1] = g_headPose.orientation.y;
        next.headOrientation[2] = g_headPose.orientation.z;
        next.headOrientation[3] = g_headPose.orientation.w;
        next.headPosition[0] = g_headPose.position.x;
        next.headPosition[1] = g_headPose.position.y;
        next.headPosition[2] = g_headPose.position.z;
        // This publisher runs on the same OpenXR frame thread that just wrote
        // the controller poses in CaptureRightControllerPose. Compute from
        // those owned values directly: Reach's render reader remains lock-free,
        // and the aim/head/eyes all belong to this exact prepared serial.
        // A failed action sync leaves the last successful globals intact for
        // the legacy getters.  Never admit those stale values into a newly
        // prepared Reach serial: this snapshot is an exact-frame contract.
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
        if (padFresh)
            next.pad = g_padState;

        for (int eye = 0; eye < 2; ++eye)
        {
            if (!VR_GetEyeViewOffset(eye, next.eyes[eye].position, next.eyes[eye].orientation) ||
                !VR_GetEyeFov(eye, next.eyes[eye].fov))
            {
                return false;
            }
        }

        const uint32_t current = g_reachRenderSnapshotIndex.load(std::memory_order_seq_cst);
        const uint32_t target = current < 2 ? 1u - current : 0u;
        uint32_t expectedState = 0;
        if (!g_reachRenderSnapshotStates[target].compare_exchange_strong(
                expectedState, kReachRenderSnapshotWriter, std::memory_order_seq_cst, std::memory_order_seq_cst))
        {
            // A reader still owns the old non-current slot. Skipping this
            // publication is safer than waiting in the OpenXR frame path; the
            // exact-serial Reach reader will fall open for this frame.
            return false;
        }

        g_reachRenderSnapshots[target] = next;
        g_reachRenderSnapshotIndex.store(target, std::memory_order_seq_cst);
        g_reachRenderSnapshotStates[target].store(0, std::memory_order_seq_cst);
        return true;
    }
    // Reach crosshair gate report (state change + 2 s heartbeat): names which gate
    // refuses the quad instead of guessing (see the 2026-07-27 objective blackout).
    void ReportReachReticleGate(bool reticleQuadSubmitted, bool reticleOwnerAdmitted, bool reachProjectionAdmitted,
        bool haveAim, bool reticleChainAdmitted, bool authoredReticleThisFrame)
    {
        // Heartbeat as well as state change. Twice this
        // point produced ZERO lines, which was ambiguous
        // between "the decision never changed" and "this
        // code never runs" - and a conclusion was drawn
        // from it anyway. A periodic line removes the
        // ambiguity: silence now means only that this
        // point is unreachable.
        static bool loggedSubmitted = true;
        static uint64_t lastHeartbeatMs = 0;
        const uint64_t nowMs = GetTickCount64();
        const bool heartbeat = nowMs - lastHeartbeatMs >= 2000;
        if (heartbeat)
            lastHeartbeatMs = nowMs;
        if (reticleQuadSubmitted != loggedSubmitted || heartbeat)
        {
            loggedSubmitted = reticleQuadSubmitted;
            LOG("REACHHUD: crosshair quad %s "
                "(ownerAdmitted=%d projectionAdmitted=%d "
                "cfgCrosshair=%d haveAim=%d chain=%d "
                "authoredThisFrame=%d heldArt=%d)",
                reticleQuadSubmitted ? "SUBMITTED" : "NOT submitted", reticleOwnerAdmitted ? 1 : 0,
                reachProjectionAdmitted ? 1 : 0, g_config.crosshair ? 1 : 0, haveAim ? 1 : 0,
                reticleChainAdmitted ? 1 : 0, authoredReticleThisFrame ? 1 : 0, g_reticleContainsAuthored ? 1 : 0);
        }
    }
    // A Reach frame whose eye pair did not finish uploading (or whose projection
    // did not expose two views) is skipped, not fatal: revoke its eye serials and
    // authored reticle so nothing stale is queued; the next frame retries.
    void DropIncompleteReachFrame(bool reachStereoUploadComplete)
    {
        g_reachEyeSerial[0].store(0, std::memory_order_release);
        g_reachEyeSerial[1].store(0, std::memory_order_release);
        g_authoredReticleReady = false;
        g_authoredReticleSerial = 0;
        // Loud but rate-limited: never a silent degrade.
        static std::atomic<uint64_t> lastSkipLogMs{0};
        const uint64_t nowMs = GetTickCount64();
        uint64_t previousMs = lastSkipLogMs.load(std::memory_order_relaxed);
        if (nowMs - previousMs >= 2000 &&
            lastSkipLogMs.compare_exchange_strong(previousMs, nowMs, std::memory_order_relaxed))
        {
            LOG("Reach frame skipped (%s); the camera core "
                "stays armed and the next frame retries",
                !reachStereoUploadComplete ? "eye pair did not finish uploading to the "
                                             "XR swapchain"
                                           : "projection did not expose two views");
        }
    }
    ReachFrameAccess BeginReachFrameAccess()
    {
        ReachFrameAccess r;
        r.title = TitleAdapter_GetActiveTitle() == GameTitle::HaloReach;
        if (r.title)
        {
            constexpr size_t reachSlot = TitleRuntimeSlotIndex(GameTitle::HaloReach);
            const TitleRuntimeAvailabilitySnapshot availability = TitleAdapter_GetAvailability();
            const ReachModuleEpoch epoch{
                availability.moduleBases[reachSlot], TitleAdapter_GetGeneration(GameTitle::HaloReach)};
            r.generation = epoch.generation;
            const ReachPreparedFrameToken prepared =
                ReachPreparedFrameToken::Create(epoch, g_preparedFrame.serial, true);
            r.images = VR_ReachBeginRenderAccess(epoch, prepared, r.access) &&
                       g_reachEyeSerial[0].load(std::memory_order_acquire) == g_preparedFrame.serial &&
                       g_reachEyeSerial[1].load(std::memory_order_acquire) == g_preparedFrame.serial;
            if (!r.images && r.access.active)
                VR_ReachEndRenderAccess(r.access);
        }
        return r;
    }
} // namespace reach_frame_bridge
ReachPreparedFrameToken VR_ReachPreparedFrame(const ReachModuleEpoch& epoch)
{
    const uint64_t serial = g_preparedSerialPublished.load(std::memory_order_acquire);
    const uint64_t viewSerial = g_preparedViewSerialPublished.load(std::memory_order_acquire);
    return ReachPreparedFrameToken::Create(
        epoch, serial, serial != 0 && viewSerial == serial && g_preparedShouldRender.load(std::memory_order_acquire));
}

bool VR_ReachGetRenderSnapshot(const ReachPreparedFrameToken& prepared, ReachVrRenderSnapshot& snapshot)
{
    snapshot = {};
    if (!prepared.Ready() || !prepared.Serial())
        return false;

    const uint32_t index = g_reachRenderSnapshotIndex.load(std::memory_order_seq_cst);
    if (index >= 2)
        return false;

    uint32_t state = g_reachRenderSnapshotStates[index].load(std::memory_order_seq_cst);
    if ((state & kReachRenderSnapshotWriter) != 0 || state >= kReachRenderSnapshotWriter - 1)
    {
        return false;
    }
    if (!g_reachRenderSnapshotStates[index].compare_exchange_strong(
            state, state + 1, std::memory_order_seq_cst, std::memory_order_seq_cst))
    {
        return false;
    }

    // A publication can switch slots between our index load and reader pin.
    // Revalidate before touching plain snapshot storage. A writer cannot claim
    // this slot while the reader count is nonzero.
    if (g_reachRenderSnapshotIndex.load(std::memory_order_seq_cst) != index)
    {
        g_reachRenderSnapshotStates[index].fetch_sub(1, std::memory_order_seq_cst);
        return false;
    }

    const ReachVrRenderSnapshot local = g_reachRenderSnapshots[index];
    g_reachRenderSnapshotStates[index].fetch_sub(1, std::memory_order_seq_cst);
    if (local.preparedSerial != prepared.Serial())
        return false;
    snapshot = local;
    return true;
}

bool VR_ReachScopeReady(const ReachVrRenderAccess& access)
{
    return access.active && access.context && access.source && g_scopeCache && g_scopeCacheSrv &&
           g_reachCaptureEnabled.load(std::memory_order_seq_cst) &&
           g_scopeCacheDesc.Width == g_reachCaptureDesc.Width && g_scopeCacheDesc.Height == g_reachCaptureDesc.Height &&
           g_scopeCacheDesc.Format == g_reachCaptureDesc.Format;
}
unsigned VR_ReachScopeNotReadyStage(const ReachVrRenderAccess& access)
{
    if (!access.active || !access.context || !access.source)
        return 54;
    if (!g_scopeCache || !g_scopeCacheSrv)
        return 55;
    if (!g_reachCaptureEnabled.load(std::memory_order_seq_cst))
        return 56;
    return 57;
}
bool VR_ReachCopyScope(ReachVrRenderAccess& access)
{
    if (!VR_ReachScopeReady(access))
        return false;
    // Reach owns the final native target. Never redirect a render-target bind
    // or publish an eye serial for the optional mono image.
    access.context->CopyResource(g_scopeCache, access.source);
    g_scopeImageTitle = GameTitle::HaloReach;
    g_scopeImageGeneration = access.proof.continuity.epoch.generation;
    g_scopeHasImage.store(true, std::memory_order_release);
    NoteScopeImageCaptured();
    return true;
}
void VR_ObserveReachAlternateDepthBind(
    ID3D11DeviceContext* context, UINT renderTargetCount, ID3D11DepthStencilView* depth)
{
    const int eye = g_dlssDepthWindowEye.load(std::memory_order_relaxed);
    if (!depth || eye < 0 || eye > 1)
        return;
    const bool reachActive = TitleAdapter_GetActiveTitle() == GameTitle::HaloReach;
    if (!reachActive)
        return;
    const bool dlssDepthWanted = VR_DlssWantsEyeCamera();
    const bool currentEyeCameraPublished = g_dlssEye[eye].published;
    const bool exactContext = context && context == g_context;
    const bool depthStateMutated = renderTargetCount != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL;
    DlssDepthViewEntry* knownEntry = nullptr;
    for (int i = 0; i < g_dlssDepthViewCount && !knownEntry; ++i)
        if (g_dlssDepthViews[i].view == depth)
            knownEntry = &g_dlssDepthViews[i];
    const bool knownView = knownEntry != nullptr;
    const bool depthTrackingCapacity = knownView || g_dlssDepthViewCount < kDlssDepthViewSlots;
    bool exactDevice = knownEntry && knownEntry->device == g_device;
    // Known views were device-checked before retention. A distinct DSV pays
    // one GetDevice AddRef/Release here, then DlssNoteEyeDepth describes and
    // pins it once; recurring binds stay pointer/atomic-only.
    if (depth && reachActive && dlssDepthWanted && currentEyeCameraPublished && exactContext && depthStateMutated &&
        depthTrackingCapacity && !exactDevice)
    {
        ID3D11Device* device = nullptr;
        depth->GetDevice(&device);
        exactDevice = device && device == g_device;
        if (exactDevice && knownEntry)
            knownEntry->device = g_device;
        if (device)
            device->Release();
    }
    const bool admitted = dlss::ReachAlternateDepthBindEligible(reachActive, dlssDepthWanted, eye,
        currentEyeCameraPublished, exactContext, exactDevice, depthStateMutated, true, depthTrackingCapacity);
    if (admitted)
    {
        DlssNoteEyeDepth(depth, false);
        // The classifier retains a distinct view before returning. Record the
        // already-proven device as borrowed metadata so recurring binds remain
        // pointer-only while the retained DSV keeps that device alive.
        for (int i = 0; i < g_dlssDepthViewCount; ++i)
            if (g_dlssDepthViews[i].view == depth)
                g_dlssDepthViews[i].device = g_device;
    }
}
