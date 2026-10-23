// vr_compose.cpp: image quality, DLSS, SMAA, FSR, the theater projection and the eye caches, moved unchanged from vr.cpp. What it provides to the other VR units is
// declared in vr_compose.h; it reaches their state through vr_internal.h.
#include <windows.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <vector>
#include <array>
#include <string>
#include <map>
#include <wincodec.h>
#include <atomic>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <share.h>
#include <cstring>
#include <limits>
#include <type_traits>
#include "../../common/titles/ce/haloce_reticle_logic.h"
#include "../../common/vr_blit_shader.h"
#include "d3d11_hook.h"
#include "d3d_state.h"
#include "smaa_resource.h"
#include "dlss_camera_ps.h"
#include "dlss_debug_ps.h"
#include "dlss_copy_ps.h"
#include "../../common/dlss_depth_tracking.h"
#include "AreaTex.h"
#include "SearchTex.h"
#include <../common/authored_reticle_logic.h>
#include "vr_internal.h"
#include "vr_frame.h"
#include "vr_compose.h"
#include "vr_controllers.h"
#include "vr_scope.h"
#include "vr_telemetry.h"
#include "vr_format_helpers.h"
#include "vr_inline_ops.h"
#include "../titles/reach/reach_frame_bridge.h"
using namespace reach_frame_bridge;
#include "../titles/h4/halo4_frame_bridge.h"
using namespace halo4_frame_bridge;
#include "../titles/ce/haloce_frame_bridge.h"
using namespace haloce_frame_bridge;
#include "../titles/h2/halo2_frame_bridge.h"
using namespace halo2_frame_bridge;
using namespace vr_core;
#include "../titles/reach/reach_display_worker.h"
using namespace reach_bridge;

extern "C" IMAGE_DOS_HEADER __ImageBase;
namespace
{
    // not constinit: its type has no constant initialiser; it is initialised dynamically, as before.
    std::vector<ID3D11Texture2D*> g_visorScreenImages;
    // not constinit: its type has no constant initialiser; it is initialised dynamically, as before.
    std::vector<ID3D11RenderTargetView*> g_visorScreenRtvs;
    constinit ID3D11PixelShader* g_visorPs = nullptr;
    constinit ID3D11PixelShader* g_visorPsLinearize = nullptr;
    constinit ID3D11Buffer* g_visorConstants = nullptr;
    // not constinit: its type has no constant initialiser; it is initialised dynamically, as before.
    std::vector<ID3D11Texture2D*> g_monitorImages[vehicle_monitors::kViewCount];
    // not constinit: its type has no constant initialiser; it is initialised dynamically, as before.
    std::vector<ID3D11RenderTargetView*> g_monitorRtvs[vehicle_monitors::kViewCount];
    constinit uint32_t g_monitorDeliveredSerial = 0;
    // Logical sampling/RTV format remains in g_eyeCacheDesc. Halo 3's native
    // DLSS cache has typeless storage, an sRGB RTV, and UAV access for NGX.
    constinit bool g_eyeCacheNativeDlss = false;
    constinit bool g_eyeCacheDlssRequested = false;
    constinit ID3D11PixelShader* g_dlssMotionPs = nullptr;
    constinit ID3D11PixelShader* g_dlssDebugPs = nullptr;
    constinit ID3D11PixelShader* g_dlssCopyPs = nullptr; // eye colour -> optical-flow input
    constinit ID3D11Buffer* g_dlssMotionCb = nullptr;
    constinit bool g_dlssMotionInitFailed = false;
    constinit uint32_t g_dlssJitterPhase = 0;
    constinit uint64_t g_dlssResolvedEyes = 0;
    constinit uint64_t g_dlssFallbackEyes = 0;
    constinit uint64_t g_dlssResets = 0;
    constinit uint64_t g_dlssFallbackLogMs = 0;
    constinit char g_dlssFallbackReason[160]{};
    // A persistent optional-feature failure must return the game to its full
    // render size, rather than stretching the reduced DLSS input forever.
    constinit bool g_dlssBatchAttemptedThisFrame = false;
    constinit uint64_t g_dlssStatusMs = 0;
    constinit ID3D11PixelShader* g_blitPsLinearize = nullptr; // sRGB-decodes in the shader
    constinit ID3D11PixelShader* g_blitPsPass = nullptr;
    // Image-quality pipeline: sharp bicubic resolve + selectable FXAA/SMAA 1x +
    // RCAS-based sharpen, applied when each eye is expanded into the headset.
    // SMAA is lazy/opt-in and adds its third eye-sized target only while selected.
    constinit ID3D11PixelShader* g_iqResolveSharp = nullptr;  // sharp Keys bicubic resolve/upscale
    constinit ID3D11PixelShader* g_iqResolveLinear = nullptr; // linear resolve (matches old blit)
    constinit ID3D11PixelShader* g_iqFxaa = nullptr;
    constinit ID3D11PixelShader* g_iqRcas = nullptr;
    constinit ID3D11PixelShader* g_iqDlssRcas = nullptr;
    constinit ID3D11Buffer* g_iqCb = nullptr;
    constinit ID3D11Texture2D* g_iqChain[3] = {nullptr, nullptr, nullptr};
    constinit ID3D11RenderTargetView* g_iqChainRtv[3] = {nullptr, nullptr, nullptr};
    constinit ID3D11ShaderResourceView* g_iqChainSrv[3] = {nullptr, nullptr, nullptr};
    constinit D3D11_TEXTURE2D_DESC g_iqChainDesc{};
    constinit ID3D11Texture2D* g_theaterDirectSourceKey[2] = {nullptr, nullptr};
    constinit ID3D11ShaderResourceView* g_theaterDirectSourceSrv[2] = {nullptr, nullptr};
    constinit ID3D11VertexShader* g_theaterProjectionVs = nullptr;
    constinit ID3D11PixelShader* g_theaterProjectionPs = nullptr;
    constinit ID3D11Buffer* g_theaterProjectionCb = nullptr;
    // Official SMAA 1x shaders and immutable lookup tables. They are created on
    // first SMAA selection and remain tiny; the third eye-sized target is the
    // mode-scoped allocation released when SMAA is deselected.
    constinit ID3D11VertexShader* g_smaaEdgesVs = nullptr;
    constinit ID3D11VertexShader* g_smaaWeightsVs = nullptr;
    constinit ID3D11VertexShader* g_smaaNeighborhoodVs = nullptr;
    constinit ID3D11PixelShader* g_smaaEdgesPs = nullptr;
    constinit ID3D11PixelShader* g_smaaWeightsPs = nullptr;
    constinit ID3D11PixelShader* g_smaaNeighborhoodPs = nullptr;
    constinit ID3D11SamplerState* g_smaaPointSampler = nullptr;
    constinit ID3D11Texture2D* g_smaaAreaTex = nullptr;
    constinit ID3D11ShaderResourceView* g_smaaAreaSrv = nullptr;
    constinit ID3D11Texture2D* g_smaaSearchTex = nullptr;
    constinit ID3D11ShaderResourceView* g_smaaSearchSrv = nullptr;
    constinit bool g_smaaInitFailed = false;
    struct IqParams // matches cbuffer IqParams (b0); 32 bytes
    {
        float srcW, srcH, dstW, dstH;
        float sharpness, srcIsSrgb, outPerceptual, aaStrength;
        float sharpGate, pad0, pad1, pad2;
    };
} // namespace
namespace vr_core
{
    bool EnsureBlitPipeline()
    {
        if (g_blitVs && g_blitPsLinearize && g_blitPsPass && g_blitSampler && g_blitRasterizer && g_blitDepthOff)
            return true;
        auto release = [&](auto*& object)
        {
            if (object)
                object->Release();
            object = nullptr;
        };
        release(g_blitVs);
        release(g_blitPsLinearize);
        release(g_blitPsPass);
        release(g_blitSampler);
        release(g_blitRasterizer);
        release(g_blitDepthOff);

        const char* src = kVrBlitShader;
        ID3DBlob* blob = nullptr;
        ID3DBlob* err = nullptr;
        auto compile = [&](const char* entry, const char* target) -> ID3DBlob*
        {
            ID3DBlob* out = nullptr;
            if (FAILED(D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, target, 0, 0, &out, &err)))
            {
                LOG("blit shader '%s' failed to compile: %s", entry, err ? (const char*)err->GetBufferPointer() : "?");
                if (err)
                {
                    err->Release();
                    err = nullptr;
                }
                return nullptr;
            }
            return out;
        };

        blob = compile("vs_main", "vs_5_0");
        if (!blob)
            return false;
        HRESULT hr = g_device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_blitVs);
        blob->Release();
        if (FAILED(hr))
            return false;

        blob = compile("ps_linearize", "ps_5_0");
        if (!blob)
            return false;
        hr = g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_blitPsLinearize);
        blob->Release();
        if (FAILED(hr))
            return false;

        blob = compile("ps_pass", "ps_5_0");
        if (!blob)
            return false;
        hr = g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_blitPsPass);
        blob->Release();
        if (FAILED(hr))
            return false;

        D3D11_SAMPLER_DESC smp{};
        smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        if (FAILED(g_device->CreateSamplerState(&smp, &g_blitSampler)))
            return false;

        D3D11_RASTERIZER_DESC rs{};
        rs.FillMode = D3D11_FILL_SOLID;
        rs.CullMode = D3D11_CULL_NONE;
        rs.DepthClipEnable = TRUE;
        if (FAILED(g_device->CreateRasterizerState(&rs, &g_blitRasterizer)))
            return false;

        D3D11_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = FALSE;
        if (FAILED(g_device->CreateDepthStencilState(&ds, &g_blitDepthOff)))
            return false;

        return true;
    }
} // namespace vr_core
namespace
{
    // Release a slot's resources so the pool can rebuild it into a new shape.
    void ReleaseIntermediateSlot(std::size_t slot)
    {
        if (slot >= kIntermediatePoolCapacity)
            return;
        if (g_intermediateSrvs[slot])
        {
            g_intermediateSrvs[slot]->Release();
            g_intermediateSrvs[slot] = nullptr;
        }
        if (g_intermediateTex[slot])
        {
            g_intermediateTex[slot]->Release();
            g_intermediateTex[slot] = nullptr;
        }
    }
} // namespace
namespace vr_core
{
    // Acquire an SRV we can sample `src` from. Backbuffers usually can't be used
    // as shader input directly, so we may need an SRV-capable intermediate copy.
    // Returns a borrowed SRV, owned by the caches above; never release it here.
    ID3D11ShaderResourceView* AcquireSrcSrv(ID3D11Texture2D* src, const D3D11_TEXTURE2D_DESC& srcDesc)
    {
        // A null source is rejected up front. The cache treats a null key as
        // "do not store" and hands the view straight back, which the caller
        // below would then release and return - a use-after-free. Refusing here
        // is cheaper than making every caller prove it.
        if (!src || !g_device)
            return nullptr;
        if ((srcDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) && srcDesc.SampleDesc.Count <= 1)
        {
            // A hit is the whole point: both eyes, the menu, the screen quad
            // and the reticle each keep their own view, so a settled frame
            // creates nothing at all here.
            const ViewCacheLookup cached = g_srcSrvCache.Find(src);
            if (cached.hit)
                return static_cast<ID3D11ShaderResourceView*>(cached.view);

            ID3D11ShaderResourceView* fresh = nullptr;
            // A TYPELESS target has no default view; name the
            // concrete format explicitly or the source can never be sampled.
            D3D11_SHADER_RESOURCE_VIEW_DESC typedDesc{};
            const bool typeless = dxgi_view_format::IsTypeless(static_cast<uint32_t>(srcDesc.Format));
            if (typeless || IsSrgb(srcDesc.Format))
            {
                typedDesc.Format = typeless ? static_cast<DXGI_FORMAT>(
                                                  dxgi_view_format::Concrete(static_cast<uint32_t>(srcDesc.Format)))
                                            : srcDesc.Format;
                typedDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                typedDesc.Texture2D.MostDetailedMip = 0;
                typedDesc.Texture2D.MipLevels = 1;
            }
            if (SUCCEEDED(g_device->CreateShaderResourceView(
                    src, (typeless || IsSrgb(srcDesc.Format)) ? &typedDesc : nullptr, &fresh)) &&
                fresh)
            {
                ++g_srcViewCreated;
                if (void* displaced = g_srcSrvCache.Insert(src, fresh))
                    static_cast<ID3D11ShaderResourceView*>(displaced)->Release();
                return fresh;
            }
            // Creation failed; fall through to the intermediate copy rather
            // than caching a null and reporting a hit that hands back nothing.
        }

        IntermediateShape shape{};
        shape.width = srcDesc.Width;
        shape.height = srcDesc.Height;
        shape.format = static_cast<uint32_t>(srcDesc.Format);
        const IntermediatePoolSlot slot = g_intermediatePool.Acquire(shape);
        if (!slot.valid)
            return nullptr;
        if (slot.needsCreate)
        {
            // The slot may still hold a resource of the evicted shape.
            ReleaseIntermediateSlot(slot.index);
            D3D11_TEXTURE2D_DESC d{};
            d.Width = srcDesc.Width;
            d.Height = srcDesc.Height;
            d.MipLevels = 1;
            d.ArraySize = 1;
            d.Format = srcDesc.Format;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(g_device->CreateTexture2D(&d, nullptr, &g_intermediateTex[slot.index])) ||
                FAILED(g_device->CreateShaderResourceView(
                    g_intermediateTex[slot.index], nullptr, &g_intermediateSrvs[slot.index])))
            {
                LOG("blit: intermediate texture creation failed (fmt %d)", (int)srcDesc.Format);
                ReleaseIntermediateSlot(slot.index);
                g_intermediatePool.Abandon(slot.index);
                return nullptr;
            }
            ++g_intermediateCreated;
            g_intermediatePool.Commit(slot.index, shape);
        }
        if (srcDesc.SampleDesc.Count > 1)
            g_context->ResolveSubresource(g_intermediateTex[slot.index], 0, src, 0, srcDesc.Format);
        else
            g_context->CopyResource(g_intermediateTex[slot.index], src);
        return g_intermediateSrvs[slot.index];
    }

