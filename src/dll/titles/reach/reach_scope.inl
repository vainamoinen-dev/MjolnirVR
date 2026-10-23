// Halo Reach scope pass: a mono scope view (or the cockpit hull camera) rendered inside the same native frame as the two eyes.
// A refused frame keeps the last good image; g_reachScopeStage records why the pass stopped.
#pragma once
// Included once, by src/dll/core/game.cpp; also compiled by tests/reach_scope_tests.cpp,
//   which supplies its own versions. Not a standalone header: it uses what the host defines
//   above its include line, namely ControllerWorldBasis,
//   ReachCallPlayerViewWithEyeScopedSuppressions, ReachOwnerScope, ReachScopeCullVertical,
//   ReachScopeFrame, ReachScopeLogCamera, ReachScopeLogFov, ReachScopeOrigin, ReachWindCopy,
//   g_aimFwdX, g_aimFwdY, g_aimFwdZ, g_reachCamera, g_reachCinematicLocked,
//   g_reachFirstPersonRenderGateReturn, g_reachFpCameraEyeScope, and 10 more.
// H3 experience, Reach ownership: one outer visibility/frame-once call, followed
// by a mono scope and the unchanged two eye passes within that same native scope.
// The scope and head share an origin so their angular union is conservative.
bool ReachPrepareScopeBody(ReachOwnerScope& frame, unsigned char* headDerived, ReachVrRenderAccess& access)
{
    // Enclosed driver/pilot seat with a proven hull basis: the scope pass renders
    // the cockpit screen's forward hull camera instead of the hand scope.
    const auto cockpitClass = cockpit_screen::ReachClassOf(frame.vehicle.identity, frame.vehicle.seatIndex);
    const bool cockpitWanted = cockpit_screen::Wanted(g_config.vehicle_cockpit_screen, cockpitClass,
        frame.vehicle.active, frame.vehicle.carrierBasisValid, frame.cutsceneTheater || g_reachCinematicLocked.load());
    VR_NoteCockpitScreen(cockpitWanted);
    if (!VR_ScopeShouldRenderThisFrame())
    {
        g_reachScopeStage.store(1);
        return false;
    }
    // No pre-pass invalidate; a refusal keeps the last good image.
    const auto generation = g_reachCamera.generation.load();
    // Diagnostic stage (lens-submit): the last reason the Reach scope pass stopped.
    if (frame.cutsceneTheater || g_reachCinematicLocked.load())
    {
        g_reachScopeStage.store(2);
        return false;
    }
    if (!generation)
    {
        g_reachScopeStage.store(3);
        return false;
    }
    if (g_reachScopeFaultGeneration.load() == generation)
    {
        g_reachScopeStage.store(4);
        return false;
    }
    if (!g_reachFirstPersonRenderGateReturn.load())
    {
        g_reachScopeStage.store(5);
        return false;
    }
    if (!g_reachCamera.hudDrawWidgetTarget || !g_reachCamera.decoratorWindState)
    {
        g_reachScopeStage.store(6);
        return false;
    }
    if (g_reachWindReplayFaults.load())
    {
        g_reachScopeStage.store(7);
        return false;
    }
    if (!VR_ReachScopeReady(access))
    {
        g_reachScopeStage.store(8);
        return false;
    }
    float basis[9]{}, aspect = 1;
    const float direction[]{g_aimFwdX.load(), g_aimFwdY.load(), g_aimFwdZ.load()};
    ScopeCameraPose pose{};
    ScopeProjectionTangents lens{};
    float monitorFov = 0;
    if (cockpitWanted && VR_GetScopeRenderAspect(aspect) &&
        VR_VehicleMonitorCamera(static_cast<int>(vehicle_monitors::Game::Reach),
            vehicle_monitors::ReachVehicleName(frame.vehicle.identity, frame.vehicle.seatIndex),
            static_cast<int>(cockpitClass), frame.vehicle.cameraBase, frame.vehicle.rawCarrierForward,
            kReachWorldUnitsPerMeter, pose.position, pose.forward, pose.up, monitorFov))
    {
        const auto monitorLens = cockpit_screen::LensFor(monitorFov, aspect);
        lens = {monitorLens.horizontal, monitorLens.vertical};
    }
    else if (cockpitWanted)
    {
        const float ahead = (g_config.cockpit_screen_ahead_m > 0 ? g_config.cockpit_screen_ahead_m
                                                                 : cockpit_screen::CameraAheadM(cockpitClass)) *
                            kReachWorldUnitsPerMeter;
        const float up = cockpit_screen::CameraUpM(cockpitClass) * kReachWorldUnitsPerMeter;
        if (!VR_GetScopeRenderAspect(aspect) ||
            !cockpit_screen::CameraPose(frame.vehicle.cameraBase, frame.vehicle.rawCarrierForward, ahead, up,
                pose.position, pose.forward, pose.up))
        {
            g_reachScopeStage.store(19);
            return false;
        }
        const auto cockpitLens = cockpit_screen::LensFor(g_config.cockpit_screen_fov_deg, aspect);
        lens = {cockpitLens.horizontal, cockpitLens.vertical};
    }
    else
    {
        // Not ControllerWorldPoseEx: it needs the H3/ODST camera globals, which
        // Reach never sets, and the scope only uses the basis.
        if (!ControllerWorldBasis(basis))
        {
            g_reachScopeStage.store(20);
            return false;
        }
        if (!VR_GetScopeRenderAspect(aspect))
        {
            g_reachScopeStage.store(31);
            return false;
        }
        float muzzleOrigin[3]{};
        if (!ComputeScopeCameraPose(basis,
                ReachScopeOrigin(reinterpret_cast<const float*>(frame.headCenter), direction, muzzleOrigin), direction,
                pose))
        {
            g_reachScopeStage.store(32);
            return false;
        }
        ReachScopeLogCamera(pose.position, reinterpret_cast<const float*>(frame.headCenter));
        lens = ComputeScopeProjectionTangents(VR_GetScopeZoom(), aspect);
    }
    ReachScopeFrame scope{};
    memcpy(scope.compact, frame.headCenter, sizeof(scope.compact));
    memcpy(scope.compact, pose.position, 12);
    if (cockpitWanted ||
        !VR_VisorZoomHeadView()) // the visor keeps the head camera's own view; the cockpit camera never does
    {
        memcpy(scope.compact + 0xC, pose.forward, 12);
        memcpy(scope.compact + 0x18, pose.up, 12);
    }
    *reinterpret_cast<float*>(scope.compact + 0x28) = 2 * atanf(lens.vertical);
    float bounds[4]{};
    if (!g_reachHelpers.frustum(scope.compact, bounds))
    {
        g_reachScopeStage.store(21);
        return false;
    }
    for (float v : bounds)
        if (!std::isfinite(v))
        {
            g_reachScopeStage.store(22);
            return false;
        }
    g_reachHelpers.projection(scope.compact, bounds, scope.derived, 0.f);
    const auto* scopeProjection = reinterpret_cast<const float*>(scope.derived + kReachDerivedProjectionOffset);
    // The native fixed-aspect projection must agree with the actual capture.
    if (!std::isfinite(scopeProjection[0]) || !std::isfinite(scopeProjection[5]) ||
        fabsf(fabsf(scopeProjection[0]) * lens.horizontal - 1) > .005f ||
        fabsf(fabsf(scopeProjection[5]) * lens.vertical - 1) > .005f)
    {
        g_reachScopeStage.store(23);
        return false;
    }
    const auto* oldProjection = reinterpret_cast<const float*>(headDerived + kReachDerivedProjectionOffset);
    ScopeProjectionTangents cover{1 / fabsf(oldProjection[0]), 1 / fabsf(oldProjection[5])};
    const float headAspect = cover.horizontal / cover.vertical;
    if (!ExpandScopeCullTangents(reinterpret_cast<const float*>(frame.headCenter + 0xC),
            reinterpret_cast<const float*>(frame.headCenter + 0x18), pose, lens, cover))
    {
        g_reachScopeStage.store(24);
        return false;
    }
    // Mode 2 caps the widening (the scope view's edges may then cull).
    const float vertical = ReachScopeCullVertical(*reinterpret_cast<const float*>(frame.headCenter + 0x28),
        2 * atanf(fmaxf(cover.vertical, cover.horizontal / headAspect)));
    ReachScopeLogFov(*reinterpret_cast<const float*>(frame.headCenter + 0x28), vertical);
    if (!std::isfinite(vertical) || vertical >= 3.0f)
    {
        g_reachScopeStage.store(25);
        return false;
    }
    alignas(16) unsigned char expanded[kReachCompactCameraBytes], derived[kReachDerivedBlockSize];
    memcpy(expanded, frame.headCenter, sizeof(expanded));
    *reinterpret_cast<float*>(expanded + 0x28) = vertical;
    if (!g_reachHelpers.frustum(expanded, bounds))
    {
        g_reachScopeStage.store(26);
        return false;
    }
    for (float v : bounds)
        if (!std::isfinite(v))
        {
            g_reachScopeStage.store(27);
            return false;
        }
    g_reachHelpers.projection(expanded, bounds, derived, 0.f);
    const auto* p = reinterpret_cast<const float*>(derived + kReachDerivedProjectionOffset);
    if (!std::isfinite(p[0]) || !std::isfinite(p[5]) || fabsf(p[0]) <= 0 || fabsf(p[5]) <= 0 ||
        1 / fabsf(p[0]) + 1.e-4f < cover.horizontal || 1 / fabsf(p[5]) + 1.e-4f < cover.vertical)
    {
        g_reachScopeStage.store(28);
        return false;
    }
    // Commit local data only after the complete optional projection is valid.
    memcpy(frame.headCenter, expanded, sizeof(expanded));
    memcpy(headDerived, derived, sizeof(derived));
    scope.ready = true;
    frame.scope = scope;
    g_reachScopeStage.store(30);
    return true;
}
void ReachPrepareScope(ReachOwnerScope& frame, unsigned char* derived, ReachVrRenderAccess& access)
{
    __try
    {
        (void)ReachPrepareScopeBody(frame, derived, access);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("preparing the Reach scope faulted");
        g_reachScopeFaultGeneration.store(g_reachCamera.generation.load());
        g_reachScopeFaults.fetch_add(1);
    }
}

