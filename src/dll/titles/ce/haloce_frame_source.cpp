// haloce_frame_source.cpp: the Halo CE pieces of the VR frame path that lived in vr.cpp (moved unchanged). vr.cpp
// calls them through haloce_frame_bridge.h; they reach vr.cpp's frame state through vr_internal.h and
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
#include "haloce_frame_bridge.h"
#include "../../core/menu.h"
#include "../../../common/lens_anchor.h"
#include "../../../common/titles/ce/haloce_reticle_logic.h"
#include "../../core/title_adapter.h"
#include "../../../common/log.h"
#include "../../../common/config.h"
#include "../../../common/vehicle_monitors_logic.h"
#include "../../../common/titles/ce/haloce_vehicle_frame.h"
#include "../../core/native_vehicle_first_person.h"
using namespace vr_core;
using namespace haloce_frame_bridge;

namespace haloce_frame_bridge
{
    // CE visor zoom as its own full-resolution third render (head view) instead of a crop.
    bool CeVisorFullResActive() noexcept
    {
        return g_config.ce_visor_fullres && TitleAdapter_GetActiveTitle() == GameTitle::HaloCE &&
               HaloCE_ClassicScopeReady() &&
               g_zoomTier.load(std::memory_order_acquire) == static_cast<uint8_t>(visor_zoom::Tier::Visor);
    }
    constinit std::atomic<float> g_ceVisorAchievedZoom{1.0f}; // CE visor crop's real magnification
    // CE eye raster aspect (width/height), for the Classic scope projection.
    constinit std::atomic<float> g_ceScopeAspect{16.0f / 9.0f};
    // The held CE gun gets the Anniversary gun-camera glass (tier block); the
    // only case the borrowed-frame switch admits CE's scope renderer.
    constinit std::atomic<bool> g_ceAnnivGlass{false};
    // CE Classic: the third world render is requested per tracking sample, so the
    // idle cadence must gate the request itself (not only the lens copy).
    constinit ScopeRefreshScheduler g_ceScopeRequestScheduler;
    void ReportCeCrosshairPresentation(bool reticleQuadSubmitted, bool haveAim, bool titleCapturesArt)
    {
        static uint64_t lastCeReticleLogMs = 0;
        const uint64_t nowMs = GetTickCount64();
        if (nowMs - lastCeReticleLogMs < 2000)
            return;
        lastCeReticleLogMs = nowMs;
        LOG("CE crosshair presentation: quad=%d aim=%d nativeOwned=%d "
            "measuredArt=%d heldArt=%d configured=%d lastAlpha=%u lastRGB=%u",
            reticleQuadSubmitted ? 1 : 0, haveAim ? 1 : 0, titleCapturesArt ? 1 : 0, g_authoredReticleGoodValid ? 1 : 0,
            g_reticleContainsAuthored ? 1 : 0, g_config.crosshair ? 1 : 0, g_authoredReticleLastAlphaInk,
            g_authoredReticleLastColorInk);
    }
    // CE's pair was drawn for the FOV it tracked, not the current located view.
    XrFovf CeTrackedEyeFov(const halo_ce::EyeCache::Completed& pair, uint32_t index)
    {
        const auto& f = pair.tracking.eyes[index].fov;
        return {f[0], f[1], f[2], f[3]};
    }
    // Repeated eye pairs submitted (and the largest pose change from the
    // pair's first submission: 0 = always its render pose), for the 1 Hz glass line.
    // Published once a second by NoteCeSubmittedPair (the last whole second), so a
    // session without scope uploads never piles counts into the next glass line.
    constinit std::atomic<unsigned> g_ceRepeatedPairs{0};
    // CE frames with a live glass, raised [1] / at the hip [0], and how many
    // submitted the lens quad (render thread; the 1 s glass line reads and resets them).
    constinit unsigned g_ceLensFrames[2]{}, g_ceLensSubmitted[2]{};
    constinit std::atomic<float> g_ceRepeatPoseDelta{0.0f};
    // Assert: a repeated eye pair (a borrowed frame, or no new pair in time) goes
    // to xrEndFrame with the pose it was rendered with, so the runtime reprojects head
    // rotation. Counts repeats and the largest pose change from the pair's first submit.
    void NoteCeSubmittedPair(const halo_ce::EyeCache::Completed& pair, const XrCompositionLayerProjectionView* views)
    {
        static halo_ce::EyeCache::Key last{};
        static XrPosef first[2]{};
        static unsigned repeats = 0;
        static float maxDelta = 0.0f;
        static uint64_t windowMs = 0;
        const uint64_t nowMs = GetTickCount64();
        if (!windowMs)
            windowMs = nowMs;
        if (nowMs - windowMs >= 1000)
        {
            g_ceRepeatedPairs.store(repeats, std::memory_order_relaxed);
            g_ceRepeatPoseDelta.store(maxDelta, std::memory_order_relaxed);
            repeats = 0;
            maxDelta = 0.0f;
            windowMs = nowMs;
        }
        if (pair.key.serial && pair.key == last)
        {
            float d = 0.0f;
            for (int i = 0; i < 2; ++i)
            {
                const XrPosef& a = views[i].pose;
                const XrPosef& b = first[i];
                d = std::max({d, std::fabs(a.position.x - b.position.x), std::fabs(a.position.y - b.position.y),
                    std::fabs(a.position.z - b.position.z), std::fabs(a.orientation.x - b.orientation.x),
                    std::fabs(a.orientation.y - b.orientation.y), std::fabs(a.orientation.z - b.orientation.z),
                    std::fabs(a.orientation.w - b.orientation.w)});
            }
            ++repeats;
            maxDelta = std::max(maxDelta, d);
            return;
        }
        last = pair.key;
        first[0] = views[0].pose;
        first[1] = views[1].pose;
    }