#include "vr_blit.inl"
} // namespace vr_core
namespace
{
    // Image-quality pipeline. Compiles the resolve/AA/sharpen pixel shaders and
    // the params constant buffer once; reuses the blit VS/sampler/rasterizer/
    // depth. All passes operate in the display's perceptual (sRGB-encoded) space
    // via UNORM intermediates, decoding to linear only at the final write to the
    // sRGB XR target. The sharp bicubic resolve deliberately uses the LINEAR
    // sampler (its 9-tap form combines bilinear fetches).
    bool EnsureIqPipeline()
    {
        if (g_iqResolveSharp && g_iqResolveLinear && g_iqFxaa && g_iqRcas && g_iqCb)
            return true;
        if (!EnsureBlitPipeline())
            return false;
        auto release = [&](auto*& o)
        {
            if (o)
                o->Release();
            o = nullptr;
        };
        release(g_iqResolveSharp);
        release(g_iqResolveLinear);
        release(g_iqFxaa);
        release(g_iqRcas);
        release(g_iqDlssRcas);
        release(g_iqCb);

        static const char* src = R"(
Texture2D srcTex : register(t0);
SamplerState smp : register(s0);
cbuffer IqParams : register(b0)
{
    float2 srcSize;      // dims of the texture being sampled this pass
    float2 dstSize;      // dims of the render target this pass
    float  sharpness;    // 0..1 UI range; RCAS correction is 2x overdriven
    float  srcIsSrgb;    // 1 = sampling srcTex already returns linear
    float  outPerceptual;// 1 = write perceptual (to UNORM); 0 = write linear (to sRGB RTV)
    float  aaStrength;   // 0 = FXAA, 1 = FXAA Strong
    float  sharpGate;    // 0 = sharpen everywhere; else local contrast below this is left alone (grain), ramping to full at 2x
    float3 pad;
};
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float3 toLin(float3 c){ return float3(
    c.r <= 0.04045 ? c.r/12.92 : pow((c.r+0.055)/1.055, 2.4),
    c.g <= 0.04045 ? c.g/12.92 : pow((c.g+0.055)/1.055, 2.4),
    c.b <= 0.04045 ? c.b/12.92 : pow((c.b+0.055)/1.055, 2.4)); }
float3 toSrgb(float3 c){ c=saturate(c); return float3(
    c.r <= 0.0031308 ? c.r*12.92 : 1.055*pow(c.r,1.0/2.4)-0.055,
    c.g <= 0.0031308 ? c.g*12.92 : 1.055*pow(c.g,1.0/2.4)-0.055,
    c.b <= 0.0031308 ? c.b*12.92 : 1.055*pow(c.b,1.0/2.4)-0.055); }
float3 srcLinear(float4 s){ return srcIsSrgb > 0.5 ? s.rgb : toLin(s.rgb); }
float3 encodeOut(float3 lin){ return outPerceptual > 0.5 ? toSrgb(lin) : lin; }
float3 finishPerceptual(float3 c){ return outPerceptual > 0.5 ? c : toLin(c); }
float lumaP(float3 c){ return dot(c, float3(0.299, 0.587, 0.114)); }

// Keys bicubic with a=-0.75. Catmull-Rom (a=-0.5) was mathematically valid but
// too close to linear at the modest horizontal scale used by this headset.
// The stronger negative lobe is still bounded, while making the F1 A/B visible.
float4 sampleSharpBicubic(float2 uv)
{
    float2 texSize = srcSize;
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5) + 0.5;
    float2 f = samplePos - texPos1;
    const float a = -0.75;
    float2 w0 = a * f * (1.0 - f) * (1.0 - f);
    float2 w1 = 1.0 - (a + 3.0) * f * f + (a + 2.0) * f * f * f;
    float2 g = 1.0 - f;
    float2 w2 = 1.0 - (a + 3.0) * g * g + (a + 2.0) * g * g * g;
    float2 w3 = a * f * f * (1.0 - f);
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / w12;
    float2 p0 = (texPos1 - 1.0) / texSize;
    float2 p3 = (texPos1 + 2.0) / texSize;
    float2 p12 = (texPos1 + offset12) / texSize;
    float4 r = float4(0,0,0,0);
    r += srcTex.SampleLevel(smp, float2(p0.x,  p0.y ), 0) * (w0.x  * w0.y );
    r += srcTex.SampleLevel(smp, float2(p12.x, p0.y ), 0) * (w12.x * w0.y );
    r += srcTex.SampleLevel(smp, float2(p3.x,  p0.y ), 0) * (w3.x  * w0.y );
    r += srcTex.SampleLevel(smp, float2(p0.x,  p12.y), 0) * (w0.x  * w12.y);
    r += srcTex.SampleLevel(smp, float2(p12.x, p12.y), 0) * (w12.x * w12.y);
    r += srcTex.SampleLevel(smp, float2(p3.x,  p12.y), 0) * (w3.x  * w12.y);
    r += srcTex.SampleLevel(smp, float2(p0.x,  p3.y ), 0) * (w0.x  * w3.y );
    r += srcTex.SampleLevel(smp, float2(p12.x, p3.y ), 0) * (w12.x * w3.y );
    r += srcTex.SampleLevel(smp, float2(p3.x,  p3.y ), 0) * (w3.x  * w3.y );
    return r;
}

float4 ps_resolve_sharp(VSOut i) : SV_Target
{
    float4 s = sampleSharpBicubic(i.uv);
    return float4(encodeOut(srcLinear(s)), s.a);
}
float4 ps_resolve_linear(VSOut i) : SV_Target
{
    float4 s = srcTex.SampleLevel(smp, i.uv, 0);
    return float4(encodeOut(srcLinear(s)), s.a);
}

// FXAA 3.11 console-quality edge smoothing, in perceptual space.
float4 ps_fxaa(VSOut i) : SV_Target
{
    float2 rcp = 1.0 / srcSize;
    float2 uv = i.uv;
    float3 m  = srcTex.SampleLevel(smp, uv, 0).rgb;
    float3 nw = srcTex.SampleLevel(smp, uv + float2(-rcp.x,-rcp.y), 0).rgb;
    float3 ne = srcTex.SampleLevel(smp, uv + float2( rcp.x,-rcp.y), 0).rgb;
    float3 sw = srcTex.SampleLevel(smp, uv + float2(-rcp.x, rcp.y), 0).rgb;
    float3 se = srcTex.SampleLevel(smp, uv + float2( rcp.x, rcp.y), 0).rgb;
    float lM=lumaP(m), lNW=lumaP(nw), lNE=lumaP(ne), lSW=lumaP(sw), lSE=lumaP(se);
    float lMin = min(lM, min(min(lNW,lNE), min(lSW,lSE)));
    float lMax = max(lM, max(max(lNW,lNE), max(lSW,lSE)));
    float edgeThresholdMin = lerp(0.0312, 0.0156, saturate(aaStrength));
    float edgeThreshold = lerp(0.125, 0.063, saturate(aaStrength));
    if ((lMax - lMin) < max(edgeThresholdMin, lMax * edgeThreshold))
        return float4(finishPerceptual(m), 1);
    float2 dir;
    dir.x = -((lNW + lNE) - (lSW + lSE));
    dir.y =  ((lNW + lSW) - (lNE + lSE));
    float dirReduce = max((lNW + lNE + lSW + lSE) * (0.25 * 0.03125), 0.0078125);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    float span = lerp(8.0, 12.0, saturate(aaStrength));
    dir = clamp(dir * rcpDirMin, -span, span) * rcp;
    float3 rgbA = 0.5 * (
        srcTex.SampleLevel(smp, uv + dir * (1.0/3.0 - 0.5), 0).rgb +
        srcTex.SampleLevel(smp, uv + dir * (2.0/3.0 - 0.5), 0).rgb);
    float3 rgbB = rgbA * 0.5 + 0.25 * (
        srcTex.SampleLevel(smp, uv + dir * -0.5, 0).rgb +
        srcTex.SampleLevel(smp, uv + dir *  0.5, 0).rgb);
    float lB = lumaP(rgbB);
    float3 result = (lB < lMin || lB > lMax) ? rgbA : rgbB;
    return float4(finishPerceptual(result), 1);
}

float3 loadClamped(int2 p)
{
    p = clamp(p, int2(0, 0), int2(srcSize) - 1);
    return srcTex.Load(int3(p, 0)).rgb;
}

// AMD FidelityFX FSR1 RCAS 32-bit limiter/resolve, followed by a 2x correction
// overdrive. This retains RCAS's safe lobe/denominator and the same five loads;
// the user can pull the intentionally aggressive top end back with the slider.
float4 ps_rcas(VSOut i) : SV_Target
{
    int2 p = int2(i.pos.xy);
    float3 b = loadClamped(p + int2( 0,-1));
    float3 d = loadClamped(p + int2(-1, 0));
    float3 e = loadClamped(p);
    float3 f = loadClamped(p + int2( 1, 0));
    float3 h = loadClamped(p + int2( 0, 1));
    float3 mn4 = min(min(b, d), min(f, h));
    float3 mx4 = max(max(b, d), max(f, h));

    // Solve the negative lobe that cannot clip either side of [0,1], then
    // apply RCAS's documented -0.1875 natural-result limit.
    float3 hitMin = min(mn4, e) / max(4.0 * mx4, 1e-5);
    float3 hitMax = (1.0 - max(mx4, e)) / min(4.0 * mn4 - 4.0, -1e-5);
    float3 lobes = max(-hitMin, hitMax);
    float lobe = max(-0.1875, min(max(lobes.r, max(lobes.g, lobes.b)), 0.0));
    float scaledLobe = lobe * saturate(sharpness);
    float3 rcas = (scaledLobe * (b + d + f + h) + e) /
                  (4.0 * scaledLobe + 1.0);
    float gate = sharpGate > 0.0 ? saturate((max(mx4.g, e.g) - min(mn4.g, e.g) - sharpGate) / sharpGate) : 1.0;
    float3 res = saturate(e + 2.0 * gate * (rcas - e));
    return float4(finishPerceptual(res), 1);
}
// Fuse the linear resolve and RCAS for perceptual DLSS input. Each tap is
// the resolved UNORM8 pixel, including quantisation (neighbor UV arithmetic
// can differ by a hardware interpolation step from the separate pass).
// Destination-grid offsets preserve the sharpening radius.
float3 resolvedClamped(float2 uv)
{
    uv=clamp(uv,0.5/dstSize,1.0-0.5/dstSize);
    float3 c=srcTex.SampleLevel(smp,uv,0).rgb;
    return round(saturate(c)*255.0)/255.0;
}
float4 ps_dlss_resolve_rcas(VSOut i) : SV_Target
{
    float2 p=i.uv,step=1.0/dstSize;
    float3 b=resolvedClamped(p+float2(0,-step.y)),d=resolvedClamped(p+float2(-step.x,0));
    float3 e=resolvedClamped(p),f=resolvedClamped(p+float2(step.x,0)),h=resolvedClamped(p+float2(0,step.y));
    float3 mn4=min(min(b,d),min(f,h)),mx4=max(max(b,d),max(f,h));
    float3 hitMin=min(mn4,e)/max(4.0*mx4,1e-5);
    float3 hitMax=(1.0-max(mx4,e))/min(4.0*mn4-4.0,-1e-5);
    float3 lobes=max(-hitMin,hitMax);
    float lobe=max(-0.1875,min(max(lobes.r,max(lobes.g,lobes.b)),0.0));
    float scaledLobe=lobe*saturate(sharpness);
    float3 rcas=(scaledLobe*(b+d+f+h)+e)/(4.0*scaledLobe+1.0);
    float gate=sharpGate>0.0?saturate((max(mx4.g,e.g)-min(mn4.g,e.g)-sharpGate)/sharpGate):1.0;
    return float4(finishPerceptual(saturate(e+2.0*gate*(rcas-e))),1);
}
)";
        ID3DBlob* err = nullptr;
        auto compile = [&](const char* entry) -> ID3DBlob*
        {
            ID3DBlob* out = nullptr;
            const HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, "ps_5_0",
                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &err);
            if (FAILED(hr))
            {
                LOG("IQ shader '%s' failed to compile: %s", entry, err ? (const char*)err->GetBufferPointer() : "?");
                if (err)
                {
                    err->Release();
                    err = nullptr;
                }
                return nullptr;
            }
            if (err)
            {
                err->Release();
                err = nullptr;
            }
            return out;
        };
        struct Target
        {
            const char* entry;
            ID3D11PixelShader** out;
        };
        // clang-format off
        const Target targets[] = {
            {"ps_resolve_sharp",   &g_iqResolveSharp},
            {"ps_resolve_linear",  &g_iqResolveLinear},
            {"ps_fxaa",            &g_iqFxaa},
            {"ps_rcas",            &g_iqRcas},
            // clang-format on
        };
        for (const auto& t : targets)
        {
            ID3DBlob* blob = compile(t.entry);
            if (!blob)
                return false;
            HRESULT hr = g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, t.out);
            blob->Release();
            if (FAILED(hr))
                return false;
        }
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = sizeof(IqParams);
        cb.Usage = D3D11_USAGE_DEFAULT;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(g_device->CreateBuffer(&cb, nullptr, &g_iqCb)))
            return false;
        // Optional optimization: compilation/allocation failure retains the
        // already working ordinary resolve, independently of VR ownership.
        if (ID3DBlob* blob = dlss::kEnableDlssFullOutputResolve ? compile("ps_dlss_resolve_rcas") : nullptr)
        {
            const HRESULT hr =
                g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_iqDlssRcas);
            blob->Release();
            if (FAILED(hr))
                LOG("DLSS: fused output shader unavailable (0x%08X); ordinary resolve stays", unsigned(hr));
        }
        return true;
    }

    void ReleaseSmaaPipeline()
    {
        auto release = [](auto*& o)
        {
            if (o)
                o->Release();
            o = nullptr;
        };
        release(g_smaaEdgesVs);
        release(g_smaaWeightsVs);
        release(g_smaaNeighborhoodVs);
        release(g_smaaEdgesPs);
        release(g_smaaWeightsPs);
        release(g_smaaNeighborhoodPs);
        release(g_smaaPointSampler);
        release(g_smaaAreaSrv);
        release(g_smaaAreaTex);
        release(g_smaaSearchSrv);
        release(g_smaaSearchTex);
    }

    // Genuine SMAA 1x: official three-stage edge -> blend-weight -> neighborhood
    // pipeline with the highest-coverage 1x preset and official lookup tables.
    // Lazy creation keeps initial Off/FXAA startup unchanged; deselected modes
    // run no SMAA passes and retain no SMAA-sized eye target.
    bool EnsureSmaaPipeline()
    {
        if (g_smaaEdgesVs && g_smaaWeightsVs && g_smaaNeighborhoodVs && g_smaaEdgesPs && g_smaaWeightsPs &&
            g_smaaNeighborhoodPs && g_smaaPointSampler && g_smaaAreaSrv && g_smaaSearchSrv)
            return true;
        if (g_smaaInitFailed)
            return false;

        ReleaseSmaaPipeline();
        auto fail = [&]()
        {
            ReleaseSmaaPipeline();
            g_smaaInitFailed = true;
            return false;
        };

        HMODULE self = reinterpret_cast<HMODULE>(&__ImageBase);
        auto resourceBytes = [&](int id, const char* name, const void*& bytes, DWORD& byteCount)
        {
            HRSRC resource = FindResourceW(self, MAKEINTRESOURCEW(id), RT_RCDATA);
            HGLOBAL handle = resource ? LoadResource(self, resource) : nullptr;
            bytes = handle ? LockResource(handle) : nullptr;
            byteCount = resource ? SizeofResource(self, resource) : 0;
            if (!bytes || !byteCount)
                LOG("IQ ERROR: embedded SMAA shader '%s' unavailable (winerr=%lu)", name, GetLastError());
            return bytes && byteCount;
        };

        auto createVs = [&](int id, const char* name, ID3D11VertexShader** out)
        {
            const void* bytes = nullptr;
            DWORD byteCount = 0;
            if (!resourceBytes(id, name, bytes, byteCount))
                return false;
            const HRESULT hr = g_device->CreateVertexShader(bytes, byteCount, nullptr, out);
            if (FAILED(hr))
                LOG("IQ ERROR: SMAA vertex shader '%s' creation failed (0x%08X)", name, (unsigned)hr);
            return SUCCEEDED(hr);
        };
        auto createPs = [&](int id, const char* name, ID3D11PixelShader** out)
        {
            const void* bytes = nullptr;
            DWORD byteCount = 0;
            if (!resourceBytes(id, name, bytes, byteCount))
                return false;
            const HRESULT hr = g_device->CreatePixelShader(bytes, byteCount, nullptr, out);
            if (FAILED(hr))
                LOG("IQ ERROR: SMAA pixel shader '%s' creation failed (0x%08X)", name, (unsigned)hr);
            return SUCCEEDED(hr);
        };

        if (!createVs(IDR_SMAA_EDGES_VS, "edges-vs", &g_smaaEdgesVs) ||
            !createVs(IDR_SMAA_WEIGHTS_VS, "weights-vs", &g_smaaWeightsVs) ||
            !createVs(IDR_SMAA_NEIGHBORHOOD_VS, "neighborhood-vs", &g_smaaNeighborhoodVs) ||
            !createPs(IDR_SMAA_EDGES_PS, "edges-ps", &g_smaaEdgesPs) ||
            !createPs(IDR_SMAA_WEIGHTS_PS, "weights-ps", &g_smaaWeightsPs) ||
            !createPs(IDR_SMAA_NEIGHBORHOOD_PS, "neighborhood-ps", &g_smaaNeighborhoodPs))
            return fail();

        D3D11_SAMPLER_DESC point{};
        point.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        point.AddressU = point.AddressV = point.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        HRESULT hr = g_device->CreateSamplerState(&point, &g_smaaPointSampler);
        if (FAILED(hr))
        {
            LOG("IQ ERROR: SMAA point sampler creation failed (0x%08X)", (unsigned)hr);
            return fail();
        }

        auto createLut = [&](const unsigned char* bytes, UINT width, UINT height, UINT pitch, DXGI_FORMAT format,
                             const char* name, ID3D11Texture2D** texture, ID3D11ShaderResourceView** srv)
        {
            D3D11_TEXTURE2D_DESC d{};
            d.Width = width;
            d.Height = height;
            d.MipLevels = 1;
            d.ArraySize = 1;
            d.Format = format;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_IMMUTABLE;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA init{};
            init.pSysMem = bytes;
            init.SysMemPitch = pitch;
            HRESULT lutHr = g_device->CreateTexture2D(&d, &init, texture);
            if (SUCCEEDED(lutHr))
                lutHr = g_device->CreateShaderResourceView(*texture, nullptr, srv);
            if (FAILED(lutHr))
                LOG("IQ ERROR: SMAA %s lookup texture creation failed (0x%08X)", name, (unsigned)lutHr);
            return SUCCEEDED(lutHr);
        };

        static_assert(sizeof(areaTexBytes) == AREATEX_SIZE);
        static_assert(sizeof(searchTexBytes) == SEARCHTEX_SIZE);
        if (!createLut(areaTexBytes, AREATEX_WIDTH, AREATEX_HEIGHT, AREATEX_PITCH, DXGI_FORMAT_R8G8_UNORM, "area",
                &g_smaaAreaTex, &g_smaaAreaSrv) ||
            !createLut(searchTexBytes, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, SEARCHTEX_PITCH, DXGI_FORMAT_R8_UNORM,
                "search", &g_smaaSearchTex, &g_smaaSearchSrv))
            return fail();

        LOG("IQ: SMAA 1x pipeline ready -- threshold=0.05 search=32 diag=16 "
            "color-edge upstream=71c806a");
        return true;
    }
} // namespace
namespace vr_core
{
    void ReleaseIqChain()
    {
        for (auto*& s : g_iqChainSrv)
        {
            if (s)
                s->Release();
            s = nullptr;
        }
        for (auto*& r : g_iqChainRtv)
        {
            if (r)
                r->Release();
            r = nullptr;
        }
        for (auto*& t : g_iqChain)
        {
            if (t)
                t->Release();
            t = nullptr;
        }
        g_iqChainDesc = {};
    }
} // namespace vr_core
namespace
{
    void ReleaseIqChainSlot(int i)
    {
        if (g_iqChainSrv[i])
        {
            g_iqChainSrv[i]->Release();
            g_iqChainSrv[i] = nullptr;
        }
        if (g_iqChainRtv[i])
        {
            g_iqChainRtv[i]->Release();
            g_iqChainRtv[i] = nullptr;
        }
        if (g_iqChain[i])
        {
            g_iqChain[i]->Release();
            g_iqChain[i] = nullptr;
        }
    }

