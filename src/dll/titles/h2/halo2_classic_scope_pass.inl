// Halo 2 Classic scope lens pass: the optional mono scope view, rendered after both eye images.
// Included once, by src/dll/titles/h2/halo2_stereo_core.cpp; also compiled by
//   tests/halo2_scope_tests.cpp, which supplies its own versions. Not a standalone header: it
//   uses what the host defines above its include line, namely ReadByteGuarded,
//   ReadEngineProjection, RestoreOwnedSpans, StereoScope, WriteByteGuarded, WriteEyeSpans,
//   g_zoomFaultGeneration, g_zoomFaults.
#pragma once
// H2EK render_view owns visibility as well as rasterization. Run
// the optional mono view only after both real eye images are safe.
if (scope->completedEyeMask == 3 && !scope->invalidated && g_zoomFaultGeneration.load() != scope->generation &&
    (Halo2Observer6Dof_UpdateCockpitScreen(), VR_ScopeShouldRenderThisFrame()))
{
    // No pre-pass invalidate (VR_EndHalo2Scope copies only on success).
    Halo2CameraBasis camera{};
    float aspect = 1;
    if (Halo2Observer6Dof_BuildScopeCamera(camera) && VR_GetScopeRenderAspect(aspect) &&
        VR_BeginHalo2Scope(scope->generation, scope->serial))
    {
        StereoScope lens = *scope;
        const auto tangents = [&]
        {
            float h = 0, v = 0;
            if (Halo2Observer6Dof_CockpitTangents(aspect, h, v))
                return ScopeProjectionTangents{h, v};
            return ComputeScopeProjectionTangents(VR_GetScopeZoom(), aspect);
        }();
        lens.eyes[0].render = lens.eyes[0].raster = camera;
        lens.renderCoverVerticalFov = lens.rasterCoverVerticalFov = 2 * atanf(tangents.vertical);
        bool rendered = false;
        uint8_t previousLatch = 0;
        const bool latchSaved = ReadByteGuarded(moduleBase + kHalo2ClassicSceneTargetLatchRva, previousLatch);
        const bool previousScope = Game_SetScopeRendering(true);
        __try
        {
            __try
            {
                if (latchSaved && scope->sceneTargetLatchValid &&
                    WriteByteGuarded(moduleBase + kHalo2ClassicSceneTargetLatchRva, scope->sceneTargetLatch) &&
                    WriteEyeSpans(lens, 0))
                {
                    original(argument01, argument02, argument03, argument04, argument05, argument06, argument07,
                        argument08, argument09, argument10, argument11, argument12, argument13, argument14, argument15,
                        argument16, argument17, argument18, argument19);
                    rendered = ReadEngineProjection(lens, 0) && lens.engineHalfFovsValid[0] &&
                               fabsf(tanf(lens.engineHalfFovs[0].horizontal) / tangents.horizontal - 1) < .01f &&
                               fabsf(tanf(lens.engineHalfFovs[0].vertical) / tangents.vertical - 1) < .01f;
                }
            }
            __finally
            {
                if (!RestoreOwnedSpans(lens))
                    rendered = false;
                if (latchSaved && !WriteByteGuarded(moduleBase + kHalo2ClassicSceneTargetLatchRva, previousLatch))
                    rendered = false;
                Game_SetScopeRendering(previousScope);
                VR_EndHalo2Scope(rendered);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("the Halo 2 Classic scope pass faulted");
            g_zoomFaultGeneration.store(scope->generation);
            g_zoomFaults.fetch_add(1);
            VR_InvalidateScopeImage();
        }
    }
}
