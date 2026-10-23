// vr_inline_ops.h: small per-frame operations on vr_internal.h state that several VR units call (Fail and XrStr keep a function-local static: one instance, as inline functions), in namespace vr_core. Moved from vr.cpp unchanged apart from `inline`.
#pragma once
#include "vr_internal.h"
#include "dlss.h"
#include "title_adapter.h"
#include "../../common/log.h"
#include "../../common/dxgi_view_format.h"
#include "vr_frame.h"
#include "vr_compose.h"
#include "vr_controllers.h"
#include "vr_scope.h"
#include "vr_telemetry.h"
#include "vr_format_helpers.h"

namespace vr_core
{
    // The rendered gun's lens centre in OpenXR local space (this frame's committed palette), or false with the reason.
    inline bool ReadAnchoredLens(float out[3], const char** why = nullptr) noexcept
    {
        g_lensReadReport = {};
        const char* reason = "";
        const GameTitle title = TitleAdapter_GetActiveTitle();
        VrContactTrackingSnapshot tracking{};
        dual_reticle::ProjectedRay projected{};
        bool ok = false;
        if (!g_config.scope_lens_node_anchor)
            reason = "scope_lens_node_anchor=0";
        else if (!lens_anchor::Supported(title))
            reason = "title has no lens receipts";
        else if (!VR_GetContactTrackingSnapshot(tracking))
            reason = "no tracking snapshot";
        else if (!g_weaponLensRays[static_cast<unsigned>(title)][0].Read(projected))
            reason = lens_anchor::ReadResultName(lens_anchor::ReadResult::NoReceipt);
        else
        {
            const auto report = lens_anchor::Classify(projected, title, TitleAdapter_GetGeneration(title),
                tracking.referenceEpoch, tracking.serial, tracking.timeNs, GetTickCount64(), 0, g_config.left_handed);
            g_lensReadReport = report;
            if (report.result == lens_anchor::ReadResult::Ok)
                ok = true;
            else
                reason = lens_anchor::ReadResultName(report.result);
        }
        if (why)
            *why = reason;
        if (!ok)
            return false;
        for (int i = 0; i < 3; ++i)
            out[i] = projected.tracking.position[i];
        return true;
    }

    inline void InvalidateTwoHandInputSmoothingLayer() noexcept
    {
        two_hand_input_smoothing::Reset(g_twoHandInputSmoothingLayer.filter);
        g_twoHandInputSmoothingLayer.lastPreparedSerial = 0;
        g_twoHandInputSmoothingLayer.haveIdentity = false;
        g_twoHandInputSmoothingOutput = TwoHandInputSmoothingPreparedOutput{};
        PublishTwoHandInputSmoothing(0, 0, GameTitle::None, 0, false, false, false, two_hand_input_smoothing::Result{});
    }

    inline void PublishAimContinuityCorrection(uint64_t serial, uint64_t contactSpaceEpoch, bool valid,
        bool stockHeadCoherent, bool supportEndpointUsedGrip, virtual_stock::Quat4 correction) noexcept
    {
        auto& published = g_aimContinuityPublication;
        published.sequence.fetch_add(1, std::memory_order_acq_rel);
        published.serial.store(serial, std::memory_order_relaxed);
        published.contactSpaceEpoch.store(contactSpaceEpoch, std::memory_order_relaxed);
        published.valid.store(valid ? 1u : 0u, std::memory_order_relaxed);
        published.stockHeadCoherent.store(stockHeadCoherent ? 1u : 0u, std::memory_order_relaxed);
        published.supportEndpointUsedGrip.store(supportEndpointUsedGrip ? 1u : 0u, std::memory_order_relaxed);
        published.cx.store(correction.x, std::memory_order_relaxed);
        published.cy.store(correction.y, std::memory_order_relaxed);
        published.cz.store(correction.z, std::memory_order_relaxed);
        published.cw.store(correction.w, std::memory_order_relaxed);
        published.sequence.fetch_add(1, std::memory_order_release);
    }

    // Conservative, deterministic invalidation: no stale history may replay
    // after a reset, epoch change, title/handedness change or feature disable.
    // Consumers see identity afterwards because the packet is published
    // invalid, which is also what every fail-open path returns.
    inline void InvalidateAimContinuityState() noexcept
    {
        ResetAimContinuity(g_aimContinuityLayer.transition);
        g_aimContinuityLayer.lastPresentedValid = false;
        g_aimContinuityLayer.lastPresented = virtual_stock::Quat4{};
        g_aimContinuityLayer.publishedValid = false;
        g_aimContinuityLayer.haveIdentity = false;
        g_aimContinuityOutput = AimContinuityPreparedOutput{};
        PublishAimContinuityCorrection(0, 0, false, false, false, virtual_stock::Quat4{});
    }