    // Allocate only the intermediates this exact pass graph needs. The old
    // fixed two-target minimum kept another full XR-eye texture alive for a
    // simple resolve+RCAS path. This depends on pass liveness, not on input
    // resolution, DLSS model, or the dormant stereo-batch experiment.
    bool EnsureIqChain(uint32_t w, uint32_t h, int targetCount)
    {
        const DXGI_FORMAT format = UnormSibling((DXGI_FORMAT)g_xrFormat);
        if (g_iqChainDesc.Width != w || g_iqChainDesc.Height != h || g_iqChainDesc.Format != format)
            ReleaseIqChain();
        if (targetCount < 1 || targetCount > 3)
            return false;
        for (int i = targetCount; i < 3; ++i)
            ReleaseIqChainSlot(i);

        D3D11_TEXTURE2D_DESC d{};
        d.Width = w;
        d.Height = h;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = format;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        for (int i = 0; i < targetCount; ++i)
        {
            if (g_iqChain[i] && g_iqChainRtv[i] && g_iqChainSrv[i])
                continue;
            ReleaseIqChainSlot(i);
            if (FAILED(g_device->CreateTexture2D(&d, nullptr, &g_iqChain[i])) ||
                FAILED(g_device->CreateRenderTargetView(g_iqChain[i], nullptr, &g_iqChainRtv[i])) ||
                FAILED(g_device->CreateShaderResourceView(g_iqChain[i], nullptr, &g_iqChainSrv[i])))
            {
                LOG("IQ ERROR: intermediate chain slot %d creation failed (%ux%u)", i, w, h);
                ReleaseIqChain();
                return false;
            }
        }
        g_iqChainDesc = d;
        return true;
    }
    // Accumulated over the reporting window, in microseconds.
    constinit double g_iqResolveUs = 0.0;
    constinit double g_iqPostUs = 0.0;
    constinit double g_iqDlssWaitUs = 0.0;
    constinit uint32_t g_iqDlssWaitEyes = 0;
    constinit uint32_t g_iqTimedFrames = 0;
    constinit uint32_t g_iqDisjointFrames = 0;
} // namespace
namespace vr_core
{
    // Harvest whichever in-flight slots the GPU has finished with. Never waits.
    void CollectIqTimings()
    {
        if (!g_iqTimerUsable || !g_context)
            return;
        for (auto& slot : g_iqTimer)
        {
            if (!slot.pending || slot.written < 2)
                continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
            if (g_context->GetData(slot.disjoint, &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
                continue; // still busy; try again next frame

            bool complete = true;
            uint64_t values[kIqTimerStamps]{};
            for (uint32_t i = 0; i < slot.written && complete; ++i)
            {
                complete = g_context->GetData(
                               slot.stamps[i], &values[i], sizeof(values[i]), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
            }
            if (!complete)
                continue;

            uint64_t waits[4]{};
            for (uint32_t i = 0; i < 4; ++i)
                if (slot.waitMask & (1u << i))
                    complete &= g_context->GetData(slot.dlssWait[i], &waits[i], sizeof(waits[i]),
                                    D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
            if (!complete)
                continue;

            slot.pending = false;
            if (dj.Disjoint || dj.Frequency == 0)
            {
                ++g_iqDisjointFrames;
                continue;
            }
            const double toUs = 1e6 / static_cast<double>(dj.Frequency);
            for (uint32_t i = 0; i < 4; i += 2)
                if ((slot.waitMask & (3u << i)) == (3u << i) && waits[i + 1] >= waits[i])
                {
                    g_iqDlssWaitUs += (waits[i + 1] - waits[i]) * toUs;
                    ++g_iqDlssWaitEyes;
                }
            // Stamps arrive in threes: start, after the resolve, after the
            // post passes. A partial triple (an eye that failed early) is
            // skipped rather than mis-attributed.
            for (uint32_t base = 0; base + 3 <= slot.written; base += 3)
            {
                g_iqResolveUs += (values[base + 1] - values[base]) * toUs;
                g_iqPostUs += (values[base + 2] - values[base + 1]) * toUs;
            }
            ++g_iqTimedFrames;
        }
    }
} // namespace vr_core
namespace
{
    // One timestamp inside the publish. Out of stamps is not an error: the
    // theatre path runs extra blits and simply goes untimed.
    void IqTimerStamp()
    {
        if (!g_iqTimerArmed || g_iqTimerStampIndex >= kIqTimerStamps)
            return;
        IqTimerSlot& slot = g_iqTimer[g_iqTimerCursor];
        g_context->End(slot.stamps[g_iqTimerStampIndex]);
        ++g_iqTimerStampIndex;
        slot.written = g_iqTimerStampIndex;
    }
} // namespace
namespace vr_core
{
    // The eye publish, in real GPU time, on the same two-second window as the
    // other steady reports. `resolve` is the bicubic/linear expand into headset
    // resolution; `post` is FXAA/SMAA/RCAS layered on top. Both are summed over
    // BOTH eyes, so together they are what this mod costs the GPU each frame
    // beyond the game's own rendering.
    //
    // What to do with the number: `post` IS the price of the second pass. It is
    // paid whenever sharpness > 0, which is every configuration ever shipped or
    // logged, and the sharpness slider in the F1 menu turns it off live. So one
    // sitting and one slider move settles what removing that pass is worth -
    // without guessing, and without another build.
    void ReportIqGpuTiming()
    {
        static uint64_t lastMs = 0;
        const uint64_t nowMs = GetTickCount64();
        if (nowMs - lastMs < 2000)
            return;
        lastMs = nowMs;
        if (!g_iqTimerUsable || !g_iqTimedFrames)
            return;
        const double frames = static_cast<double>(g_iqTimedFrames);
        double dlssCpuUs = 0.0;
        uint32_t dlssEvaluations = 0;
        Dlss_TakeCpuTime(dlssCpuUs, dlssEvaluations);
        const auto raster = g_eyeGpuTiming.Collect(g_context, g_config.upscaler == dlss::kUpscalerDlss,
            g_gameBackbufferDesc.Width, g_gameBackbufferDesc.Height);
        if (raster.eyes)
            LOG("DLSS BUDGET: %s render %ux%u: graphics raster %.3f ms/eye, "
                "input preparation/submission %.3f ms/eye, %u sampled eyes; "
                "GPU intervals include scheduling contention; compare the same scene",
                raster.dlss ? "ON" : "OFF", raster.width, raster.height, raster.rasterUs / raster.eyes / 1000.0,
                raster.prepareUs / raster.eyes / 1000.0, raster.eyes);
        DlssGpuTiming dlssGpu{};
        Dlss_TakeGpuTime(dlssGpu);
        if (g_iqDlssWaitEyes)
            LOG("DLSS BUDGET: graphics residual wait for compute/flow %.3f ms/eye over %u eyes "
                "(already overlaps other work; do not add the full compute cost again)",
                g_iqDlssWaitUs / g_iqDlssWaitEyes / 1000.0, g_iqDlssWaitEyes);
        g_iqDlssWaitUs = 0;
        g_iqDlssWaitEyes = 0;
        if (dlssGpu.evaluations)
            LOG("DLSS BUDGET: compute merge %.3f ms/eye, transformer/evaluate %.3f ms/eye, "
                "%u evaluations (GPU intervals, excludes optical-flow wait)",
                dlssGpu.mergeMicroseconds / dlssGpu.evaluations / 1000.0,
                dlssGpu.evaluateMicroseconds / dlssGpu.evaluations / 1000.0, dlssGpu.evaluations);
        LOG("IQ GPU: eye publish %.3f ms/frame (resolve %.3f + post %.3f, both "
            "eyes) over %u timed frames%s; DLSS CPU %.3f ms/frame inside %u evaluations",
            (g_iqResolveUs + g_iqPostUs) / frames / 1000.0, g_iqResolveUs / frames / 1000.0,
            g_iqPostUs / frames / 1000.0, g_iqTimedFrames,
            g_iqDisjointFrames ? " (some frames discarded: the GPU changed clocks)" : "",
            dlssEvaluations ? dlssCpuUs / frames / 1000.0 : 0.0, dlssEvaluations);
        g_iqResolveUs = 0.0;
        g_iqPostUs = 0.0;
        g_iqTimedFrames = 0;
        g_iqDisjointFrames = 0;
    }
    // Expand one captured eye (src, render resolution) into the XR eye image
    // (dst, headset resolution) with the player's chosen resolve filter, optional
    // anti-aliasing, and optional sharpening. Universal to every title. This
    // function always owns the eye resolve; missing prerequisites fail loudly.
    bool BlitImageQuality(ID3D11Texture2D* src, const D3D11_TEXTURE2D_DESC& srcDesc, ID3D11Texture2D* /*dst*/,
        uint32_t dstW, uint32_t dstH, ID3D11RenderTargetView* dstRtv, bool reconstructed)
    {
        reconstructed = reconstructed && dlss::kEnableDlssFullOutputResolve;
        const bool xrSrgb = IsSrgb((DXGI_FORMAT)g_xrFormat);
        const bool finalPerceptual = !xrSrgb;
        const bool wantSharp = g_config.upscale_filter == 1; // sharp bicubic resolve vs linear
        // Preserve the player's explicit post-AA setting. The combined DLSS
        // resolve is selected only when no additional AA pass was requested.
        const bool wantSmaa = (g_config.aa_mode == 3 || g_config.aa_mode == 4);
        const bool wantFxaa = (g_config.aa_mode == 1 || g_config.aa_mode == 2 || g_config.aa_mode == 4);
        const bool fxaaStrong = g_config.aa_mode == 2 || g_config.aa_mode == 4;
        const bool wantAa = wantSmaa || wantFxaa;
        const bool wantRcas = g_config.sharpness > 0.001f;
        const bool post = wantAa || wantRcas;
        // DLSS already produced perceptual pixels at the destination size.
        // RCAS can consume those directly: the identity resolve otherwise
        // decodes/encodes every pixel and writes a redundant full-size target.
        const bool directRcas = reconstructed && wantRcas && !wantAa && srcDesc.Width == dstW &&
                                srcDesc.Height == dstH && srcDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM;

        // NO FALLBACKS (project policy). The IQ path always renders the eye --
        // "everything off" still runs the linear resolve pass, it never calls
        // back into the stock blit. A genuinely missing prerequisite is a BUG and
        // is surfaced LOUDLY, not hidden by silently reverting to the old look.
        if (!EnsureIqPipeline())
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                LOG("IQ ERROR: shader pipeline unavailable; eye NOT processed");
            }
            return false;
        }
        const DXGI_FORMAT chainFormat = UnormSibling(static_cast<DXGI_FORMAT>(g_xrFormat));
        const bool fusedRcas = reconstructed && wantRcas && !wantAa && !wantSharp && g_iqDlssRcas &&
                               srcDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM &&
                               (chainFormat == DXGI_FORMAT_R8G8B8A8_UNORM || chainFormat == DXGI_FORMAT_B8G8R8A8_UNORM);
        if (wantSmaa && !EnsureSmaaPipeline())
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                LOG("IQ ERROR: SMAA 1x pipeline unavailable; eye NOT processed");
            }
            return false;
        }
        ID3D11ShaderResourceView* srcSrv = AcquireSrcSrv(src, srcDesc);
        if (!srcSrv)
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                LOG("IQ ERROR: source SRV unavailable; eye NOT processed");
            }
            return false;
        }
        const int intermediateCount =
            dlss::ResolveIntermediateCount(wantSmaa, wantFxaa, wantRcas, directRcas || fusedRcas);
        if (intermediateCount == 0)
            ReleaseIqChain();
        else if (!EnsureIqChain(dstW, dstH, intermediateCount))
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                LOG("IQ ERROR: intermediate chain unavailable; eye NOT processed");
            }
            return false;
        }

        // One-time and on-change: prove in the log exactly what the eye pass does.
        {
            const char* aaName = "off";
            if (g_config.aa_mode == 1)
                aaName = "fxaa";
            else if (g_config.aa_mode == 2)
                aaName = "fxaa-strong";
            else if (g_config.aa_mode == 3)
                aaName = "smaa1x";
            else if (g_config.aa_mode == 4)
                aaName = "smaa1x+fxaa-strong";
            static int lf = -99, la = -99;
            static float ls = -1.0f;
            static uint32_t lsw = 0, lsh = 0, ldw = 0, ldh = 0;
            static int lc = -1, li = -1;
            static float lg = -1.0f;
            const int combined = directRcas || fusedRcas ? 1 : 0;
            if (lf != g_config.upscale_filter || la != g_config.aa_mode || ls != g_config.sharpness ||
                lg != g_config.sharpness_noise_gate || lsw != srcDesc.Width || ldw != dstW || lsh != srcDesc.Height ||
                ldh != dstH || lc != combined || li != intermediateCount)
            {
                lf = g_config.upscale_filter;
                la = g_config.aa_mode;
                ls = g_config.sharpness;
                lg = g_config.sharpness_noise_gate;
                lsw = srcDesc.Width;
                ldw = dstW;
                lsh = srcDesc.Height;
                ldh = dstH;
                lc = combined;
                li = intermediateCount;
                LOG("IQ: eye pass active -- resolve=%s aa=%s(%d) sharpen=%.2f gate=%.3f "
                    "rcasGain=2.00 src=%ux%u dst=%ux%u post=%d xrSrgb=%d combined=%d "
                    "intermediates=%d (%.1f MiB allocated)",
                    wantSharp ? "bicubic-a075" : "linear", aaName, g_config.aa_mode, g_config.sharpness,
                    g_config.sharpness_noise_gate, srcDesc.Width, srcDesc.Height, dstW, dstH, post ? 1 : 0,
                    xrSrgb ? 1 : 0, combined, intermediateCount,
                    double(dstW) * dstH * DxgiBytesPerPixel(chainFormat) * intermediateCount / (1024.0 * 1024.0));
            }
        }

        const float srcIsSrgb = IsSrgb(srcDesc.Format) ? 1.0f : 0.0f;

        D3DStateBackup backup;
        backup.Capture(g_context);
        ID3D11Buffer* savedPsCb0 = nullptr;
        ID3D11Buffer* savedVsCb0 = nullptr;
        ID3D11ShaderResourceView* savedExtraSrvs[2]{};
        ID3D11SamplerState* savedPointSampler = nullptr;
        g_context->PSGetConstantBuffers(0, 1, &savedPsCb0);
        if (wantSmaa)
        {
            g_context->VSGetConstantBuffers(0, 1, &savedVsCb0);
            g_context->PSGetShaderResources(1, 2, savedExtraSrvs);
            g_context->PSGetSamplers(1, 1, &savedPointSampler);
        }

        // Shared pipeline state for every pass.
        g_context->RSSetState(g_blitRasterizer);
        g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_blitDepthOff, 0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->GSSetShader(nullptr, nullptr, 0);
        g_context->PSSetSamplers(0, 1, &g_blitSampler);
        if (wantSmaa)
        {
            g_context->PSSetSamplers(1, 1, &g_smaaPointSampler);
            g_context->VSSetConstantBuffers(0, 1, &g_iqCb);
        }
        g_context->PSSetConstantBuffers(0, 1, &g_iqCb);

        auto pass = [&](ID3D11VertexShader* vs, ID3D11PixelShader* ps, ID3D11ShaderResourceView* in0,
                        ID3D11ShaderResourceView* in1, ID3D11ShaderResourceView* in2, ID3D11RenderTargetView* out,
                        uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH, bool outPerceptual, float inIsSrgb)
        {
            IqParams p{};
            p.srcW = (float)inW;
            p.srcH = (float)inH;
            p.dstW = (float)outW;
            p.dstH = (float)outH;
            p.sharpness = g_config.sharpness;
            p.sharpGate = g_config.sharpness_noise_gate;
            p.srcIsSrgb = inIsSrgb;
            p.outPerceptual = outPerceptual ? 1.0f : 0.0f;
            p.aaStrength = fxaaStrong ? 1.0f : 0.0f;
            g_context->UpdateSubresource(g_iqCb, 0, nullptr, &p, 0, 0);
            g_context->OMSetRenderTargets(1, &out, nullptr);
            D3D11_VIEWPORT vp{0, 0, (float)outW, (float)outH, 0, 1};
            g_context->RSSetViewports(1, &vp);
            g_context->VSSetShader(vs, nullptr, 0);
            g_context->PSSetShader(ps, nullptr, 0);
            ID3D11ShaderResourceView* inputs[3] = {in0, in1, in2};
            const UINT inputCount = (in1 || in2) ? 3u : 1u;
            g_context->PSSetShaderResources(0, inputCount, inputs);
            g_context->Draw(3, 0);
            ID3D11ShaderResourceView* nullSrvs[3]{}; // unbind before an input becomes an RTV
            g_context->PSSetShaderResources(0, inputCount, nullSrvs);
        };

        ID3D11PixelShader* resolve = wantSharp ? g_iqResolveSharp : g_iqResolveLinear;
        // Exactly three stamps per eye on BOTH branches, so the collector can
        // read them as fixed triples: before the resolve, after the resolve,
        // after everything. On the single-pass branch the third equals the
        // second and post reads as zero, which is the answer we want it to give.
        IqTimerStamp();
        if (directRcas || fusedRcas)
        {
            IqTimerStamp(); // no separate resampling stage
            pass(g_blitVs, directRcas ? g_iqRcas : g_iqDlssRcas, srcSrv, nullptr, nullptr, dstRtv, srcDesc.Width,
                srcDesc.Height, dstW, dstH, finalPerceptual, 0.0f);
        }
        else if (!post)
        {
            // One pass straight to XR: linear for an sRGB RTV, perceptual for
            // the runtime's non-sRGB fallback formats.
            pass(g_blitVs, resolve, srcSrv, nullptr, nullptr, dstRtv, srcDesc.Width, srcDesc.Height, dstW, dstH,
                finalPerceptual, srcIsSrgb);
            IqTimerStamp();
        }
        else
        {
            int cur = 0;
            pass(g_blitVs, resolve, srcSrv, nullptr, nullptr, g_iqChainRtv[cur], srcDesc.Width, srcDesc.Height, dstW,
                dstH, true, srcIsSrgb);
            IqTimerStamp();

            if (wantSmaa)
            {
                // Preserve resolved color in slot 0; edge data goes to slot 1,
                // official blend weights to slot 2, then slot 1 is reused for
                // neighborhood output if another pass follows.
                pass(g_smaaEdgesVs, g_smaaEdgesPs, g_iqChainSrv[0], nullptr, nullptr, g_iqChainRtv[1], dstW, dstH, dstW,
                    dstH, true, 1.0f);
                pass(g_smaaWeightsVs, g_smaaWeightsPs, g_iqChainSrv[1], g_smaaAreaSrv, g_smaaSearchSrv, g_iqChainRtv[2],
                    dstW, dstH, dstW, dstH, true, 1.0f);

                const bool smaaFinal = !wantFxaa && !wantRcas;
                ID3D11RenderTargetView* smaaOut = smaaFinal ? dstRtv : g_iqChainRtv[1];
                pass(g_smaaNeighborhoodVs, g_smaaNeighborhoodPs, g_iqChainSrv[0], g_iqChainSrv[2], nullptr, smaaOut,
                    dstW, dstH, dstW, dstH, smaaFinal ? finalPerceptual : true, 1.0f);
                cur = 1;
            }

            if (wantFxaa)
            {
                const bool fxaaFinal = !wantRcas;
                const int next = 1 - cur;
                ID3D11RenderTargetView* fxaaOut = fxaaFinal ? dstRtv : g_iqChainRtv[next];
                pass(g_blitVs, g_iqFxaa, g_iqChainSrv[cur], nullptr, nullptr, fxaaOut, dstW, dstH, dstW, dstH,
                    fxaaFinal ? finalPerceptual : true, 1.0f);
                cur = next;
            }
            if (wantRcas)
            {
                pass(g_blitVs, g_iqRcas, g_iqChainSrv[cur], nullptr, nullptr, dstRtv, dstW, dstH, dstW, dstH,
                    finalPerceptual, 1.0f);
            }
        }
        IqTimerStamp();

        backup.Restore(g_context);
        g_context->PSSetConstantBuffers(0, 1, &savedPsCb0);
        if (savedPsCb0)
            savedPsCb0->Release();
        if (wantSmaa)
        {
            g_context->VSSetConstantBuffers(0, 1, &savedVsCb0);
            g_context->PSSetShaderResources(1, 2, savedExtraSrvs);
            g_context->PSSetSamplers(1, 1, &savedPointSampler);
            if (savedVsCb0)
                savedVsCb0->Release();
            for (auto*& srv : savedExtraSrvs)
                if (srv)
                    srv->Release();
            if (savedPointSampler)
                savedPointSampler->Release();
        }
        return true;
    }
} // namespace vr_core
namespace
{
    // One bounded line per reason change or per two seconds, plus the F1
    // status, so a persistent fallback is visible without flooding.
    void NoteDlssFallback(int eye, const char* reason)
    {
        ++g_dlssFallbackEyes;
        const uint64_t nowMs = GetTickCount64();
        const bool changed = strncmp(g_dlssFallbackReason, reason, sizeof(g_dlssFallbackReason) - 1) != 0;
        if (changed || nowMs - g_dlssFallbackLogMs >= 2000)
        {
            g_dlssFallbackLogMs = nowMs;
            snprintf(g_dlssFallbackReason, sizeof(g_dlssFallbackReason), "%s", reason);
            LOG("DLSS: eye %d uses the mod's own resolve: %s", eye, reason);
            char text[256];
            snprintf(text, sizeof(text), "not running: %s", reason);
            SetDlssStatus(text);
        }
    }

