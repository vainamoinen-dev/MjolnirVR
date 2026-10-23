// ODST scope pass: renders a magnified world view along the gun's aim, or the cockpit hull camera in enclosed seats.
// A failed optional view never retries the native draw or changes camera-core ownership.
#pragma once
// Included once, by src/dll/titles/odst/odst_camera_core.cpp; also compiled by tests/odst_scope_tests.cpp,
//   which supplies its own versions. Not a standalone header: it uses what the host defines
//   above its include line, namely ControllerWorldPoseEx, FpInterpolationContext,
//   OdstReadSeatSnapshot, OdstScopeLogCamera, OdstScopeOrigin, OdstSeatSnapshot,
//   ReadOdstCinematicControl, g_aimFwdX, g_aimFwdY, g_aimFwdZ, g_fpInterpolationContexts,
//   g_odstCamera, g_odstRuntimeGeneration, g_scopeRenderActive, kOdstCameraProfile.
// H3 behavior: an independently magnified world view follows the gun's aim,
// while the two headset views remain wide. ODST uses its own verified compact,
// derived and nested-camera layout and native rebuild/upload functions.
std::atomic<uint32_t> g_odstScopeFaultGeneration{0};
std::atomic<uint64_t> g_odstScopeFaults{0};

void RenderOdstScopeBody(void* view)
{
    const auto generation = g_odstRuntimeGeneration.load(std::memory_order_acquire);
    if (!view || !generation || g_odstScopeFaultGeneration.load() == generation || !g_odstCamera.armed.load() ||
        g_odstCamera.teardownRequested.load() || !g_odstCamera.buildViewport || !g_odstCamera.buildMatrices ||
        !g_odstCamera.prepareView || !g_odstCamera.originalRenderView)
        return;
    int32_t scene = -1, shot = -1;
    const bool playerControlled = ReadOdstCinematicControl(scene, shot) == CinematicControlState::PlayerControlled;
    // Enclosed seat (Scorpion, Wraith, Banshee): the second render is the cockpit screen's hull camera.
    float aspect = 4.f / 3.f;
    ScopeCameraPose pose{};
    ScopeProjectionTangents tangents{};
    bool cockpitCamera = false;
    {
        OdstSeatSnapshot seatSnapshot{};
        const auto cockpitClass = playerControlled && g_config.vehicle_cockpit_screen &&
                                          OdstReadSeatSnapshot(seatSnapshot) && seatSnapshot.anchorValid &&
                                          VR_GetScopeRenderAspect(aspect)
                                      ? cockpit_screen::LegacyClassOf(static_cast<int>(seatSnapshot.identity),
                                            seatSnapshot.seatIndex, seatSnapshot.mounted)
                                      : cockpit_screen::Class::None;
        float monitorFov = 0;
        if (cockpitClass != cockpit_screen::Class::None &&
            VR_VehicleMonitorCamera(static_cast<int>(vehicle_monitors::Game::Halo3Odst),
                vehicle_monitors::LegacyVehicleName(
                    static_cast<int>(seatSnapshot.identity), seatSnapshot.seatIndex, seatSnapshot.mounted),
                static_cast<int>(cockpitClass), seatSnapshot.anchor, seatSnapshot.rawFwd,
                cockpit_screen::kWorldUnitsPerMeter, pose.position, pose.forward, pose.up, monitorFov))
        {
            const auto lens = cockpit_screen::LensFor(monitorFov, aspect);
            tangents = {lens.horizontal, lens.vertical};
            cockpitCamera = true;
        }
        else if (cockpitClass != cockpit_screen::Class::None)
        {
            const float ahead = (g_config.cockpit_screen_ahead_m > 0 ? g_config.cockpit_screen_ahead_m
                                                                     : cockpit_screen::CameraAheadM(cockpitClass)) *
                                cockpit_screen::kWorldUnitsPerMeter;
            const float up = cockpit_screen::CameraUpM(cockpitClass) * cockpit_screen::kWorldUnitsPerMeter;
            const auto lens = cockpit_screen::LensFor(g_config.cockpit_screen_fov_deg, aspect);
            cockpitCamera = cockpit_screen::CameraPose(
                seatSnapshot.anchor, seatSnapshot.rawFwd, ahead, up, pose.position, pose.forward, pose.up);
            if (cockpitCamera)
                tangents = {lens.horizontal, lens.vertical};
        }
    }
    VR_NoteCockpitScreen(cockpitCamera);
    if (!VR_ScopeShouldRenderThisFrame() || !playerControlled)
        return;
    const auto& layout = kOdstCameraProfile.layout;
    auto* bytes = static_cast<unsigned char*>(view);
    auto* camera = bytes + layout.rootCurrentCompact;
    if (!cockpitCamera)
    {
        float basis[9]{}, seat[3]{}, scale = 1;
        const float direction[3]{g_aimFwdX.load(), g_aimFwdY.load(), g_aimFwdZ.load()};
        float muzzleOrigin[3]{};
        if (!ControllerWorldPoseEx(false, basis, seat, scale) ||
            !ComputeScopeCameraPose(basis,
                OdstScopeOrigin(
                    reinterpret_cast<const float*>(camera + layout.compactPosition), direction, muzzleOrigin),
                direction, pose) ||
            !VR_GetScopeRenderAspect(aspect))
            return;
        OdstScopeLogCamera(pose.position, reinterpret_cast<const float*>(camera + layout.compactPosition));
        tangents = ComputeScopeProjectionTangents(VR_GetScopeZoom(), aspect);
    }
    OdstHalo3FovMatch fov{};
    if (!ComputeOdstHalo3FovMatch(atanf(tangents.horizontal), atanf(tangents.vertical), fov))
        return;
    constexpr size_t capacity = 0xC0;
    const uintptr_t offsets[]{layout.rootCurrentCompact, layout.rootCurrentDerived, layout.rootSecondaryCompact,
        layout.rootSecondaryDerived, layout.nestedCurrentCompact, layout.nestedCurrentDerived,
        layout.nestedSecondaryCompact, layout.nestedSecondaryDerived};
    const size_t sizes[]{layout.compactSize, layout.derivedSize, layout.compactSize, layout.derivedSize,
        layout.compactSize, layout.derivedSize, layout.compactSize, layout.derivedSize};
    alignas(16) unsigned char saved[8][capacity]{};
    for (int i = 0; i < 8; ++i)
    {
        if (sizes[i] > capacity)
            return;
        std::memcpy(saved[i], bytes + offsets[i], sizes[i]);
    }
    if (!VR_BeginScopeRaster())
        return;
    const FpInterpolationContext contexts[]{g_fpInterpolationContexts[0], g_fpInterpolationContexts[1]};
    const bool previousScope = g_scopeRenderActive.exchange(true, std::memory_order_acq_rel);
    __try
    {
        std::memcpy(camera + layout.compactPosition, pose.position, 12);
        if (cockpitCamera ||
            !VR_VisorZoomHeadView()) // the visor keeps the head camera's own view; the cockpit never does
        {
            std::memcpy(camera + layout.compactForward, pose.forward, 12);
            std::memcpy(camera + layout.compactUp, pose.up, 12);
        }
        *reinterpret_cast<float*>(camera + layout.verticalFov) = fov.compactVerticalInput;
        *reinterpret_cast<float*>(camera + layout.referenceFov) = fov.compactReferenceInput;
        alignas(16) unsigned char temporary[0x40]{};
        g_odstCamera.buildViewport(camera, temporary);
        g_odstCamera.buildMatrices(camera, temporary, bytes + layout.rootCurrentDerived, 0.f);
        auto* projection = reinterpret_cast<float*>(bytes + layout.rootCurrentDerived + layout.projectionMatrix);
        projection[0] = fov.projectionX;
        projection[5] = fov.projectionY;
        for (int i = 2; i < 8; i += 2)
        {
            std::memcpy(bytes + offsets[i], camera, layout.compactSize);
            std::memcpy(bytes + offsets[i + 1], bytes + layout.rootCurrentDerived, layout.derivedSize);
        }
        if (g_odstCamera.fpCameraUpload)
            g_odstCamera.fpCameraUpload(bytes + layout.rootSecondaryCompact, bytes + layout.rootSecondaryDerived);
        g_odstCamera.prepareView(view, 0);
        g_odstCamera.originalRenderView(view);
        VR_CaptureScope();
    }
    __finally
    {
        for (int i = 0; i < 8; ++i)
            std::memcpy(bytes + offsets[i], saved[i], sizes[i]);
        g_fpInterpolationContexts[0] = contexts[0];
        g_fpInterpolationContexts[1] = contexts[1];
        g_scopeRenderActive.store(previousScope, std::memory_order_release);
        VR_EndScopeRaster();
    }
}
void RenderOdstScope(void* view)
{
    __try
    {
        RenderOdstScopeBody(view);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("the ODST scope render faulted");
        g_odstScopeFaultGeneration.store(g_odstRuntimeGeneration.load(), std::memory_order_release);
        g_odstScopeFaults.fetch_add(1, std::memory_order_relaxed);
        // An optional third-view failure never retries the native draw or
        // changes camera-core ownership. Its next request stays native.
    }
}
void ReportOdstScope()
{
    const auto faults = g_odstScopeFaults.exchange(0);
    if (faults)
        LOG("ODST scope StockFallback: optional third view faulted %llu times; camera core retained", faults);
}