    inline void InvalidateAimContinuityLayer() noexcept
    {
        InvalidateAimContinuityState();
        InvalidateTwoHandInputSmoothingLayer();
    }

    inline const char* XrStr(XrResult r)
    {
        static char buf[XR_MAX_RESULT_STRING_SIZE];
        if (g_instance != XR_NULL_HANDLE && XR_SUCCEEDED(xrResultToString(g_instance, r, buf)))
            return buf;
        snprintf(buf, sizeof(buf), "XrResult(%d)", (int)r);
        return buf;
    }

    // Tell the player VR failed without freezing the game (own thread) and let
    // the game keep running flat.
    inline void Fail(const char* what, XrResult r = XR_SUCCESS)
    {
        char msg[512];
        if (r != XR_SUCCESS)
            snprintf(msg, sizeof(msg), "%s (%s)", what, XrStr(r));
        else
            snprintf(msg, sizeof(msg), "%s", what);
        LOG("VR FAILED: %s", msg);
        g_authoredReticlePreparationReady.store(false, std::memory_order_release);
        g_state = State::Failed;

        static char popupText[640];
        snprintf(popupText, sizeof(popupText),
            "MjolnirVR mod could not start VR:\n\n%s\n\n"
            "The game will keep running flat on the monitor.\n"
            "Details are in HaloMCCVR.log next to the mod DLL.",
            msg);
        CreateThread(
            nullptr, 0,
            [](LPVOID p) -> DWORD
            {
                MessageBoxA(nullptr, (const char*)p, "MjolnirVR mod", MB_OK | MB_ICONWARNING | MB_TOPMOST);
                return 0;
            },
            popupText, 0, nullptr);
    }

    // Drop one source's cached view. The owner calls this before it destroys or
    // recreates a texture we may have sampled, so the table never keeps a dead
    // resource alive and never matches a recycled address.
    inline void ForgetSourceView(ID3D11Texture2D* source)
    {
        if (!source)
            return;
        if (void* view = g_srcSrvCache.Forget(source))
            static_cast<ID3D11ShaderResourceView*>(view)->Release();
    }

    inline void SetDlssStatus(const char* text)
    {
        AcquireSRWLockExclusive(&g_dlssStatusLock);
        snprintf(g_dlssStatusText, sizeof(g_dlssStatusText), "%s", text);
        ReleaseSRWLockExclusive(&g_dlssStatusLock);
    }

    inline void ReleaseDlssStereoBatch()
    {
        g_dlssKicked[0].Reset();
        g_dlssKicked[1].Reset();
        auto release = [](auto*& object)
        {
            if (object)
                object->Release();
            object = nullptr;
        };
        if (g_dlssBatch.output)
            ForgetSourceView(g_dlssBatch.output);
        for (auto* output : g_dlssBatch.eyeOutput)
            if (output)
                ForgetSourceView(output);
        release(g_dlssBatch.motionRtv);
        release(g_dlssBatch.depthRtv);
        release(g_dlssBatch.color);
        release(g_dlssBatch.motion);
        release(g_dlssBatch.depth);
        release(g_dlssBatch.output);
        release(g_dlssBatch.eyeOutput[0]);
        release(g_dlssBatch.eyeOutput[1]);
        g_dlssBatch = {};
    }

    inline void ReleaseDlssDepthTracking()
    {
        if (g_dlssSceneDepthDsv)
        {
            g_dlssSceneDepthDsv->Release();
            g_dlssSceneDepthDsv = nullptr;
        }
        for (int i = 0; i < g_dlssDepthViewCount; ++i)
        {
            if (g_dlssDepthViews[i].view)
                g_dlssDepthViews[i].view->Release();
            g_dlssDepthViews[i] = {};
        }
        g_dlssDepthViewCount = 0;
        g_dlssDepthViewTableFull = false;
        g_dlssEyeSceneDepthSeen = false;
        g_dlssEyeLastFullRasterDsv = nullptr;
        g_dlssDepthSourceLogged = false;
    }

    // Resize, title detach, or the player turning the upscaler off: every
    // DLSS-owned texture, feature and depth reference goes. NGX itself stays
    // initialized for the device; the shaders are device-lifetime.
    inline void ReleaseDlssResources()
    {
        g_dlssKicked[0].Reset();
        g_dlssKicked[1].Reset();
        Dlss_ReleaseFeatures();
        ReleaseDlssStereoBatch();
        for (DlssEyeState& state : g_dlssEye)
        {
            ReleaseDlssEyeTextures(state);
            state.previous = {};
            state.published = false;
        }
        ReleaseDlssDepthTracking();
        g_dlssJitterArmed = false;
        g_dlssEyesResolvedThisFrame = 0;
        g_dlssResourcesLogged = false;
    }