    bool EnsureDlssMotionPipeline()
    {
        if (g_dlssMotionPs && g_dlssDebugPs && g_dlssCopyPs && g_dlssMotionCb)
            return true;
        if (g_dlssMotionInitFailed || !g_device || !EnsureBlitPipeline())
            return false;
        auto release = [](auto*& object)
        {
            if (object)
                object->Release();
            object = nullptr;
        };
        release(g_dlssMotionPs);
        release(g_dlssDebugPs);
        release(g_dlssCopyPs);
        release(g_dlssMotionCb);

        if (FAILED(g_device->CreatePixelShader(g_dlssCameraPs, sizeof(g_dlssCameraPs), nullptr, &g_dlssMotionPs)) ||
            FAILED(
                g_device->CreatePixelShader(g_dlssDebugPsCode, sizeof(g_dlssDebugPsCode), nullptr, &g_dlssDebugPs)) ||
            FAILED(g_device->CreatePixelShader(g_dlssCopyPsCode, sizeof(g_dlssCopyPsCode), nullptr, &g_dlssCopyPs)))
        {
            g_dlssMotionInitFailed = true;
            return false;
        }
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = sizeof(DlssMotionParams);
        cb.Usage = D3D11_USAGE_DEFAULT;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(g_device->CreateBuffer(&cb, nullptr, &g_dlssMotionCb)))
        {
            g_dlssMotionInitFailed = true;
            return false;
        }
        LOG("DLSS: camera-motion pipeline ready");
        return true;
    }
} // namespace
namespace vr_core
{
    void ReleaseDlssEyeTextures(DlssEyeState& state)
    {
        // Revoke before Release: pause or a skipped submission can leave an
        // unconsumed result pointing at this texture when resize destroys it.
        g_dlssKicked[static_cast<int>(&state - g_dlssEye)].Reset();
        auto release = [](auto*& object)
        {
            if (object)
                object->Release();
            object = nullptr;
        };
        // Blit() may have cached a view of the output; drop ours first.
        if (state.output)
            ForgetSourceView(state.output);
        release(state.depthCopySrv);
        release(state.depthCopy);
        release(state.colorIn);
        release(state.motionSrv);
        release(state.motionRtv);
        release(state.motion);
        release(state.depthOutRtv);
        release(state.depthOut);
        release(state.output);
        release(state.colorInSrv);
        for (int i = 0; i < 2; ++i)
        {
            release(state.flowInputRtv[i]);
            release(state.flowInput[i]);
        }
        // Shared D3D12 textures: the backend drops its own references too.
        Dlss_ReleaseEyeTextures(static_cast<int>(&state - g_dlssEye));
        state.depthCopyDesc = {};
        state.colorInDesc = {};
        state.outputDesc = {};
        state.depthValid = false;
        state.depthDirect = false;
        state.resetPending = true;
    }
} // namespace vr_core
namespace
{
    // Whether a depth view is the size the eye is rasterized at RIGHT NOW:
    // the backbuffer once Present re-learned it, else the render plan. A
    // live resize binds the new depth views before the first Present at the
    // new size, so this is evaluated on every bind, never stored: the
    // DLSS-4 log classified all four views as "not full raster" during that
    // window and DLSS then had no depth for the rest of the session.
    bool DlssDepthIsFullRaster(const D3D11_TEXTURE2D_DESC& desc)
    {
        if (!desc.Width || !desc.Height)
            return false;
        if (g_gameBackbufferDescValid)
            return desc.Width == g_gameBackbufferDesc.Width && desc.Height == g_gameBackbufferDesc.Height;
        unsigned renderW = 0, renderH = 0, outputW = 0, outputH = 0;
        bool dlss = false, runtimePresent = false;
        D3D_GetRenderPlan(renderW, renderH, outputW, outputH, dlss, runtimePresent);
        return renderW && desc.Width == renderW && desc.Height == renderH;
    }
} // namespace
namespace vr_core
{
    // OM hook, inside an eye. `sceneBound` is true when this bind carried the
    // learned scene-colour target.
    void DlssNoteEyeDepth(ID3D11DepthStencilView* depth, bool sceneBound)
    {
        if (!depth)
            return;
        if (sceneBound)
        {
            if (depth != g_dlssSceneDepthDsv)
            {
                depth->AddRef();
                if (g_dlssSceneDepthDsv)
                    g_dlssSceneDepthDsv->Release();
                g_dlssSceneDepthDsv = depth;
            }
            g_dlssEyeSceneDepthSeen = true;
            return;
        }
        for (int i = 0; i < g_dlssDepthViewCount; ++i)
        {
            if (g_dlssDepthViews[i].view == depth)
            {
                g_dlssDepthViews[i].seenThisFrame = true;
                if (DlssDepthIsFullRaster(g_dlssDepthViews[i].desc))
                    g_dlssEyeLastFullRasterDsv = depth;
                return;
            }
        }
        if (g_dlssDepthViewCount >= kDlssDepthViewSlots)
        {
            g_dlssDepthViewTableFull = true;
            return;
        }
        // First sight of this view: describe it once (the only COM work this
        // hook ever does for depth) and pin it.
        ID3D11Resource* resource = nullptr;
        depth->GetResource(&resource);
        ID3D11Texture2D* texture = nullptr;
        D3D11_TEXTURE2D_DESC desc{};
        if (resource &&
            SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))))
        {
            texture->GetDesc(&desc);
        }
        const bool fullRaster = texture && DlssDepthIsFullRaster(desc);
        if (texture)
            texture->Release();
        if (resource)
            resource->Release();
        depth->AddRef();
        DlssDepthViewEntry& entry = g_dlssDepthViews[g_dlssDepthViewCount++];
        entry.view = depth;
        entry.seenThisFrame = true;
        entry.fullRaster = fullRaster;
        entry.desc = desc;
        if (fullRaster)
            g_dlssEyeLastFullRasterDsv = depth;
    }

    // Eye end, render thread: copy the depth buffer that represents this eye
    // before the other eye or the next frame overwrites it.
    void DlssCaptureEyeDepth(int eye)
    {
        if (eye < 0 || eye > 1 || !VR_DlssWantsEyeCamera() || !g_context || !g_device)
            return;
        DlssEyeState& state = g_dlssEye[eye];
        state.depthValid = false;
        if (!state.published)
            return; // no camera for this eye: the resolve would not run anyway
        bool usedSceneBound = false;
        if (!dlss::ChooseDepthSource(g_dlssEyeSceneDepthSeen && g_dlssSceneDepthDsv != nullptr,
                g_dlssEyeLastFullRasterDsv != nullptr, usedSceneBound))
        {
            static uint64_t loggedMs = 0;
            const uint64_t nowMs = GetTickCount64();
            if (nowMs - loggedMs >= 5000)
            {
                loggedMs = nowMs;
                LOG("DLSS: eye %d saw no usable depth view (%d classified%s; "
                    "raster %ux%u%s)",
                    eye, g_dlssDepthViewCount, g_dlssDepthViewTableFull ? ", table full" : "",
                    g_gameBackbufferDescValid ? g_gameBackbufferDesc.Width : 0u,
                    g_gameBackbufferDescValid ? g_gameBackbufferDesc.Height : 0u,
                    g_gameBackbufferDescValid ? "" : " not yet re-learned");
                for (int i = 0; i < g_dlssDepthViewCount && i < kDlssDepthViewSlots; ++i)
                {
                    LOG("DLSS:   depth view %d: %ux%u fmt %u samples %u", i, g_dlssDepthViews[i].desc.Width,
                        g_dlssDepthViews[i].desc.Height, static_cast<unsigned>(g_dlssDepthViews[i].desc.Format),
                        g_dlssDepthViews[i].desc.SampleDesc.Count);
                }
            }
            return;
        }
        ID3D11DepthStencilView* dsv = usedSceneBound ? g_dlssSceneDepthDsv : g_dlssEyeLastFullRasterDsv;
        ID3D11Resource* resource = nullptr;
        dsv->GetResource(&resource);
        if (!resource)
            return;
        ID3D11Texture2D* texture = nullptr;
        if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))))
        {
            resource->Release();
            return;
        }
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        uint32_t copyFormat = 0, viewFormat = 0;
        const bool formatOk = dlss::DepthCopyFormats(static_cast<uint32_t>(desc.Format), copyFormat, viewFormat);
        const bool shapeOk = desc.SampleDesc.Count == 1 && desc.Width && desc.Height;
        if (!formatOk || !shapeOk)
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                LOG("DLSS: eye depth %ux%u fmt %u samples %u cannot be copied "
                    "(%s); DLSS stays off for this title",
                    desc.Width, desc.Height, static_cast<unsigned>(desc.Format), desc.SampleDesc.Count,
                    formatOk ? "multisampled" : "not a depth format");
            }
            texture->Release();
            resource->Release();
            return;
        }
        // The old per-eye Halo 3 path could borrow this view because it
        // evaluated before the other eye rendered. A stereo batch defers its
        // one evaluation until both eyes exist, so each eye's depth must be
        // retained independently.
        unsigned planRenderW = 0, planRenderH = 0, planOutputW = 0;
        unsigned planOutputH = 0;
        bool plannedDlss = false, runtimePresent = false;
        D3D_GetRenderPlan(planRenderW, planRenderH, planOutputW, planOutputH, plannedDlss, runtimePresent);
        const bool direct = TitleAdapter_GetActiveTitle() == GameTitle::Halo3 && !g_config.dlss_debug_view &&
                            !Dlss_UsesSharedTextures() && (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) &&
                            desc.Format == static_cast<DXGI_FORMAT>(copyFormat);
        if (direct && (!state.depthDirect || state.depthCopy != texture))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = static_cast<DXGI_FORMAT>(viewFormat);
            view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels = 1;
            ID3D11ShaderResourceView* srv = nullptr;
            if (SUCCEEDED(g_device->CreateShaderResourceView(texture, &view, &srv)))
            {
                if (state.depthCopySrv)
                    state.depthCopySrv->Release();
                if (state.depthCopy)
                    state.depthCopy->Release();
                texture->AddRef();
                state.depthCopy = texture;
                state.depthCopySrv = srv;
                state.depthCopyDesc = desc;
                state.depthDirect = true;
            }
        }
        const bool usingDirect = direct && state.depthDirect && state.depthCopy == texture;
        if (!usingDirect && (!state.depthCopy || state.depthDirect || state.depthCopyDesc.Width != desc.Width ||
                                state.depthCopyDesc.Height != desc.Height ||
                                state.depthCopyDesc.Format != static_cast<DXGI_FORMAT>(copyFormat)))
        {
            state.depthDirect = false;
            if (state.depthCopySrv)
            {
                state.depthCopySrv->Release();
                state.depthCopySrv = nullptr;
            }
            if (state.depthCopy)
            {
                state.depthCopy->Release();
                state.depthCopy = nullptr;
            }
            D3D11_TEXTURE2D_DESC copyDesc{};
            copyDesc.Width = desc.Width;
            copyDesc.Height = desc.Height;
            copyDesc.MipLevels = 1;
            copyDesc.ArraySize = 1;
            copyDesc.Format = static_cast<DXGI_FORMAT>(copyFormat);
            copyDesc.SampleDesc.Count = 1;
            copyDesc.Usage = D3D11_USAGE_DEFAULT;
            copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = static_cast<DXGI_FORMAT>(viewFormat);
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            if (FAILED(g_device->CreateTexture2D(&copyDesc, nullptr, &state.depthCopy)) ||
                FAILED(g_device->CreateShaderResourceView(state.depthCopy, &srvDesc, &state.depthCopySrv)))
            {
                LOG("DLSS ERROR: eye %d depth copy (%ux%u fmt %u) could not be created", eye, desc.Width, desc.Height,
                    copyFormat);
                if (state.depthCopySrv)
                {
                    state.depthCopySrv->Release();
                    state.depthCopySrv = nullptr;
                }
                if (state.depthCopy)
                {
                    state.depthCopy->Release();
                    state.depthCopy = nullptr;
                }
                state.depthCopyDesc = {};
                texture->Release();
                resource->Release();
                return;
            }
            state.depthCopyDesc = copyDesc;
        }
        // Whole-subresource copy: the only copy D3D11 allows from a depth
        // buffer, and it needs no unbinding.
        if (!usingDirect)
            g_context->CopySubresourceRegion(state.depthCopy, 0, 0, 0, 0, texture, 0, nullptr);
        state.depthValid = true;
        if (!g_dlssDepthSourceLogged)
        {
            g_dlssDepthSourceLogged = true;
            LOG("DLSS: depth input %s", usingDirect ? "direct engine SRV; no depth copy" : "captured per eye");
            LOG("DLSS: eye depth source is %s: %ux%u fmt %u (copy fmt %u, view fmt %u); "
                "%d depth view(s) classified%s",
                usedSceneBound ? "the depth view bound with the learned scene-colour target"
                               : "the last full-raster depth view bound inside the eye",
                desc.Width, desc.Height, static_cast<unsigned>(desc.Format), copyFormat, viewFormat,
                g_dlssDepthViewCount, g_dlssDepthViewTableFull ? " (table full)" : "");
        }
        texture->Release();
        resource->Release();
    }
} // namespace vr_core
namespace
{
    bool EnsureDlssEyeResources(
        int eye, DlssEyeState& state, const D3D11_TEXTURE2D_DESC& srcDesc, const dlss::OutputSize& out)
    {
        const bool directColor =
            !Dlss_UsesSharedTextures() && g_eyeCacheNativeDlss && TitleAdapter_GetActiveTitle() == GameTitle::Halo3;
        const bool needColorCopy =
            !directColor && (IsSrgb(srcDesc.Format) || !(srcDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS));
        const bool renderMatches = state.motion && state.colorInDesc.Width == srcDesc.Width &&
                                   state.colorInDesc.Height == srcDesc.Height && (!needColorCopy || state.colorIn);
        const bool outputMatches =
            state.output && state.outputDesc.Width == out.width && state.outputDesc.Height == out.height;
        if (renderMatches && outputMatches)
            return true;
        if (state.failedRenderW == srcDesc.Width && state.failedRenderH == srcDesc.Height &&
            state.failedOutputW == out.width && state.failedOutputH == out.height)
            return false;
        // Keep the depth copy: it belongs to the engine's buffer shape.
        ID3D11Texture2D* depthCopy = state.depthCopy;
        ID3D11ShaderResourceView* depthCopySrv = state.depthCopySrv;
        const D3D11_TEXTURE2D_DESC depthCopyDesc = state.depthCopyDesc;
        const bool depthValid = state.depthValid;
        const bool depthDirect = state.depthDirect;
        state.depthCopy = nullptr;
        state.depthCopySrv = nullptr;
        ReleaseDlssEyeTextures(state);
        state.depthCopy = depthCopy;
        state.depthCopySrv = depthCopySrv;
        state.depthCopyDesc = depthCopyDesc;
        state.depthValid = depthValid;
        state.depthDirect = depthDirect;

        D3D11_TEXTURE2D_DESC d{};
        d.Width = srcDesc.Width;
        d.Height = srcDesc.Height;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        bool ok = true;
        D3D11_TEXTURE2D_DESC o = d;
        o.Width = out.width;
        o.Height = out.height;
        o.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        o.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
        if (Dlss_UsesSharedTextures())
        {
            // D3D12 backend: the textures live on the D3D12 heap and are
            // opened here; the eye is always copied into the shared colour
            // input (the game's own target cannot be shared). With object
            // motion the backend also hands out the two flow input frames.
            DlssEyeTextures shared{};
            ok = Dlss_CreateEyeTextures(eye, srcDesc.Width, srcDesc.Height, out.width, out.height,
                needColorCopy ? UnormSibling(srcDesc.Format) : srcDesc.Format, shared);
            if (ok)
            {
                state.colorIn = shared.color;
                state.motion = shared.motion;
                state.depthOut = shared.depth;
                state.output = shared.output;
                state.flowInput[0] = shared.flowInput[0];
                state.flowInput[1] = shared.flowInput[1];
                d.Format = needColorCopy ? UnormSibling(srcDesc.Format) : srcDesc.Format;
                ok = SUCCEEDED(g_device->CreateRenderTargetView(state.motion, nullptr, &state.motionRtv)) &&
                     SUCCEEDED(g_device->CreateShaderResourceView(state.motion, nullptr, &state.motionSrv)) &&
                     SUCCEEDED(g_device->CreateRenderTargetView(state.depthOut, nullptr, &state.depthOutRtv));
                for (int i = 0; i < 2 && ok && state.flowInput[i]; ++i)
                    ok = SUCCEEDED(
                        g_device->CreateRenderTargetView(state.flowInput[i], nullptr, &state.flowInputRtv[i]));
            }
        }
        else
        {
            if (needColorCopy)
            {
                // Every DLSS input carries UAV access as well: NGX's D3D11
                // layer rebuilt a "cache helper" for inputs without it on
                // every evaluation (DLSS-1 log); with the flags the count
                // fell to a handful per session (DLSS-7 log).
                d.Format = UnormSibling(srcDesc.Format);
                d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                ok = SUCCEEDED(g_device->CreateTexture2D(&d, nullptr, &state.colorIn));
            }
            d.Format = DXGI_FORMAT_R16G16_FLOAT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
            ok = ok && SUCCEEDED(g_device->CreateTexture2D(&d, nullptr, &state.motion)) &&
                 SUCCEEDED(g_device->CreateRenderTargetView(state.motion, nullptr, &state.motionRtv)) &&
                 SUCCEEDED(g_device->CreateShaderResourceView(state.motion, nullptr, &state.motionSrv));
            d.Format = DXGI_FORMAT_R32_FLOAT;
            ok = ok && SUCCEEDED(g_device->CreateTexture2D(&d, nullptr, &state.depthOut)) &&
                 SUCCEEDED(g_device->CreateRenderTargetView(state.depthOut, nullptr, &state.depthOutRtv));
            ok = ok && SUCCEEDED(g_device->CreateTexture2D(&o, nullptr, &state.output));
        }
        if (ok && state.colorIn)
            ok = SUCCEEDED(g_device->CreateShaderResourceView(state.colorIn, nullptr, &state.colorInSrv));
        state.colorInDesc = d;
        state.outputDesc = o;
        if (!ok)
        {
            LOG("DLSS ERROR: eye %d textures could not be created (%ux%u -> %ux%u); "
                "this shape is not retried",
                eye, srcDesc.Width, srcDesc.Height, out.width, out.height);
            ReleaseDlssEyeTextures(state);
            state.failedRenderW = srcDesc.Width;
            state.failedRenderH = srcDesc.Height;
            state.failedOutputW = out.width;
            state.failedOutputH = out.height;
            return false;
        }
        state.failedRenderW = state.failedRenderH = 0;
        state.failedOutputW = state.failedOutputH = 0;
        state.resetPending = true;
        if (!g_dlssResourcesLogged || eye == 1)
        {
            g_dlssResourcesLogged = true;
            const double renderMb = static_cast<double>(srcDesc.Width) * srcDesc.Height *
                                    ((state.depthDirect ? 0.0 : DxgiBytesPerPixel(state.depthCopyDesc.Format)) + 4.0 +
                                        4.0 + (state.colorIn ? 4.0 : 0.0) + (state.flowInput[0] ? 12.0 : 0.0)) /
                                    (1024.0 * 1024.0);
            const double outMb = static_cast<double>(out.width) * out.height * 4.0 / (1024.0 * 1024.0);
            LOG("DLSS: eye %d resources: render %ux%u (%s%s%s), output %ux%u%s; "
                "about %.0f MB per eye before DLSS's own allocations",
                eye, srcDesc.Width, srcDesc.Height,
                directColor ? "direct eye colour, " : (needColorCopy ? "eye copied to UNORM, " : ""),
                "motion RG16F + depth R32F",
                Dlss_HasImageMotion(eye) ? ", GPU image motion (private history/pyramids excluded from estimate)"
                                         : (state.flowInput[0] ? ", external optical flow" : ", camera-only motion"),
                out.width, out.height,
                out.exact ? " (exact headset slice)" : " (raster aspect, stretched into the slice)", renderMb + outMb);
        }
        return true;
    }

