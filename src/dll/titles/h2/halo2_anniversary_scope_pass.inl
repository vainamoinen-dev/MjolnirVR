// Halo 2 Anniversary scope lens pass: the optional mono scope view, rendered inside the stereo frame.
// Included once, by src/dll/titles/h2/halo2_anniversary_stereo.cpp; also compiled by
//   tests/halo2_scope_tests.cpp, which supplies its own versions. Not a standalone header: it
//   uses what the host defines above its include line, namely ApplyEyeCamera, AspectFailed,
//   BeginRefused, CameraFailed, Ended, NotRendered, ReadGuarded, ScopePassStage,
//   g_scopePassActual, g_scopePassAttempts, g_scopePassRequested, g_scopePassSeq,
//   g_scopePassStage, g_zoomFaultGeneration, g_zoomFaults.
#pragma once
if (ok && haveLatch && g_zoomFaultGeneration.load() != generation &&
    (Halo2Observer6Dof_UpdateCockpitScreen(), VR_ScopeShouldRenderThisFrame()))
{
    // No pre-pass invalidate (VR_EndHalo2Scope copies only on success).
    g_scopePassSeq.fetch_add(1, std::memory_order_acq_rel);
    g_scopePassAttempts.fetch_add(1, std::memory_order_relaxed);
    Halo2CameraBasis lensCamera{};
    float aspect = 1;
    const bool haveCamera = Halo2Observer6Dof_BuildScopeCamera(lensCamera);
    const bool haveAspect = haveCamera && VR_GetScopeRenderAspect(aspect);
    const bool begun = haveAspect && VR_BeginHalo2Scope(generation, serial);
    g_scopePassStage.store(!haveCamera   ? ScopePassStage::CameraFailed
                           : !haveAspect ? ScopePassStage::AspectFailed
                           : !begun      ? ScopePassStage::BeginRefused
                                         : ScopePassStage::NotRendered,
        std::memory_order_relaxed);
    if (begun)
    {
        const auto tangents = [&]
        {
            float h = 0, v = 0;
            if (Halo2Observer6Dof_CockpitTangents(aspect, h, v))
                return ScopeProjectionTangents{h, v};
            return ComputeScopeProjectionTangents(VR_GetScopeZoom(), aspect);
        }();
        g_scopePassRequested[0].store(tangents.horizontal, std::memory_order_relaxed);
        g_scopePassRequested[1].store(tangents.vertical, std::memory_order_relaxed);
        g_scopePassActual[0].store(0, std::memory_order_relaxed);
        g_scopePassActual[1].store(0, std::memory_order_relaxed);
        Halo2SaberEyeCover lens = cover;
        lens.halfHorizontalRadians = atanf(tangents.horizontal);
        lens.halfVerticalRadians = atanf(tangents.vertical);
        lens.horizontalDegrees = 2 * lens.halfHorizontalRadians * 57.2957795f;
        lens.verticalDegrees = 2 * lens.halfVerticalRadians * 57.2957795f;
        bool rendered = false, contextSaved = false;
        unsigned char postEyeContext[kHalo2SaberSceneContextResetBytes];
        const bool previousScope = Game_SetScopeRendering(true);
        __try
        {
            __try
            {
                memcpy(postEyeContext, reinterpret_cast<uint8_t*>(ctx) + kHalo2SaberSceneContextResetOffset,
                    sizeof(postEyeContext));
                contextSaved = true;
                memcpy(reinterpret_cast<uint8_t*>(ctx) + kHalo2SaberSceneContextResetOffset, savedContext,
                    kHalo2SaberSceneContextResetBytes);
                *reinterpret_cast<int32_t*>(base + kHalo2SaberSceneOnceLatchRva) = savedLatch;
                if (ApplyEyeCamera(record, lensCamera, constants, lens))
                {
                    original(ctx, rdx, viewIndex);
                    float sx = 0, sy = 0;
                    Halo2SymmetricHalfFovs actual{};
                    const bool readBack = ReadGuarded(record + kHalo2SaberProjectionScaleXOffset, sx) &&
                                          ReadGuarded(record + kHalo2SaberProjectionScaleYOffset, sy) &&
                                          Halo2HalfFovsFromProjectionScales(sx, sy, actual);
                    if (readBack)
                    {
                        g_scopePassActual[0].store(tanf(actual.horizontal), std::memory_order_relaxed);
                        g_scopePassActual[1].store(tanf(actual.vertical), std::memory_order_relaxed);
                    }
                    rendered = readBack && fabsf(tanf(actual.horizontal) / tangents.horizontal - 1) < .01f &&
                               fabsf(tanf(actual.vertical) / tangents.vertical - 1) < .01f;
                }
            }
            __finally
            {
                // Restore the state left by the real right eye, not
                // the pre-frame reset template used to admit a view.
                __try
                {
                    if (contextSaved)
                        memcpy(reinterpret_cast<uint8_t*>(ctx) + kHalo2SaberSceneContextResetOffset, postEyeContext,
                            sizeof(postEyeContext));
                }
                __finally
                {
                    Game_SetScopeRendering(previousScope);
                    VR_EndHalo2Scope(rendered && !AbnormalTermination());
                    if (rendered)
                        g_scopePassStage.store(ScopePassStage::Ended, std::memory_order_relaxed);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("the Halo 2 Anniversary scope pass faulted");
            g_zoomFaultGeneration.store(generation);
            g_zoomFaults.fetch_add(1);
            VR_InvalidateScopeImage();
            g_scopePassStage.store(ScopePassStage::Fault, std::memory_order_relaxed);
        }
    }
    g_scopePassSeq.fetch_add(1, std::memory_order_release);
}
