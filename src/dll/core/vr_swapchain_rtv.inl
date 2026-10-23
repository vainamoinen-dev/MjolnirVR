#pragma once
// Included once, by src/dll/core/vr_compose.cpp; also compiled by
//   tests/haloce_reticle_pixels_tests.cpp, which supplies its own versions. Not a standalone
//   header: it uses what the host defines above its include line, namely
//   CreateRenderTargetView, ViewDimension, g_device, g_xrFormat.
// Production XR image-view creation; shared with RGB reticle pixel tests.
ID3D11RenderTargetView* GetRtv(std::vector<ID3D11Texture2D*>& images, std::vector<ID3D11RenderTargetView*>& rtvs,
    uint32_t idx, bool rgbReticleUpload)
{
    if (!g_device || idx >= images.size() || idx >= rtvs.size())
        return nullptr;
    if (!rtvs[idx])
    {
        const HRESULT defaultResult = g_device->CreateRenderTargetView(images[idx], nullptr, &rtvs[idx]);
        if (VrHalo4ReticleRtvNeedsTypedFallback(rgbReticleUpload, FAILED(defaultResult), !rtvs[idx]))
        {
            // Halo 4 and CE: SteamVR may expose a TYPELESS OpenXR image. Such
            // a resource has no default RTV, but the negotiated XR format
            // is the exact concrete view format the runtime expects.
            D3D11_RENDER_TARGET_VIEW_DESC desc{};
            desc.Format = static_cast<DXGI_FORMAT>(g_xrFormat);
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MipSlice = 0;
            (void)g_device->CreateRenderTargetView(images[idx], &desc, &rtvs[idx]);
        }
    }
    return rtvs[idx];
}