    // Eye colour -> the optical-flow input frame for this render (the
    // engine's BGRA8 format; the render target view does the channel order).
    // True when the frame was written.
    bool RunDlssFlowCopyPass(DlssEyeState& state, int index, uint32_t width, uint32_t height)
    {
        ID3D11RenderTargetView* target = (index == 0 || index == 1) ? state.flowInputRtv[index] : nullptr;
        if (!target || !state.colorInSrv || !g_dlssCopyPs)
            return false;
        D3DStateBackup backup;
        backup.Capture(g_context);
        ID3D11ShaderResourceView* savedSrv3 = nullptr;
        g_context->PSGetShaderResources(3, 1, &savedSrv3);
        g_context->OMSetRenderTargets(1, &target, nullptr);
        D3D11_VIEWPORT vp{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
        g_context->RSSetViewports(1, &vp);
        g_context->RSSetState(g_blitRasterizer);
        g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_blitDepthOff, 0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetShader(g_blitVs, nullptr, 0);
        g_context->GSSetShader(nullptr, nullptr, 0);
        g_context->PSSetShader(g_dlssCopyPs, nullptr, 0);
        g_context->PSSetShaderResources(3, 1, &state.colorInSrv);
        g_context->Draw(3, 0);
        ID3D11ShaderResourceView* nullSrv = nullptr;
        g_context->PSSetShaderResources(3, 1, &nullSrv);
        ID3D11RenderTargetView* nullRtv = nullptr;
        g_context->OMSetRenderTargets(1, &nullRtv, nullptr);
        backup.Restore(g_context);
        g_context->PSSetShaderResources(3, 1, &savedSrv3);
        if (savedSrv3)
            savedSrv3->Release();
        return true;
    }

    void RunDlssMotionPass(DlssEyeState& state, const dlss::CameraSample& previous, uint32_t width, uint32_t height,
        ID3D11RenderTargetView* motionTarget = nullptr, ID3D11RenderTargetView* depthTarget = nullptr,
        uint32_t viewportX = 0)
    {
        const dlss::CameraSample& cur = state.current;
        DlssMotionParams p{};
        auto put3 = [](float out[4], const float in[3])
        {
            out[0] = in[0];
            out[1] = in[1];
            out[2] = in[2];
            out[3] = 0.0f;
        };
        put3(p.curPos, cur.position);
        put3(p.curRight, cur.right);
        put3(p.curUp, cur.up);
        put3(p.curFwd, cur.forward);
        p.curProj[0] = cur.projection.tanX;
        p.curProj[1] = cur.projection.tanY;
        p.curProj[2] = cur.projection.centerX;
        p.curProj[3] = cur.projection.centerY;
        p.curDepth[0] = cur.projection.depthA;
        p.curDepth[1] = cur.projection.depthB;
        put3(p.prevPos, previous.position);
        put3(p.prevRight, previous.right);
        put3(p.prevUp, previous.up);
        put3(p.prevFwd, previous.forward);
        p.prevProj[0] = previous.projection.tanX;
        p.prevProj[1] = previous.projection.tanY;
        p.prevProj[2] = previous.projection.centerX;
        p.prevProj[3] = previous.projection.centerY;
        p.prevDepth[0] = previous.projection.depthA;
        p.prevDepth[1] = previous.projection.depthB;
        p.size[0] = static_cast<float>(width);
        p.size[1] = static_cast<float>(height);
        // Classic's final full-raster output can be resized from its native
        // world raster. Both have the same proven full-view UV mapping.
        p.size[2] = static_cast<float>(state.depthCopyDesc.Width) / width;
        p.size[3] = static_cast<float>(state.depthCopyDesc.Height) / height;
        p.debug[0] = 1.0f / 16.0f; // +-16 render pixels spans the debug colour range
        p.debug[1] = cur.jitterNdcX;
        p.debug[2] = cur.jitterNdcY;
        p.debug[3] = static_cast<float>(viewportX);
        g_context->UpdateSubresource(g_dlssMotionCb, 0, nullptr, &p, 0, 0);

        D3DStateBackup backup;
        backup.Capture(g_context);
        ID3D11Buffer* savedCb = nullptr;
        ID3D11ShaderResourceView* savedSrv1 = nullptr;
        g_context->PSGetConstantBuffers(0, 1, &savedCb);
        g_context->PSGetShaderResources(1, 1, &savedSrv1);

        ID3D11RenderTargetView* targets[2] = {
            motionTarget ? motionTarget : state.motionRtv, depthTarget ? depthTarget : state.depthOutRtv};
        g_context->OMSetRenderTargets(2, targets, nullptr);
        D3D11_VIEWPORT vp{
            static_cast<float>(viewportX), 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
        g_context->RSSetViewports(1, &vp);
        g_context->RSSetState(g_blitRasterizer);
        g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_blitDepthOff, 0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetShader(g_blitVs, nullptr, 0);
        g_context->GSSetShader(nullptr, nullptr, 0);
        g_context->PSSetShader(g_dlssMotionPs, nullptr, 0);
        g_context->PSSetConstantBuffers(0, 1, &g_dlssMotionCb);
        ID3D11ShaderResourceView* inputs[1] = {state.borrowedCeDepth ? state.borrowedCeDepth : state.depthCopySrv};
        g_context->PSSetShaderResources(0, 1, inputs);
        g_context->PSSetSamplers(0, 1, &g_blitSampler);
        g_context->Draw(3, 0);
        ID3D11ShaderResourceView* nullSrvs[1]{};
        g_context->PSSetShaderResources(0, 1, nullSrvs);
        ID3D11RenderTargetView* nullRtvs[2]{};
        g_context->OMSetRenderTargets(2, nullRtvs, nullptr);

        backup.Restore(g_context);
        g_context->PSSetConstantBuffers(0, 1, &savedCb);
        g_context->PSSetShaderResources(1, 1, &savedSrv1);
        if (savedCb)
            savedCb->Release();
        if (savedSrv1)
            savedSrv1->Release();
    }

    // Diagnostic view: the motion vectors as colour, straight into the eye.
    void RunDlssDebugPass(DlssEyeState& state, ID3D11RenderTargetView* dstRtv, uint32_t dstW, uint32_t dstH)
    {
        D3DStateBackup backup;
        backup.Capture(g_context);
        ID3D11Buffer* savedCb = nullptr;
        ID3D11ShaderResourceView* savedSrv1 = nullptr;
        g_context->PSGetConstantBuffers(0, 1, &savedCb);
        g_context->PSGetShaderResources(1, 1, &savedSrv1);
        g_context->OMSetRenderTargets(1, &dstRtv, nullptr);
        D3D11_VIEWPORT vp{0, 0, static_cast<float>(dstW), static_cast<float>(dstH), 0, 1};
        g_context->RSSetViewports(1, &vp);
        g_context->RSSetState(g_blitRasterizer);
        g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_blitDepthOff, 0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetShader(g_blitVs, nullptr, 0);
        g_context->GSSetShader(nullptr, nullptr, 0);
        g_context->PSSetShader(g_dlssDebugPs, nullptr, 0);
        g_context->PSSetConstantBuffers(0, 1, &g_dlssMotionCb);
        ID3D11ShaderResourceView* inputs[6] = {nullptr, state.motionSrv, nullptr, nullptr, nullptr, nullptr};
        g_context->PSSetShaderResources(0, 6, inputs);
        g_context->PSSetSamplers(0, 1, &g_blitSampler);
        g_context->Draw(3, 0);
        ID3D11ShaderResourceView* nullSrvs[6]{};
        g_context->PSSetShaderResources(0, 6, nullSrvs);
        backup.Restore(g_context);
        g_context->PSSetConstantBuffers(0, 1, &savedCb);
        g_context->PSSetShaderResources(1, 1, &savedSrv1);
        if (savedCb)
            savedCb->Release();
        if (savedSrv1)
            savedSrv1->Release();
    }

    // The DLSS output for one raster: the planned headset picture (native x
    // resolution_scale, the size the launcher shrank the render from) when the
    // raster has its shape and is no larger; otherwise the raster-shaped fit
    // into the slice, for a raster the plan did not produce (the fit is off
    // and MCC chose its own resolution, an old launcher, a supersampled
    // raster, which then runs as DLAA).
    dlss::OutputSize DlssOutputFor(uint32_t renderW, uint32_t renderH, uint32_t sliceW, uint32_t sliceH)
    {
        unsigned planRenderW = 0, planRenderH = 0, planOutW = 0, planOutH = 0;
        bool planDlss = false, runtimePresent = false;
        D3D_GetRenderPlan(planRenderW, planRenderH, planOutW, planOutH, planDlss, runtimePresent);
        dlss::OutputSize out = dlss::PlannedOutput(renderW, renderH, planOutW, planOutH, sliceW, sliceH);
        if (!out.width || !out.height)
            out = dlss::FitOutput(renderW, renderH, sliceW, sliceH);
        return out;
    }
    bool EnsureDlssStereoBatchResources(const dlss::StereoBatchPlan& plan, DXGI_FORMAT sourceFormat);
    bool DlssStereoBatchResourcesMatch(const dlss::StereoBatchPlan& plan, DXGI_FORMAT sourceFormat);
} // namespace
namespace vr_core
{
    // Starts one eye's upscale: colour copy, motion pass and the DLSS
    // submission. Called at the end of that eye's render, so the upscale
    // runs on the compute queue while the game rasters the other eye. False
    // means nothing was submitted and the caller keeps the mod's own resolve
    // for this eye and frame. `debugRtv` is only needed for the motion-vector
    // view, which draws straight into the headset image.
    bool DlssKickEye(int eye, ID3D11Texture2D* src, const D3D11_TEXTURE2D_DESC& srcDesc, uint32_t dstW, uint32_t dstH,
        ID3D11RenderTargetView* debugRtv)
    {
        if (!VR_DlssWantsEyeCamera())
            return false;
        if (!g_config.dlss_debug_view)
        {
            if (g_dlssBatchSuppressed)
                return false;
            unsigned rw = 0, rh = 0, ow = 0, oh = 0;
            bool plannedDlss = false, runtimePresent = false;
            D3D_GetRenderPlan(rw, rh, ow, oh, plannedDlss, runtimePresent);
        }
        auto fallback = [&](const char* why)
        {
            NoteDlssFallback(eye, why);
            return false;
        };
        if (eye < 0 || eye > 1 || !src || !g_device || !g_context || !dstW || !dstH)
            return fallback("no device or eye image");
        if (g_config.dlss_debug_view && !debugRtv)
            return false; // the view needs the headset image; submit-time kick
        if (!dlss::UsesWorldRender(DlssPresentationMode()))
        {
            g_dlssJitterArmed = false;
            return false; // Menus are full-resolution, non-temporal canvases.
        }
        {
            int nativeW = 0, nativeH = 0;
            nativeW = kNativeRenderWidth;
            nativeH = kNativeRenderHeight;
            unsigned rw = 0, rh = 0, ow = 0, oh = 0;
            bool plannedDlss = false, runtimePresent = false;
            D3D_GetRenderPlan(rw, rh, ow, oh, plannedDlss, runtimePresent);
            const auto wanted = dlss::PlanRender(
                nativeW, nativeH, g_config.resolution_scale, g_config.upscaler, g_config.dlss_mode, runtimePresent);
            // The old path allocated DLAA features for the full-size raster
            // during the debounce, then immediately destroyed them on resize.
            if (wanted.dlss &&
                (srcDesc.Width != static_cast<unsigned>(wanted.renderW) ||
                    srcDesc.Height != static_cast<unsigned>(wanted.renderH) ||
                    ow != static_cast<unsigned>(wanted.outputW) || oh != static_cast<unsigned>(wanted.outputH)))
            {
                g_dlssJitterArmed = false;
                return fallback(D3D_CanLiveResize() ? "waiting for the selected render size"
                                                    : "world resize hooks unavailable; native resolution retained");
            }
        }
        // Count only admitted-size world attempts; waiting for a requested
        // resize must never trip the feature failure backoff.
        g_dlssBatchAttemptedThisFrame = true;
        if (!Dlss_EnsureInitialized(g_device))
            return fallback(Dlss_StatusText());
        DlssEyeState& state = g_dlssEye[eye];
        if (!state.published || !state.current.valid)
            return fallback("no eye camera arrived for this eye render "
                            "(the title's hook did not publish, or its "
                            "projection was rejected; see the log)");
        if (!state.depthValid || (!state.borrowedCeDepth && (!state.depthCopy || !state.depthCopySrv)))
            return fallback("no depth buffer was captured for this eye");
        if (!state.borrowedCeDepth &&
            (state.depthCopyDesc.Width != srcDesc.Width || state.depthCopyDesc.Height != srcDesc.Height))
            return fallback("the eye's depth and colour buffers differ in size");
        if (srcDesc.SampleDesc.Count != 1)
            return fallback("the eye image is multisampled");
        dlss::OutputSize out = DlssOutputFor(srcDesc.Width, srcDesc.Height, dstW, dstH);
        if (g_config.dlss_debug_view)
        {
            out.width = srcDesc.Width;
            out.height = srcDesc.Height;
            out.exact = false;
        }
        if (!out.width || !out.height)
            return fallback("no valid DLSS output size");

        const float ratio = static_cast<float>(out.width) / static_cast<float>(srcDesc.Width);
        dlss::Quality order[5];
        const int count = dlss::QualitySearchOrder(dlss::QualityForRatio(ratio), order);
        int quality = -1;
        DlssFeatureDesc cachedFeature{};
        if (Dlss_GetFeatureDesc(eye, cachedFeature) && cachedFeature.renderWidth == srcDesc.Width &&
            cachedFeature.renderHeight == srcDesc.Height && cachedFeature.outputWidth == out.width &&
            cachedFeature.outputHeight == out.height)
            quality = cachedFeature.quality; // shape already admitted; NGX queries belong to resize
        for (int i = 0; i < count && quality < 0; ++i)
        {
            uint32_t minW = 0, minH = 0, maxW = 0, maxH = 0, optW = 0, optH = 0;
            if (!Dlss_QueryRenderRange(
                    out.width, out.height, static_cast<int>(order[i]), minW, minH, maxW, maxH, optW, optH))
                continue;
            if (dlss::RenderSizeAdmitted(srcDesc.Width, srcDesc.Height, minW, minH, maxW, maxH))
                quality = static_cast<int>(order[i]);
        }
        if (g_config.dlss_debug_view)
            quality = static_cast<int>(dlss::Quality::Dlaa);
        if (quality < 0)
            return fallback("the render size fits no DLSS range for this picture; "
                            "change DLSS mode or Resolution scale to retry live");

        DlssFeatureDesc desc{};
        desc.renderWidth = srcDesc.Width;
        desc.renderHeight = srcDesc.Height;
        desc.outputWidth = out.width;
        desc.outputHeight = out.height;
        desc.quality = quality;
        desc.preset = g_config.dlss_preset;
        desc.depthInverted = state.current.projection.depthInverted;
        bool recreated = false;
        char reason[160];
        if (!g_config.dlss_debug_view && !Dlss_EnsureFeature(eye, g_context, desc, recreated, reason, sizeof(reason)))
            return fallback(reason);
        if (!EnsureDlssMotionPipeline())
            return fallback("the motion-vector shader is unavailable");
        if (!EnsureDlssEyeResources(eye, state, srcDesc, out))
            return fallback("DLSS textures could not be created");

        // GPU work from here on. No IQ stamps here: this runs at the end of
        // the eye's render, outside the timed publish; the final stretch in
        // DlssFinishEye is timed exactly like the DLSS-off resolve.
        ID3D11Resource* colorInput = src;
        if (state.colorIn)
        {
            g_context->CopyResource(state.colorIn, src);
            colorInput = state.colorIn;
        }
        const bool reset = recreated || state.resetPending || dlss::ShouldReset(state.current, state.previous);
        const dlss::CameraSample& previous = reset ? state.current : state.previous;
        // The active image-motion backend consumes shared colour directly.
        // This optional copy only belongs to the dormant external flow path.
        const int flowIndex = Dlss_FlowInputIndex(eye);
        const bool flowWritten = flowIndex >= 0 && RunDlssFlowCopyPass(state, flowIndex, srcDesc.Width, srcDesc.Height);
        RunDlssMotionPass(state, previous, srcDesc.Width, srcDesc.Height);

        if (g_config.dlss_debug_view)
        {
            RunDlssDebugPass(state, debugRtv, dstW, dstH);
            g_dlssKicked[eye].Publish(g_preparedFrame.serial, {});
        }
        else
        {
            DlssEvalInputs inputs{};
            inputs.color = colorInput;
            inputs.depth = state.depthOut;
            inputs.motion = state.motion;
            inputs.output = state.output;
            dlss::NdcToJitterPixels(state.current.jitterNdcX, state.current.jitterNdcY, srcDesc.Width, srcDesc.Height,
                inputs.jitterPixelX, inputs.jitterPixelY);
            if (!reset)
                dlss::JitterDeltaPixels(
                    state.current, previous, srcDesc.Width, srcDesc.Height, inputs.jitterDeltaX, inputs.jitterDeltaY);
            inputs.flowInputWritten = flowWritten;
            inputs.frameTimeMs =
                static_cast<float>(static_cast<double>(g_displayPeriodNs.load(std::memory_order_relaxed)) / 1.0e6);
            inputs.reset = reset;
            uint32_t failure = 0;
            if (!Dlss_Evaluate(eye, g_context, inputs, failure))
            {
                state.resetPending = true;
                char text[96];
                snprintf(text, sizeof(text), "DLSS evaluation failed (0x%08X)", failure);
                return fallback(text);
            }
            g_dlssKicked[eye].Publish(g_preparedFrame.serial, {state.output, state.outputDesc});
        }
        if (reset)
            ++g_dlssResets;
        state.resetPending = false;
        ++g_dlssEyesResolvedThisFrame;
        ++g_dlssResolvedEyes;
        g_dlssFallbackReason[0] = '\0';

        const uint64_t nowMs = GetTickCount64();
        if (eye == 1 && nowMs - g_dlssStatusMs >= 2000)
        {
            g_dlssStatusMs = nowMs;
            char text[256];
            const bool jitterApplied = state.current.jitterNdcX != 0.0f || state.current.jitterNdcY != 0.0f;
            snprintf(text, sizeof(text),
                "active: %ux%u -> %ux%u (%s slot, %s), jitter %s, depth %s, "
                "motion %s, resets %llu, fallbacks %llu%s",
                srcDesc.Width, srcDesc.Height, out.width, out.height,
                dlss::QualityName(static_cast<dlss::Quality>(quality)), dlss::PresetName(g_config.dlss_preset),
                !g_config.dlss_jitter
                    ? "off"
                    : (!g_dlssJitterArmed ? "arming" : (jitterApplied ? "on" : "not written by this title")),
                desc.depthInverted ? "reversed" : "standard", Dlss_HasImageMotion(eye) ? "image+camera" : "camera only",
                static_cast<unsigned long long>(g_dlssResets), static_cast<unsigned long long>(g_dlssFallbackEyes),
                g_config.dlss_debug_view ? " [motion-vector view]" : "");
            SetDlssStatus(text);
        }
        return true;
    }
} // namespace vr_core
namespace
{
    bool SameStereoBatchPlan(const dlss::StereoBatchPlan& a, const dlss::StereoBatchPlan& b)
    {
        return a.eyeRenderW == b.eyeRenderW && a.eyeRenderH == b.eyeRenderH && a.atlasRenderW == b.atlasRenderW &&
               a.atlasRenderH == b.atlasRenderH && a.atlasOutputW == b.atlasOutputW && a.atlasOutputH == b.atlasOutputH;
    }

    bool DlssStereoBatchResourcesMatch(const dlss::StereoBatchPlan& plan, DXGI_FORMAT sourceFormat)
    {
        return g_dlssBatch.color && g_dlssBatch.motion && g_dlssBatch.depth && g_dlssBatch.output &&
               g_dlssBatch.eyeOutput[0] && g_dlssBatch.eyeOutput[1] && g_dlssBatch.motionRtv && g_dlssBatch.depthRtv &&
               SameStereoBatchPlan(g_dlssBatch.plan, plan) &&
               g_dlssBatch.inputDesc.Format == UnormSibling(sourceFormat);
    }

    bool EnsureDlssStereoBatchResources(const dlss::StereoBatchPlan& plan, DXGI_FORMAT sourceFormat)
    {
        if (DlssStereoBatchResourcesMatch(plan, sourceFormat))
            return true;

        ReleaseDlssStereoBatch();
        D3D11_TEXTURE2D_DESC input{};
        input.Width = static_cast<UINT>(plan.atlasRenderW);
        input.Height = static_cast<UINT>(plan.atlasRenderH);
        input.MipLevels = input.ArraySize = input.SampleDesc.Count = 1;
        input.Usage = D3D11_USAGE_DEFAULT;
        input.Format = UnormSibling(sourceFormat);
        input.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bool ok = SUCCEEDED(g_device->CreateTexture2D(&input, nullptr, &g_dlssBatch.color));

        D3D11_TEXTURE2D_DESC field = input;
        field.Format = DXGI_FORMAT_R16G16_FLOAT;
        field.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
        ok = ok && SUCCEEDED(g_device->CreateTexture2D(&field, nullptr, &g_dlssBatch.motion)) &&
             SUCCEEDED(g_device->CreateRenderTargetView(g_dlssBatch.motion, nullptr, &g_dlssBatch.motionRtv));
        field.Format = DXGI_FORMAT_R32_FLOAT;
        ok = ok && SUCCEEDED(g_device->CreateTexture2D(&field, nullptr, &g_dlssBatch.depth)) &&
             SUCCEEDED(g_device->CreateRenderTargetView(g_dlssBatch.depth, nullptr, &g_dlssBatch.depthRtv));

        D3D11_TEXTURE2D_DESC output = input;
        output.Width = static_cast<UINT>(plan.atlasOutputW);
        output.Height = static_cast<UINT>(plan.atlasOutputH);
        output.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        output.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
        ok = ok && SUCCEEDED(g_device->CreateTexture2D(&output, nullptr, &g_dlssBatch.output));
        D3D11_TEXTURE2D_DESC eyeOutput = output;
        eyeOutput.Width = static_cast<UINT>(plan.eyeOutputW);
        eyeOutput.Height = static_cast<UINT>(plan.eyeOutputH);
        eyeOutput.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ok = ok && SUCCEEDED(g_device->CreateTexture2D(&eyeOutput, nullptr, &g_dlssBatch.eyeOutput[0])) &&
             SUCCEEDED(g_device->CreateTexture2D(&eyeOutput, nullptr, &g_dlssBatch.eyeOutput[1]));
        if (!ok)
        {
            ReleaseDlssStereoBatch();
            return false;
        }
        g_dlssBatch.inputDesc = input;
        g_dlssBatch.outputDesc = output;
        g_dlssBatch.eyeOutputDesc = eyeOutput;
        g_dlssBatch.plan = plan;
        const double bytes = static_cast<double>(plan.atlasRenderW) * plan.atlasRenderH * 16.0 +
                             static_cast<double>(plan.atlasOutputW) * plan.atlasOutputH * 8.0;
        LOG("DLSS: stereo batch resources: two %dx%d eyes -> one %dx%d atlas -> "
            "two %dx%d reconstructed halves; %.1f MiB before NGX",
            plan.eyeRenderW, plan.eyeRenderH, plan.atlasRenderW, plan.atlasRenderH, plan.eyeOutputW, plan.eyeOutputH,
            bytes / (1024.0 * 1024.0));
        return true;
    }
} // namespace
namespace vr_core
{
    // Every Present exit, including flat/no-render/failed submissions. Retire
    // results and advance history only for a completed, consumed eye pair.
    void DlssEndFrame()
    {
        // No eye may retain a borrowed table pointer across frame retirement.
        // A full table used to pin obsolete DSVs for the entire title session,
        // preventing replacement scene buffers from ever being classified.
        g_dlssEyeLastFullRasterDsv = nullptr;
        g_dlssDepthViewCount = static_cast<int>(dlss::FinishDepthViewFrame(g_dlssDepthViews,
            static_cast<size_t>(g_dlssDepthViewCount), g_dlssDepthViewTableFull,
            [](DlssDepthViewEntry& entry)
            {
                if (entry.view)
                    entry.view->Release();
            }));
        if (g_dlssDepthViewCount < kDlssDepthViewSlots)
            g_dlssDepthViewTableFull = false;
        static int lastDisabledReason = -1;
        const bool outputsConsumed =
            !g_dlssKicked[0].Ready(g_preparedFrame.serial) && !g_dlssKicked[1].Ready(g_preparedFrame.serial);
        g_dlssKicked[0].Reset();
        g_dlssKicked[1].Reset();
        g_dlssBatch.eyePrepared[0] = g_dlssBatch.eyePrepared[1] = false;
        g_dlssBatch.reset = false;
        if (!VR_DlssWantsEyeCamera())
        {
            g_dlssBatchAttemptedThisFrame = false;
            g_dlssBatchFailedFrames = 0;
            g_dlssBatchSuppressed = false;
            if (g_dlssEye[0].output || g_dlssEye[1].output || g_dlssSceneDepthDsv || g_dlssDepthViewCount)
            {
                ReleaseDlssResources();
                LOG("DLSS: disabled or optional runtime absent; resources released");
            }
            g_dlssJitterArmed = false;
            g_dlssEyesResolvedThisFrame = 0;
            const int disabledReason = g_config.upscaler != dlss::kUpscalerDlss
                                           ? 0
                                           : (!dlss::TitleHasCameraContract(TitleAdapter_GetActiveTitle()) ? 2 : 1);
            if (disabledReason != lastDisabledReason)
            {
                lastDisabledReason = disabledReason;
                SetDlssStatus(disabledReason == 2
                                  ? "not running: this title has no verified DLSS camera adapter; full-size VR resolve"
                              : disabledReason == 1 ? "unavailable: optional nvngx_dlss.dll missing; normal VR resolve"
                                                    : "off");
            }
            return;
        }
        lastDisabledReason = -1;
        if (g_dlssBatchAttemptedThisFrame && !g_config.dlss_debug_view)
        {
            if (g_dlssEyesResolvedThisFrame == 2)
            {
                g_dlssBatchFailedFrames = 0;
            }
            else if (!g_dlssBatchSuppressed && ++g_dlssBatchFailedFrames >= 16)
            {
                g_dlssBatchSuppressed = true;
                ReleaseDlssResources();
                LOG("DLSS: eye resolve failed for 16 consecutive admitted "
                    "frames; releasing it and requesting the full-resolution "
                    "stock resolve (change a DLSS setting to retry)");
                SetDlssStatus("not running: persistent eye-resolve failure; "
                              "returning to full resolution (change a DLSS setting to retry)");
            }
        }
        g_dlssBatchAttemptedThisFrame = false;
        if (g_config.dlss_debug_view && g_dlssBatch.output)
        {
            Dlss_ReleaseFeatures();
            ReleaseDlssStereoBatch();
            LOG("DLSS: stereo batch released while the motion diagnostic is active");
        }
        g_dlssJitterArmed = outputsConsumed && g_dlssEyesResolvedThisFrame == 2;
        g_dlssEyesResolvedThisFrame = 0;
        for (DlssEyeState& state : g_dlssEye)
        {
            if (g_dlssJitterArmed && state.published && state.current.valid)
                state.previous = state.current;
            else
                state.resetPending = true;
            state.published = false;
            state.depthValid = false;
        }
        ++g_dlssJitterPhase;
    }
    // Copy the lower band of the title's finished backbuffer. The band shares
    // the backbuffer's format and covers full width, so its U is the screen's U
    // and the text keeps the horizontal position the title gave it.
    bool CaptureTheaterSubtitleBand(ID3D11Texture2D* backbuffer, const D3D11_TEXTURE2D_DESC& backbufferDesc)
    {
        if (!g_device || !g_context || !backbuffer || !backbufferDesc.Width || !backbufferDesc.Height ||
            // A multisampled backbuffer cannot be region-copied; the theatre
            // keeps its stereo image and this feature stays off.
            backbufferDesc.SampleDesc.Count != 1)
            return false;
        const float bandFraction = std::clamp(g_config.cutscene_theater_subtitle_band, 0.05f, 1.0f);
        const UINT bandHeight = static_cast<UINT>(static_cast<float>(backbufferDesc.Height) * bandFraction);
        if (!bandHeight || bandHeight > backbufferDesc.Height)
            return false;
        if (g_theaterSubtitleBandDesc.Width != backbufferDesc.Width || g_theaterSubtitleBandDesc.Height != bandHeight ||
            g_theaterSubtitleBandDesc.Format != backbufferDesc.Format)
        {
            ReleaseTheaterSubtitleBand();
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = backbufferDesc.Width;
            desc.Height = bandHeight;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            // CopySubresourceRegion needs an identical format, so the band is
            // created from the backbuffer's own. A typeless backbuffer would
            // fail the view below and disable only this feature.
            desc.Format = backbufferDesc.Format;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            HRESULT hr = g_device->CreateTexture2D(&desc, nullptr, &g_theaterSubtitleBand);
            if (SUCCEEDED(hr))
                hr = g_device->CreateShaderResourceView(g_theaterSubtitleBand, nullptr, &g_theaterSubtitleBandSrv);
            if (FAILED(hr))
            {
                LOG("cutscene theatre: subtitle band unavailable "
                    "(HRESULT 0x%08X); the theatre keeps its stereo image and "
                    "the title's subtitles stay on the monitor only",
                    static_cast<unsigned>(hr));
                ReleaseTheaterSubtitleBand();
                return false;
            }
            g_theaterSubtitleBandDesc = desc;
            LOG("cutscene theatre: subtitle band armed (%ux%u from the bottom "
                "%.0f%% of the %ux%u backbuffer)%s",
                desc.Width, desc.Height, bandFraction * 100.0f, backbufferDesc.Width, backbufferDesc.Height,
                g_config.cutscene_theater_subtitle_debug ? "; DIAGNOSTIC view on - the strip is painted unfiltered"
                                                         : "");
        }
        D3D11_BOX box{};
        box.left = 0;
        box.right = backbufferDesc.Width;
        box.top = backbufferDesc.Height - bandHeight;
        box.bottom = backbufferDesc.Height;
        box.front = 0;
        box.back = 1;
        g_context->CopySubresourceRegion(g_theaterSubtitleBand, 0, 0, 0, 0, backbuffer, 0, &box);
        g_theaterSubtitleBandStartV = 1.0f - bandFraction;
        return true;
    }

    void ReleaseTheaterProjectionResources()
    {
        ReleaseTheaterResolvedResources();
        ReleaseTheaterSubtitleBand();
        for (int eye = 0; eye < 2; ++eye)
        {
            if (g_theaterDirectSourceSrv[eye])
                g_theaterDirectSourceSrv[eye]->Release();
            g_theaterDirectSourceSrv[eye] = nullptr;
            g_theaterDirectSourceKey[eye] = nullptr;
        }
        auto release = [](auto*& object)
        {
            if (object)
                object->Release();
            object = nullptr;
        };
        release(g_theaterProjectionVs);
        release(g_theaterProjectionPs);
        release(g_theaterProjectionCb);
    }
    bool EnsureTheaterProjectionResources(uint32_t width, uint32_t height, bool needResolved)
    {
        if (!width || !height || !g_device || !EnsureBlitPipeline())
            return false;
        if (!g_theaterProjectionVs || !g_theaterProjectionPs || !g_theaterProjectionCb)
        {
            static const char* shader = R"(
Texture2D srcTex : register(t0);
Texture2D bandTex : register(t1);
SamplerState smp : register(s0);
cbuffer TheaterProjectionParams : register(b0)
{
    float4 clipPositions[4];
    float4 colorParams;
    float4 matteParams;
    float4 bandParams;
}
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut vs_main(uint id : SV_VertexID)
{
    VSOut o; o.pos=clipPositions[id];
    o.uv=float2(id & 1, (id >> 1) & 1); return o;
}
float lin(float c)
{
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
float enc(float c)
{
    c = max(c, 0.0);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0/2.4) - 0.055;
}
float3 recode(float3 c)
{
    if (colorParams.z < 0.5)
    {
        if (colorParams.x < 0.5)
            c = float3(lin(c.r), lin(c.g), lin(c.b));
        if (colorParams.y > 0.5)
            c = float3(enc(c.r), enc(c.g), enc(c.b));
    }
    return c;
}
float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float4 ps_main(VSOut i) : SV_Target
{
    // Cine bars: the strips of the authored frame a monitor never shows are
    // opaque black. Subtitles still draw over them, exactly as on a monitor.
    float3 outColor = 0.0;
    bool inBar = matteParams.z > 0.5 &&
        (i.uv.y < matteParams.x || i.uv.y > matteParams.y);
    if (!inBar)
        outColor = recode(srcTex.SampleLevel(smp, i.uv, 0).rgb);
    if (matteParams.w >= 0.0 && i.uv.y >= matteParams.w)
    {
        // The band came from the same frame at the same U, so the text lands
        // where the title put it. Take only bright, near-neutral pixels that
        // are also clearly brighter than what the stereo image already shows
        // there: that is glyph over background, and nothing else in a frame
        // satisfies all three at once.
        float2 bandUv = float2(
            i.uv.x, (i.uv.y - matteParams.w) / max(1.0 - matteParams.w, 0.0001));
        float3 b = recode(bandTex.SampleLevel(smp, bandUv, 0).rgb);
        // Diagnostic: show the captured strip exactly as it arrived, so a
        // headset report can say whether the text is in it at all, and where.
        if (bandParams.x > 0.5)
            return float4(b, 1.0);
        float bandLuma = luma(b);
        float chroma = max(max(b.r, b.g), b.b) - min(min(b.r, b.g), b.b);
        float key = saturate((bandLuma - 0.72) / 0.28) *
                    saturate((0.16 - chroma) / 0.16) *
                    saturate((bandLuma - luma(outColor)) * 4.0);
        outColor = lerp(outColor, b, key);
    }
    return float4(outColor, 1.0);
}
)";
            ID3DBlob* errors = nullptr;
            auto compile = [&](const char* entry, const char* target)
            {
                ID3DBlob* blob = nullptr;
                const HRESULT result = D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, entry, target,
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
                if (FAILED(result))
                    LOG("theatre projection shader '%s' failed: %s", entry,
                        errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no compiler text");
                if (errors)
                {
                    errors->Release();
                    errors = nullptr;
                }
                return blob;
            };
            ID3DBlob* blob = compile("vs_main", "vs_5_0");
            HRESULT hr = blob ? g_device->CreateVertexShader(
                                    blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_theaterProjectionVs)
                              : E_FAIL;
            if (blob)
                blob->Release();
            blob = SUCCEEDED(hr) ? compile("ps_main", "ps_5_0") : nullptr;
            if (blob)
            {
                hr = g_device->CreatePixelShader(
                    blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_theaterProjectionPs);
                blob->Release();
            }
            D3D11_BUFFER_DESC cb{};
            cb.ByteWidth = sizeof(TheaterProjectionParams);
            cb.Usage = D3D11_USAGE_DEFAULT;
            cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            if (SUCCEEDED(hr))
                hr = g_device->CreateBuffer(&cb, nullptr, &g_theaterProjectionCb);
            if (FAILED(hr))
            {
                LOG("theatre projection pipeline unavailable (HRESULT 0x%08X)", static_cast<unsigned>(hr));
                ReleaseTheaterProjectionResources();
                return false;
            }
        }

        if (!needResolved)
        {
            ReleaseTheaterResolvedResources();
            return true;
        }
        const DXGI_FORMAT format = static_cast<DXGI_FORMAT>(g_xrFormat);
        if (g_theaterResolvedDesc.Width != width || g_theaterResolvedDesc.Height != height ||
            g_theaterResolvedDesc.Format != format)
        {
            ReleaseTheaterResolvedResources();
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        for (int eye = 0; eye < 2; ++eye)
        {
            if (g_theaterResolved[eye] && g_theaterResolvedRtv[eye] && g_theaterResolvedSrv[eye])
                continue;
            HRESULT hr = g_device->CreateTexture2D(&desc, nullptr, &g_theaterResolved[eye]);
            if (SUCCEEDED(hr))
                hr = g_device->CreateRenderTargetView(g_theaterResolved[eye], nullptr, &g_theaterResolvedRtv[eye]);
            if (SUCCEEDED(hr))
                hr = g_device->CreateShaderResourceView(g_theaterResolved[eye], nullptr, &g_theaterResolvedSrv[eye]);
            if (FAILED(hr))
            {
                LOG("theatre projection eye %d target unavailable "
                    "(HRESULT 0x%08X)",
                    eye, static_cast<unsigned>(hr));
                ReleaseTheaterProjectionResources();
                return false;
            }
        }
        g_theaterResolvedDesc = desc;
        return true;
    }

    bool CompositeTheaterProjectionEye(uint32_t targetEye, ID3D11ShaderResourceView* sourceSrv,
        const D3D11_TEXTURE2D_DESC& sourceDesc, bool sourceAlreadyXrEncoded, const XrView& nativeView,
        ID3D11RenderTargetView* targetRtv)
    {
        if (targetEye > 1 || !sourceSrv || !targetRtv || !g_theaterProjectionVs || !g_theaterProjectionPs ||
            !g_theaterProjectionCb)
            return false;
        const float width = g_config.cutscene_theater_width_m;
        const float height = CutsceneTheaterHeightFromAspect(width, g_cutsceneTheaterAuthoredAspect);
        const XrVector3f offset = Rotate(g_centerRot, {0.0f, 0.0f, -g_config.cutscene_theater_distance_m});
        const float eyePosition[3]{nativeView.pose.position.x, nativeView.pose.position.y, nativeView.pose.position.z};
        const float eyeOrientation[4]{nativeView.pose.orientation.x, nativeView.pose.orientation.y,
            nativeView.pose.orientation.z, nativeView.pose.orientation.w};
        const float screenCenter[3]{g_centerPos.x + offset.x, g_centerPos.y + offset.y, g_centerPos.z + offset.z};
        const float screenOrientation[4]{g_centerRot.x, g_centerRot.y, g_centerRot.z, g_centerRot.w};
        const float fov[4]{
            nativeView.fov.angleLeft, nativeView.fov.angleRight, nativeView.fov.angleUp, nativeView.fov.angleDown};
        CutsceneTheaterClipVertex vertices[4]{};
        if (!BuildCutsceneTheaterProjectionQuad(
                eyePosition, eyeOrientation, screenCenter, screenOrientation, width, height, fov, vertices))
            return false;

        TheaterProjectionParams params{};
        for (int i = 0; i < 4; ++i)
        {
            params.clipPositions[i][0] = vertices[i].x;
            params.clipPositions[i][1] = vertices[i].y;
            params.clipPositions[i][2] = vertices[i].w * 0.5f;
            params.clipPositions[i][3] = vertices[i].w;
        }
        params.color[0] = IsSrgb(sourceDesc.Format) ? 1.0f : 0.0f;
        params.color[1] = IsSrgb(static_cast<DXGI_FORMAT>(g_xrFormat)) ? 0.0f : 1.0f;
        params.color[2] = sourceAlreadyXrEncoded ? 1.0f : 0.0f;
        const CutsceneTheaterMatte matte = ComputeCutsceneTheaterMatte(g_cutsceneTheaterAuthoredAspect,
            g_config.cutscene_theater_matte_aspect, g_config.cutscene_theater_matte_offset);
        params.matte[0] = matte.vMin;
        params.matte[1] = matte.vMax;
        params.matte[2] = matte.active ? 1.0f : 0.0f;
        const bool bandReady =
            !g_nativeTheatreCaptionReady && g_theaterSubtitleBandSrv && g_theaterSubtitleBandStartV >= 0.0f;
        params.matte[3] = bandReady ? g_theaterSubtitleBandStartV : -1.0f;
        params.band[0] = bandReady && g_config.cutscene_theater_subtitle_debug ? 1.0f : 0.0f;
        g_context->UpdateSubresource(g_theaterProjectionCb, 0, nullptr, &params, 0, 0);

        D3DStateBackup backup;
        backup.Capture(g_context);
        ID3D11Buffer* savedVsCb0 = nullptr;
        ID3D11Buffer* savedPsCb0 = nullptr;
        // D3DStateBackup restores pixel shader resource slot 0 only, and the
        // subtitle band binds slot 1. Return that slot to the title itself.
        ID3D11ShaderResourceView* savedPsSrv1 = nullptr;
        g_context->VSGetConstantBuffers(0, 1, &savedVsCb0);
        g_context->PSGetConstantBuffers(0, 1, &savedPsCb0);
        g_context->PSGetShaderResources(1, 1, &savedPsSrv1);
        const float black[4]{0, 0, 0, 1};
        g_context->ClearRenderTargetView(targetRtv, black);
        g_context->OMSetRenderTargets(1, &targetRtv, nullptr);
        D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(g_stereoW), static_cast<float>(g_stereoH), 0.0f, 1.0f};
        g_context->RSSetViewports(1, &viewport);
        g_context->RSSetState(g_blitRasterizer);
        g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_blitDepthOff, 0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        g_context->VSSetShader(g_theaterProjectionVs, nullptr, 0);
        g_context->VSSetConstantBuffers(0, 1, &g_theaterProjectionCb);
        g_context->GSSetShader(nullptr, nullptr, 0);
        g_context->PSSetShader(g_theaterProjectionPs, nullptr, 0);
        g_context->PSSetConstantBuffers(0, 1, &g_theaterProjectionCb);
        ID3D11ShaderResourceView* projectionSrvs[2]{sourceSrv, bandReady ? g_theaterSubtitleBandSrv : nullptr};
        g_context->PSSetShaderResources(0, 2, projectionSrvs);
        g_context->PSSetSamplers(0, 1, &g_blitSampler);
        g_context->Draw(4, 0);
        backup.Restore(g_context);
        g_context->VSSetConstantBuffers(0, 1, &savedVsCb0);
        g_context->PSSetConstantBuffers(0, 1, &savedPsCb0);
        g_context->PSSetShaderResources(1, 1, &savedPsSrv1);
        if (savedVsCb0)
            savedVsCb0->Release();
        if (savedPsCb0)
            savedPsCb0->Release();
        if (savedPsSrv1)
            savedPsSrv1->Release();
        static bool logged = false;
        if (!logged && targetEye == 1)
        {
            logged = true;
            LOG("cutscene theatre: native-FOV two-view projection active; "
                "no eye-selective layers or private theatre swapchain");
        }
        return true;
    }
#include "vr_swapchain_rtv.inl"
} // namespace vr_core
namespace
{
    // Procedural cyborg-HUD frame: SDF lines only (border, corner brackets,
    // gapped reticle, tick ladders, 7-segment "NN.Nx" readout). No textures.
    bool EnsureVisorPipeline()
    {
        if (g_visorPs && g_visorPsLinearize && g_visorConstants)
            return true;
        static bool failed = false;
        if (failed || !g_device || !EnsureScopeUploadPipeline())
            return false;
        static const char* source = R"(
Texture2D srcTex : register(t0);
SamplerState smp : register(s0);
cbuffer Visor : register(b0) { float4 params; int4 digits; float4 aim; float4 rect; }; // opacity, aspect, feather, - | tens, ones, tenths, - | aim x, y (virtual window units), visible, reticle divisor | visible uv min, max
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
static const uint kSeg[10]={0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};
float lin(float c) { return c<=0.04045 ? c/12.92 : pow((c+0.055)/1.055,2.4); }
float sdBox(float2 p,float2 b){ float2 d=abs(p)-b; return length(max(d,0))+min(max(d.x,d.y),0); }
float sdSeg(float2 p,float2 a,float2 b){ float2 pa=p-a,ba=b-a; float h=saturate(dot(pa,ba)/dot(ba,ba)); return length(pa-ba*h); }
float lineMask(float d,float w){ float aa=max(fwidth(d),1e-4); return 1-smoothstep(w-aa,w+aa,d); }
float digitDist(float2 p,int digit,float w,float h){
    if(digit<0||digit>9) return 1e3;
    uint m=kSeg[digit]; float d=1e3; float hh=h*0.5;
    if(m&1)  d=min(d,sdSeg(p,float2(0,0),float2(w,0)));
    if(m&2)  d=min(d,sdSeg(p,float2(w,0),float2(w,hh)));
    if(m&4)  d=min(d,sdSeg(p,float2(w,hh),float2(w,h)));
    if(m&8)  d=min(d,sdSeg(p,float2(0,h),float2(w,h)));
    if(m&16) d=min(d,sdSeg(p,float2(0,hh),float2(0,h)));
    if(m&32) d=min(d,sdSeg(p,float2(0,0),float2(0,hh)));
    if(m&64) d=min(d,sdSeg(p,float2(0,hh),float2(w,hh)));
    return d;
}
float4 visor(float2 uv,bool decode){
    float aspect=params.y;
    uint sw,sh; srcTex.GetDimensions(sw,sh);
    float sa=(float)sw/max(1.0,(float)sh);
    float2 scale=sa>aspect ? float2(aspect/sa,1) : float2(1,sa/aspect);
    float3 rgb=srcTex.Sample(smp,0.5+(uv-0.5)*scale).rgb;
    if(decode) rgb=float3(lin(rgb.r),lin(rgb.g),lin(rgb.b));
    float2 pv=(uv-0.5)*float2(aspect,1);         // the virtual window (the picture), height = 1, y down
    float2 extent=max(rect.zw-rect.xy,1e-4);      // F1 round C: the frame is the visible part
    float2 wuv=(uv-rect.xy)/extent;
    float faspect=aspect*extent.x/extent.y;
    float2 p=(wuv-0.5)*float2(faspect,1);        // the visible window, height = 1, y down
    float2 halfSize=float2(faspect*0.5,0.5);
    rgb*=0.95+0.05*sin(wuv.y*3.14159*384.0);      // faint scan lines
    rgb*=1.0-0.30*smoothstep(0.55,1.0,length(p/halfSize));
    float f=0;
    f=max(f,lineMask(abs(sdBox(p,halfSize-0.035)),0.0025));          // border
    float2 q=abs(p); float2 c=halfSize-0.015;                         // corner brackets
    f=max(f,lineMask(min(sdSeg(q,float2(c.x-0.10,c.y),c),sdSeg(q,float2(c.x,c.y-0.10),c)),0.006));
    float2 ap=(pv-aim.xy)*max(aim.w,0.5); float2 aq=abs(ap);                             // gun reticle at its aim point
    float r=length(ap);
    float g=lineMask(abs(r-0.05),0.0022);
    g=max(g,lineMask(min(sdSeg(aq,float2(0.014,0),float2(0.04,0)),sdSeg(aq,float2(0,0.014),float2(0,0.04))),0.002));
    g=max(g,lineMask(r,0.003));
    f=max(f,g*aim.z);
    float lx=halfSize.x-0.075;                                         // tick ladders
    float cellY=round(p.y/0.04)*0.04;
    float major=abs(frac(cellY/0.16+0.5)-0.5)<0.01 ? 1 : 0;
    float len=major>0 ? 0.03 : 0.015;
    f=max(f,lineMask(sdSeg(float2(q.x,p.y),float2(lx-len,cellY),float2(lx,cellY)),0.0018)*step(abs(p.y),0.33));
    float w=0.024,h=0.046;                                            // readout "NN.Nx"
    float2 o=float2(halfSize.x-0.205,halfSize.y-0.105);
    float dd=digitDist(p-o,digits.x,w,h);
    dd=min(dd,digitDist(p-o-float2(0.036,0),digits.y,w,h));
    dd=min(dd,length(p-o-float2(0.068,h)));
    dd=min(dd,digitDist(p-o-float2(0.080,0),digits.z,w,h));
    float2 xq=p-o-float2(0.118,h*0.45);
    dd=min(dd,min(sdSeg(xq,float2(0,0),float2(0.02,h*0.55)),sdSeg(xq,float2(0.02,0),float2(0,h*0.55))));
    if(params.w<0.5) f=max(f,lineMask(dd,0.0035));
    // Soft rounded window: a superellipse whose image (and frame) fades
    // gradually into the view over the outer feather band. No hard edge.
    float2 n=abs(p/halfSize);
    float shape=pow(pow(n.x,4.0)+pow(n.y,4.0),0.25);
    float feather=clamp(params.z,0.02,0.5);
    float alpha=1-smoothstep(1.0-feather,1.0,shape);
    float3 hud=params.w>0.5 ? float3(0.30,0.85,1.0) : float3(1.0,0.38,0.22);
    if(params.w>0.5) rgb*=float3(0.90,1.0,1.06);                       // slight cockpit-glass tint
    rgb=lerp(rgb,hud,saturate(f*params.x*alpha));
    return float4(rgb*alpha,alpha);
}
float4 ps_visor(VSOut i):SV_Target { return visor(i.uv,false); }
float4 ps_visor_linearize(VSOut i):SV_Target { return visor(i.uv,true); }
)";
        HRESULT hr = S_OK;
        auto compile = [&](const char* entry) -> ID3DBlob*
        {
            ID3DBlob *blob = nullptr, *errors = nullptr;
            hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr, entry, "ps_5_0", 0, 0, &blob, &errors);
            if (FAILED(hr))
                LOG("visor shader %s failed: HRESULT 0x%08X: %s", entry, (unsigned)hr,
                    errors ? (const char*)errors->GetBufferPointer() : "no compiler text");
            if (errors)
                errors->Release();
            return blob;
        };
        ID3DBlob* blob = compile("ps_visor");
        if (blob)
        {
            hr = g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_visorPs);
            blob->Release();
        }
        if (SUCCEEDED(hr))
            blob = compile("ps_visor_linearize");
        if (SUCCEEDED(hr) && blob)
        {
            hr = g_device->CreatePixelShader(
                blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_visorPsLinearize);
            blob->Release();
        }
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = 64;
        cb.Usage = D3D11_USAGE_DYNAMIC;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (SUCCEEDED(hr))
            hr = g_device->CreateBuffer(&cb, nullptr, &g_visorConstants);
        if (FAILED(hr))
        {
            failed = true;
            LOG("visor zoom pipeline failed: HRESULT 0x%08X; the lens keeps working", (unsigned)hr);
            auto release = [](auto*& p)
            {
                if (p)
                    p->Release();
                p = nullptr;
            };
            release(g_visorPs);
            release(g_visorPsLinearize);
            release(g_visorConstants);
            return false;
        }
        return true;
    }

    // The visor needs a zoom render to show: the shared scope render
    // (H2/H3/ODST/Reach/H4) or, in Halo CE, the eye-image crop; both report
    // through Game_HasScopeRenderer().
    bool VisorZoomSourceAvailable()
    {
        return Game_HasScopeRenderer();
    }

    bool UploadVisorToRtv(ID3D11RenderTargetView* rtv)
    {
        if (!rtv || !EnsureVisorPipeline())
            return false;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(g_context->Map(g_visorConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return false;
        const float shownZoom =
            g_scopeImageTitle == GameTitle::HaloCE && !CeVisorFullResActive()
                ? visor_zoom::CappedZoom(g_scopeRuntimeZoom.load(std::memory_order_acquire), g_config.ce_visor_zoom_max)
                : g_scopeRuntimeZoom.load(std::memory_order_acquire);
        const visor_zoom::Readout readout = visor_zoom::Digits(shownZoom);
        float* params = static_cast<float*>(mapped.pData);
        params[0] =
            std::isfinite(g_config.visor_frame_opacity) ? std::clamp(g_config.visor_frame_opacity, 0.0f, 1.0f) : 0.85f;
        params[1] = visor_zoom::kAspect;
        params[2] = visor_zoom::VisorFeather(g_config.zoom_edge_softness);
        params[3] = g_visorCockpitMode ? 1.0f : 0.0f; // cockpit display: no zoom readout/reticle, cool tint
        int* digits = reinterpret_cast<int*>(params + 4);
        digits[0] = readout.tens;
        digits[1] = readout.ones;
        digits[2] = readout.tenths;
        digits[3] = 0;
        // The gun's aim point (crosshair distance) in the head frame, placed in
        // the window, which shows the head's view at 1/zoom of its own angle.
        float* aim = params + 8;
        aim[0] = aim[1] = aim[2] = aim[3] = 0;
        // The reticle keeps its size in metres as the window grows or its edges move.
        const visor_zoom::EdgeWindow visorWindow = VisorWindow();
        aim[3] = visor_zoom::ReticleDivisor(visorWindow.virtualWidth);
        {
            // The frame follows the visible part: the same pixels the quad's imageRect shows.
            const XrRect2Di shown = VisorImageRect(visorWindow);
            float* rect = params + 12;
            rect[0] = static_cast<float>(shown.offset.x) / kVisorScreenWidth;
            rect[1] = static_cast<float>(shown.offset.y) / kVisorScreenHeight;
            rect[2] = static_cast<float>(shown.offset.x + shown.extent.width) / kVisorScreenWidth;
            rect[3] = static_cast<float>(shown.offset.y + shown.extent.height) / kVisorScreenHeight;
        }
        {
            float hq[4]{}, hp[3]{}, aq[4]{}, ap[3]{};
            if (VR_GetHeadPose(hq, hp) && VR_GetAimPose(aq, ap))
            {
                const XrVector3f f = Rotate(XrQuaternionf{aq[0], aq[1], aq[2], aq[3]}, {0, 0, -1});
                const float d = std::clamp(g_config.crosshair_distance_m, 2.0f, 50.0f);
                const XrVector3f local = Rotate(XrQuaternionf{-hq[0], -hq[1], -hq[2], hq[3]},
                    {ap[0] + f.x * d - hp[0], ap[1] + f.y * d - hp[1], ap[2] + f.z * d - hp[2]});
                float px = 0, py = 0;
                // CE shows a pixel crop: place the reticle for what it really magnifies.
                const float aimZoom = g_scopeImageTitle == GameTitle::HaloCE && !CeVisorFullResActive()
                                          ? g_ceVisorAchievedZoom.load(std::memory_order_acquire)
                                          : g_scopeRuntimeZoom.load(std::memory_order_acquire);
                if (local.z < -1e-3f && visor_zoom::AimInWindow(local.x / -local.z, local.y / -local.z, aimZoom,
                                            VisorWindowWidthM(), g_config.visor_zoom_distance_m, px, py))
                {
                    aim[0] = px;
                    aim[1] = py;
                    aim[2] = 1;
                }
            }
        }
        if (g_visorCockpitMode)
            aim[2] = 0; // no gun reticle on a cockpit screen
        g_context->Unmap(g_visorConstants, 0);
        const bool linearize = !IsSrgb(ScopeCacheViewFormat()) && IsSrgb((DXGI_FORMAT)g_xrFormat);
        D3DStateBackup backup;
        backup.Capture(g_context);
        g_context->OMSetRenderTargets(1, &rtv, nullptr);
        D3D11_VIEWPORT viewport{0, 0, (float)kVisorScreenWidth, (float)kVisorScreenHeight, 0, 1};
        g_context->RSSetViewports(1, &viewport);
        g_context->RSSetState(g_scopeUploadRasterizer);
        g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_scopeUploadDepthOff, 0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetShader(g_scopeUploadVs, nullptr, 0);
        g_context->GSSetShader(nullptr, nullptr, 0);
        g_context->PSSetShader(linearize ? g_visorPsLinearize : g_visorPs, nullptr, 0);
        g_context->PSSetConstantBuffers(0, 1, &g_visorConstants);
        g_context->PSSetShaderResources(0, 1, &g_scopeCacheSrv);
        g_context->PSSetSamplers(0, 1, &g_scopeUploadSampler);
        g_context->Draw(3, 0);
        backup.Restore(g_context);
        return true;
    }
} // namespace
namespace vr_core
{
    bool PrepareVisorImageDelivery()
    {
        static glass_reprojection::RetryLatch creationFailed;
        // A helmet-zoom pass that keeps refusing leaves no frozen visor window.
        if (!HeldScopeImageFresh())
            return false;
        if (creationFailed.Blocked(GetTickCount64()) || !VisorZoomSourceAvailable() || !g_scopeHasImage.load() ||
            g_scopeImageTitle != TitleAdapter_GetActiveTitle() ||
            g_scopeImageGeneration != TitleAdapter_GetGeneration(g_scopeImageTitle) || !g_scopeCache ||
            !g_scopeCacheSrv || !EnsureVisorPipeline())
            return false;
        if (g_visorScreenChain == XR_NULL_HANDLE &&
            !CreateChain(kVisorScreenWidth, kVisorScreenHeight, g_visorScreenChain, g_visorScreenImages,
                g_visorScreenRtvs, "visor zoom window"))
        {
            creationFailed.Fail(GetTickCount64());
            return false;
        }
        uint32_t index = 0;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = 1000000000;
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if (XR_FAILED(xrAcquireSwapchainImage(g_visorScreenChain, &acquire, &index)))
            return false;
        if (XR_FAILED(xrWaitSwapchainImage(g_visorScreenChain, &wait)))
        {
            xrReleaseSwapchainImage(g_visorScreenChain, &release);
            return false;
        }
        ID3D11RenderTargetView* rtv = nullptr;
        if (index < g_visorScreenImages.size() && index < g_visorScreenRtvs.size())
        {
            if (!g_visorScreenRtvs[index])
            {
                D3D11_RENDER_TARGET_VIEW_DESC desc{};
                desc.Format = (DXGI_FORMAT)g_xrFormat;
                desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                (void)g_device->CreateRenderTargetView(g_visorScreenImages[index], &desc, &g_visorScreenRtvs[index]);
            }
            rtv = g_visorScreenRtvs[index];
        }
        const bool uploaded = UploadVisorToRtv(rtv);
        if (!uploaded)
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                LOG("visor zoom upload failed: no valid output RTV or pipeline");
            }
        }
        if (XR_FAILED(xrReleaseSwapchainImage(g_visorScreenChain, &release)))
            return false;
        return uploaded;
    }

    // Vehicle monitors: upload the newest scope image (if new) into the chain of
    // the view it was rendered for. False = no image delivered this frame.
    bool DeliverMonitorImage()
    {
        static glass_reprojection::RetryLatch creationFailed[vehicle_monitors::kViewCount]{};
        const uint32_t serial = g_scopeImageSerial.load(std::memory_order_acquire);
        const int view = g_scopeImageView;
        if (serial == g_monitorDeliveredSerial || view < 0 || view >= vehicle_monitors::kViewCount ||
            creationFailed[view].Blocked(GetTickCount64()) || !VisorZoomSourceAvailable() || !g_scopeHasImage.load() ||
            g_scopeImageTitle != TitleAdapter_GetActiveTitle() ||
            g_scopeImageGeneration != TitleAdapter_GetGeneration(g_scopeImageTitle) || !g_scopeCache ||
            !g_scopeCacheSrv || !EnsureVisorPipeline())
            return false;
        XrSwapchain& chain = g_monitorChains[view];
        if (chain == XR_NULL_HANDLE && !CreateChain(kVisorScreenWidth, kVisorScreenHeight, chain, g_monitorImages[view],
                                           g_monitorRtvs[view], "vehicle monitor"))
        {
            creationFailed[view].Fail(GetTickCount64());
            return false;
        }
        uint32_t index = 0;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = 1000000000;
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if (XR_FAILED(xrAcquireSwapchainImage(chain, &acquire, &index)))
            return false;
        if (XR_FAILED(xrWaitSwapchainImage(chain, &wait)))
        {
            xrReleaseSwapchainImage(chain, &release);
            return false;
        }
        ID3D11RenderTargetView* rtv = nullptr;
        if (index < g_monitorImages[view].size() && index < g_monitorRtvs[view].size())
        {
            if (!g_monitorRtvs[view][index])
            {
                D3D11_RENDER_TARGET_VIEW_DESC desc{};
                desc.Format = (DXGI_FORMAT)g_xrFormat;
                desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                (void)g_device->CreateRenderTargetView(
                    g_monitorImages[view][index], &desc, &g_monitorRtvs[view][index]);
            }
            rtv = g_monitorRtvs[view][index];
        }
        g_visorCockpitMode = true;
        const bool uploaded = UploadVisorToRtv(rtv);
        g_visorCockpitMode = false;
        if (XR_FAILED(xrReleaseSwapchainImage(chain, &release)) || !uploaded)
            return false;
        g_monitorDeliveredSerial = serial;
        g_monitorHasImage[view] = true;
        return true;
    }
    bool EnsureEyeCaches(const D3D11_TEXTURE2D_DESC& source)
    {
        const bool nativeDlss =
            VR_DlssWantsEyeCamera() && TitleAdapter_GetActiveTitle() == GameTitle::Halo3 &&
            source.SampleDesc.Count == 1 &&
            (source.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || source.Format == DXGI_FORMAT_R8G8B8A8_UNORM);
        if (g_eyeCache[0] && g_eyeCache[1] && g_eyeCacheRtvs[0] && g_eyeCacheRtvs[1] &&
            g_eyeCacheDesc.Width == source.Width && g_eyeCacheDesc.Height == source.Height &&
            g_eyeCacheDesc.Format == source.Format && g_eyeCacheDlssRequested == nativeDlss)
            return true;
        // Revoke admission before any pointer is released. A new nonzero epoch
        // prevents pointer reuse after resize from validating an old eye stamp.
        VR_ResetHalo2SynchronousStereo();
        g_halo2SynchronousResourceEpoch.fetch_add(1, std::memory_order_acq_rel);
        for (auto*& texture : g_eyeCache)
        {
            // Drop the cached source view first. Ours is a strong reference, so
            // without this the old eye texture would survive the Release below
            // and stay pinned at its old resolution.
            ForgetSourceView(texture);
            if (texture)
                texture->Release();
            texture = nullptr;
        }
        for (auto*& rtv : g_eyeCacheRtvs)
        {
            if (rtv)
                rtv->Release();
            rtv = nullptr;
        }
        D3D11_TEXTURE2D_DESC desc = source;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        D3D11_RENDER_TARGET_VIEW_DESC view{};
        view.Format = source.Format;
        view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        if (nativeDlss)
        {
            desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
            desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        }
        auto create = [&](bool direct)
        {
            for (int eye = 0; eye < 2; ++eye)
            {
                if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &g_eyeCache[eye])) ||
                    FAILED(g_device->CreateRenderTargetView(
                        g_eyeCache[eye], direct ? &view : nullptr, &g_eyeCacheRtvs[eye])))
                    return false;
            }
            return true;
        };
        bool direct = nativeDlss;
        bool created = create(direct);
        if (!created && direct)
        {
            for (int eye = 0; eye < 2; ++eye)
            {
                if (g_eyeCacheRtvs[eye])
                    g_eyeCacheRtvs[eye]->Release();
                if (g_eyeCache[eye])
                    g_eyeCache[eye]->Release();
                g_eyeCacheRtvs[eye] = nullptr;
                g_eyeCache[eye] = nullptr;
            }
            desc.Format = source.Format;
            desc.BindFlags &= ~D3D11_BIND_UNORDERED_ACCESS;
            direct = false;
            created = create(false);
            LOG("DLSS: direct eye allocation unavailable; using the ordinary eye cache and colour copy");
        }
        if (!created)
        {
            LOG("failed to create persistent eye frame caches");
            return false;
        }
        g_eyeCacheDesc = desc;
        g_eyeCacheDesc.Format = source.Format;
        g_eyeCacheNativeDlss = direct;
        g_eyeCacheDlssRequested = nativeDlss;
        g_eyeHasImage[0] = g_eyeHasImage[1] = false;
        g_halo4EyeSerial[0].store(0, std::memory_order_release);
        g_halo4EyeSerial[1].store(0, std::memory_order_release);
        g_stereoValidationDone = false;
        LOG("persistent eye frame caches created: %ux%u", desc.Width, desc.Height);
        return true;
    }
} // namespace vr_core
namespace
{
    constinit ID3D11Buffer* g_comfortConstants = nullptr;
} // namespace
namespace vr_core
{
    bool EnsureComfortPipeline()
    {
        if (g_vignettePs && g_dashPs && g_comfortConstants)
            return true;
        static bool failed = false;
        if (failed || !g_device || !EnsureScopeUploadPipeline())
            return false;
        static const char* source = R"(
cbuffer Comfort : register(b0) { float4 params; }; // x = strength
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 ps_vignette(VSOut i):SV_Target {
    float r=length((i.uv-0.5)*2);
    float a=saturate(params.x)*smoothstep(0.30,0.95,r);
    return float4(0,0,0,a); // premultiplied black
}
float4 ps_dash(VSOut i):SV_Target {
    float2 p=abs(i.uv-0.5)*2;
    float a=0.22*(1-smoothstep(0.35,1.0,p.y))*(1-smoothstep(0.70,1.0,p.x));
    float3 c=float3(0.80,0.85,0.90);
    return float4(c*a,a);
}
)";
        HRESULT hr = S_OK;
        auto compile = [&](const char* entry) -> ID3DBlob*
        {
            ID3DBlob *blob = nullptr, *errors = nullptr;
            hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr, entry, "ps_5_0", 0, 0, &blob, &errors);
            if (FAILED(hr))
                LOG("comfort shader %s failed: HRESULT 0x%08X: %s", entry, (unsigned)hr,
                    errors ? (const char*)errors->GetBufferPointer() : "no compiler text");
            if (errors)
                errors->Release();
            return blob;
        };
        ID3DBlob* blob = compile("ps_vignette");
        if (blob)
        {
            hr = g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_vignettePs);
            blob->Release();
        }
        if (SUCCEEDED(hr))
            blob = compile("ps_dash");
        if (SUCCEEDED(hr) && blob)
        {
            hr = g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_dashPs);
            blob->Release();
        }
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = 16;
        cb.Usage = D3D11_USAGE_DYNAMIC;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (SUCCEEDED(hr))
            hr = g_device->CreateBuffer(&cb, nullptr, &g_comfortConstants);
        if (FAILED(hr))
        {
            failed = true;
            LOG("vehicle comfort pipeline failed: HRESULT 0x%08X; comfort layers off", (unsigned)hr);
            auto release = [](auto*& p)
            {
                if (p)
                    p->Release();
                p = nullptr;
            };
            release(g_vignettePs);
            release(g_dashPs);
            release(g_comfortConstants);
            return false;
        }
        return true;
    }

    bool DrawComfortImage(XrSwapchain chain, std::vector<ID3D11Texture2D*>& images,
        std::vector<ID3D11RenderTargetView*>& rtvs, uint32_t w, uint32_t h, ID3D11PixelShader* ps, float strength)
    {
        uint32_t index = 0;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = 1000000000;
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if (XR_FAILED(xrAcquireSwapchainImage(chain, &acquire, &index)))
            return false;
        if (XR_FAILED(xrWaitSwapchainImage(chain, &wait)))
        {
            xrReleaseSwapchainImage(chain, &release);
            return false;
        }
        ID3D11RenderTargetView* rtv = GetRtv(images, rtvs, index);
        bool ok = false;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (rtv && SUCCEEDED(g_context->Map(g_comfortConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            float* p = static_cast<float*>(mapped.pData);
            p[0] = strength;
            p[1] = p[2] = p[3] = 0;
            g_context->Unmap(g_comfortConstants, 0);
            D3DStateBackup backup;
            backup.Capture(g_context);
            g_context->OMSetRenderTargets(1, &rtv, nullptr);
            D3D11_VIEWPORT viewport{0, 0, (float)w, (float)h, 0, 1};
            g_context->RSSetViewports(1, &viewport);
            g_context->RSSetState(g_scopeUploadRasterizer);
            g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
            g_context->OMSetDepthStencilState(g_scopeUploadDepthOff, 0);
            g_context->IASetInputLayout(nullptr);
            g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g_context->VSSetShader(g_scopeUploadVs, nullptr, 0);
            g_context->GSSetShader(nullptr, nullptr, 0);
            g_context->PSSetShader(ps, nullptr, 0);
            g_context->PSSetConstantBuffers(0, 1, &g_comfortConstants);
            g_context->Draw(3, 0);
            backup.Restore(g_context);
            ok = true;
        }
        xrReleaseSwapchainImage(chain, &release);
        return ok;
    }
} // namespace vr_core
bool VR_VehicleMonitorCamera(int game, const char* vehicle, int cockpitClass, const float anchor[3],
    const float hullForward[3], float unitsPerMeter, float outPosition[3], float outForward[3], float outUp[3],
    float& fovDeg)
{
    if (!std::isfinite(unitsPerMeter) || unitsPerMeter <= 0)
        return false;
    if (!g_config.vehicle_monitors || !g_config.vehicle_first_person)
        return false;
    const bool on[vehicle_monitors::kViewCount]{
        g_config.vehicle_monitor_forward, g_config.vehicle_monitor_left, g_config.vehicle_monitor_right};
    int view = g_monitorCurrentView.load(std::memory_order_acquire);
    const uint64_t askNow = GetTickCount64();
    uint64_t since = g_monitorViewSinceMs.load(std::memory_order_acquire);
    const uint64_t lastAsk = g_monitorRequestMs.load(std::memory_order_acquire);
    // A new seating (no request in the last 250 ms) starts the stuck timer afresh.
    if (!since || !lastAsk || askNow < lastAsk || askNow - lastAsk > 250)
    {
        since = askNow;
        g_monitorViewSinceMs.store(askNow, std::memory_order_release);
    }
    if (view >= 0 && view < vehicle_monitors::kViewCount && askNow >= since &&
        askNow - since >= vehicle_monitors::RoundRobin::kStuckMs)
    {
        // This view keeps failing to render (e.g. refused by the title's pass): move on.
        static bool loggedStuck[vehicle_monitors::kViewCount]{};
        if (!loggedStuck[view])
        {
            loggedStuck[view] = true;
            LOG("vehicle monitors: %s view gave no image for %llu ms; skipping it this turn",
                vehicle_monitors::ViewName(static_cast<vehicle_monitors::View>(view)),
                vehicle_monitors::RoundRobin::kStuckMs);
        }
        vehicle_monitors::RoundRobin skip;
        skip.Resume(view);
        view = skip.Next(on);
        g_monitorCurrentView.store(view, std::memory_order_release);
        g_monitorViewSinceMs.store(askNow, std::memory_order_release);
    }
    if (view < 0 || view >= vehicle_monitors::kViewCount || !on[view])
    {
        vehicle_monitors::RoundRobin first;
        view = first.Next(on);
        g_monitorCurrentView.store(view, std::memory_order_release);
    }
    if (view < 0)
        return false;
    const auto cls = static_cast<cockpit_screen::Class>(cockpitClass);
    const auto* row = vehicle_monitors::RowFor(static_cast<vehicle_monitors::Game>(game), vehicle);
    if (row && !row->verified)
    {
        static bool logged[vehicle_monitors::kRowCount]{};
        const int i = vehicle_monitors::RowIndex(row);
        if (i >= 0 && !logged[i])
        {
            logged[i] = true;
            LOG("vehicle monitors: camera row unverified: %s (ahead %.1f m, up %.1f m); measure it in the first test",
                row->vehicle, row->aheadM, row->upM);
        }
    }
    const auto spec = vehicle_monitors::SpecFor(row, cls, static_cast<vehicle_monitors::View>(view));
    if (!cockpit_screen::CameraPose(
            anchor, hullForward, spec.aheadM * unitsPerMeter, spec.upM * unitsPerMeter, outPosition, outForward, outUp))
        return false;
    vehicle_monitors::YawAboutUp(outForward, outUp, spec.yawDeg);
    fovDeg = spec.fovDeg;
    if (static_cast<vehicle_monitors::Game>(game) == vehicle_monitors::Game::Reach && view != 0)
    {
        static bool loggedReachSide = false;
        if (!loggedReachSide)
        {
            loggedReachSide = true;
            LOG("vehicle monitors: reach side view limited to %.0f deg by shared cull (the Reach scope pass refuses rays past 90 deg from the head)",
                vehicle_monitors::kReachSideYawDeg);
        }
    }
    g_monitorPendingView.store(view, std::memory_order_release);
    g_monitorVehicle.store(vehicle, std::memory_order_release);
    g_monitorRequestMs.store(GetTickCount64(), std::memory_order_release);
    return true;
}
bool VR_CaptureRenderedEye(int eye)
{
    if (eye < 0 || eye > 1)
        return false;
    if (g_rasterRedirected[eye] && g_eyeCache[eye])
    {
        g_eyeHasImage[eye] = true;
        return true;
    }
    static bool loggedMissing = false;
    if (!loggedMissing)
    {
        LOG("raster: no internal scene-color RTV redirect occurred; refusing fake eye copy");
        loggedMissing = true;
    }
    return false;
}

void VR_PublishEyeCamera(int eye, const VrEyeCameraSample& sample)
{
    if (eye < 0 || eye > 1)
        return;
    DlssEyeState& state = g_dlssEye[eye];
    if (!VR_DlssWantsEyeCamera())
    {
        state.published = false;
        return;
    }
    state.current = dlss::MakeCameraSample(
        sample.position, sample.forward, sample.up, sample.projection, sample.jitterNdcX, sample.jitterNdcY);
    state.published = state.current.valid;
    // One-time evidence per eye and per title: the decoded terms, or the
    // rejected rows (the evidence needed when a title's layout differs).
    static uint8_t loggedValid[2]{};
    static uint8_t loggedRejected[2]{};
    const GameTitle title = TitleAdapter_GetActiveTitle();
    const uint8_t titleBit = static_cast<uint8_t>(1u << (static_cast<unsigned>(title) & 7u));
    if (state.current.valid && !(loggedValid[eye] & titleBit))
    {
        loggedValid[eye] |= titleBit;
        const dlss::ProjectionTerms& t = state.current.projection;
        LOG("DLSS: %s eye %d camera published%s: tan %.5f/%.5f centre %.6f/%.6f "
            "depth A=%.7f B=%.5f (%s Z), jitter ndc %.6f/%.6f, "
            "pos (%.2f, %.2f, %.2f) fwd (%.3f, %.3f, %.3f) up (%.3f, %.3f, %.3f)",
            DlssTitleName(title), eye, t.transposed ? " (column-vector projection, transposed)" : "", t.tanX, t.tanY,
            t.centerX, t.centerY, t.depthA, t.depthB, t.depthInverted ? "reversed" : "standard", sample.jitterNdcX,
            sample.jitterNdcY, sample.position[0], sample.position[1], sample.position[2], sample.forward[0],
            sample.forward[1], sample.forward[2], sample.up[0], sample.up[1], sample.up[2]);
    }
    else if (!state.current.valid && !(loggedRejected[eye] & titleBit))
    {
        loggedRejected[eye] |= titleBit;
        const float* p = sample.projection;
        LOG("DLSS: %s eye %d camera sample REJECTED; projection rows "
            "[%.5f %.5f %.5f %.5f] [%.5f %.5f %.5f %.5f] "
            "[%.5f %.5f %.5f %.5f] [%.5f %.5f %.5f %.5f], "
            "pos (%.2f, %.2f, %.2f) fwd (%.3f, %.3f, %.3f) up (%.3f, %.3f, %.3f)",
            DlssTitleName(title), eye, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12],
            p[13], p[14], p[15], sample.position[0], sample.position[1], sample.position[2], sample.forward[0],
            sample.forward[1], sample.forward[2], sample.up[0], sample.up[1], sample.up[2]);
    }
}

