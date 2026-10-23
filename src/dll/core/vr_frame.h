// vr_frame.h: the vr.cpp functions the title frame sources call, in namespace vr_core. Defined in vr.cpp.
#pragma once
#include "aim_pose_trace.h"
#include "vr_internal.h"

namespace vr_core
{
    void NoteScopeImageCaptured();
    XrPosef PresentedAimContinuityPose(XrPosef pose, uint64_t preparedSerial) noexcept;
    void DlssNoteEyeDepth(ID3D11DepthStencilView* depth, bool sceneBound);
    bool EnsureScopeCache(const D3D11_TEXTURE2D_DESC& sourceDesc);
    bool EnsureScreenChain(uint32_t w, uint32_t h);
    bool EnsureEyeCaches(const D3D11_TEXTURE2D_DESC& source);
    XrCompositionLayerQuad MakeQuad(XrSwapchain chain, int32_t imgW, int32_t imgH, float widthMeters, float distMeters,
        float yOffset, XrCompositionLayerFlags flags, bool headLocked, float xOffset = 0.0f);
    void ApplyPauseWorldLock(XrCompositionLayerQuad& quad, bool paused, float distMeters);
    AimPoseInputs CurrentAimPoseInputs(
        bool rightValid, const XrPosef& right, bool leftValid, const XrPosef& left) noexcept;
    AimPoseInputs CurrentFrameStockAimPoseInputs(
        bool rightValid, const XrPosef& right, bool leftValid, const XrPosef& left) noexcept;
    AimPoseResult ComputeAimPose(const AimPoseInputs& inputs) noexcept;
    AimPoseResult ComputeAimPose(const AimPoseInputs& inputs, AimPoseTrace* trace) noexcept;
    XrPosef PresentedTwoHandLabTemporalPose(XrPosef pose, uint64_t preparedSerial, uint64_t labGeneration) noexcept;
    void FLog(const char* step);
    // From the .inl files vr.cpp includes in vr_core (vr_blit.inl, vr_swapchain_rtv.inl). The default
    // arguments are given here only.
    bool Blit(ID3D11Texture2D* src, const D3D11_TEXTURE2D_DESC& srcDesc, ID3D11Texture2D* dst, uint32_t dstW,
        uint32_t dstH, ID3D11RenderTargetView* dstRtv, bool authoredReticleUpload = false);
    ID3D11RenderTargetView* GetRtv(std::vector<ID3D11Texture2D*>& images, std::vector<ID3D11RenderTargetView*>& rtvs,
        uint32_t idx, bool rgbReticleUpload = false);
    // vr.cpp functions the subject units call (E2-VR 7b).
    bool CreateChain(uint32_t w, uint32_t h, XrSwapchain& chain, std::vector<ID3D11Texture2D*>& images,
        std::vector<ID3D11RenderTargetView*>& rtvs, const char* what);
    const char* DlssTitleName(GameTitle title);
    bool PistolTwoHandVisualOnly() noexcept;
    bool PrepareScopeImageDelivery(const scope_parallax::Eye* eyes = nullptr);
    bool ReadSupportInvocationReceiptInternal(
        GameTitle title, support_grip::SupportInvocationReceipt& receipt) noexcept;
} // namespace vr_core

// The title frame sources reach the small shared helpers through vr_frame.h.
#include "vr_format_helpers.h"
#include "vr_inline_ops.h"