    // Once per frame: the render plan the config asks for (headset shape,
    // Resolution scale, Upscaler, DLSS mode) against the one in force. After
    // the wanted plan has been the same for half a second (a slider drag
    // changes it every frame) and it still differs, ask MCC to re-size.
    // D3D_RequestRenderPlan ignores a pending or refused size, so this is
    // idempotent and never spams.
    inline RuntimeMode DlssPresentationMode()
    {
        return g_pausePresentation.load() ? RuntimeMode::Paused : TitleAdapter_GetRuntimeMode();
    }

    inline void ReleaseTheaterResolvedResources()
    {
        for (int eye = 0; eye < 2; ++eye)
        {
            if (g_theaterResolvedSrv[eye])
                g_theaterResolvedSrv[eye]->Release();
            if (g_theaterResolvedRtv[eye])
                g_theaterResolvedRtv[eye]->Release();
            if (g_theaterResolved[eye])
                g_theaterResolved[eye]->Release();
            g_theaterResolvedSrv[eye] = nullptr;
            g_theaterResolvedRtv[eye] = nullptr;
            g_theaterResolved[eye] = nullptr;
        }
        g_theaterResolvedDesc = {};
    }

    inline void ReleaseTheaterSubtitleBand()
    {
        if (g_theaterSubtitleBandSrv)
            g_theaterSubtitleBandSrv->Release();
        if (g_theaterSubtitleBand)
            g_theaterSubtitleBand->Release();
        g_theaterSubtitleBandSrv = nullptr;
        g_theaterSubtitleBand = nullptr;
        g_theaterSubtitleBandDesc = {};
        g_theaterSubtitleBandStartV = -1.0f;
    }

    // The format the scope cache is viewed (sampled) as: the concrete format
    // for a TYPELESS cache. The sRGB/linearize choice depends on this one.
    inline DXGI_FORMAT ScopeCacheViewFormat()
    {
        return static_cast<DXGI_FORMAT>(dxgi_view_format::Concrete(static_cast<uint32_t>(g_scopeCacheDesc.Format)));
    }

    // Same acquire/wait/upload/release cycle as the lens, into the visor chain.
    // The held scope image is delivered only while it is under 0.5 s old
    // (CE Anniversary borrows exempt). Shared by the scope screen and the visor window.
    inline bool HeldScopeImageFresh()
    {
        const uint64_t heldNowMs = GetTickCount64(),
                       capturedMs = g_scopeImageCapturedMs.load(std::memory_order_acquire);
        return glass_reprojection::HeldImageFresh(
            g_scopeImageTitle == GameTitle::HaloCE, capturedMs && heldNowMs >= capturedMs ? heldNowMs - capturedMs : 0);
    }

    inline void EnterFrameWaitFatalDrain(const char* reason)
    {
        g_waitPipelineFaulted.store(true, std::memory_order_release);
        // Not every fatal-drain caller resets the prepared frame first; clear
        // the pairing here too so no stale coherence survives session loss.
        g_stockAimFresh.store(false, std::memory_order_release);
        g_supportGripPoseFresh.store(false, std::memory_order_release);
        InvalidateAimContinuityLayer();
        InvalidateTwoHandLabTemporal();
        g_waitThreadStop.store(true, std::memory_order_release);
        if (g_waitConsumedEvent)
            SetEvent(g_waitConsumedEvent);
        if (g_waitReadyEvent)
            SetEvent(g_waitReadyEvent);
        if (g_waitStartedEvent)
            SetEvent(g_waitStartedEvent);
        g_authoredReticlePreparationReady.store(false, std::memory_order_release);
        g_sessionRunning = false;
        if (!g_frameWaitFatalExitReason)
            g_frameWaitFatalExitReason = reason;
        g_frameWaitExitRequestAccepted = false;
        g_frameWaitExitLastRequestMs = 0;
        g_frameWaitExitLastLogMs = 0;
        Game_DetachForVrRuntimeFailure();
    }

    inline bool RequireExactSwapchainCompletion(XrResult result, const char* failureReason)
    {
        // Strict title transactions accept only exact XR_SUCCESS.
        // XR_TIMEOUT_EXPIRED does not
        // complete a wait; XR_SESSION_LOSS_PENDING is a successful operation
        // result but signals an unhealthy session. Both abort this candidate's
        // complete layer transaction and enter terminal session recovery. Do
        // not attempt another acquire/release from this frame.
        if (result == XR_SUCCESS)
            return true;
        g_abortFrameForSwapchainFailure = true;
        EnterFrameWaitFatalDrain(failureReason);
        return false;
    }

    inline bool SecondaryWeaponPresentationActive() noexcept
    {
        const GameTitle title = TitleAdapter_GetActiveTitle();
        const size_t slot = TitleRuntimeSlotIndex(title);
        return slot < kTitleRuntimeSlotCount &&
               g_secondaryWeaponPresentation[slot].Active(TitleAdapter_GetGeneration(title), GetTickCount64());
    }