bool VR_DlssWantsEyeCamera()
{
    if (g_config.upscaler != dlss::kUpscalerDlss || !dlss::TitleHasCameraContract(TitleAdapter_GetActiveTitle()))
        return false;
    unsigned rw = 0, rh = 0, ow = 0, oh = 0;
    bool plannedDlss = false, runtimePresent = false;
    D3D_GetRenderPlan(rw, rh, ow, oh, plannedDlss, runtimePresent);
    return runtimePresent;
}

void VR_DlssBeginEyeDepthWindow(int eye)
{
    if (eye >= 0 && eye < 2)
        g_dlssKicked[eye].Reset();
    if (eye < 0 || eye > 1 || !VR_DlssWantsEyeCamera())
    {
        g_dlssDepthWindowEye.store(-1, std::memory_order_relaxed);
        return;
    }
    // Same contract as VR_BeginRasterEye: this eye's camera and depth must
    // both arrive during THIS render.
    g_dlssEye[eye].published = false;
    g_dlssEye[eye].depthValid = false;
    g_dlssEyeSceneDepthSeen = false;
    g_dlssEyeLastFullRasterDsv = nullptr;
    g_dlssDepthWindowEye.store(eye, std::memory_order_relaxed);
}

void VR_DlssEndEyeDepthWindow(int eye)
{
    if (g_dlssDepthWindowEye.load(std::memory_order_relaxed) != eye)
        return;
    g_dlssDepthWindowEye.store(-1, std::memory_order_relaxed);
    DlssCaptureEyeDepth(eye);
}

