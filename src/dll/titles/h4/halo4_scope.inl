// Halo 4 scope pass: renders the scope view through Halo 4's own observer and per-window setup.
// Magnification comes from measured H4 projection gain; a refused frame keeps the last good image.
#pragma once
// Included once, by src/dll/titles/h4/halo4_camera_core.cpp; also compiled by tests/halo4_scope_tests.cpp,
//   which supplies its own versions. Not a standalone header: it uses what the host defines
//   above its include line, namely Halo4FloatingTargetFrame, Halo4FreezeFloatingTargetFrame,
//   Halo4RestoreMonoCamera, Halo4SafeRead, Halo4SafeWrite, Halo4ScopeLogCamera,
//   Halo4ScopeOrigin, Halo4SetupArgs, g_halo4Camera, g_halo4OrigCuiGameplayRender,
//   g_halo4OrigModelSkinning, g_halo4OrigSetup, g_halo4OrigWrapper, g_halo4RigTracking,
//   g_halo4ScopeFaultGeneration, g_halo4ScopeFaults, and 3 more.
// H4 observer conversion and native per-window setup own this optical pass.
// Magnification comes from measured H4 projection gain, never an H3 field.
void Halo4RenderScopeBody(uintptr_t element, uintptr_t view, uint32_t window, const Halo4SetupArgs& args,
    const unsigned char* savedObserver, const Halo4CameraBasis& root, const Halo4FovCalibration& calibration)
{
    if (!VR_ScopeShouldRenderThisFrame())
        return;
    // No pre-pass invalidate; a refusal keeps the last good image.
    if (!calibration.learned || !g_halo4RigTracking.rightAimValid || !g_halo4OrigCuiGameplayRender ||
        !g_halo4OrigModelSkinning || !std::isfinite(calibration.gain) || calibration.gain <= .001f)
        return;
    Halo4FloatingTargetFrame frame{};
    Halo4ControllerWorldPose controller{};
    if (!Halo4FreezeFloatingTargetFrame(frame))
        return;
    auto input = frame.common;
    memcpy(input.controllerOrientation, g_halo4RigTracking.rightAimOrientation, sizeof(input.controllerOrientation));
    memcpy(input.controllerPosition, g_halo4RigTracking.rightAimPosition, sizeof(input.controllerPosition));
    if (!Halo4BuildControllerWorldPose(input, controller))
        return;
    ScopeCameraPose pose{};
    float aspect = 1;
    float muzzleOrigin[3]{};
    if (!ComputeScopeCameraPose(
            controller.basis, Halo4ScopeOrigin(root.position, root.forward, muzzleOrigin), root.forward, pose) ||
        !VR_GetScopeRenderAspect(aspect))
        return;
    Halo4ScopeLogCamera(pose.position, root.position);
    const auto lens = ComputeScopeProjectionTangents(VR_GetScopeZoom(), aspect);
    const float fov = atanf(lens.vertical) / calibration.gain;
    if (!std::isfinite(fov) || fov <= .001f || fov >= 3.14159f)
        return;
    unsigned char observer[kHalo4ObserverSnapshotBytes];
    memcpy(observer, savedObserver, sizeof(observer));
    memcpy(observer + kHalo4ObserverPositionOffset, pose.position, 12);
    if (!VR_VisorZoomHeadView()) // the visor keeps the head camera's own view
    {
        memcpy(observer + kHalo4ObserverForwardOffset, pose.forward, 12);
        memcpy(observer + kHalo4ObserverUpOffset, pose.up, 12);
    }
    memcpy(observer + kHalo4ObserverVerticalFovOffset, &fov, 4);
    if (!VR_BeginScopeRaster())
        return;
    const bool previous = g_scopeRenderActive.exchange(true);
    g_halo4ScopeRendering = true;
    g_halo4ScopePixelsValid = true;
    __try
    {
        if (!Halo4SafeWrite(reinterpret_cast<void*>(args.observer), observer, sizeof(observer)))
            return;
        g_halo4OrigSetup(args.view, args.window, args.count, args.mode, args.user, args.observer);
        float projection[16]{};
        if (!Halo4SafeRead(
                reinterpret_cast<void*>(element + kHalo4ElementProjectionMatrixOffset), projection, sizeof(projection)))
            return;
        float x = 0, y = 0, cx = 0, cy = 0;
        if (!Halo4DecodeSymmetricProjectionHalfFovs(projection, x, y, cx, cy) ||
            fabsf(tanf(x) / lens.horizontal - 1) > .01f || fabsf(tanf(y) / lens.vertical - 1) > .01f)
            return;
        g_halo4OrigWrapper(element, view, window);
        if (g_halo4ScopePixelsValid)
            VR_CaptureScope();
    }
    __finally
    {
        g_halo4ScopeRendering = false;
        g_scopeRenderActive.store(previous);
        VR_EndScopeRaster();
        if (!Halo4RestoreMonoCamera(args, savedObserver))
        {
            VR_InvalidateScopeImage();
            g_halo4ScopeFaultGeneration.store(g_halo4Camera.generation.load());
            g_halo4ScopeFaults.fetch_add(1);
        }
    }
}
void Halo4RenderScope(uintptr_t element, uintptr_t view, uint32_t window, const Halo4SetupArgs& args,
    const unsigned char* savedObserver, const Halo4CameraBasis& root, const Halo4FovCalibration& calibration)
{
    __try
    {
        Halo4RenderScopeBody(element, view, window, args, savedObserver, root, calibration);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("the Halo 4 scope render faulted");
        VR_InvalidateScopeImage();
        g_halo4ScopeFaultGeneration.store(g_halo4Camera.generation.load());
        g_halo4ScopeFaults.fetch_add(1);
    }
}
