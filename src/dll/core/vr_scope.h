// vr_scope.h: what vr_scope.cpp provides to the other VR units (the scope image path, the authored reticle and the visor), in namespace vr_core.
// Defined in vr_scope.cpp.
#pragma once
#include "vr_internal.h"
#include "telemetry_recorder.h"

namespace vr_core
{
    void DrawScopeInset(ID3D11Texture2D* backbuffer, const D3D11_TEXTURE2D_DESC& bd);
    bool EnsureReticleChain();
    bool EnsureScopeUploadPipeline();
    void FillPresentedReticleTelemetry(TelemetryFrame& frame) noexcept;
    void LogLensProof(const std::vector<XrCompositionLayerProjectionView>& views, const float quad[3],
        const float aimDerived[3], bool anchored) noexcept;
    bool PrepareDualReticleImage();
    bool PrepareLensQuad(const float aimQ[4], const float aimP[3], bool viewsValid);
    void ProbeScopeImage(ID3D11Texture2D* image, bool stereo);
    void PublishPresentedReticleAimPose(
        const XrPosef* pose, uint64_t supportEpoch, bool supportTrusted, uint64_t solveSerial);
    void ReleaseScopeInset();
    bool UploadAuthoredReticle(bool requireSuccessfulRelease, bool identityChanged);
    bool UploadScopeToRtv(ID3D11RenderTargetView* rtv, const scope_parallax::Eye* eye = nullptr, float viewportX = 0,
        const D3D11_VIEWPORT* customViewport = nullptr, int linearizeOverride = -1);
} // namespace vr_core