bool VR_GetEyeJitter(int eye, float& outNdcX, float& outNdcY)
{
    outNdcX = 0.0f;
    outNdcY = 0.0f;
    if (eye < 0 || eye > 1 || !VR_DlssWantsEyeCamera() || !g_config.dlss_jitter || !g_dlssJitterArmed ||
        !Dlss_IsAvailable() || !dlss::UsesWorldRender(DlssPresentationMode()) || !g_gameBackbufferDescValid ||
        !g_stereoW || !g_stereoH)
        return false;
    const uint32_t renderW = g_gameBackbufferDesc.Width;
    const uint32_t renderH = g_gameBackbufferDesc.Height;
    const dlss::OutputSize out = DlssOutputFor(renderW, renderH, g_stereoW, g_stereoH);
    uint32_t jitterOutputW = out.width;
    unsigned planRenderW = 0, planRenderH = 0, planOutputW = 0;
    unsigned planOutputH = 0;
    bool plannedDlss = false, runtimePresent = false;
    D3D_GetRenderPlan(planRenderW, planRenderH, planOutputW, planOutputH, plannedDlss, runtimePresent);
    const uint32_t phases = dlss::JitterPhaseCount(renderW, jitterOutputW);
    float pixelX = 0.0f, pixelY = 0.0f;
    dlss::JitterPixels(g_dlssJitterPhase, phases, pixelX, pixelY);
    dlss::JitterToNdc(pixelX, pixelY, renderW, renderH, outNdcX, outNdcY);
    return true;
}