    inline void ApplyPreparedTwoHandInputSmoothing(AimPoseInputs& inputs, uint64_t preparedSerial) noexcept
    {
        const TwoHandInputSmoothingPreparedOutput& smoothing = g_twoHandInputSmoothingOutput;
        // The strength is the frozen per-serial value the packet was produced
        // with, never a live config read. Strength 0 makes the packet inactive,
        // so the raw path is taken exactly. The eligibility terms match the
        // prepared-frame advance gate and the cross-thread apply gate (one
        // rule, both stock modes): a VS-ON solve consumes the same smoothed
        // copies a VS-OFF solve does.
        if (smoothing.strength > 0.0f && smoothing.active && smoothing.sampleValid &&
            smoothing.serial == preparedSerial && inputs.twoHandEnabled && inputs.twoHandLatched && inputs.leftValid)
        {
            ApplyTwoHandSmoothingGeometry(inputs, smoothing.mixed);
        }
    }

    inline void PublishTwoHandLabTemporalPacket(uint64_t serial, uint64_t contactSpaceEpoch, uint64_t generation,
        bool valid, virtual_stock::Quat4 base, virtual_stock::Quat4 correction) noexcept
    {
        auto& published = g_twoHandLabTemporalPublication;
        published.sequence.fetch_add(1, std::memory_order_acq_rel);
        published.serial.store(serial, std::memory_order_relaxed);
        published.contactSpaceEpoch.store(contactSpaceEpoch, std::memory_order_relaxed);
        published.generation.store(generation, std::memory_order_relaxed);
        published.valid.store(valid ? 1u : 0u, std::memory_order_relaxed);
        published.sx.store(base.x, std::memory_order_relaxed);
        published.sy.store(base.y, std::memory_order_relaxed);
        published.sz.store(base.z, std::memory_order_relaxed);
        published.sw.store(base.w, std::memory_order_relaxed);
        published.cx.store(correction.x, std::memory_order_relaxed);
        published.cy.store(correction.y, std::memory_order_relaxed);
        published.cz.store(correction.z, std::memory_order_relaxed);
        published.cw.store(correction.w, std::memory_order_relaxed);
        published.sequence.fetch_add(1, std::memory_order_release);
    }

    // ---- Weapon-order diagnostics (read-only evidence) ----
    // All probes below are gated on Telemetry_WeaponEventsAccepting(): when
    // recording is off they cost one atomic load and change nothing. When on,
    // they perform bounded read-only native observations (no alloc/log/lock/
    // COM/scan/IO/mutation) and publish fixed records with a global sequence.
    // Gameplay, latch, aim, presentation, and lifecycle state are untouched.
    inline void PublishWeaponOrderMarker(WeaponOrderEventKind kind) noexcept
    {
        if (!Telemetry_WeaponEventsAccepting())
            return;
        const GameTitle title = TitleAdapter_GetActiveTitle();
        Telemetry_PublishWeaponEvent(static_cast<uint8_t>(kind),
            static_cast<uint8_t>(WeaponOrderEventStatus::NoObservation), static_cast<uint8_t>(title),
            TitleAdapter_GetGeneration(title), g_preparedFrame.serial, UINT32_MAX, UINT32_MAX, 0, 0);
    }

    inline void PublishCaptureProbeResult(
        GameTitle title, uint64_t beginSeq, bool readOk, uint32_t unit, uint32_t weapon, uint32_t detail) noexcept
    {
        WeaponOrderEventStatus status;
        if (readOk)
            status = WeaponOrderEventStatus::Success;
        else if (detail & 0x80000000u)
            status = WeaponOrderEventStatus::ExceptionOrFault;
        else if (detail & 0x40000000u)
            status = WeaponOrderEventStatus::GuardRejected;
        else
            status = WeaponOrderEventStatus::ReaderReturnedFalse;
        Telemetry_PublishWeaponEvent(static_cast<uint8_t>(WeaponOrderEventKind::CaptureProbeResult),
            static_cast<uint8_t>(status), static_cast<uint8_t>(title), TitleAdapter_GetGeneration(title),
            g_preparedFrame.serial, readOk ? unit : UINT32_MAX, readOk ? weapon : UINT32_MAX, beginSeq,
            detail & 0x3FFFFFFFu);
    }

    inline void StopControllerHaptics()
    {
        if (g_session == XR_NULL_HANDLE || g_hapticAction == XR_NULL_HANDLE)
            return;
        XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
        info.action = g_hapticAction;
        info.subactionPath = g_leftHandPath;
        xrStopHapticFeedback(g_session, &info);
        info.subactionPath = g_rightHandPath;
        xrStopHapticFeedback(g_session, &info);
    }
} // namespace vr_core