void ReachRenderScopeBody(uintptr_t playerView, ReachVrRenderAccess& access)
{
    const auto& frame = g_reachOwnerScope;
    // Not prepared: keep the prepare stage (1-28) as the reason.
    if (!frame.scope.ready)
        return;
    if (!VR_ReachScopeReady(access))
    {
        g_reachScopeStage.store(VR_ReachScopeNotReadyStage(access));
        return;
    }
    const uintptr_t workspace = frame.workspace;
    const uintptr_t ownerAddress = g_reachCamera.base + kReachRenderCameraOwnerRva;
    const uintptr_t owner = *reinterpret_cast<const uintptr_t*>(ownerAddress);
    if (owner != playerView + kReachPlayerViewCameraStateOffset)
    {
        g_reachScopeStage.store(52);
        return;
    }
    alignas(16) unsigned char savedWorkspace[kReachRenderScopeSnapshotSize], savedPv[kReachPvSnapshotBytes];
    unsigned char wind[ReachWindReplay::kBytes]{};
    void* const windState = g_reachCamera.decoratorWindState;
    if (!windState || !ReachWindCopy(wind, windState, sizeof(wind)))
    {
        g_reachScopeStage.store(53);
        return;
    }
    memcpy(savedWorkspace, reinterpret_cast<void*>(workspace), sizeof(savedWorkspace));
    memcpy(savedPv, reinterpret_cast<void*>(playerView + kReachPvSnapshotBegin), sizeof(savedPv));
    const auto previousFp = g_reachFpCameraEyeScope;
    const bool previousScope = g_scopeRenderActive.exchange(true);
    g_reachScopeRendering = true;
    __try
    {
        memcpy(reinterpret_cast<void*>(workspace), frame.scope.compact, kReachCompactCameraBytes);
        memcpy(reinterpret_cast<void*>(workspace + kReachPrimaryDerivedOffset), frame.scope.derived,
            kReachDerivedBlockSize);
        memcpy(reinterpret_cast<void*>(workspace + kReachSecondaryCompactOffset), frame.scope.compact,
            kReachCompactCameraBytes);
        memcpy(reinterpret_cast<void*>(workspace + kReachSecondaryDerivedOffset), frame.scope.derived,
            kReachDerivedBlockSize);
        auto* compact = reinterpret_cast<unsigned char*>(workspace);
        auto* derived = reinterpret_cast<unsigned char*>(workspace + kReachPrimaryDerivedOffset);
        g_reachHelpers.cameraState(reinterpret_cast<void*>(owner), compact);
        g_reachHelpers.matrix(reinterpret_cast<void*>(playerView + kReachPlayerViewCurrentMatricesOffset), derived,
            derived + kReachDerivedProjectionOffset, compact + kReachCompactRenderBoundsOffset,
            reinterpret_cast<void*>(playerView + kReachPlayerViewProjectionOffsetPairOffset));
        g_reachHelpers.commitOuterCamera();
        *reinterpret_cast<uintptr_t*>(ownerAddress) = owner;
        *reinterpret_cast<uint8_t*>(playerView + kReachLastWindowFlagOffset) = 0;
        g_reachFpCameraEyeScope.active = false;
        bool transient = false;
        if (ReachCallPlayerViewWithEyeScopedSuppressions(playerView, &transient))
            g_reachScopeStage.store(VR_ReachCopyScope(access) ? 99 : 98);
        // A pending lightmap restore skips this frame (stage 96); the glass keeps its
        // last image and the next frame retries, instead of retiring the Reach scope
        // until the next map.
        else if (transient)
            g_reachScopeStage.store(96);
        else
        {
            g_reachScopeStage.store(97);
            g_reachScopeFaultGeneration.store(g_reachCamera.generation.load());
            g_reachScopeFaults.fetch_add(1);
        }
    }
    __finally
    {
        memcpy(reinterpret_cast<void*>(workspace), savedWorkspace, sizeof(savedWorkspace));
        memcpy(reinterpret_cast<void*>(playerView + kReachPvSnapshotBegin), savedPv, sizeof(savedPv));
        *reinterpret_cast<uintptr_t*>(ownerAddress) = owner;
        (void)ReachWindCopy(windState, wind, sizeof(wind));
        g_reachFpCameraEyeScope = previousFp;
        g_reachScopeRendering = false;
        g_scopeRenderActive.store(previousScope);
    }
}
void ReachRenderScope(uintptr_t playerView, ReachVrRenderAccess& access)
{
    __try
    {
        ReachRenderScopeBody(playerView, access);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("the Reach scope render faulted");
        VR_InvalidateScopeImage();
        g_reachScopeFaultGeneration.store(g_reachCamera.generation.load());
        g_reachScopeFaults.fetch_add(1);
    }
}
void ReportReachScope()
{
    const auto faults = g_reachScopeFaults.exchange(0);
    if (faults)
        LOG("Reach scope StockFallback: optional lens faulted %u times; camera core retained", faults);
}