void VR_GetDlssStatus(char* out, size_t bytes)
{
    if (!out || !bytes)
        return;
    AcquireSRWLockShared(&g_dlssStatusLock);
    snprintf(out, bytes, "%s", g_dlssStatusText);
    ReleaseSRWLockShared(&g_dlssStatusLock);
}

bool VR_GetDlssRenderPlan(VrDlssRenderPlan& out)
{
    out = {};
    D3D_GetRenderPlan(
        out.plannedRenderW, out.plannedRenderH, out.outputW, out.outputH, out.plannedDlss, out.runtimePresent);
    if (g_gameBackbufferDescValid)
    {
        out.liveRenderW = g_gameBackbufferDesc.Width;
        out.liveRenderH = g_gameBackbufferDesc.Height;
    }
    return out.outputW != 0 && out.outputH != 0;
}

bool VR_CaptureBackbufferEye(int eye)
{
    if (eye < 0 || eye > 1 || !g_context || !g_gameBackbufferDescValid || !g_eyeCache[eye] || !g_eyeCacheRtvs[eye])
        return false;
    ID3D11Texture2D* backbuffer = g_nextGameBackbuffer.load(std::memory_order_acquire);
    if (!backbuffer || g_eyeCacheDesc.Width != g_gameBackbufferDesc.Width ||
        g_eyeCacheDesc.Height != g_gameBackbufferDesc.Height)
        return false;
    if (!Blit(backbuffer, g_gameBackbufferDesc, g_eyeCache[eye], g_eyeCacheDesc.Width, g_eyeCacheDesc.Height,
            g_eyeCacheRtvs[eye]))
        return false;
    g_eyeHasImage[eye] = true;
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true, std::memory_order_relaxed))
        LOG("raster: ODST direct backbuffer death-camera capture active");
    return true;
}

bool VR_IsEyeRasterActive(ID3D11DeviceContext* context) noexcept
{
    const int eye = g_rasterEye.load(std::memory_order_acquire);
    return context && context == g_context && eye >= 0 && eye < 2 &&
           g_rasterOwnerThread.load(std::memory_order_acquire) == GetCurrentThreadId();
}

void VR_BeginRasterEye(int eye)
{
    g_halo2SynchronousEyeScope = {};
    // Revoke the prior scope before any resource precondition. Otherwise a
    // transient resize/no-device return can leave yesterday's redirect flag
    // looking like a capture from this prepared frame.
    g_rasterOwnerThread.store(0, std::memory_order_release);
    g_rasterEye.store(-1, std::memory_order_release);
    if (eye >= 0 && eye < 2)
    {
        g_rasterRedirected[eye] = false;
        g_dlssKicked[eye].Reset();
    }
    if (eye < 0 || eye > 1 || !g_gameSwapchain || !g_device)
        return;
    // Eye caches are created lazily when Halo binds its final scene-color RTV.
    // That RTV's typed view format (not the swapchain resource format) controls
    // the required sRGB conversion.
    g_rasterEye.store(eye, std::memory_order_release);
    g_rasterOwnerThread.store(GetCurrentThreadId(), std::memory_order_release);
    g_eyeGpuTiming.Begin(g_context, VR_DlssWantsEyeCamera(), g_gameBackbufferDesc.Width, g_gameBackbufferDesc.Height);
    // DLSS: this eye's camera and depth must both arrive during THIS render.
    g_dlssEye[eye].published = false;
    g_dlssEye[eye].depthValid = false;
    g_dlssEyeSceneDepthSeen = false;
    g_dlssEyeLastFullRasterDsv = nullptr;
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
    {
        const uint64_t token = g_halo2SynchronousPairToken.load(std::memory_order_acquire);
        if (Halo2SynchronousSerialIsReserved(token))
        {
            const uint64_t serial = token & ~kHalo2SynchronousReservationBit;
            const uint32_t generation = g_halo2SynchronousPairGeneration.load(std::memory_order_relaxed);
            const uint32_t resourceEpoch = g_halo2SynchronousPairResourceEpoch.load(std::memory_order_relaxed);
            ID3D11RenderTargetView* const finalRtv = g_halo2SynchronousFinalRtv.load(std::memory_order_relaxed);
            if (finalRtv && g_halo2SynchronousTargetSerial.load(std::memory_order_acquire) == serial &&
                Halo2SynchronousContextCurrent(generation, serial, resourceEpoch) &&
                g_halo2SynchronousPairToken.load(std::memory_order_acquire) == token)
            {
                Halo2SynchronousEyeScope& scope = g_halo2SynchronousEyeScope;
                scope.active = true;
                scope.generation = generation;
                scope.resourceEpoch = resourceEpoch;
                scope.preparedSerial = serial;
                scope.eye = eye;
                scope.finalRtv = finalRtv;
                scope.sceneRtv = g_halo2SynchronousSceneRtv.load(std::memory_order_relaxed);
                scope.lastBoundRtv = nullptr;
                scope.binds = 0;
            }
        }
    }
}
bool VR_BeginNativeHudEyeDraw(int eye)
{
    auto& route = g_nativeHudEyeRoute;
    if (!g_context || eye < 0 || eye > 1 || !g_eyeCacheRtvs[eye] || route.active)
        return false;

    g_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, route.rtvs, &route.dsv);
    route.viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    g_context->RSGetViewports(&route.viewportCount, route.viewports);
    route.scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    g_context->RSGetScissorRects(&route.scissorCount, route.scissors);
    // ODST's unique prepare callback owns slot 0 at both native CHUD boundaries:
    // it establishes target 1 immediately before secondary, then performs its
    // engine-owned transition before primary. A non-null slot-0 RTV captured
    // only at these TLS-scoped hooks is therefore the title-proven phase output,
    // even when its view pointer differs from the later scene-color view.
    if (!route.rtvs[0])
    {
        for (auto*& rtv : route.rtvs)
        {
            if (rtv)
                rtv->Release();
            rtv = nullptr;
        }
        if (route.dsv)
        {
            route.dsv->Release();
            route.dsv = nullptr;
        }
        route.viewportCount = 0;
        route.scissorCount = 0;
        return false;
    }

    route.phaseOutputRtv = route.rtvs[0];
    route.eye = eye;
    ID3D11RenderTargetView* routed[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        routed[i] = route.rtvs[i];
    routed[0] = g_eyeCacheRtvs[eye];
    route.bypassOmRedirect = true;
    g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, routed, route.dsv);
    route.bypassOmRedirect = false;
    route.targetCopy = false;
    route.active = true;
    return true;
}

void VR_EndNativeHudEyeDraw()
{
    auto& route = g_nativeHudEyeRoute;
    if (!route.active || !g_context)
        return;

    route.active = false;
    route.targetCopy = false;
    route.bypassOmRedirect = true;
    g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, route.rtvs, route.dsv);
    route.bypassOmRedirect = false;
    g_context->RSSetViewports(route.viewportCount, route.viewportCount ? route.viewports : nullptr);
    g_context->RSSetScissorRects(route.scissorCount, route.scissorCount ? route.scissors : nullptr);
    for (auto*& rtv : route.rtvs)
    {
        if (rtv)
            rtv->Release();
        rtv = nullptr;
    }
    if (route.dsv)
    {
        route.dsv->Release();
        route.dsv = nullptr;
    }
    route.phaseOutputRtv = nullptr;
    route.viewportCount = 0;
    route.scissorCount = 0;
    route.eye = -1;
    g_nativeHudPhaseScopes.fetch_add(1, std::memory_order_release);
}

void VR_BeginNativeHudTargetCopy()
{
    auto& route = g_nativeHudEyeRoute;
    route.targetCopy = route.active;
}

void VR_EndNativeHudTargetCopy()
{
    auto& route = g_nativeHudEyeRoute;
    if (!route.targetCopy)
        return;
    route.targetCopy = false;
    g_nativeHudExactCopyScopes.fetch_add(1, std::memory_order_relaxed);
}

ID3D11Resource* VR_RedirectNativeHudCopySource(ID3D11Resource* source)
{
    const auto& route = g_nativeHudEyeRoute;
    if (!route.active || !route.targetCopy || route.eye < 0 || route.eye > 1 || !g_eyeCache[route.eye] ||
        !g_sceneColorResource || source != g_sceneColorResource)
        return source;

    g_nativeHudCopySubstitutions.fetch_add(1, std::memory_order_relaxed);
    return g_eyeCache[route.eye];
}
bool VR_GetEyeViewOffset(int eye, float outPosition[3], float outQuat[4])
{
    if (eye < 0 || eye > 1 || !outPosition || !outQuat || g_views.size() < 2)
        return false;
    const XrQuaternionf& a = g_views[0].pose.orientation;
    const XrQuaternionf& b = g_views[1].pose.orientation;
    // Quaternions q and -q encode the same rotation. Align their signs before
    // averaging so a runtime choosing opposite representations cannot collapse
    // the midpoint to zero.
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const float sign = dot < 0.0f ? -1.0f : 1.0f;
    float cx = a.x + b.x * sign, cy = a.y + b.y * sign;
    float cz = a.z + b.z * sign, cw = a.w + b.w * sign;
    const float len = sqrtf(cx * cx + cy * cy + cz * cz + cw * cw);
    if (!std::isfinite(len) || len < 1e-5f)
        return false;
    cx /= len;
    cy /= len;
    cz /= len;
    cw /= len;

    // The VIEW reference origin is the centroid of the stereo view origins.
    // Reconstruct the same midpoint from this atomic xrLocateViews result, then
    // express each eye's position in that midpoint's local axes. This preserves
    // the runtime's actual, possibly adjustable IPD and any non-horizontal eye
    // offset instead of imposing the PSVR2 baseline on every headset.
    const XrVector3f& ap = g_views[0].pose.position;
    const XrVector3f& bp = g_views[1].pose.position;
    const float separationX = bp.x - ap.x;
    const float separationY = bp.y - ap.y;
    const float separationZ = bp.z - ap.z;
    const float separation = sqrtf(separationX * separationX + separationY * separationY + separationZ * separationZ);
    if (!std::isfinite(separation) || separation < 0.03f || separation > 0.10f)
        return false;
    const XrVector3f& ep = g_views[eye].pose.position;
    const XrVector3f delta{ep.x - (ap.x + bp.x) * 0.5f, ep.y - (ap.y + bp.y) * 0.5f, ep.z - (ap.z + bp.z) * 0.5f};
    const XrVector3f local = Rotate({-cx, -cy, -cz, cw}, delta);
    if (!std::isfinite(local.x) || !std::isfinite(local.y) || !std::isfinite(local.z))
        return false;
    outPosition[0] = local.x;
    outPosition[1] = local.y;
    outPosition[2] = local.z;

    // relative = conj(center) * eye
    const XrQuaternionf& e = g_views[eye].pose.orientation;
    outQuat[0] = cw * e.x - cx * e.w - cy * e.z + cz * e.y;
    outQuat[1] = cw * e.y + cx * e.z - cy * e.w - cz * e.x;
    outQuat[2] = cw * e.z - cx * e.y + cy * e.x - cz * e.w;
    outQuat[3] = cw * e.w + cx * e.x + cy * e.y + cz * e.z;
    return std::isfinite(outQuat[0]) && std::isfinite(outQuat[1]) && std::isfinite(outQuat[2]) &&
           std::isfinite(outQuat[3]);
}
bool VR_HudHidden() noexcept
{
    if (!g_config.hide_hud)
        return false;
    const auto title = TitleAdapter_GetActiveTitle();
    return !g_config.hud_reveal_near_head ||
           !g_hudReveal.Active(title, TitleAdapter_GetGeneration(title), GetTickCount64());
}
bool VR_GetEyeFov(int eye, float outFov[4])
{
    if (eye < 0 || eye > 1 || !outFov)
        return false;
    // xrLocateViews and the game render hook execute on the render thread in
    // this integration, so the latest located view is stable here.
    outFov[0] = g_views[eye].fov.angleLeft;
    outFov[1] = g_views[eye].fov.angleRight;
    outFov[2] = g_views[eye].fov.angleUp;
    outFov[3] = g_views[eye].fov.angleDown;
    return outFov[0] < 0.0f && outFov[1] > 0.0f && outFov[2] > 0.0f && outFov[3] < 0.0f;
}