    XrPosef CeTrackedEyePose(const halo_ce::EyeCache::Completed& pair, uint32_t index)
    {
        const auto& tracking = pair.tracking;
        const auto& eye = tracking.eyes[index];
        XrPosef pose{};
        pose.position = {tracking.headPosition.x + eye.offset.x, tracking.headPosition.y + eye.offset.y,
            tracking.headPosition.z + eye.offset.z};
        pose.orientation = {eye.orientation.x, eye.orientation.y, eye.orientation.z, eye.orientation.w};
        return pose;
    }

    // Halo CE visor zoom: CE has no second zoom render, so the visor window
    // shows a 4:3 crop of one eye image centred on where the gun points.
    // Runs on the present path with the borrowed pair; the shared zoom state
    // (R3 / raise-to-eye, zoom stick) comes from VR_ScopeShouldRenderThisFrame.
    // Crop magnification 2-4x maps from the scope zoom (6-12x and up).
    // The CE hull camera for this tracking sample (cockpit screen or a
    // monitor view), when the player drives an enclosed CE vehicle.
    void CeRequestHullCamera(halo_ce::Tracking& ce)
    {
        if (!g_config.vehicle_cockpit_screen || !g_config.vehicle_first_person || !HaloCE_Armed() ||
            VR_IsCutsceneTheaterActive() || Menu_IsOpen())
            return;
        NativeVehicleCameraOwner owner{};
        haloce_vehicle_frame::Frame vf{};
        if (!Game_ReadVehicleCameraOwner(GameTitle::HaloCE, owner) || !HaloCEControls_ReadVehicleFrame(vf) ||
            vf.seat != owner.seat)
            return;
        const cockpit_screen::Class cls = vehicle_monitors::HaloCEClassOf(owner.vehicleIdentity, owner.seat);
        if (cls == cockpit_screen::Class::None)
            return;
        const bool anniversary =
            !HaloCE_ClassicScopeReady() && HaloCE_AnniversaryActive() && g_config.ce_anniv_monitor_frames;
        if (!HaloCE_ClassicScopeReady() && !anniversary)
        {
            // The third render pass exists only in the Original (Classic) graphics
            // renderer; Anniversary graphics needs monitor frames (switch off).
            static uint64_t loggedIdentity = 0;
            static int loggedSeat = -1;
            if (owner.vehicleIdentity != loggedIdentity || owner.seat != loggedSeat)
            {
                loggedIdentity = owner.vehicleIdentity;
                loggedSeat = owner.seat;
                LOG("CE monitors unavailable: Anniversary graphics (%s seat %d; the CE hull camera needs the Original "
                    "graphics renderer's third pass)",
                    vehicle_monitors::HaloCEVehicleName(owner.vehicleIdentity, owner.seat), owner.seat);
            }
            return;
        }
        // The cockpit refresh divisor: the hull pass runs on every Nth sample
        // (the screens keep their last image in between).
        static unsigned sample = 0;
        const unsigned divisor = static_cast<unsigned>(std::clamp(g_config.cockpit_screen_refresh_divisor, 1, 4));
        VR_NoteCockpitScreen(true);
        if (!anniversary && (++sample % divisor) != 0)
            return;
        haloce_vehicle_frame::Vec3 seatLocal{}, seat{};
        if (!haloce_vehicle_frame::OccupantLocal(vf, seatLocal) ||
            !haloce_vehicle_frame::FromVehicleLocal(vf, seatLocal, seat))
            return;
        const float anchor[3]{seat.x, seat.y, seat.z}, hullForward[3]{vf.forward.x, vf.forward.y, vf.forward.z};
        float position[3]{}, forward[3]{}, up[3]{}, fov = g_config.cockpit_screen_fov_deg;
        int view = -1;
        if (VR_VehicleMonitorCamera(static_cast<int>(vehicle_monitors::Game::HaloCE),
                vehicle_monitors::HaloCEVehicleName(owner.vehicleIdentity, owner.seat), static_cast<int>(cls), anchor,
                hullForward, cockpit_screen::kWorldUnitsPerMeter, position, forward, up, fov))
            view = g_monitorPendingView.exchange(-1, std::memory_order_acq_rel); // carried by the sample instead
        else
        {
            const float ahead = (g_config.cockpit_screen_ahead_m > 0 ? g_config.cockpit_screen_ahead_m
                                                                     : cockpit_screen::CameraAheadM(cls)) *
                                cockpit_screen::kWorldUnitsPerMeter;
            const float upM = cockpit_screen::CameraUpM(cls) * cockpit_screen::kWorldUnitsPerMeter;
            if (!cockpit_screen::CameraPose(anchor, hullForward, ahead, upM, position, forward, up))
                return;
        }
        const cockpit_screen::Lens lens = cockpit_screen::LensFor(fov, g_ceScopeAspect.load(std::memory_order_relaxed));
        if (!std::isfinite(lens.vertical) || lens.vertical <= 0.0f)
            return;
        // Anniversary: a candidate monitor frame; CeAnniversaryMonitorFrame decides
        // whether this sample is stolen. Classic: the third pass.
        ce.scopeWanted = !anniversary;
        ce.monitorFrame = anniversary ? 1 : 0;
        ce.scopeHeadView = false;
        ce.scopeHullCamera = true;
        ce.scopeMonitorView = static_cast<int8_t>(view);
        ce.scopeTanHalfVertical = lens.vertical;
        ce.scopeHullPosition = {position[0], position[1], position[2]};
        ce.scopeHullForward = {forward[0], forward[1], forward[2]};
        ce.scopeHullUp = {up[0], up[1], up[2]};
    }
    void CeFillGunCameraSource(halo_ce::Tracking& ce)
    {
        ce.scopeForwardM = g_config.ce_scope_forward_m;
        ce.scopeLensKnown = false;
        ce.scopeReceiptValid = false;
        // Start at the gun's kit glass (same aim-frame
        // offsets as the lens quad), then only the clearance past it.
        if (const auto* model =
                weapon_model::Find(GameTitle::HaloCE, VR_ZoomWeaponIdentity(GameTitle::HaloCE, ce.generation)))
            if (const auto* kit = weapon_profile::KitLensRowFor(GameTitle::HaloCE, model->name))
            {
                ce.scopeLensKnown = true;
                ce.scopeLensLocal = {
                    g_config.left_handed ? -kit->row.right : kit->row.right, kit->row.up, -kit->row.forward};
                ce.scopeForwardM = g_config.ce_scope_lens_clearance_m;
            }
        // Found in headset testing (CE zoom camera placed wrong): the fresh anchored lens
        // receipt (the gun's own lens node, native world units) places and
        // aims the camera, as H3 follows its barrel receipt; the
        // aim-frame kit offset above is the fallback.
        {
            const unsigned lensTitle = static_cast<unsigned>(GameTitle::HaloCE);
            const uint64_t lensAt = g_scopeLensMs[lensTitle].load(std::memory_order_acquire),
                           lensNow = GetTickCount64();
            weapon_muzzle::Ray lensRay{};
            if (lensAt && lensNow >= lensAt && lensNow - lensAt <= 150 && g_scopeLogLens[lensTitle].Read(lensRay))
            {
                ce.scopeReceiptValid = true;
                ce.scopeReceiptPosition = {lensRay.position[0], lensRay.position[1], lensRay.position[2]};
                ce.scopeReceiptForward = {lensRay.direction[0], lensRay.direction[1], lensRay.direction[2]};
                ce.scopeReceiptUp = {lensRay.up[0], lensRay.up[1], lensRay.up[2]};
                ce.scopeForwardM = g_config.ce_scope_lens_clearance_m;
            }
        }
    }

