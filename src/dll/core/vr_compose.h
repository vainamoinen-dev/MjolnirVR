// vr_compose.h: what vr_compose.cpp provides to the other VR units (image quality, DLSS, SMAA, FSR, the theater projection and the eye caches), in namespace vr_core.
// Defined in vr_compose.cpp.
#pragma once
#include "vr_internal.h"

namespace vr_core
{
    ID3D11ShaderResourceView* AcquireSrcSrv(ID3D11Texture2D* src, const D3D11_TEXTURE2D_DESC& srcDesc);
    bool BlitImageQuality(ID3D11Texture2D* src, const D3D11_TEXTURE2D_DESC& srcDesc, ID3D11Texture2D* /*dst*/,
        uint32_t dstW, uint32_t dstH, ID3D11RenderTargetView* dstRtv, bool reconstructed = false);
    bool CaptureTheaterSubtitleBand(ID3D11Texture2D* backbuffer, const D3D11_TEXTURE2D_DESC& backbufferDesc);
    void CollectIqTimings();
    bool CompositeTheaterProjectionEye(uint32_t targetEye, ID3D11ShaderResourceView* sourceSrv,
        const D3D11_TEXTURE2D_DESC& sourceDesc, bool sourceAlreadyXrEncoded, const XrView& nativeView,
        ID3D11RenderTargetView* targetRtv);
    bool DeliverMonitorImage();
    void DlssCaptureEyeDepth(int eye);
    void DlssEndFrame();
    bool DlssKickEye(int eye, ID3D11Texture2D* src, const D3D11_TEXTURE2D_DESC& srcDesc, uint32_t dstW, uint32_t dstH,
        ID3D11RenderTargetView* debugRtv);
    bool DrawComfortImage(XrSwapchain chain, std::vector<ID3D11Texture2D*>& images,
        std::vector<ID3D11RenderTargetView*>& rtvs, uint32_t w, uint32_t h, ID3D11PixelShader* ps, float strength);
    bool EnsureBlitPipeline();
    bool EnsureComfortPipeline();
    bool EnsureTheaterProjectionResources(uint32_t width, uint32_t height, bool needResolved);
    bool PrepareVisorImageDelivery();
    void ReleaseDlssEyeTextures(DlssEyeState& state);
    void ReleaseIqChain();
    void ReleaseTheaterProjectionResources();
    void ReportIqGpuTiming();
} // namespace vr_core