    void CeAnniversaryMonitorFrame(halo_ce::Tracking& ce)
    {
        if (!g_config.ce_anniv_monitor_frames || TitleAdapter_GetActiveTitle() != GameTitle::HaloCE ||
            HaloCE_ClassicScopeReady() || !HaloCE_AnniversaryActive() || Menu_IsOpen())
        {
            ce.monitorFrame = 0;
            (void)g_annivZoomBoost.Every(false, 0, 0, 0);
            return;
        }
        if (g_scopeActive.load(std::memory_order_acquire) &&
            g_zoomTier.load(std::memory_order_acquire) == static_cast<uint8_t>(visor_zoom::Tier::Visor))
        {
            float zoom = g_scopeRuntimeZoom.load(std::memory_order_acquire);
            zoom = visor_zoom::ScopeZoomForWindow(
                zoom, visor_zoom::kAspect, VisorWindowWidthM(), g_config.visor_zoom_distance_m);
            const ScopeProjectionTangents lens =
                ComputeScopeProjectionTangents(zoom, g_ceScopeAspect.load(std::memory_order_relaxed));
            if (std::isfinite(lens.vertical) && lens.vertical > 0.001f && lens.vertical < 1.0f)
            {
                ce.monitorFrame = 2;
                ce.scopeHullCamera = false;
                ce.scopeMonitorView = -1;
                ce.scopeTanHalfVertical = lens.vertical;
            }
        }
        // Found in headset testing that CE Anniversary had no working scope glass: a live
        // lens glass on a CE Optic gun now borrows frames for a gun
        // camera on the glass (mode 3; it wins over a hull request). Saber renders it as
        // an ordinary frame in the eye slots (its own depth, LOD and visibility; no third
        // view index, so the left-eye depth reuse and the worker count never apply);
        // FinishMonitor sends it to the scope bank and the eye pair repeats.
        bool glassRaised = false;
        if (g_scopeGlassLive.load(std::memory_order_acquire) &&
            g_zoomTier.load(std::memory_order_acquire) == static_cast<uint8_t>(visor_zoom::Tier::Lens))
        {
            const ScopeProjectionTangents lens = ComputeScopeProjectionTangents(
                g_scopeRuntimeZoom.load(std::memory_order_acquire), g_ceScopeAspect.load(std::memory_order_relaxed));
            const float glass = g_scopeGlassAspect.load(std::memory_order_acquire);
            const float source = g_ceScopeAspect.load(std::memory_order_relaxed);
            // Rendered wide enough that the reprojected picture never reaches its
            // edge at 30 deg/s of aim between images (glass_reprojection::kRoomDegrees).
            const float tanH0 = lens.vertical * scope_window::GlassTangentScale(glass, source) * source;
            const float cap = glass_reprojection::CapForRoom(glass, source, g_config.scope_parallax_max_shift, tanH0,
                std::tan(glass_reprojection::kRoomDegrees * 0.017453292f));
            const float tanHalf = lens.vertical * scope_window::GlassTangentScale(glass, source) *
                                  scope_window::GlassShiftMargin(glass, source, cap);
            if (std::isfinite(tanHalf) && tanHalf > 0.001f && tanHalf < 1.0f)
            {
                ce.scopeShiftCap = cap;
                CeFillGunCameraSource(ce);
                ce.monitorFrame = 3;
                ce.scopeHullCamera = false;
                ce.scopeMonitorView = -1;
                ce.scopeHeadView = false;
                ce.scopeTanHalfVertical = tanHalf;
                ce.scopeGlassAspect = glass;
                glassRaised = g_scopeActive.load(std::memory_order_acquire);
            }
        }
        // The zoom-on boost: visor frames.
        // The visor only; the raised glass never boosts (1 in 2 repeated half the eye pairs).
        const bool boosted = ce.monitorFrame == 2;
        if (!boosted)
            (void)g_annivZoomBoost.Every(false, 0, 0, 0); // zoom off ends the boost
        if (!ce.monitorFrame)
            return;
        const uint64_t wantedNow = GetTickCount64();
        (ce.monitorFrame == 3      ? g_annivGlassWantedMs
            : ce.monitorFrame == 2 ? g_annivVisorWantedMs
                                   : g_annivHullWantedMs)
            .store(wantedNow, std::memory_order_release);
        HaloCE_WantScopeBank();
        bool hullStill = true;
        if (ce.monitorFrame == 1)
        {
            int proof = 0, agree = 0, disagree = 0;
            float speed = 0;
            HaloCEControls_VehicleProofStatus(proof, agree, disagree, speed);
            static halo_ce::Vec3 lastHull{};
            static bool haveHull = false;
            const halo_ce::Vec3 f = ce.scopeHullForward;
            float hullTurnDeg = 1000.0f;
            if (haveHull)
            {
                const float d = std::clamp(f.x * lastHull.x + f.y * lastHull.y + f.z * lastHull.z, -1.0f, 1.0f);
                hullTurnDeg = std::acos(d) * 57.2957795f;
            }
            lastHull = f;
            haveHull = true;
            hullStill = std::isfinite(speed) && speed <= kAnnivHullStillSpeedMps && std::isfinite(hullTurnDeg) &&
                        hullTurnDeg <= g_config.ce_anniv_monitor_still_deg;
        }
        // Head still: the rotation since the previous sample (degrees).
        static halo_ce::Quat last{};
        static bool haveLast = false;
        const auto& q = ce.headOrientation;
        float dot = haveLast ? std::fabs(q.x * last.x + q.y * last.y + q.z * last.z + q.w * last.w) : 0.0f;
        dot = std::min(dot, 1.0f);
        const float turnedDeg = haveLast ? 2.0f * std::acos(dot) * 57.2957795f : 1000.0f;
        last = q;
        haveLast = true;
        static unsigned sinceStolen = 0;
        // Hull frames while driving (the owner wants to see out): one in
        // ce_anniv_hull_frames_moving_every (0 = only when stopped).
        const bool hullMoving = ce.monitorFrame == 1 && !hullStill;
        if (ce.monitorFrame == 1)
            g_annivHullMoving.store(hullMoving ? 1 : 0, std::memory_order_relaxed);
        const int movingEvery = g_config.ce_anniv_hull_frames_moving_every;
        unsigned every = static_cast<unsigned>(
            hullMoving ? std::clamp(movingEvery, 2, 30)
            : ce.monitorFrame == 3
                ? (glassRaised ? static_cast<unsigned>(std::clamp(g_config.ce_anniv_glass_raised_every, 2, 12))
                               : kAnnivGlassHipEvery)
                : std::clamp(g_config.ce_anniv_monitor_every, 2, 12));
        // The glass borrows more often while the gun turns fast, so the held
        // picture's reprojection stays inside its rendered room (glass_reprojection::BorrowEvery).
        static glass_reprojection::RateHold gunRate;
        static glass_reprojection::GunRateMeter gunMeter;
        float gunRateDegPerS = 0.0f;
        if (ce.monitorFrame == 3 && ce.scopeReceiptValid)
        {
            const halo_ce::Vec3 f = ce.scopeReceiptForward;
            LARGE_INTEGER qpc{}, frequency{};
            QueryPerformanceCounter(&qpc);
            QueryPerformanceFrequency(&frequency);
            const double nowS =
                frequency.QuadPart ? static_cast<double>(qpc.QuadPart) / static_cast<double>(frequency.QuadPart) : 0.0;
            gunRateDegPerS = gunRate.Update(gunMeter.Update({f.x, f.y, f.z}, nowS), GetTickCount64());
            const float period = g_preparedFrame.predictedDisplayDelta > 0
                                     ? static_cast<float>(g_preparedFrame.predictedDisplayDelta) * 1.0e-9f
                                     : 0.0f;
            const float displayPeriod = static_cast<float>(g_displayPeriodNs.load(std::memory_order_relaxed)) * 1.0e-9f;
            every =
                glass_reprojection::BorrowEvery(gunRateDegPerS, displayPeriod > 0.0f ? displayPeriod : period, every);
        }
        else
            gunMeter.Reset();
        {
            // Agreed deviation: a faster fill right after zoom-on (visor and raised glass frames).
            bool started = false;
            const unsigned normal = every;
            every = g_annivZoomBoost.Every(
                boosted, normal, g_config.ce_anniv_zoom_boost_frames, g_config.ce_anniv_zoom_boost_every, &started);
            if (started)
                LOG("CE anniv zoom boost: %d frames at 1 in %u, then 1 in %u", g_config.ce_anniv_zoom_boost_frames,
                    every, normal);
        }
        ++sinceStolen;
        const bool headStill = std::isfinite(turnedDeg) && turnedDeg <= g_config.ce_anniv_monitor_still_deg;
        const bool still = headStill && (!hullMoving || movingEvery > 0);
        const bool steal = sinceStolen >= every && still && HaloCE_ScopeBankReady();
        if (ce.monitorFrame == 3)
        {
            // Diagnostic, once a second while the glass is live: borrowed frames
            // (= repeated eye pairs) raised/hip, moving skips, images that reached
            // the glass, the newest image's age, and the gun camera's source.
            static uint64_t windowMs = 0;
            static unsigned frames = 0, raisedSteals = 0, hipSteals = 0, movingSkips = 0, bankWaits = 0;
            ++frames;
            if (steal)
                ++(glassRaised ? raisedSteals : hipSteals);
            else if (sinceStolen >= every && !still)
                ++movingSkips;
            else if (sinceStolen >= every)
                ++bankWaits;
            if (!windowMs)
                windowMs = wantedNow;
            if (wantedNow - windowMs >= 1000)
            {
                const uint64_t imageMs = g_annivGlassImageMs.load(std::memory_order_acquire);
                LOG("CE anniv glass: %s, borrowed %u of %u frames (raised %u, hip %u; 1 in %u now, gun %.0f deg/s), skipped-moving %u, "
                    "bank-not-ready %u, images on the glass %u, newest image %lld ms old, camera from %s, tan %.4f (1 s)",
                    glassRaised ? "raised" : "hip", raisedSteals + hipSteals, frames, raisedSteals, hipSteals, every,
                    gunRateDegPerS, movingSkips, bankWaits, g_annivGlassImages.exchange(0),
                    imageMs ? static_cast<long long>(wantedNow - imageMs) : -1LL,
                    ce.scopeReceiptValid ? "lens receipt"
                    : ce.scopeLensKnown  ? "kit glass (aim)"
                                         : "aim only",
                    ce.scopeTanHalfVertical);
                windowMs = wantedNow;
                frames = raisedSteals = hipSteals = movingSkips = bankWaits = 0;
            }
        }
        if (!steal)
        {
            if (sinceStolen >= every && !still)
                g_annivMovingSkips.fetch_add(1, std::memory_order_relaxed);
            ce.monitorFrame = 0;
            ce.scopeHullCamera = false;
            return;
        }
        sinceStolen = 0;
        g_annivStolen.fetch_add(1, std::memory_order_relaxed);
        if (ce.monitorFrame == 2)
            g_annivStolenVisor.fetch_add(1, std::memory_order_relaxed);
        if (ce.monitorFrame == 3)
            g_annivStolenGlass.fetch_add(1, std::memory_order_relaxed);
        if (hullMoving)
            g_annivStolenMoving.fetch_add(1, std::memory_order_relaxed);
    }
    void CeVisorCrop(const halo_ce::EyeCache::Completed& pair, const float halfX[2], const float halfY[2])
    {
        if (TitleAdapter_GetActiveTitle() != GameTitle::HaloCE)
            return;
        if (pair.descriptor.Width && pair.descriptor.Height)
            g_ceScopeAspect.store(
                static_cast<float>(pair.descriptor.Width) / static_cast<float>(pair.descriptor.Height),
                std::memory_order_relaxed);
        // This is CE's only call of the scope decision (engage on the raise, release,
        // live glass), so it runs every pair. The held monitor-frame returns below used to
        // skip it, which froze the scope state while a borrowed image was held.
        const bool hadImage = g_scopeHasImage.load(std::memory_order_acquire);
        const bool refresh = VR_ScopeShouldRenderThisFrame();
        if (pair.scope && pair.scopeTracking.monitorFrame)
        {
            // The held monitor-frame image (Anniversary). Copy each new one once.
            static uint64_t lastSerial = 0;
            const uint64_t heldNow = GetTickCount64();
            const auto live = [&](const std::atomic<uint64_t>& at)
            {
                const uint64_t t = at.load(std::memory_order_acquire);
                return t && heldNow >= t && heldNow - t <= 250;
            };
            const bool hull = pair.scopeTracking.monitorFrame == 1 && live(g_annivHullWantedMs);
            const bool visorShown =
                pair.scopeTracking.monitorFrame == 2 && live(g_annivVisorWantedMs) &&
                g_scopeActive.load(std::memory_order_acquire) &&
                g_zoomTier.load(std::memory_order_acquire) == static_cast<uint8_t>(visor_zoom::Tier::Visor);
            // The gun camera's image goes to the lens glass while the glass is live.
            const bool glassShown =
                pair.scopeTracking.monitorFrame == 3 && live(g_annivGlassWantedMs) &&
                g_scopeGlassLive.load(std::memory_order_acquire) &&
                g_zoomTier.load(std::memory_order_acquire) == static_cast<uint8_t>(visor_zoom::Tier::Lens);
            if (hull || visorShown || glassShown)
            {
                bool copied = false;
                if (pair.scopeTracking.serial != lastSerial && g_context)
                {
                    D3D11_TEXTURE2D_DESC cacheDesc = pair.descriptor;
                    if (!EnsureScopeCache(cacheDesc))
                        return;
                    lastSerial = pair.scopeTracking.serial;
                    g_context->CopyResource(g_scopeCache, pair.scope);
                    g_scopeImageTitle = GameTitle::HaloCE;
                    g_scopeImageGeneration = TitleAdapter_GetGeneration(GameTitle::HaloCE);
                    g_monitorPendingView.store(
                        hull ? pair.scopeTracking.scopeMonitorView : -1, std::memory_order_release);
                    g_scopeImageGlassAspect.store(
                        glassShown ? pair.scopeTracking.scopeGlassAspect : 0.0f, std::memory_order_release);
                    g_scopeHasImage.store(true, std::memory_order_release);
                    NoteScopeImageCaptured();
                    copied = true;
                    if (glassShown)
                    {
                        // The image's camera, for the reprojection between borrowed frames.
                        const auto& t = pair.scopeTracking;
                        // And the margin cap it was rendered with (the crop must match).
                        g_glassImageCap = t.scopeShiftCap > 0.0f ? t.scopeShiftCap : g_config.scope_parallax_max_shift;
                        g_glassImageCapSerial = g_scopeImageSerial.load(std::memory_order_acquire);
                        g_glassCameraSerial = 0;
                        if (t.scopeReceiptValid && pair.descriptor.Height)
                        {
                            g_glassCamera.forward = {
                                t.scopeReceiptForward.x, t.scopeReceiptForward.y, t.scopeReceiptForward.z};
                            g_glassCamera.up = {t.scopeReceiptUp.x, t.scopeReceiptUp.y, t.scopeReceiptUp.z};
                            g_glassCamera.tanV = t.scopeTanHalfVertical;
                            g_glassCamera.tanH = t.scopeTanHalfVertical * static_cast<float>(pair.descriptor.Width) /
                                                 static_cast<float>(pair.descriptor.Height);
                            g_glassCameraSerial = g_scopeImageSerial.load(std::memory_order_acquire);
                        }
                        g_annivGlassImages.fetch_add(1, std::memory_order_relaxed);
                        g_annivGlassImageMs.store(heldNow, std::memory_order_release);
                    }
                }
                // Between borrows the held image is shown again (the refresh
                // above cleared the flag); before, the lens quad went out 1 raised frame in 6.
                if (glass_reprojection::HeldImagePresent(copied, hadImage,
                        g_scopeImageTitle == GameTitle::HaloCE &&
                            g_scopeImageGeneration == TitleAdapter_GetGeneration(GameTitle::HaloCE)))
                    g_scopeHasImage.store(true, std::memory_order_release);
                if (visorShown || glassShown || !pair.tracking.scopeHullCamera)
                    return; // never fall through to the crop
            }
        }
        if (pair.tracking.scopeHullCamera)
        {
            // The hull pass for the cockpit screen / a monitor view. The
            // view travels with the sample that rendered it.
            // A held Anniversary monitor image was copied above,
            // once per borrow and with its own view; re-copying it here every frame under
            // the current sample's view sent view A's picture to monitor B.
            if (pair.scopeTracking.monitorFrame)
                return;
            if (!pair.scope || !g_context)
                return;
            D3D11_TEXTURE2D_DESC cacheDesc = pair.descriptor;
            if (!EnsureScopeCache(cacheDesc))
                return;
            g_context->CopyResource(g_scopeCache, pair.scope);
            g_scopeImageTitle = GameTitle::HaloCE;
            g_scopeImageGeneration = TitleAdapter_GetGeneration(GameTitle::HaloCE);
            g_monitorPendingView.store(pair.tracking.scopeMonitorView, std::memory_order_release);
            g_scopeHasImage.store(true, std::memory_order_release);
            NoteScopeImageCaptured();
            return;
        }
        if (!g_scopeGlassLive.load(std::memory_order_acquire))
            return;
        if (g_zoomTier.load(std::memory_order_acquire) == static_cast<uint8_t>(visor_zoom::Tier::Lens) ||
            CeVisorFullResActive())
        {
            // Classic look-through scope: the third render pass captured with
            // this exact pair. The request site sets the cadence, so any pair
            // that carries a scope image is shown. No scope image this frame ->
            // the lens keeps its previous CE frame (never blanked by the shared
            // scheduler's refresh).
            // A held Anniversary monitor image (another mode's
            // picture, e.g. the pistol's visor frame after a swap) is never the Classic
            // look-through scope: the lens keeps its previous CE frame instead.
            if (!pair.scope || !g_context || pair.scopeTracking.monitorFrame)
            {
                if (hadImage && g_scopeImageTitle == GameTitle::HaloCE &&
                    g_scopeImageGeneration == TitleAdapter_GetGeneration(GameTitle::HaloCE))
                    g_scopeHasImage.store(true, std::memory_order_release);
                return;
            }
            D3D11_TEXTURE2D_DESC cacheDesc = pair.descriptor;
            if (!EnsureScopeCache(cacheDesc))
                return;
            g_context->CopyResource(g_scopeCache, pair.scope);
            g_scopeImageTitle = GameTitle::HaloCE;
            g_scopeImageGeneration = TitleAdapter_GetGeneration(GameTitle::HaloCE);
            g_scopeImageGlassAspect.store(pair.scopeTracking.scopeGlassAspect, std::memory_order_release);
            g_scopeHasImage.store(true, std::memory_order_release);
            NoteScopeImageCaptured();
            return;
        }
        if (!refresh)
            return;
        const uint32_t eye = g_config.left_handed ? 0u : 1u; // the aiming eye
        ID3D11Texture2D* source = pair.eyes[eye];
        const D3D11_TEXTURE2D_DESC& desc = pair.descriptor;
        if (!source || !g_context || desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || desc.Width < 256 ||
            desc.Height < 192)
            return;
        // A fixed zoom of where the head looks: the eye image's own centre
        // (the gun reticle is drawn by the visor shader at its aim point).
        const float coverX = tanf(halfX[eye]), coverY = tanf(halfY[eye]);
        if (!(coverX > 0.0f) || !(coverY > 0.0f))
            return;
        const float w = static_cast<float>(desc.Width), h = static_cast<float>(desc.Height);
        const float u = w * 0.5f, v = h * 0.5f;
        // Show the window's own half-angle / zoom: the crop keeps
        // (WindowHalfTangent/zoom)/coverX of the eye's half-width.
        const visor_zoom::CeCrop ceCrop = visor_zoom::CeVisorCrop(
            visor_zoom::CappedZoom(g_scopeRuntimeZoom.load(std::memory_order_acquire), g_config.ce_visor_zoom_max),
            coverX, VisorWindowWidthM(), g_config.visor_zoom_distance_m, desc.Width, desc.Height);
        const uint32_t cropW = ceCrop.width, cropH = ceCrop.height;
        if (!cropW || !cropH)
            return;
        g_ceVisorAchievedZoom.store(ceCrop.achievedZoom, std::memory_order_release);
        const uint32_t x0 = static_cast<uint32_t>(std::clamp(u - cropW * 0.5f, 0.0f, w - static_cast<float>(cropW)));
        const uint32_t y0 = static_cast<uint32_t>(std::clamp(v - cropH * 0.5f, 0.0f, h - static_cast<float>(cropH)));
        D3D11_TEXTURE2D_DESC cacheDesc = desc;
        cacheDesc.Width = cropW;
        cacheDesc.Height = cropH;
        if (!EnsureScopeCache(cacheDesc))
            return;
        const D3D11_BOX box{x0, y0, 0, x0 + cropW, y0 + cropH, 1};
        g_context->CopySubresourceRegion(g_scopeCache, 0, 0, 0, 0, source, 0, &box);
        g_scopeImageTitle = GameTitle::HaloCE;
        g_scopeImageGeneration = TitleAdapter_GetGeneration(GameTitle::HaloCE);
        g_scopeHasImage.store(true, std::memory_order_release);
        NoteScopeImageCaptured();
    }
} // namespace haloce_frame_bridge
namespace
{
    // Diagnostic: the CE scope camera actually built (render thread), for the CE shot line.
    constinit std::atomic<float> g_ceScopeCam[6]{};
    constinit std::atomic<float> g_ceScopeCamScale{0};
    constinit std::atomic<bool> g_ceScopeCamReceipt{false};
    constinit std::atomic<uint64_t> g_ceScopeCamMs{0};
} // namespace
void VR_PublishCeScopeCamera(
    const float position[3], const float forward[3], bool fromReceipt, float unitsPerMeter) noexcept
{
    if (!position || !forward)
        return;
    for (int i = 0; i < 3; ++i)
    {
        g_ceScopeCam[i].store(position[i], std::memory_order_relaxed);
        g_ceScopeCam[3 + i].store(forward[i], std::memory_order_relaxed);
    }
    g_ceScopeCamScale.store(unitsPerMeter, std::memory_order_relaxed);
    g_ceScopeCamReceipt.store(fromReceipt, std::memory_order_relaxed);
    g_ceScopeCamMs.store(GetTickCount64(), std::memory_order_release);
}
bool VR_CeAnnivGlassGun() noexcept
{
    return g_ceAnnivGlass.load(std::memory_order_acquire);
}
bool VR_CeScopeCamera(
    float position[3], float forward[3], bool& fromReceipt, float& unitsPerMeter, uint64_t& builtMs) noexcept
{
    builtMs = g_ceScopeCamMs.load(std::memory_order_acquire);
    if (!builtMs || !position || !forward)
        return false;
    for (int i = 0; i < 3; ++i)
    {
        position[i] = g_ceScopeCam[i].load(std::memory_order_relaxed);
        forward[i] = g_ceScopeCam[3 + i].load(std::memory_order_relaxed);
    }
    unitsPerMeter = g_ceScopeCamScale.load(std::memory_order_relaxed);
    fromReceipt = g_ceScopeCamReceipt.load(std::memory_order_relaxed);
    return true;
}
bool VR_CeAuthoredReticleFrameMatches(ID3D11DeviceContext* context, uint64_t serial)
{
    return TitleAdapter_GetActiveTitle() == GameTitle::HaloCE && context && context == g_context &&
           halo_ce::ReticleReceiptSerialCurrent(serial, g_preparedFrame.serial) && g_preparedFrame.begun &&
           g_preparedFrame.state.shouldRender && g_stereoEnabled.load(std::memory_order_acquire) &&
           !g_pausePresentation.load(std::memory_order_acquire);
}
