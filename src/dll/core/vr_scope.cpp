// vr_scope.cpp: the scope image path, the authored reticle and the visor, moved unchanged from vr.cpp. What it provides to the other VR units is
// declared in vr_scope.h; it reaches their state through vr_internal.h.
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
#include "menu.h"
#include "../../common/titles/ce/haloce_reticle_logic.h"
#include "../titles/ce/haloce_hud.h"
#include "../titles/ce/haloce_hud_layout_runtime.h"
#include "d3d_state.h"
#include <../common/authored_reticle_logic.h>
#include "../../common/titles/h4/halo4_cui_reticle_logic.h"
#include "../../common/scope_log.h"
#include "display_resource_lock.h"
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

namespace
{
    // not constinit: its type has no constant initialiser; it is initialised dynamically, as before.
    std::vector<ID3D11Texture2D*> g_dualReticleImages;
    // not constinit: its type has no constant initialiser; it is initialised dynamically, as before.
    std::vector<ID3D11RenderTargetView*> g_dualReticleRtvs;
    constinit bool g_dualReticleAcquired = false, g_dualReticleReady = false, g_dualReticleFailed = false;
    constinit uint32_t g_dualReticleIndex = 0;
    constinit uint64_t g_dualReticleRetryMs = 0;
    constinit float g_dualReticleColor[3]{-1, -1, -1};
} // namespace
namespace vr_core
{
    // The latest muzzle / lens node rays per title, game world units.
    constinit halo_ce::Snapshot<weapon_muzzle::Ray> g_scopeLogMuzzle[7], g_scopeLogLens[7];
} // namespace vr_core
namespace
{
    constinit std::atomic<uint64_t> g_scopeMuzzleMs[7]{}; // when each title's muzzle receipt was last published
} // namespace
namespace vr_core
{
    constinit std::atomic<uint64_t> g_scopeLensMs[7]{}; // same for the scope-lens node receipt
} // namespace vr_core
namespace
{
    constinit std::atomic<uint64_t> g_scopeEngagedMs{0}; // when the scope last turned on
    constinit std::atomic<unsigned> g_lensPublishOk[7]{},
        g_lensPublishEmpty[7]{}; // committed-palette lens receipts per title
    ID3D11ShaderResourceView* ScopeWindowFor(
        GameTitle title, uint64_t identity); // per-gun window mask (null = circle), logs once
} // namespace
namespace vr_core
{
    // Once per gun (and again if the anchored/fallback state changes) whenever the lens tier is up: where the quad is drawn
    // vs where the rendered node says the glass is, in the left eye; or why the aim-derived lens is still in use.
    void LogLensProof(const std::vector<XrCompositionLayerProjectionView>& views, const float quad[3],
        const float aimDerived[3], bool anchored) noexcept
    {
        static uint64_t loggedKey = ~0ull;
        const GameTitle title = TitleAdapter_GetActiveTitle();
        // The zoom gate's own identity: bridges the short gaps in the per-frame model receipt (~1 s) that made Reach
        // report "no held gun" while the sniper was in hand.
        const uint64_t identity = VR_ZoomWeaponIdentity(title, TitleAdapter_GetGeneration(title));
        // Per-gun consumer state, once it has held for 750 ms (the line below logs at the first frame of a swap, before this
        // gun's first receipt exists, so it cannot say why a settled gun stays on the fallback).
        {
            static uint64_t stateKey = ~0ull, stateSinceMs = 0, stateLogged = ~0ull;
            float scratch[3];
            const char* stateWhy = "";
            if (!anchored)
                (void)ReadAnchoredLens(scratch, &stateWhy);
            const auto* stateModel = weapon_model::Find(title, identity);
            const bool noRow = identity && !(stateModel && weapon_profile::KitLensNodeFor(title, stateModel->name));
            const unsigned code = anchored ? 0u : noRow ? 1u : 2u + static_cast<unsigned>(g_lensReadReport.result);
            const uint64_t sk = identity ^ (uint64_t(code) << 48) ^ (uint64_t(title) << 56);
            const uint64_t nowMs = GetTickCount64();
            if (sk != stateKey)
            {
                stateKey = sk;
                stateSinceMs = nowMs;
            }
            else if (nowMs - stateSinceMs >= 750 && stateLogged != sk)
            {
                stateLogged = sk;
                const char* gun = stateModel ? stateModel->name : (identity ? "unknown model" : "no held gun");
                const auto& r = g_lensReadReport;
                LOG("lens receipt state: %s %s (%s); age=%lldms serial=%s space=%s", gun,
                    anchored ? "ANCHORED" : "FALLBACK",
                    anchored ? "ok"
                    : noRow  ? "no kit lens row for this gun (visor/none zoom kind or unmeasured glass)"
                             : stateWhy,
                    static_cast<long long>(r.ageMs), r.serialMatch ? "match" : "differs",
                    r.spaceMatch ? "match" : "differs");
            }
        }
        const uint64_t key = identity ^ (anchored ? 0x8000000000000000ull : 0) ^ (uint64_t(title) << 56);
        if (key == loggedKey)
            return;
        const auto* model = weapon_model::Find(title, identity);
        const char* name = model ? model->name : (identity ? "unknown model" : "no held gun");
        if (identity)
            (void)ScopeWindowFor(title, identity);
        if (!anchored)
        {
            float unused[3];
            const char* why = "";
            (void)ReadAnchoredLens(unused, &why);
            loggedKey = key;
            const auto t = static_cast<unsigned>(title) < 7 ? static_cast<unsigned>(title) : 0u;
            LOG("lens proof: %s FALLBACK aim-derived lens (%s%s); lens receipts ok/empty=%u/%u", name,
                identity ? "" : "no fresh held-gun model identity (turret/vehicle seat or no model receipt); ", why,
                g_lensPublishOk[t].load(), g_lensPublishEmpty[t].load());
            return;
        }
        float node[3];
        if (!ReadAnchoredLens(node) || views.empty())
            return;
        const auto& v = views[0];
        lens_anchor::Eye eye;
        eye.position[0] = v.pose.position.x;
        eye.position[1] = v.pose.position.y;
        eye.position[2] = v.pose.position.z;
        eye.orientation[0] = v.pose.orientation.x;
        eye.orientation[1] = v.pose.orientation.y;
        eye.orientation[2] = v.pose.orientation.z;
        eye.orientation[3] = v.pose.orientation.w;
        eye.tanLeft = std::tan(v.fov.angleLeft);
        eye.tanRight = std::tan(v.fov.angleRight);
        eye.tanUp = std::tan(v.fov.angleUp);
        eye.tanDown = std::tan(v.fov.angleDown);
        eye.width = v.subImage.imageRect.extent.width;
        eye.height = v.subImage.imageRect.extent.height;
        float qx = 0, qy = 0, nx = 0, ny = 0, ax = 0, ay = 0;
        if (!lens_anchor::ProjectPx(eye, quad, qx, qy) || !lens_anchor::ProjectPx(eye, node, nx, ny) ||
            !lens_anchor::ProjectPx(eye, aimDerived, ax, ay))
        {
            loggedKey = key;
            LOG("lens proof: %s anchored, but the quad or node is outside the left eye's view (cannot project)", name);
            return;
        }
        loggedKey = key;
        LOG("lens proof: %s node=root(0) quad_px=(%.1f,%.1f) node_px=(%.1f,%.1f) d_px=%.1f (old aim-derived lens was %.1f px away)",
            name, qx, qy, nx, ny, std::sqrt((qx - nx) * (qx - nx) + (qy - ny) * (qy - ny)),
            std::sqrt((ax - nx) * (ax - nx) + (ay - ny) * (ay - ny)));
    }
    constinit std::atomic<bool> g_reticleChainFailed{false};
} // namespace vr_core
namespace
{
    // not constinit: its type has no constant initialiser; it is initialised dynamically, as before.
    std::vector<ID3D11Texture2D*> g_reticleImages;
    // not constinit: its type has no constant initialiser; it is initialised dynamically, as before.
    std::vector<ID3D11RenderTargetView*> g_reticleRtvs;
    constinit bool g_scopePassCaptured = false; // this scope raster captured an image
} // namespace
namespace vr_core
{
    constinit std::atomic<float> g_scopeImageGlassAspect{
        0.0f}; // glass aspect the current CE glass image was rendered for (0 = n/a)
} // namespace vr_core
namespace
{
    // The fraction (top-left S x S) of the scope cache the latest image fills. It is
    // set by VR_CaptureScope from the pass's VR_SetScopeImageSubrect and holds only for the
    // image with g_scopeImageSubrectSerial; any other capture path is a whole image.
    constinit float g_scopeImageSubrect = 1.0f, g_scopePendingSubrect = 1.0f;
    constinit uint32_t g_scopeImageSubrectSerial = 0;
    constinit ScopeRefreshScheduler g_scopeRefreshScheduler;
    constinit ScopeRefreshScheduler g_cockpitScheduler;
} // namespace
namespace vr_core
{
    void NoteScopeImageCaptured()
    {
        g_scopeImageCapturedMs.store(GetTickCount64(), std::memory_order_release);
        const int view = g_monitorPendingView.exchange(-1, std::memory_order_acq_rel);
        g_monitorViewSinceMs.store(0, std::memory_order_relaxed);
        g_scopeImageView = view;
        g_scopeImageSerial.fetch_add(1, std::memory_order_acq_rel);
        if (view < 0 || view >= vehicle_monitors::kViewCount)
            return;
        g_monitorViewsRendered[view].fetch_add(1, std::memory_order_relaxed);
        const bool on[vehicle_monitors::kViewCount]{
            g_config.vehicle_monitor_forward, g_config.vehicle_monitor_left, g_config.vehicle_monitor_right};
        vehicle_monitors::RoundRobin next;
        next.Resume(view);
        g_monitorCurrentView.store(next.Next(on), std::memory_order_release);
    }
} // namespace vr_core
namespace
{
    constinit std::atomic<uint64_t> g_lensMarkerMs{0};
    constinit ScopeZoomResolver g_scopeZoomResolver;
    constinit ScopeZoomController g_scopeZoomController;
    constinit uint64_t g_scopeZoomLastMs = 0;
    constinit std::atomic<uint64_t> g_scopeToggleSerial{0};
    constinit uint64_t g_scopeToggleObserved = 0;
    // Color the reticle was last painted with, so we repaint only when the
    // player changes it (not every frame). Sentinel forces the first paint.
    constinit float g_reticlePaintedColor[3] = {-1.0f, -1.0f, -1.0f};
    constinit bool g_reticleEnemyPainted = false; // which color is currently on the image
    // Set by the game layer when the crosshair is over an enemy (the engine's
    // target-lock state). While set, the reticle repaints red like the OG HUD.
    constinit std::atomic<bool> g_reticleEnemy{false};

    // Halo's class-2 CHUD widget is rendered into this small transparent
    // target instead of either eye. The resulting game-owned, per-weapon
    // artwork is uploaded to the existing controller-ray OpenXR quad.
    constinit ID3D11Texture2D* g_authoredReticleTexture = nullptr;
    // Halo 4's CUI reticle is bracketed around a subtree rather than guarded by
    // a visibility predicate. The non-capture eye therefore executes the full
    // subtree against this prepared throwaway target. It must not share the
    // authored target: drawing the second eye there would double-blend or
    // replace the pixels selected for the gun-ray quad.
    constinit ID3D11Texture2D* g_authoredReticleDiscardTexture = nullptr;
    // Two earlier guards checked the capture's IDENTITY and its PIECE COUNT,
    // and the runtime log proved both inert: across a whole combat session
    // the key and the count (`pieces 5, held 5`) never changed, while the
    // quad read SUBMITTED with held art throughout - and the player still
    // lost the crosshair on every hit. The same five widgets kept drawing,
    // so what changed can only be the PIXELS they produced. Measure that directly instead of guessing which engine
    // state did it: a capture that painted nothing visible must never reach
    // the quad. This also covers the crosshair being kicked outside the
    // magnified centre crop, which no identity or count can see either.
    constinit ID3D11ShaderResourceView* g_authoredReticleSrv = nullptr;
    // Last capture measured to actually contain art. The swapchain is only
    // ever fed from here, so a blank capture cannot become the held image.
    constinit ID3D11Texture2D* g_authoredReticleGoodTexture = nullptr;
    // 8x8 mip readback, summed. HOW MUCH ink the capture holds is the measure
    // that matters, not whether any survives: Reach's crosshair is five
    // widgets - ar_reticule plus l/r/t/b_crosshair - and the four petals are
    // animated by `weapon barrel error scale` (official HREK
    // `ui\chud\*.chud_definition`: every one carries
    // `external input B = weapon barrel error scale` and drives its `active`
    // animation from `extern 2`). Taking a hit spikes barrel error, the petals
    // bloom outward, and they leave the magnified centre crop this capture
    // reads - while the centre reticle can stay. A "did anything survive"
    // test would pass on that leftover dot and publish a crosshair with its
    // petals missing, which is the same disappearance from the player's seat.
    constinit ID3D11Texture2D* g_authoredReticleProbeStaging = nullptr;
    // The probe is deliberately pipelined. A refresh queues its tiny readback;
    // later frames poll this fence without flushing or waiting, while the last
    // known-good crosshair stays on the quad.
    constinit ID3D11Query* g_authoredReticleProbeFence = nullptr;
    constexpr uint32_t kAuthoredReticleCoveragePending = UINT32_MAX;
    constexpr uint32_t kAuthoredReticleProbeSize = 8;
    // Absolute floor for "there is nothing here at all".
    constexpr uint32_t kAuthoredReticleArtAlphaThreshold = 2;
    // A capture holding less than this fraction of the known-good ink is the
    // crosshair blooming out of the crop, not a new crosshair.
    constexpr uint32_t kAuthoredReticleInkNumerator = 1;
    constexpr uint32_t kAuthoredReticleInkDenominator = 2;
    // ...but a genuinely simpler crosshair (weapon swap) must not be held out
    // forever. After this many consecutive refusals the capture is accepted
    // and becomes the new reference.
    constexpr uint32_t kAuthoredReticleMaxConsecutiveHolds = 24;
} // namespace
namespace vr_core
{
    constinit uint32_t g_authoredReticleLastAlphaInk = 0;
    constinit uint32_t g_authoredReticleLastColorInk = 0;
} // namespace vr_core
namespace
{
    constinit bool g_authoredReticleProbeUsable = true;
    struct ReticleAimPosePublication
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint64_t> sampleMs{0};
        std::atomic<uint8_t> valid{0};
        // Solve-time support receipt. Filled from the receipt frozen
        // by the solve that produced this exact presented pose; never from a
        // live durable re-read at the producer. 0/false when the feature is
        // off.
        std::atomic<uint64_t> supportEpoch{0};
        std::atomic<uint8_t> supportTrusted{0};
        std::atomic<uint64_t> preparedSerial{0};
        std::atomic<float> qx{0.0f};
        std::atomic<float> qy{0.0f};
        std::atomic<float> qz{0.0f};
        std::atomic<float> qw{1.0f};
        std::atomic<float> px{0.0f};
        std::atomic<float> py{0.0f};
        std::atomic<float> pz{0.0f};
    };
    constinit ReticleAimPosePublication g_presentedReticleAimPose;
} // namespace
namespace vr_core
{
    void PublishPresentedReticleAimPose(
        const XrPosef* pose, uint64_t supportEpoch, bool supportTrusted, uint64_t solveSerial)
    {
        auto& published = g_presentedReticleAimPose;
        published.sequence.fetch_add(1, std::memory_order_acq_rel);
        const bool valid = pose != nullptr;
        published.sampleMs.store(valid ? GetTickCount64() : 0, std::memory_order_relaxed);
        published.valid.store(valid ? 1u : 0u, std::memory_order_relaxed);
        published.supportEpoch.store(valid ? supportEpoch : 0, std::memory_order_relaxed);
        published.supportTrusted.store(valid && supportTrusted ? 1u : 0u, std::memory_order_relaxed);
        published.preparedSerial.store(valid ? solveSerial : 0, std::memory_order_relaxed);
        published.qx.store(valid ? pose->orientation.x : 0.0f, std::memory_order_relaxed);
        published.qy.store(valid ? pose->orientation.y : 0.0f, std::memory_order_relaxed);
        published.qz.store(valid ? pose->orientation.z : 0.0f, std::memory_order_relaxed);
        published.qw.store(valid ? pose->orientation.w : 1.0f, std::memory_order_relaxed);
        published.px.store(valid ? pose->position.x : 0.0f, std::memory_order_relaxed);
        published.py.store(valid ? pose->position.y : 0.0f, std::memory_order_relaxed);
        published.pz.store(valid ? pose->position.z : 0.0f, std::memory_order_relaxed);
        published.sequence.fetch_add(1, std::memory_order_release);
    }
} // namespace vr_core
namespace
{
    constinit ID3D11PixelShader* g_scopeUploadPs = nullptr;
    constinit ID3D11PixelShader* g_scopeUploadPsLinearize = nullptr;
    constinit ID3D11ShaderResourceView* g_scopeMaskSrv = nullptr; // the held gun's scope window shape (null = circle)
    // not constinit: its type has no constant initialiser; it is initialised dynamically, as before.
    std::map<uint64_t, ID3D11ShaderResourceView*> g_scopeWindowCache; // model identity -> mask (null = none)
    // not constinit: its type has no constant initialiser; it is initialised dynamically, as before.
    std::map<uint64_t, const char*> g_scopeWindowShapeCache; // model identity -> local mask shape name
    // not constinit: its type has no constant initialiser; it is initialised dynamically, as before.
    std::map<uint64_t, bool> g_scopeWindowWideCache;        // model identity -> wide CE glass mask
    constinit ID3D11Buffer* g_scopeLensConstants = nullptr; // x = edge feather width (0..1 of radius)
    void ReleaseScopeCache()
    {
        if (g_scopeCacheSrv)
        {
            g_scopeCacheSrv->Release();
            g_scopeCacheSrv = nullptr;
        }
        if (g_scopeCacheRtv)
        {
            g_scopeCacheRtv->Release();
            g_scopeCacheRtv = nullptr;
        }
        if (g_scopeCache)
        {
            g_scopeCache->Release();
            g_scopeCache = nullptr;
        }
        g_scopeCacheDesc = {};
        g_scopeHasImage.store(false);
    }
} // namespace
namespace vr_core
{
    bool EnsureScopeCache(const D3D11_TEXTURE2D_DESC& sourceDesc)
    {
        if (!g_device || !sourceDesc.Width || !sourceDesc.Height || sourceDesc.SampleDesc.Count != 1)
            return false;
        if (g_scopeCache && g_scopeCacheRtv && g_scopeCacheSrv && g_scopeCacheDesc.Width == sourceDesc.Width &&
            g_scopeCacheDesc.Height == sourceDesc.Height && g_scopeCacheDesc.Format == sourceDesc.Format)
            return true;
        ReleaseScopeCache();
        D3D11_TEXTURE2D_DESC desc = sourceDesc;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        HRESULT hr = g_device->CreateTexture2D(&desc, nullptr, &g_scopeCache);
        // A TYPELESS source (CE's eye images can be) has no default view: name
        // the concrete format, as the swapchain source path does.
        const bool typeless = dxgi_view_format::IsTypeless(static_cast<uint32_t>(desc.Format));
        const DXGI_FORMAT viewFormat =
            static_cast<DXGI_FORMAT>(dxgi_view_format::Concrete(static_cast<uint32_t>(desc.Format)));
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = viewFormat;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = viewFormat;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        if (SUCCEEDED(hr))
            hr = g_device->CreateRenderTargetView(g_scopeCache, typeless ? &rtvDesc : nullptr, &g_scopeCacheRtv);
        if (SUCCEEDED(hr))
            hr = g_device->CreateShaderResourceView(g_scopeCache, typeless ? &srvDesc : nullptr, &g_scopeCacheSrv);
        if (FAILED(hr))
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                LOG("scope cache creation failed: HRESULT 0x%08X (%ux%u format %d)", (unsigned)hr, desc.Width,
                    desc.Height, (int)desc.Format);
            }
            ReleaseScopeCache();
            return false;
        }
        g_scopeCacheDesc = desc;
        LOG("scope private render cache created: %ux%u format %d", desc.Width, desc.Height, (int)desc.Format);
        return true;
    }
} // namespace vr_core
namespace
{
    // Per-gun scope window: <mod dir>/assets/scope-masks/<game>/<weapon>.png is the player's own extracted scope art (never in the
    // repo). Decoded once per gun, reduced to a 128x128 window mask (scope_window_logic.h); nothing found = circle.
    std::wstring ScopeMaskRoot()
    {
        HMODULE module = nullptr;
        wchar_t path[MAX_PATH]{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&ScopeMaskRoot), &module) ||
            !GetModuleFileNameW(module, path, MAX_PATH))
            return L"";
        std::wstring dir(path);
        dir.resize(dir.find_last_of(L'\\') + 1);
        return dir + L"assets\\scope-masks\\";
    }
    bool DecodePngAlpha(const std::wstring& file, std::vector<uint8_t>& alpha, int& width, int& height)
    {
        const HRESULT init =
            CoInitializeEx(nullptr, COINIT_MULTITHREADED); // S_FALSE / RPC_E_CHANGED_MODE: already initialised, fine
        IWICImagingFactory* factory = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;
        bool ok = false;
        if (SUCCEEDED(
                CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateDecoderFromFilename(
                file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
            SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                WICBitmapPaletteTypeCustom)))
        {
            UINT w = 0, h = 0;
            if (SUCCEEDED(converter->GetSize(&w, &h)) && w >= 8 && h >= 8 && w <= scope_window::kMaxSide &&
                h <= scope_window::kMaxSide)
            {
                std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4);
                if (SUCCEEDED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(bgra.size()), bgra.data())))
                {
                    alpha.resize(static_cast<size_t>(w) * h);
                    for (size_t i = 0; i < alpha.size(); ++i)
                        alpha[i] = bgra[i * 4 + 3];
                    width = static_cast<int>(w);
                    height = static_cast<int>(h);
                    ok = true;
                }
            }
        }
        if (converter)
            converter->Release();
        if (frame)
            frame->Release();
        if (decoder)
            decoder->Release();
        if (factory)
            factory->Release();
        if (SUCCEEDED(init))
            CoUninitialize();
        return ok;
    }
    ID3D11ShaderResourceView* LoadScopeWindow(
        GameTitle title, const char* modelName, const char** shapeName = nullptr, bool* wideGlass = nullptr)
    {
        static bool announced = false;
        const std::wstring root = ScopeMaskRoot();
        if (!announced)
        {
            announced = true;
            unsigned found = 0;
            for (const GameTitle t :
                {GameTitle::Halo2, GameTitle::Halo3, GameTitle::Halo3ODST, GameTitle::HaloReach, GameTitle::HaloCE})
            {
                WIN32_FIND_DATAW data{};
                const char* folder = scope_window::GameDir(t);
                const std::wstring pattern = root + std::wstring(folder, folder + strlen(folder)) + L"\\*.png";
                if (HANDLE h = FindFirstFileW(pattern.c_str(), &data); h != INVALID_HANDLE_VALUE)
                {
                    do
                    {
                        if (!wcsstr(data.cFileName, L"__"))
                            ++found;
                    } while (FindNextFileW(h, &data));
                    FindClose(h);
                }
            }
            if (found)
                LOG("scope masks: %u available in %ls (loaded per gun on first use)", found, root.c_str());
            else
                LOG("scope masks: none found in %ls (lens windows stay circular)", root.c_str());
        }
        const char* dir = scope_window::GameDir(title);
        char base[96];
        if (root.empty() || !dir || !scope_window::FileBase(modelName, base, sizeof(base)))
            return nullptr;
        const std::wstring file =
            root + std::wstring(dir, dir + strlen(dir)) + L"\\" + std::wstring(base, base + strlen(base)) + L".png";
        std::vector<uint8_t> alpha;
        int w = 0, h = 0;
        static scope_window::Window window;
        if (!g_device || !DecodePngAlpha(file, alpha, w, h) || !scope_window::Extract(alpha.data(), w, h, window) ||
            window.circle)
            return nullptr;
        // A wide CE mask fills the texture; the non-square quad gives it its shape.
        const bool wide = scope_window::CeWideGlass(title, w, h, window.aspect);
        const float natural = window.aspect;
        if (wide && !scope_window::Extract(alpha.data(), w, h, window, 1.0f / natural))
            return nullptr;
        if (wideGlass)
            *wideGlass = wide;
        if (wide)
            LOG("scope window: %s art window %.2f:1 fills the texture; the glass is a non-square quad "
                "(ce_sniper_glass_aspect %.2f)",
                modelName, natural, g_config.ce_sniper_glass_aspect);
        if (shapeName)
            *shapeName = scope_window::ShapeName(window);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = scope_window::kSize;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{window.mask, scope_window::kSize, 0};
        ID3D11Texture2D* texture = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        if (SUCCEEDED(g_device->CreateTexture2D(&desc, &init, &texture)))
            g_device->CreateShaderResourceView(texture, nullptr, &srv);
        if (texture)
            texture->Release();
        return srv;
    }
} // namespace
namespace vr_core
{
    bool EnsureScopeUploadPipeline()
    {
        if (g_scopeUploadVs && g_scopeUploadPs && g_scopeUploadPsLinearize && g_scopeUploadSampler &&
            g_scopeUploadRasterizer && g_scopeUploadDepthOff && g_scopeLensConstants)
            return true;
        static bool failed = false;
        if (failed || !g_device)
            return false;
        static const char* source = R"(
Texture2D srcTex : register(t0);
SamplerState smp : register(s0);
cbuffer Lens : register(b0) { float4 lens; float4 eye; float4 shadow; float4 wide; }; // lens: x = edge feather, y = 1 window mask (t1), 2 kit rounded rectangle (z = short/long), 0 = circle, w = #10 quad width/height (0 = square)
// S4 eye: xy = picture shift (uv), z = eyebox k, w = 1 on | shadow: xy = shadow centre (uv), z = 1 eyebox on, w = brightness floor
// z42 wide: x = parallax shift cap (glass heights): a wide glass's camera rendered GlassShiftMargin wider;
// y = #8 E5/z45: the image fills the top-left y x y of the texture (0 or 1 = all);
// zw = #12 z47: glass reprojection shift (texture uv), kept inside the crop's headroom
Texture2D<float> maskTex : register(t1);
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut vs_main(uint id : SV_VertexID) {
    VSOut o; float2 uv=float2((id<<1)&2,id&2);
    o.pos=float4(uv*float2(2,-2)+float2(-1,1),0,1); o.uv=uv; return o;
}
float lin(float c) { return c<=0.04045 ? c/12.92 : pow((c+0.055)/1.055,2.4); }
float4 paint(float2 uv, bool decode) {
    uint sw,sh; srcTex.GetDimensions(sw,sh);
    float sa=(float)sw/max(1.0,(float)sh), da=lens.w>0.0?lens.w:1.0;  // #10: the picture is cropped at the quad's aspect
    float2 phys=float2(da,1);                                             // one texel's drawn width/height
    float2 scale=sa>da ? float2(da/sa,1) : float2(1,sa/da);
    if(da>1.0) { float c=clamp(wide.x,0.0,1.0); scale/=max(1.0,max(min(1.0,da/sa)*(1.0+2.0*c/da),min(1.0,sa/da)*(1.0+2.0*c))); } // GlassShiftMargin (z42/z44)
    // S4: this eye's picture shift; the reticle follows it with a 10% lag.
    const bool stereo=eye.w>0.5;
    float2 suv=stereo?uv+eye.xy:uv;
    float2 at=0.5+(suv-0.5)*scale;
    { float2 room=max(float2(0,0),0.5-0.5*scale-abs(stereo?eye.xy*scale:float2(0,0))); at+=clamp(wide.zw,-room,room); } // #12 z47
    // #8 E5/z45: the filled top-left corner only, between its first and last texel centres.
    // z67 (A2-24): the pass fills lround(y*size) texels per axis, so the high end is that
    // count's last centre, half a texel in like the low end (was one whole texel in).
    // Mirrored by glass_reprojection::SubrectClamp.
    if(wide.y>0.0&&wide.y<1.0) { float2 dim=float2(sw,sh); float2 px1=1.0/dim;
        at=clamp(at*wide.y,0.5*px1,(floor(wide.y*dim+0.5)-0.5)*px1); }
    float3 rgb=srcTex.Sample(smp,at).rgb;
    // Off the image edge: the clamp sampler repeats the edge (dimmed by the
    // eyebox floor), never a black crescent (spec item 1).
    if(decode) rgb=float3(lin(rgb.r),lin(rgb.g),lin(rgb.b));
    float2 px=abs((stereo?uv-0.5+0.9*eye.xy:uv-0.5)*1024*phys);
    float outer=max((1-step(3,px.x))*(1-step(16,px.y)),
                    (1-step(3,px.y))*(1-step(16,px.x)));
    float inner=max((1-step(1.2,px.x))*(1-step(12,px.y)),
                    (1-step(1.2,px.y))*(1-step(12,px.x)));
    rgb=lerp(rgb,float3(0,0,0),outer);
    rgb=lerp(rgb,float3(0.35,1,0.35),inner);
    // Soft circular lens: the image fades into the view over the outer
    // feather band (no hard edge), premultiplied for the alpha quad.
    float radius=length((uv-0.5)*2);
    float aa=max(fwidth(radius),1.0/1024.0);
    float feather=clamp(lens.x,0.0,0.5);
    float alpha=1-smoothstep(max(0.985-feather,0.0)-aa,0.985+aa,radius);
    // Gun-specific window (sniper rounded rectangle, beam rifle oval, ...) from the kit scope art.
    if(lens.y>1.5)
    {
        // S2: the kit glass shape (long side across), same feather as the circle; z39: in drawn units,
        // so on a wide CE quad (no mask PNG) it fills the quad with round, unstretched corners.
        float2 half_=float2(0.985*da,0.985*clamp(lens.z,0.05,1.0));
        float corner=0.25*half_.y;
        float2 q=abs((uv-0.5)*2*phys)-(half_-corner);
        float d=length(max(q,0))+min(max(q.x,q.y),0)-corner;
        float daa=max(fwidth(d),1.0/1024.0);
        alpha=1-smoothstep(-feather*half_.y-daa,daa,d);
    }
    else if(lens.y>0.5) alpha=maskTex.Sample(smp,uv);
    if(stereo&&shadow.z>0.5)
    {
        // S4 scope shadow: a soft clear disc (radius k) that chases the eye; the glass stays opaque
        // and never darker than shadow.w (spec item 1: the glass is always live).
        float sd=length((uv-shadow.xy)*2*phys);
        float lit=eye.z<=0.0?0.0:1-smoothstep(max(eye.z-0.15,0.0),eye.z,sd);
        rgb*=max(lit,saturate(shadow.w));
    }
    return float4(rgb*alpha,alpha);
}
float4 ps_scope(VSOut i):SV_Target { return paint(i.uv,false); }
float4 ps_scope_linearize(VSOut i):SV_Target { return paint(i.uv,true); }
)";
        HRESULT hr = S_OK;
        auto compile = [&](const char* entry, const char* target) -> ID3DBlob*
        {
            ID3DBlob *blob = nullptr, *errors = nullptr;
            hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr, entry, target, 0, 0, &blob, &errors);
            if (FAILED(hr))
                LOG("scope shader %s failed: HRESULT 0x%08X: %s", entry, (unsigned)hr,
                    errors ? (const char*)errors->GetBufferPointer() : "no compiler text");
            if (errors)
                errors->Release();
            return blob;
        };
        ID3DBlob* blob = compile("vs_main", "vs_5_0");
        if (blob)
        {
            hr = g_device->CreateVertexShader(
                blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_scopeUploadVs);
            blob->Release();
        }
        if (SUCCEEDED(hr))
            blob = compile("ps_scope", "ps_5_0");
        if (SUCCEEDED(hr) && blob)
        {
            hr =
                g_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_scopeUploadPs);
            blob->Release();
        }
        if (SUCCEEDED(hr))
            blob = compile("ps_scope_linearize", "ps_5_0");
        if (SUCCEEDED(hr) && blob)
        {
            hr = g_device->CreatePixelShader(
                blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_scopeUploadPsLinearize);
            blob->Release();
        }
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        if (SUCCEEDED(hr))
            hr = g_device->CreateSamplerState(&sampler, &g_scopeUploadSampler);
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        if (SUCCEEDED(hr))
            hr = g_device->CreateRasterizerState(&raster, &g_scopeUploadRasterizer);
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = FALSE;
        if (SUCCEEDED(hr))
            hr = g_device->CreateDepthStencilState(&depth, &g_scopeUploadDepthOff);
        D3D11_BUFFER_DESC lensCb{};
        lensCb.ByteWidth = 64;
        lensCb.Usage = D3D11_USAGE_DYNAMIC;
        lensCb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        lensCb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (SUCCEEDED(hr))
            hr = g_device->CreateBuffer(&lensCb, nullptr, &g_scopeLensConstants);
        if (FAILED(hr))
        {
            failed = true;
            LOG("scope isolated upload pipeline failed: HRESULT 0x%08X", (unsigned)hr);
            auto release = [](auto*& p)
            {
                if (p)
                    p->Release();
                p = nullptr;
            };
            release(g_scopeUploadVs);
            release(g_scopeUploadPs);
            release(g_scopeUploadPsLinearize);
            release(g_scopeUploadSampler);
            release(g_scopeUploadRasterizer);
            release(g_scopeUploadDepthOff);
            release(g_scopeLensConstants);
            return false;
        }
        return true;
    }
} // namespace vr_core
namespace
{
    ID3D11ShaderResourceView* ScopeWindowFor(GameTitle title, uint64_t identity)
    {
        if (!identity || !g_config.scope_window_shape)
            return nullptr;
        const uint64_t key = scope_window::MaskCacheKey(title, identity);
        const auto cached = g_scopeWindowCache.find(key);
        if (cached != g_scopeWindowCache.end())
            return cached->second;
        const auto* model = weapon_model::Find(title, identity);
        const char* shape = "circle";
        bool wide = false;
        ID3D11ShaderResourceView* srv = model ? LoadScopeWindow(title, model->name, &shape, &wide) : nullptr;
        g_scopeWindowCache[key] = srv;
        g_scopeWindowWideCache[key] = srv && wide;
        g_scopeWindowShapeCache[key] = srv ? shape : "circle";
        LOG("scope window shape: %s -> %s", model ? model->name : "unknown model",
            srv ? "local mask" : "circle (no local mask for this gun)");
        return srv;
    }
    float RefreshScopeMaskState(GameTitle title, uint64_t identity)
    {
        const uint64_t key = scope_window::MaskCacheKey(title, identity);
        if (key != g_scopeMaskIdentity)
        {
            g_scopeMaskIdentity = key;
            g_scopeMaskSrv = identity && g_config.scope_window_shape ? ScopeWindowFor(title, identity) : nullptr;
            {
                const auto wide = g_scopeWindowWideCache.find(key);
                g_scopeMaskWide = g_scopeMaskSrv && wide != g_scopeWindowWideCache.end() && wide->second;
            }
            // No local mask -> the kit's glass shape (rectangular glass only).
            g_scopeKitAspect = 0;
            if (identity && !g_scopeMaskSrv)
                if (const auto* model = weapon_model::Find(title, identity))
                {
                    // The CE sniper stays a wide glass without its PNG: a rounded
                    // rectangle filling the wide quad (the kit has no glass measurement).
                    if (scope_window::CeWideGlassFallback(title, model->name))
                    {
                        g_scopeMaskWide = true;
                        g_scopeKitAspect = 1.0f;
                        LOG("scope window shape: %s -> wide rounded rectangle (no local mask; "
                            "ce_sniper_glass_aspect %.2f)",
                            model->name, g_config.ce_sniper_glass_aspect);
                    }
                    else if (const auto* kit = weapon_profile::KitLensRowFor(title, model->name))
                    {
                        g_scopeKitAspect = scope_window::KitShapeAspect(kit->glassAspect);
                        LOG("scope window shape: %s -> %s (kit glass %.2f)", model->name,
                            g_scopeKitAspect > 0 ? "kit rounded rectangle" : "circle", kit->glassAspect);
                    }
                }
        }
        // The glass aspect follows the F1 slider live (the mask itself never changes).
        const float glassAspect =
            scope_window::GlassAspect(g_config.scope_window_shape && g_scopeMaskWide, g_config.ce_sniper_glass_aspect);
        g_scopeGlassAspect.store(glassAspect, std::memory_order_release);
        return glassAspect;
    }

    void GlassReprojection(float& du, float& dv); // with the CE Anniversary glass state
    float GlassImageCap();                        // same
} // namespace
namespace vr_core
{
    bool UploadScopeToRtv(ID3D11RenderTargetView* rtv, const scope_parallax::Eye* eye, float viewportX,
        const D3D11_VIEWPORT* customViewport, int linearizeOverride)
    {
        if (!rtv || !EnsureScopeUploadPipeline())
            return false;
        const bool linearize = linearizeOverride >= 0
                                   ? linearizeOverride != 0
                                   : !IsSrgb(ScopeCacheViewFormat()) && IsSrgb((DXGI_FORMAT)g_xrFormat);
        ID3D11PixelShader* ps = linearize ? g_scopeUploadPsLinearize : g_scopeUploadPs;
        {
            // The lens gets a gentler version of the visor's feather.
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(g_context->Map(g_scopeLensConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                float* lens = static_cast<float*>(mapped.pData);
                lens[0] = visor_zoom::LensFeather(g_config.zoom_edge_softness);
                lens[1] = lens[2] = lens[3] = 0;
                for (int i = 4; i < 16; ++i)
                    lens[i] = 0;
                lens[12] =
                    GlassImageCap(); // GlassShiftMargin headroom (wide glass only), or the CEA glass image's own cap
                lens[13] = g_scopeImageSubrectSerial == g_scopeImageSerial.load(std::memory_order_acquire)
                               ? g_scopeImageSubrect
                               : 1.0f;
                GlassReprojection(lens[14], lens[15]);
                if (eye)
                {
                    lens[4] = eye->shiftU;
                    lens[5] = eye->shiftV;
                    lens[6] = eye->k;
                    lens[7] = 1.0f;
                    lens[8] = eye->shadowU;
                    lens[9] = eye->shadowV;
                    lens[10] = g_config.scope_eyebox ? 1.0f : 0.0f;
                    lens[11] = eye->floor;
                }
                // The held gun's own window shape from the player's local scope art (loaded once per gun); circle otherwise.
                const GameTitle title = TitleAdapter_GetActiveTitle();
                const uint64_t identity = VR_ZoomWeaponIdentity(title, TitleAdapter_GetGeneration(title));
                const float glassAspect = RefreshScopeMaskState(title, identity);
                const float kitAspect = g_scopeKitAspect;
                lens[3] = glassAspect;
                if (g_config.scope_window_shape && g_scopeMaskSrv)
                    lens[1] = 1.0f;
                else if (g_config.scope_window_shape && kitAspect > 0)
                {
                    lens[1] = 2.0f;
                    lens[2] = kitAspect;
                }
                if (!g_config.scope_window_shape)
                    g_scopeShapeApplied = "none";
                else if (g_scopeMaskSrv)
                {
                    const auto named = g_scopeWindowShapeCache.find(scope_window::MaskCacheKey(title, identity));
                    g_scopeShapeApplied = named != g_scopeWindowShapeCache.end() ? named->second : "circle";
                }
                else
                    g_scopeShapeApplied = kitAspect > 0 ? "rounded-rect" : "circle";
                g_context->Unmap(g_scopeLensConstants, 0);
            }
        }
        D3DStateBackup backup;
        backup.Capture(g_context);
        g_context->OMSetRenderTargets(1, &rtv, nullptr);
        D3D11_VIEWPORT viewport{viewportX, 0, (float)kScopeScreenWidth, (float)kScopeScreenHeight, 0, 1};
        if (customViewport)
            viewport = *customViewport;
        g_context->RSSetViewports(1, &viewport);
        g_context->RSSetState(g_scopeUploadRasterizer);
        g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_scopeUploadDepthOff, 0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetShader(g_scopeUploadVs, nullptr, 0);
        g_context->GSSetShader(nullptr, nullptr, 0);
        g_context->PSSetShader(ps, nullptr, 0);
        g_context->PSSetConstantBuffers(0, 1, &g_scopeLensConstants);
        g_context->PSSetShaderResources(0, 1, &g_scopeCacheSrv);
        {
            ID3D11ShaderResourceView* mask = g_config.scope_window_shape ? g_scopeMaskSrv : nullptr;
            g_context->PSSetShaderResources(1, 1, &mask);
        }
        g_context->PSSetSamplers(0, 1, &g_scopeUploadSampler);
        g_context->Draw(3, 0);
        backup.Restore(g_context);
        return true;
    }
} // namespace vr_core
namespace
{
    constinit ID3D11RenderTargetView* g_scopeInsetRtv = nullptr;
    constinit ID3D11Texture2D* g_scopeInsetBuffer = nullptr;
} // namespace
namespace vr_core
{
    void ReleaseScopeInset()
    {
        if (g_scopeInsetRtv)
            g_scopeInsetRtv->Release();
        g_scopeInsetRtv = nullptr;
        g_scopeInsetBuffer = nullptr;
    }
} // namespace vr_core
namespace
{
    bool ScopeProbeFormat(DXGI_FORMAT f)
    {
        switch (f)
        {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return true;
        default:
            return false;
        }
    }
} // namespace
namespace vr_core
{
    void ProbeScopeImage(ID3D11Texture2D* image, bool stereo)
    {
        auto& p = g_scopeImageProbe;
        if (!image || !g_context || !g_device)
            return;
        if (p.pending && p.staging)
        {
            D3D11_MAPPED_SUBRESOURCE m{};
            const HRESULT mapped = g_context->Map(p.staging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
            if (mapped == DXGI_ERROR_WAS_STILL_DRAWING)
                return; // not ready yet
            if (FAILED(mapped))
            {
                p.pending = false;
                return;
            } // drop this sample, keep probing
            double luma[2]{}, alpha[2]{};
            const int eyes = p.stereo ? 2 : 1;
            for (int y = 0; y < 64; ++y)
            {
                const auto* row = static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch;
                for (int e = 0; e < eyes; ++e)
                    for (int x = 0; x < 64; ++x)
                    {
                        const uint8_t* px = row + (e * 64 + x) * 4; // RGBA or BGRA: luma = mean of the 3 colours
                        luma[e] += (px[0] + px[1] + px[2]) / 3.0;
                        alpha[e] += px[3] / 255.0;
                    }
            }
            g_context->Unmap(p.staging, 0);
            p.pending = false;
            const double n = 64.0 * 64.0;
            if (p.stereo)
                LOG("scope image: mean luma %.1f alpha %.2f (left eye) | mean luma %.1f alpha %.2f (right eye)",
                    luma[0] / n, alpha[0] / n, luma[1] / n, alpha[1] / n);
            else
                LOG("scope image: mean luma %.1f alpha %.2f", luma[0] / n, alpha[0] / n);
        }
        const uint64_t now = GetTickCount64();
        if (now - p.lastMs < 1000)
            return;
        p.lastMs = now;
        D3D11_TEXTURE2D_DESC desc{};
        image->GetDesc(&desc);
        if (!ScopeProbeFormat(desc.Format) || desc.SampleDesc.Count != 1 || desc.Height < 64 ||
            desc.Width < (stereo ? 2u : 1u) * kScopeScreenWidth)
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                LOG("scope image: probe skipped (format %d, %ux%u)", (int)desc.Format, desc.Width, desc.Height);
            }
            return;
        }
        const UINT width = stereo ? 128 : 64;
        if (!p.staging || p.width != width || p.format != desc.Format)
        {
            if (p.staging)
            {
                p.staging->Release();
                p.staging = nullptr;
            }
            D3D11_TEXTURE2D_DESC sd{};
            sd.Width = width;
            sd.Height = 64;
            sd.MipLevels = 1;
            sd.ArraySize = 1;
            sd.Format = desc.Format;
            sd.SampleDesc.Count = 1;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(g_device->CreateTexture2D(&sd, nullptr, &p.staging)))
            {
                p.staging = nullptr;
                return;
            }
            p.width = width;
            p.format = desc.Format;
        }
        const UINT cy = kScopeScreenHeight / 2 - 32;
        for (UINT e = 0; e < (stereo ? 2u : 1u); ++e)
        {
            const UINT cx = e * kScopeScreenWidth + kScopeScreenWidth / 2 - 32;
            const D3D11_BOX box{cx, cy, 0, cx + 64, cy + 64, 1};
            g_context->CopySubresourceRegion(p.staging, 0, e * 64, 0, 0, image, 0, &box);
        }
        p.pending = true;
        p.stereo = stereo;
    }
    bool PrepareLensQuad(const float aimQ[4], const float aimP[3], bool viewsValid)
    {
        {
            // Never a glass sized by the previous gun's profile (weapon switch race).
            const GameTitle lensTitle = TitleAdapter_GetActiveTitle();
            const uint64_t held = VR_ZoomWeaponIdentity(lensTitle, TitleAdapter_GetGeneration(lensTitle));
            const uint64_t applied = Config_AppliedWeaponIdentity();
            static uint64_t waitingFor = 0;
            if (!scope_window::LensProfileReady(held, applied))
            {
                if (waitingFor != held)
                {
                    waitingFor = held;
                    LOG("scope quad: held weapon %016llX waits for its lens row (applied profile %016llX); no glass this frame",
                        static_cast<unsigned long long>(held), static_cast<unsigned long long>(applied));
                }
                return false;
            }
            waitingFor = 0;
            // The held gun's glass aspect before the quad is sized (never a square first frame).
            const float glassAspect = RefreshScopeMaskState(lensTitle, held);
            // And never a picture rendered for another glass (one zoomed frame after a swap).
            static bool waitingImage = false;
            const float imageAspect =
                g_scopeImageTitle == GameTitle::HaloCE ? g_scopeImageGlassAspect.load(std::memory_order_acquire) : 0.0f;
            if (!scope_window::GlassImageMatches(imageAspect, glassAspect))
            {
                if (!waitingImage)
                {
                    waitingImage = true;
                    LOG("scope quad: glass %.2f:1 waits for a picture rendered for it (latest is for %.2f:1); no glass this frame",
                        glassAspect, imageAspect);
                }
                return false;
            }
            waitingImage = false;
        }
        g_lensPlanTransform = ComputeScopeQuadTransform(aimQ, aimP,
            g_config.left_handed ? -g_config.scope_screen_right_m : g_config.scope_screen_right_m,
            g_config.scope_screen_up_m, g_config.scope_screen_forward_m, g_config.scope_screen_width_m);
        // A wide glass keeps the square's height and widens.
        g_lensPlanTransform.width *= g_scopeGlassAspect.load(std::memory_order_acquire);
        {
            float anchored[3];
            if (ReadAnchoredLens(anchored))
                for (int i = 0; i < 3; ++i)
                    g_lensPlanTransform.position[i] = anchored[i];
        }
        const scope_parallax::Settings settings{g_config.scope_parallax, g_config.scope_parallax_depth,
            g_config.scope_eyebox, g_config.scope_eyebox_tolerance_m, g_config.scope_eyebox_fade_m,
            g_config.scope_eyebox_other_eye, g_config.scope_parallax_max_shift, g_config.scope_eyebox_floor};
        bool stereo = false;
        if ((settings.parallax || settings.eyebox) && viewsValid && g_views.size() >= 2)
        {
            const XrQuaternionf qInv{-aimQ[0], -aimQ[1], -aimQ[2], aimQ[3]};
            for (int eye = 0; eye < 2; ++eye)
            {
                const XrVector3f& at = g_views[eye].pose.position;
                const XrVector3f local =
                    Rotate(qInv, {at.x - g_lensPlanTransform.position[0], at.y - g_lensPlanTransform.position[1],
                                     at.z - g_lensPlanTransform.position[2]});
                const float eyeLocal[3]{local.x, local.y, local.z};
                g_lensPlanEyes[eye] =
                    scope_parallax::Compute(eyeLocal, g_lensPlanTransform.width, settings, g_lensPlanTransform.height);
            }
            scope_parallax::ApplyOtherEye(g_lensPlanEyes, settings);
            stereo = g_lensPlanEyes[0].valid && g_lensPlanEyes[1].valid;
            static bool loggedStock = false;
            if (!stereo && !loggedStock)
            {
                loggedStock = true;
                LOG("scope parallax: an eye is at or behind the glass (or its pose is bad); stock picture this frame");
            }
        }
        return PrepareScopeImageDelivery(stereo ? g_lensPlanEyes : nullptr);
    }

    void DrawScopeInset(ID3D11Texture2D* backbuffer, const D3D11_TEXTURE2D_DESC& bd)
    {
        if (!g_config.mirror_scope_inset || !backbuffer || !g_device || !g_context)
            return;
        const GameTitle title = TitleAdapter_GetActiveTitle();
        if (title != GameTitle::HaloReach && title != GameTitle::Halo3 && title != GameTitle::Halo3ODST &&
            title != GameTitle::Halo2 && title != GameTitle::Halo4)
            return;
        const uint64_t at = g_lensSubmittedMs.load(std::memory_order_acquire), now = GetTickCount64();
        if (!at || now < at || now - at > 100 || !g_scopeHasImage.load(std::memory_order_acquire) || !g_scopeCacheSrv)
            return;
        if (bd.SampleDesc.Count != 1 || bd.Width < 256 || bd.Height < 256)
            return;
        static ID3D11Texture2D* failedBuffer = nullptr; // give up on this backbuffer until it changes
        if (backbuffer == failedBuffer)
            return;
        if (g_scopeInsetBuffer != backbuffer)
        {
            ReleaseScopeInset();
            // A TYPELESS backbuffer needs a typed view (UNORM sibling).
            D3D11_RENDER_TARGET_VIEW_DESC view{};
            view.Format = bd.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS || bd.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS
                              ? UnormSibling(bd.Format)
                              : bd.Format;
            view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            if (FAILED(g_device->CreateRenderTargetView(backbuffer, &view, &g_scopeInsetRtv)))
            {
                g_scopeInsetRtv = nullptr;
                failedBuffer = backbuffer;
                static bool logged = false;
                if (!logged)
                {
                    logged = true;
                    LOG("monitor scope inset: no render target on the desktop backbuffer (format %d)", (int)bd.Format);
                }
                return;
            }
            g_scopeInsetBuffer = backbuffer;
        }
        const float side = std::floor(static_cast<float>(bd.Height) * 0.30f), margin = 16.0f;
        const D3D11_VIEWPORT viewport{static_cast<float>(bd.Width) - side - margin,
            static_cast<float>(bd.Height) - side - margin, side, side, 0.0f, 1.0f};
        const int linearize = !IsSrgb(ScopeCacheViewFormat()) && IsSrgb(bd.Format) ? 1 : 0;
        const bool drawn = UploadScopeToRtv(g_scopeInsetRtv, nullptr, 0.0f, &viewport, linearize);
        static bool logged = false;
        if (!logged && drawn)
        {
            logged = true;
            LOG("monitor scope inset: %.0fx%.0f at the bottom-right of the %ux%u desktop (mirror_scope_inset)", side,
                side, bd.Width, bd.Height);
        }
    }
} // namespace vr_core
namespace
{
    // Create the crosshair swapchain on first use (lazily, once the session is
    // running — SteamVR presented pre-session eye chains black) and paint the
    // reticle into it a single time.  Four cyan arc segments and short inner
    // ticks echo Halo 3's original blue CHUD reticle, but at a VR-friendly
    // angular size. A dark-blue outline keeps it legible without a black disc.
    // Paint the reticle image in the given color (0-1 per channel). The bright
    // arcs/ticks take the color; the outline is a darkened version of the same
    // hue so it reads at any color. Called on first use and whenever the color
    // changes (a settings edit, or the enemy-red switch below) — not per frame.
    std::vector<uint32_t> BuildReticlePixels(float cr, float cg, float cb, float opacity)
    {
        std::vector<uint32_t> px(kReticleSize * kReticleSize);
        const float c = (kReticleSize - 1) * 0.5f;
        const float scale = kReticleSize / 64.0f;
        // coverage: 1 inside the shape, 0 outside, ~1px linear edge for AA
        auto cov = [scale](float d, float halfWidth)
        {
            const float v = (halfWidth - d) * scale + 0.5f;
            return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        };
        // Bright fill = the requested color at full brightness; outline = the
        // same color at ~28% (a legible dark rim without a black disc).
        const float brR = fminf(cr, 1.0f) * 255.0f, brG = fminf(cg, 1.0f) * 255.0f, brB = fminf(cb, 1.0f) * 255.0f;
        const float olR = brR * 0.28f, olG = brG * 0.28f, olB = brB * 0.28f;
        for (uint32_t y = 0; y < kReticleSize; ++y)
            for (uint32_t x = 0; x < kReticleSize; ++x)
            {
                const float dx = (x - c) / scale, dy = (y - c) / scale;
                const float r = sqrtf(dx * dx + dy * dy);
                const float ax = fabsf(dx), ay = fabsf(dy);
                const bool cardinal = ax > ay * 1.22f || ay > ax * 1.22f;
                const float dRing = fabsf(r - 19.0f);
                const float arc = cardinal ? cov(dRing, 1.55f) : 0.0f;
                const float arcOutline = cardinal ? cov(dRing, 3.0f) : 0.0f;
                const float tickDistance = (ax > ay) ? ax : ay;
                const float tickWidth = (ax > ay) ? ay : ax;
                const float tick = (tickDistance >= 7.0f && tickDistance <= 11.5f) ? cov(tickWidth, 1.2f) : 0.0f;
                const float tickOutline = (tickDistance >= 5.8f && tickDistance <= 12.7f) ? cov(tickWidth, 2.6f) : 0.0f;
                const float bright = fmaxf(arc, tick);
                const float outline = fmaxf(arcOutline, tickOutline);
                const uint32_t r8 = (uint32_t)(opacity * fminf(255.0f, olR * outline + brR * bright) + 0.5f);
                const uint32_t g8 = (uint32_t)(opacity * fminf(255.0f, olG * outline + brG * bright) + 0.5f);
                const uint32_t b8 = (uint32_t)(opacity * fminf(255.0f, olB * outline + brB * bright) + 0.5f);
                const uint32_t a8 = (uint32_t)(opacity * outline * 255.0f + 0.5f);
                // OpenXR's preferred swapchain is RGBA8 on this runtime.
                px[y * kReticleSize + x] = (a8 << 24) | (b8 << 16) | (g8 << 8) | r8;
            }
        return px;
    }

    bool PaintReticle(float cr, float cg, float cb, float opacity)
    {
        const auto px = BuildReticlePixels(cr, cg, cb, opacity);
        uint32_t idx = 0;
        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wi.timeout = 1000000000;
        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if (XR_FAILED(xrAcquireSwapchainImage(g_reticleChain, &ai, &idx)) ||
            XR_FAILED(xrWaitSwapchainImage(g_reticleChain, &wi)))
            return false;
        g_context->UpdateSubresource(g_reticleImages[idx], 0, nullptr, px.data(), kReticleSize * 4, 0);
        xrReleaseSwapchainImage(g_reticleChain, &ri);
        return true;
    }
} // namespace
namespace vr_core
{
    bool PrepareDualReticleImage()
    {
        if (g_dualReticleFailed)
            return false;
        const bool rgba = g_xrFormat == DXGI_FORMAT_R8G8B8A8_UNORM || g_xrFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        const bool bgra = g_xrFormat == DXGI_FORMAT_B8G8R8A8_UNORM || g_xrFormat == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        if (!rgba && !bgra)
            return false;
        if (g_dualReticleChain == XR_NULL_HANDLE)
        {
            const uint64_t now = GetTickCount64();
            if (now < g_dualReticleRetryMs)
                return false;
            g_dualReticleRetryMs = now + 5000;
            if (!CreateChain(kReticleSize, kReticleSize, g_dualReticleChain, g_dualReticleImages, g_dualReticleRtvs,
                    "independent dual-weapon reticles"))
                return false;
        }
        const float color[3]{g_config.reticle_r, g_config.reticle_g, g_config.reticle_b};
        const bool dirty = !g_dualReticleReady || color[0] != g_dualReticleColor[0] ||
                           color[1] != g_dualReticleColor[1] || color[2] != g_dualReticleColor[2];
        if (!dirty && !g_dualReticleAcquired)
            return true;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        wait.timeout = 0;
        if (!g_dualReticleAcquired)
        {
            if (xrAcquireSwapchainImage(g_dualReticleChain, &acquire, &g_dualReticleIndex) != XR_SUCCESS)
                return false;
            g_dualReticleAcquired = true;
        }
        const XrResult ready = xrWaitSwapchainImage(g_dualReticleChain, &wait);
        if (ready == XR_TIMEOUT_EXPIRED)
            return false;
        if (ready != XR_SUCCESS)
        {
            g_dualReticleFailed = true;
            LOG("Dual reticles: optional image wait failed (%d); existing crosshair retained", ready);
            return false;
        }
        const bool valid = g_dualReticleIndex < g_dualReticleImages.size();
        if (valid)
        {
            const auto pixels =
                BuildReticlePixels(bgra ? color[2] : color[0], color[1], bgra ? color[0] : color[2], 1.f);
            g_context->UpdateSubresource(
                g_dualReticleImages[g_dualReticleIndex], 0, nullptr, pixels.data(), kReticleSize * 4, 0);
        }
        const XrResult released = xrReleaseSwapchainImage(g_dualReticleChain, &release);
        g_dualReticleAcquired = false;
        if (!valid || released != XR_SUCCESS)
        {
            g_dualReticleFailed = true;
            LOG("Dual reticles: optional image release failed (%d); existing crosshair retained", released);
            return false;
        }
        std::copy(std::begin(color), std::end(color), g_dualReticleColor);
        g_dualReticleReady = true;
        return true;
    }
} // namespace vr_core
namespace
{
    void ReleaseAuthoredReticleProbe()
    {
        if (g_authoredReticleSrv)
        {
            g_authoredReticleSrv->Release();
            g_authoredReticleSrv = nullptr;
        }
        if (g_authoredReticleGoodTexture)
        {
            // The reticle publish samples this texture, so its cached view
            // holds a reference; drop that before releasing ours.
            ForgetSourceView(g_authoredReticleGoodTexture);
            g_authoredReticleGoodTexture->Release();
            g_authoredReticleGoodTexture = nullptr;
        }
        if (g_authoredReticleProbeStaging)
        {
            g_authoredReticleProbeStaging->Release();
            g_authoredReticleProbeStaging = nullptr;
        }
        if (g_authoredReticleProbeFence)
        {
            g_authoredReticleProbeFence->Release();
            g_authoredReticleProbeFence = nullptr;
        }
        g_authoredReticleProbePending = false;
        g_authoredReticleGoodValid = false;
    }

    bool EnsureAuthoredReticleTexture()
    {
        if (g_authoredReticleTexture && g_authoredReticleRtv)
            return true;
        if (!g_device)
            return false;

        D3D11_TEXTURE2D_DESC desc{};
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        desc.Width = kReticleSize;
        desc.Height = kReticleSize;
        // A full mip chain is what makes the content check cheap: the engine
        // downsamples the capture for us and we read one tiny level.
        desc.MipLevels = 0;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = desc.Format;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
        const HRESULT textureResult = g_device->CreateTexture2D(&desc, nullptr, &g_authoredReticleTexture);
        const HRESULT rtvResult = SUCCEEDED(textureResult) ? g_device->CreateRenderTargetView(g_authoredReticleTexture,
                                                                 &rtvDesc, &g_authoredReticleRtv)
                                                           : E_FAIL;
        if (FAILED(textureResult) || FAILED(rtvResult))
        {
            static std::atomic<bool> loggedCreationFailure{false};
            if (!loggedCreationFailure.exchange(true, std::memory_order_relaxed))
            {
                LOG("authored crosshair capture target creation failed "
                    "(texture=0x%08X rtv=0x%08X); optional title capture "
                    "will retry without affecting stereo",
                    static_cast<unsigned>(textureResult), static_cast<unsigned>(rtvResult));
            }
            if (g_authoredReticleRtv)
            {
                g_authoredReticleRtv->Release();
                g_authoredReticleRtv = nullptr;
            }
            if (g_authoredReticleTexture)
            {
                g_authoredReticleTexture->Release();
                g_authoredReticleTexture = nullptr;
            }
            return false;
        }

        // The content check and the known-good copy are OPTIONAL. If any of
        // them cannot be created, the mip-chained capture target is torn back
        // down and rebuilt exactly as it was before this candidate - the
        // swapchain copy requires matching mip counts, so a half-built guard
        // must not be left in place. A diagnostic must never be able to take
        // the crosshair down (AGENTS.md failure isolation).
        D3D11_TEXTURE2D_DESC goodDesc = desc;
        goodDesc.MipLevels = 1;
        goodDesc.MiscFlags = 0;
        goodDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_TEXTURE2D_DESC probeDesc{};
        probeDesc.Width = kAuthoredReticleProbeSize;
        probeDesc.Height = kAuthoredReticleProbeSize;
        probeDesc.MipLevels = 1;
        probeDesc.ArraySize = 1;
        probeDesc.Format = desc.Format;
        probeDesc.SampleDesc.Count = 1;
        probeDesc.Usage = D3D11_USAGE_STAGING;
        probeDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        D3D11_QUERY_DESC probeQueryDesc{};
        probeQueryDesc.Query = D3D11_QUERY_EVENT;
        g_authoredReticleProbeUsable =
            SUCCEEDED(g_device->CreateShaderResourceView(g_authoredReticleTexture, nullptr, &g_authoredReticleSrv)) &&
            SUCCEEDED(g_device->CreateTexture2D(&goodDesc, nullptr, &g_authoredReticleGoodTexture)) &&
            SUCCEEDED(g_device->CreateTexture2D(&probeDesc, nullptr, &g_authoredReticleProbeStaging)) &&
            SUCCEEDED(g_device->CreateQuery(&probeQueryDesc, &g_authoredReticleProbeFence));
        g_authoredReticleGoodValid = false;

        if (!g_authoredReticleProbeUsable)
        {
            ReleaseAuthoredReticleProbe();
            g_authoredReticleRtv->Release();
            g_authoredReticleRtv = nullptr;
            g_authoredReticleTexture->Release();
            g_authoredReticleTexture = nullptr;
            desc.MipLevels = 1;
            desc.MiscFlags = 0;
            if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &g_authoredReticleTexture)) ||
                FAILED(g_device->CreateRenderTargetView(g_authoredReticleTexture, nullptr, &g_authoredReticleRtv)))
            {
                if (g_authoredReticleRtv)
                {
                    g_authoredReticleRtv->Release();
                    g_authoredReticleRtv = nullptr;
                }
                if (g_authoredReticleTexture)
                {
                    g_authoredReticleTexture->Release();
                    g_authoredReticleTexture = nullptr;
                }
                return false;
            }
        }
        LOG("authored crosshair capture target ready (%ux%u); blank-art "
            "guard %s",
            kReticleSize, kReticleSize,
            g_authoredReticleProbeUsable ? "armed" : "UNAVAILABLE - crosshair keeps its previous behavior");
        return true;
    }

    bool EnsureAuthoredReticleDiscardTexture()
    {
        if (g_authoredReticleDiscardTexture && g_authoredReticleDiscardRtv)
            return true;
        if (!g_device)
            return false;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = kReticleSize;
        desc.Height = kReticleSize;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &g_authoredReticleDiscardTexture)) ||
            FAILED(g_device->CreateRenderTargetView(
                g_authoredReticleDiscardTexture, nullptr, &g_authoredReticleDiscardRtv)))
        {
            if (g_authoredReticleDiscardRtv)
            {
                g_authoredReticleDiscardRtv->Release();
                g_authoredReticleDiscardRtv = nullptr;
            }
            if (g_authoredReticleDiscardTexture)
            {
                g_authoredReticleDiscardTexture->Release();
                g_authoredReticleDiscardTexture = nullptr;
            }
            return false;
        }
        LOG("authored crosshair suppression target ready (%ux%u)", kReticleSize, kReticleSize);
        return true;
    }

    // How much crosshair ink does the capture hold? Returns the summed alpha
    // of the downsampled capture. Once a valid image is held, the readback is
    // enqueued then polled on later frames: this returns the explicit pending
    // sentinel instead of ever stalling the headset render thread.
    uint32_t MeasureAuthoredReticleCoverage()
    {
        if (!g_authoredReticleProbeUsable || !g_context || !g_authoredReticleSrv || !g_authoredReticleProbeStaging ||
            !g_authoredReticleProbeFence || !g_authoredReticleTexture)
        {
            return 0;
        }

        D3D11_TEXTURE2D_DESC desc{};
        g_authoredReticleTexture->GetDesc(&desc);
        uint32_t probeMip = 0;
        uint32_t width = desc.Width;
        while (width > kAuthoredReticleProbeSize && probeMip + 1 < desc.MipLevels)
        {
            width >>= 1;
            ++probeMip;
        }
        if (width != kAuthoredReticleProbeSize)
            return 0;

        if (g_authoredReticleGoodValid)
        {
            if (!g_authoredReticleProbePending)
            {
                g_context->GenerateMips(g_authoredReticleSrv);
                g_context->CopySubresourceRegion(
                    g_authoredReticleProbeStaging, 0, 0, 0, 0, g_authoredReticleTexture, probeMip, nullptr);
                g_context->End(g_authoredReticleProbeFence);
                g_authoredReticleProbePending = true;
                return kAuthoredReticleCoveragePending;
            }

            BOOL complete = FALSE;
            if (g_context->GetData(
                    g_authoredReticleProbeFence, &complete, sizeof(complete), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
                !complete)
            {
                return kAuthoredReticleCoveragePending;
            }
            g_authoredReticleProbePending = false;
        }
        else
        {
            // Bootstrap once so the player receives art before the defer policy
            // has a known-good image to hold.
            g_context->GenerateMips(g_authoredReticleSrv);
            g_context->CopySubresourceRegion(
                g_authoredReticleProbeStaging, 0, 0, 0, 0, g_authoredReticleTexture, probeMip, nullptr);
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(g_context->Map(g_authoredReticleProbeStaging, 0, D3D11_MAP_READ, 0, &mapped)))
        {
            return 0;
        }
        uint32_t alphaInk = 0;
        uint32_t colorInk = 0;
        const auto* rows = static_cast<const uint8_t*>(mapped.pData);
        for (uint32_t y = 0; y < kAuthoredReticleProbeSize; ++y)
        {
            const uint8_t* texel = rows + static_cast<size_t>(y) * mapped.RowPitch;
            for (uint32_t x = 0; x < kAuthoredReticleProbeSize; ++x, texel += 4)
            {
                alphaInk += texel[3];
                colorInk += static_cast<uint32_t>(texel[0]) + texel[1] + texel[2];
            }
        }
        g_context->Unmap(g_authoredReticleProbeStaging, 0);
        g_authoredReticleLastAlphaInk = alphaInk;
        g_authoredReticleLastColorInk = colorInk;
        // CE's native bitmap pass writes RGB only. Like Halo 4's CUI, its
        // transparent capture cannot be judged by the untouched alpha plane.
        // This is independently proven by CE's native blend descriptor.
        if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE)
            return halo_ce::ReticleVisibleInk(alphaInk, colorInk);
        // Other titles retain their accepted coverage policy.
        return VrResolveAuthoredReticleCoverage(TitleAdapter_GetActiveTitle() == GameTitle::Halo4, alphaInk, colorInk);
    }
} // namespace
namespace vr_core
{
    bool EnsureReticleChain()
    {
        if (g_reticleChainFailed.load(std::memory_order_acquire))
            return false;
        if (g_reticleChain == XR_NULL_HANDLE)
        {
            if (!CreateChain(kReticleSize, kReticleSize, g_reticleChain, g_reticleImages, g_reticleRtvs, "crosshair"))
            {
                g_reticleChainFailed.store(true, std::memory_order_release);
                return false;
            }
        }
        const GameTitle reticleTitle = TitleAdapter_GetActiveTitle();
        const bool titleHasAuthoredCapture = Game_TitleCapturesAuthoredCrosshair();
        // A live optional Halo 4 capture hook is not proof that valid authored
        // pixels have reached this swapchain. On its bootstrap frame we must
        // paint the procedural gun-ray reticle before a deferred or rejected
        // upload can leave the newly-created swapchain as undefined pixels.
        const bool halo4ProceduralBootstrap =
            reticleTitle == GameTitle::Halo4 &&
            Halo4CuiReticleUsesProceduralFallback(titleHasAuthoredCapture, g_config.crosshair, g_config.kill_reticle);
        const bool ceProceduralBootstrap =
            reticleTitle == GameTitle::HaloCE && halo_ce::ReticleNeedsProceduralBootstrap(titleHasAuthoredCapture,
                                                     g_authoredReticleGoodValid, g_reticleContainsAuthored);
        const bool proceduralBootstrap = halo4ProceduralBootstrap || ceProceduralBootstrap;
        const bool authoredThisFrame = g_authoredReticleReady && g_authoredReticleSerial == g_preparedFrame.serial;
        if (authoredThisFrame && !proceduralBootstrap)
            return true;

        // Halo can omit the authored widget in some of the repeated FP passes
        // of one displayed frame. Retain the last authored image across that
        // short gap; otherwise the render thread alternates upload/clear and
        // pays a swapchain repaint every frame. A genuine death/loading gap
        // still clears once after this small grace window.
        // This must comfortably exceed the authored upload's frame cap. At 2,
        // the grace expired between capped uploads, the procedural reticle was
        // repainted over the authored art, and the next upload put it back -
        // the two crosshairs visibly alternating at the beat frequency of the
        // two numbers. Holding the last authored image instead is also what
        // removes the per-frame swapchain repaint this comment warns about,
        // which is the same cost Halo 3 and ODST pay.
        constexpr uint64_t kAuthoredReticleGraceFrames = 24;
        const bool authoredCaptureRecent =
            g_authoredReticleSerial != 0 && g_preparedFrame.serial >= g_authoredReticleSerial &&
            g_preparedFrame.serial - g_authoredReticleSerial <= kAuthoredReticleGraceFrames;
        if (g_reticleContainsAuthored && authoredCaptureRecent && !ceProceduralBootstrap)
            return true;

        // Once the swapchain holds real authored art, LEAVE IT ALONE. For a
        // title that captures its crosshair, repainting can only ever destroy
        // that art - it paints the procedural reticle, which for these titles
        // is fully transparent, so the crosshair simply vanishes until the next
        // upload restores it. That is the flashing, and the repaint is also
        // pure per-frame cost for something that does not change.
        //
        // Reach legitimately stops drawing its crosshair during reloads, melee
        // and similar, which made the old grace window expire and wipe the art
        // mid-fight. Holding the last art is both correct and free; a genuine
        // change (weapon swap, zoom, colour) re-uploads through the key path.
        if (g_reticleContainsAuthored && titleHasAuthoredCapture && !ceProceduralBootstrap)
            return true;

        // Halo can stop drawing its authored widget during death and other
        // non-gameplay states. Keep the old procedural fallback fully
        // transparent so it cannot appear close to the viewer; authored
        // crosshairs use UploadAuthoredReticle below and are unaffected.
        // The procedural reticle is normally transparent: titles with the
        // authored CHUD capture (Halo 3) get their visible crosshair from
        // UploadAuthoredReticle, and a visible procedural fallback could flash
        // during death/loading gaps. The private ODST camera core installs no
        // authored capture yet, so there the procedural reticle IS the
        // crosshair and must be opaque to be seen at all.
        // Reach DOES capture authored art now - its crosshair widgets are
        // found through the owning collection's scripting class rather than a
        // class-2 gate - so it is in the same case as Halo 3: the authored
        // widget is the crosshair and the procedural one must stay invisible.
        // While Reach was still listed here as having no capture, it painted
        // the procedural reticle fully opaque into the SAME swapchain the
        // authored art lives in, wiping that art out on every repaint. That is
        // what showed the old crosshair, made the two alternate, and cost a
        // swapchain repaint per frame.
        const float kProceduralOpacity = titleHasAuthoredCapture && !proceduralBootstrap ? 0.0f : 1.0f;
        const bool enemy = g_reticleEnemy.load(std::memory_order_relaxed);
        const float wantR = enemy ? 1.0f : g_config.reticle_r;
        const float wantG = enemy ? 0.18f : g_config.reticle_g;
        const float wantB = enemy ? 0.14f : g_config.reticle_b;
        // Repaint only when the desired color OR opacity changed (compositor
        // keeps showing the last released image, so a static reticle costs
        // nothing per frame). Opacity is included so a Halo 3 -> ODST transition
        // repaints the swapchain from transparent to visible.
        const bool colorChanged = g_reticleContainsAuthored || g_reticleEnemyPainted != enemy ||
                                  g_reticlePaintedOpacity != kProceduralOpacity ||
                                  (!enemy && (g_reticlePaintedColor[0] != g_config.reticle_r ||
                                                 g_reticlePaintedColor[1] != g_config.reticle_g ||
                                                 g_reticlePaintedColor[2] != g_config.reticle_b));
        if (!colorChanged)
            return true;
        const bool clearingAuthored = g_reticleContainsAuthored;
        if (!PaintReticle(wantR, wantG, wantB, kProceduralOpacity))
        {
            g_reticleChainFailed.store(true, std::memory_order_release);
            return false;
        }
        g_reticlePaintedColor[0] = wantR;
        g_reticlePaintedColor[1] = wantG;
        g_reticlePaintedColor[2] = wantB;
        g_reticlePaintedOpacity = kProceduralOpacity;
        g_reticleEnemyPainted = enemy;
        g_reticleContainsAuthored = false;
        if (clearingAuthored)
            LOG("authored crosshair cleared after capture stopped");
        return true;
    }

    bool UploadAuthoredReticle(bool requireSuccessfulRelease, bool identityChanged)
    {
        // Halo 4 publishes the fixed sentinel identity 1 while its
        // legitimate bloom and target tint can swing ink by more than 10x.
        // Treat each H4 sample like a fresh identity so the shared half-ink
        // guard cannot freeze animation. The absolute nonblank bar remains.
        if (TitleAdapter_GetActiveTitle() == GameTitle::Halo4)
            identityChanged = true;
        const bool probePending = g_authoredReticleProbePending;
        if ((!probePending && (!g_authoredReticleReady || g_authoredReticleSerial != g_preparedFrame.serial)) ||
            !g_authoredReticleTexture || g_reticleChain == XR_NULL_HANDLE)
            return false;
        g_authoredReticleHeldBlank = false;
        if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE &&
            (!g_authoredReticleProbeUsable || !g_authoredReticleGoodTexture))
        {
            // Optional capture measurement failure keeps CE's visible gun-ray
            // fallback. Never label an unmeasured texture as authored artwork.
            g_authoredReticleHeldBlank = true;
            return false;
        }
        if (!probePending && g_authoredReticleUploadedSerial == g_authoredReticleSerial)
            return true;

        // Decide what this upload is allowed to publish BEFORE touching the
        // swapchain. A capture with visible pixels becomes the new known-good
        // art; one without is refused outright, so the quad keeps showing the
        // last crosshair the player could actually see instead of the blank
        // the engine just handed us.
        ID3D11Texture2D* source = g_authoredReticleTexture;
        if (g_authoredReticleProbeUsable && g_authoredReticleGoodTexture)
        {
            const uint32_t ink = MeasureAuthoredReticleCoverage();
            if (ink == kAuthoredReticleCoveragePending)
            {
                // The released known-good image remains visible. Defer is not a
                // failure and must never let a blank capture replace it.
                g_authoredReticleHeldBlank = true;
                return false;
            }
            g_authoredReticleLastCoverage = ink;
            // The bar to clear: something at all, and at least half the ink of
            // the crosshair currently on the quad. The second half is what
            // catches the bloom - the petals leaving the crop takes most of
            // the ink with them even when the centre reticle stays behind.
            // ...but only when this is the SAME crosshair. The half-ink bar
            // exists to catch a crosshair blooming out of the crop, which by
            // definition redraws the same widgets and so folds the same
            // identity key. A DIFFERENT crosshair - a weapon swap - legitimately
            // carries less ink, and holding it to the old one's coverage is
            // what left the previous weapon's reticle on the quad. A preserved
            // Halo 3 log measured exactly that: ink fell 446 -> 106 across a
            // switch and the guard held for 24 refusals (~13 s) before letting
            // it through. ODST samples one frame in thirty, so the same 24
            // refusals there are hundreds of frames.
            const uint32_t required =
                (g_authoredReticleGoodValid && !identityChanged)
                    ? (g_authoredReticleGoodInk * kAuthoredReticleInkNumerator) / kAuthoredReticleInkDenominator
                    : 0;
            const bool hasAnyArt = ink >= kAuthoredReticleArtAlphaThreshold;
            // The escape hatch below exists so a genuinely simpler crosshair
            // (a weapon swap) is never held out forever. It must NEVER apply
            // to a capture holding nothing at all: an earlier build's log caught it
            // doing exactly that - `art 0` with 6 and 7 uploads in the same
            // window - so the guard held the good crosshair 24 times and then
            // published the empty capture anyway, roughly three times a
            // second. An empty capture is never a crosshair, however long it
            // persists; while good art is held it is refused unconditionally.
            // A new crosshair must not inherit refusals accumulated against
            // the previous one: the counter is otherwise cleared only on a
            // successful publish, so a weapon switch could start life most of
            // the way through its own escape hatch.
            if (identityChanged)
                g_authoredReticleConsecutiveHolds = 0;
            const bool staleEnoughToAccept =
                hasAnyArt && g_authoredReticleConsecutiveHolds >= kAuthoredReticleMaxConsecutiveHolds;
            const bool hasArt = hasAnyArt && (ink >= required || staleEnoughToAccept);
            const bool bootstrapNeedsMeasuredArt = (TitleAdapter_GetActiveTitle() == GameTitle::Halo4 ||
                                                       TitleAdapter_GetActiveTitle() == GameTitle::HaloCE) &&
                                                   !g_authoredReticleGoodValid;
            if (!hasArt && (g_authoredReticleGoodValid || bootstrapNeedsMeasuredArt))
            {
                // Halo 4 and CE also hold before the first known-good capture. A
                // blank or below-threshold bootstrap sample is not authored
                // art and must never replace its procedural fallback with an
                // invisible but "authored" image. The previously shipped
                // titles retain their accepted bootstrap behavior; once any
                // title has good art, this remains the shared bloom guard.
                // This is not a transport failure, so leave the currently
                // released swapchain image untouched.
                ++g_authoredReticleConsecutiveHolds;
                ++g_authoredReticleBlankHeld;
                g_authoredReticleHeldBlank = true;
                return false;
            }
            // Publish: the capture is measured good, or its non-empty
            // reduction has persisted long enough to be a real change rather
            // than a bloom. Mip 0 only: the capture carries a mip chain for
            // the check and the swapchain image does not.
            g_context->CopySubresourceRegion(
                g_authoredReticleGoodTexture, 0, 0, 0, 0, g_authoredReticleTexture, 0, nullptr);
            g_authoredReticleConsecutiveHolds = 0;
            if (ink >= kAuthoredReticleArtAlphaThreshold)
            {
                g_authoredReticleGoodValid = true;
                g_authoredReticleGoodInk = ink;
            }
            source = g_authoredReticleGoodTexture;
        }

        uint32_t index = 0;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = 1000000000;
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        const XrResult acquireResult = xrAcquireSwapchainImage(g_reticleChain, &acquire, &index);
        if (requireSuccessfulRelease && acquireResult != XR_SUCCESS)
        {
            if (XR_SUCCEEDED(acquireResult))
            {
                (void)RequireExactSwapchainCompletion(
                    acquireResult, "Reach authored-reticle swapchain acquire did not complete");
            }
            return false;
        }
        if (XR_FAILED(acquireResult))
            return false;
        const XrResult waitResult = xrWaitSwapchainImage(g_reticleChain, &wait);
        if (requireSuccessfulRelease)
        {
            if (!RequireExactSwapchainCompletion(waitResult, "Reach authored-reticle swapchain wait did not complete"))
            {
                return false;
            }
        }
        else if (XR_FAILED(waitResult))
            return false;

        D3D11_TEXTURE2D_DESC sourceDesc{};
        source->GetDesc(&sourceDesc);
        const bool copied = Blit(source, sourceDesc, g_reticleImages[index], kReticleSize, kReticleSize,
            GetRtv(g_reticleImages, g_reticleRtvs, index,
                TitleAdapter_GetActiveTitle() == GameTitle::Halo4 ||
                    TitleAdapter_GetActiveTitle() == GameTitle::HaloCE),
            true);
        const XrResult releaseResult = xrReleaseSwapchainImage(g_reticleChain, &release);
        const bool released = requireSuccessfulRelease
                                  ? RequireExactSwapchainCompletion(
                                        releaseResult, "Reach authored-reticle swapchain release did not complete")
                                  : XR_SUCCEEDED(releaseResult);
        if (!copied || ((requireSuccessfulRelease || TitleAdapter_GetActiveTitle() == GameTitle::HaloCE) && !released))
            return false;
        g_authoredReticleUploadedSerial = g_authoredReticleSerial;
        g_reticleContainsAuthored = true;
        return true;
    }
    void FillPresentedReticleTelemetry(TelemetryFrame& frame) noexcept
    {
        frame.reticlePresentedValid = false;
        frame.reticlePresentedSerial = 0;
        frame.reticlePresentedSampleMs = 0;
        frame.reticlePresentedSupportEpoch = 0;
        frame.reticlePresentedSupportTrusted = false;
        frame.reticlePresentedOrientation = TelemetryQuat{0.0f, 0.0f, 0.0f, 0.0f};
        frame.reticlePresentedPosition = TelemetryVec3{0.0f, 0.0f, 0.0f};
        float orientation[4]{};
        float position[3]{};
        uint64_t sampleMs = 0, supportEpoch = 0, solveSerial = 0;
        bool supportTrusted = false;
        // The exact consumer-visible reticle pose: a bounded lock-free
        // 2-attempt read (no g_headCs, no LOG, no sampling). Capture runs
        // BEFORE the reticle block publishes in the same frame, so this is
        // normally the PREVIOUS serial's pose; its own serial travels in the
        // field and the validator accepts the lag.
        if (!VR_GetPresentedReticleAimPoseWithSupportProvenance(
                orientation, position, sampleMs, supportEpoch, supportTrusted, solveSerial))
            return;
        if (!TelemetryFinite3(orientation + 1) || !std::isfinite(orientation[0]) || !TelemetryFinite3(position))
            return;
        frame.reticlePresentedValid = true;
        frame.reticlePresentedSerial = solveSerial;
        frame.reticlePresentedSampleMs = sampleMs;
        frame.reticlePresentedSupportEpoch = supportEpoch;
        frame.reticlePresentedSupportTrusted = supportTrusted;
        frame.reticlePresentedOrientation =
            TelemetryQuat{orientation[0], orientation[1], orientation[2], orientation[3]};
        frame.reticlePresentedPosition = TelemetryVec3{position[0], position[1], position[2]};
    }
    constinit GlassCamera g_glassCamera{};
    constinit uint32_t g_glassCameraSerial = 0;
    // The GlassShiftMargin cap of the current CEA glass image (g_glassImageCapSerial).
    constinit float g_glassImageCap = 0.25f;
    constinit uint32_t g_glassImageCapSerial = 0;
} // namespace vr_core
namespace
{
    // The CE Anniversary glass picture shifted by the gun's rotation since it
    // was rendered (texture uv; the shader keeps it inside the crop's headroom). Only for a
    // glass image with its camera recorded and a fresh lens receipt; 0 otherwise. Feeds the
    // 1 Hz reprojection line (image age, shift, edge reached, repeated pairs).
    float GlassImageCap()
    {
        return g_scopeImageTitle == GameTitle::HaloCE && g_glassImageCapSerial &&
                       g_glassImageCapSerial == g_scopeImageSerial.load(std::memory_order_acquire)
                   ? g_glassImageCap
                   : g_config.scope_parallax_max_shift;
    }

    void GlassReprojection(float& du, float& dv)
    {
        du = dv = 0;
        static std::vector<float> ages, degrees;
        static unsigned clampedCount = 0, shifted = 0;
        static uint64_t windowMs = 0;
        const uint64_t now = GetTickCount64();
        const unsigned title = static_cast<unsigned>(GameTitle::HaloCE);
        const bool glass = g_scopeImageTitle == GameTitle::HaloCE && g_glassCameraSerial &&
                           g_glassCameraSerial == g_scopeImageSerial.load(std::memory_order_acquire);
        if (glass)
        {
            const uint64_t imageMs = g_annivGlassImageMs.load(std::memory_order_acquire);
            if (imageMs && now >= imageMs)
                ages.push_back(static_cast<float>(now - imageMs));
            const uint64_t lensAt = g_scopeLensMs[title].load(std::memory_order_acquire);
            weapon_muzzle::Ray ray{};
            glass_reprojection::Shift s{};
            if (lensAt && now >= lensAt && now - lensAt <= 150 && g_scopeLogLens[title].Read(ray) &&
                glass_reprojection::Compute(g_glassCamera.forward, g_glassCamera.up,
                    {ray.direction[0], ray.direction[1], ray.direction[2]}, g_glassCamera.tanH, g_glassCamera.tanV, s))
            {
                bool clamped = false;
                (void)glass_reprojection::Clamp(s,
                    glass_reprojection::CropRoom(g_scopeGlassAspect.load(std::memory_order_acquire),
                        g_glassCamera.tanH / g_glassCamera.tanV, GlassImageCap()),
                    clamped);
                du = s.du;
                dv = s.dv; // the shader clamps (with this eye's parallax shift)
                degrees.push_back(s.degrees);
                ++shifted;
                clampedCount += clamped;
            }
        }
        if (!windowMs)
            windowMs = now;
        if (now - windowMs < 1000)
            return;
        if (!ages.empty())
        {
            const auto pct = [](std::vector<float>& v, double p)
            {
                if (v.empty())
                    return 0.0f;
                std::sort(v.begin(), v.end());
                return v[std::min(v.size() - 1, static_cast<size_t>(std::ceil(p * v.size())) - 1)];
            };
            LOG("CE anniv glass reprojection (1 s): uploads %zu, image age p50 %.0f p95 %.0f ms, shifted %u, shift p95 %.2f max %.2f deg, "
                "at the picture edge %u, margin cap %.2f; repeated eye pairs %u in the last whole second (pose change from first submit max %.5f); "
                "lens quad submitted %u of %u raised frames, %u of %u hip frames",
                ages.size(), pct(ages, 0.5), pct(ages, 0.95), shifted, pct(degrees, 0.95), pct(degrees, 1.0),
                clampedCount, GlassImageCap(), g_ceRepeatedPairs.load(std::memory_order_relaxed),
                g_ceRepeatPoseDelta.load(std::memory_order_relaxed), g_ceLensSubmitted[1], g_ceLensFrames[1],
                g_ceLensSubmitted[0], g_ceLensFrames[0]);
        }
        ages.clear();
        degrees.clear();
        clampedCount = shifted = 0;
        windowMs = now;
        g_ceLensFrames[0] = g_ceLensFrames[1] = g_ceLensSubmitted[0] = g_ceLensSubmitted[1] = 0;
    }
} // namespace
void VR_NoteLensMarker()
{
    g_lensMarkerMs.store(GetTickCount64(), std::memory_order_release);
}
bool VR_LensMarkerActive()
{
    const uint64_t at = g_lensMarkerMs.load(std::memory_order_acquire), now = GetTickCount64();
    return at && now >= at && now - at <= 250;
}
bool VR_ScopeShouldRenderThisFrame()
{
    const auto title = TitleAdapter_GetActiveTitle();
    const auto generation = TitleAdapter_GetGeneration(title);
    if (title != g_scopeRequestTitle || generation != g_scopeRequestGeneration)
    {
        g_scopeRequestTitle = title;
        g_scopeRequestGeneration = generation;
        VR_SetScopeActive(false);
    }
    const bool enabled = g_config.scope_enabled && !Menu_IsOpen() && Game_IsHeadTracking() && Game_HasScopeRenderer() &&
                         !g_zoomWeaponBlocked.load(std::memory_order_acquire);
    if (g_scopeResetRequested.exchange(false, std::memory_order_acq_rel))
    {
        g_scopeZoomResolver.Reset();
        g_scopeToggleObserved = g_scopeToggleSerial.load(std::memory_order_acquire);
    }
    const uint64_t requested = g_scopeToggleSerial.load(std::memory_order_acquire);
    if (requested != g_scopeToggleObserved)
    {
        g_scopeToggleObserved = requested;
        g_scopeZoomResolver.RequestToggle();
    }
    // Native Halo zoom is deliberately suppressed by the input layer: it hides
    // the VR body/viewmodel. This resolver now supplies only the delayed R3
    // toggle that keeps input and render-thread ownership race-free.
    SetScopeLevelHorizon(g_config.zoom_level_horizon);
    const bool buttonLatched = g_scopeZoomResolver.Update(enabled, false);
    // A measurement older than 200 ms (projection branch not running) is stale.
    const uint64_t sightAt = g_scopeSightingMs.load(std::memory_order_acquire);
    const uint64_t sightNow = GetTickCount64();
    const bool sighting =
        g_scopeSighting.load(std::memory_order_acquire) && sightAt && sightNow >= sightAt && sightNow - sightAt <= 200;
    // A held scope-glass gun is always active; R3 and the look
    // gate do not switch it (the gate only sets the render rate below). Helmet
    // zoom and scope_live_glass = 0 keep the look/button engage.
    const uint64_t glassAt = g_scopeGlassHeldMs.load(std::memory_order_acquire);
    const bool glass = g_config.scope_live_glass && glassAt && sightNow >= glassAt && sightNow - glassAt <= 250;
    // g_scopeActive stays "engaged" for its consumers (stick zoom, smoothing,
    // cull, pacing); g_scopeGlassLive only adds the pass for a held glass gun.
    const scope_sight::GlassState state =
        scope_sight::Resolve(enabled, glass, g_config.scope_activation, buttonLatched, sighting);
    const bool active = state.engaged;
    // A glass that stops being live without an engage edge drops its image too.
    if (g_scopeGlassLive.exchange(state.live, std::memory_order_acq_rel) && !state.live)
        g_scopeHasImage.store(false, std::memory_order_release);
    const bool previous = g_scopeActive.exchange(active, std::memory_order_acq_rel);
    if (!previous && active)
        LOG("Action fired: %s via %s",
            g_zoomTier.load(std::memory_order_acquire) == static_cast<uint8_t>(visor_zoom::Tier::Visor) ? "visor zoom"
                                                                                                        : "scope",
            glass                                        ? "glass raised"
            : sighting && g_config.scope_activation != 0 ? "look"
                                                         : "button");
    // An unrecognised held gun keeps the lens tier (the glass stays live), so say so once per engage: test runs read this to
    // tell a real lens proof from an engage with no known gun (save/seat problem).
    if (!previous && active)
    {
        const GameTitle idTitle = TitleAdapter_GetActiveTitle();
        if (!VR_ZoomWeaponIdentity(idTitle, TitleAdapter_GetGeneration(idTitle)))
            LOG("scope identity unknown: title %u has no recognised held weapon at engage (save, seat or turret problem); lens tier kept",
                static_cast<unsigned>(idTitle));
    }
    static bool activeByGlass = false;
    if (active)
        activeByGlass = glass;
    // Whether the look (not the button) is what holds the scope on, for the release log line.
    static bool activeByLook = false;
    if (active)
        activeByLook = sighting && (glass || (g_config.scope_activation != 0 && !buttonLatched));
    if (!previous && active)
        g_scopeEngagedMs.store(GetTickCount64(), std::memory_order_release);
    if (previous && !active)
    {
        if (!state.live)
            g_scopeHasImage.store(false, std::memory_order_release);
        // Why it turned off. A look engagement that just ended gives its
        // detector reason; anything else (R3, mode change) is "button".
        const int look = g_scopeSightDetector.LastRelease();
        const bool lookEnded = activeByLook && look > 0 && !g_scopeSightDetector.Sighting();
        const uint64_t engagedAt = g_scopeEngagedMs.load(std::memory_order_acquire), releasedAt = GetTickCount64();
        char line[96];
        scope_log::FormatReleased(line, sizeof line,
            activeByGlass && !glass ? scope_log::Release::Weapon
            : lookEnded             ? static_cast<scope_log::Release>(look)
                                    : scope_log::Release::Button,
            engagedAt && releasedAt >= engagedAt ? releasedAt - engagedAt : 0);
        LOG("%s", line);
    }
    const uint64_t now = GetTickCount64();
    // A live glass (even at the hip) holds the configured magnification.
    // Before, the controller only reset to scope_zoom on the first engage, so a
    // glass held from the hip rendered at the class default 1.25x, whose wide
    // frustum failed the Reach scope pass: no image, no quad until the first
    // raise (in one test, the first quad came 2.7 s into a 3 s raise). The stick still trims
    // only while engaged.
    const bool zoomLive = active || state.live;
    const float deltaSeconds =
        zoomLive && g_scopeZoomLastMs ? static_cast<float>(now - g_scopeZoomLastMs) / 1000.0f : 0.0f;
    g_scopeZoomLastMs = zoomLive ? now : 0;
    const float zoom = g_scopeZoomController.Update(
        zoomLive, active ? g_scopeZoomStickY.load(std::memory_order_acquire) : 0.0f, deltaSeconds, g_config.scope_zoom);
    g_scopeRuntimeZoom.store(zoom, std::memory_order_release);
    // A fixed workload each frame avoids alternating cheap two-eye frames and
    // expensive scope-refresh frames. Keep the old config key readable only.
    // A cockpit screen renders on its own, slower cadence; scoping keeps full rate.
    const bool cockpit = VR_CockpitScreenActive();
    // Live glass renders at full rate while raised to the eye (look gate),
    // every scope_idle_cadence frames otherwise (always live, just cheaper).
    const unsigned activeDivisor =
        glass && !sighting ? static_cast<unsigned>(std::clamp(g_config.scope_idle_cadence, 1, 8)) : 1u;
    {
        static bool lastRaised = true;
        static bool loggedGlass = false;
        if (glass && (!loggedGlass || lastRaised != sighting))
        {
            loggedGlass = true;
            lastRaised = sighting;
            LOG("scope glass live: %s, rendering every %u frame(s)",
                sighting ? "raised to the eye" : "away from the eye", activeDivisor);
        }
        if (!glass)
            loggedGlass = false;
    }
    const bool render = state.live
                            ? g_scopeRefreshScheduler.Advance(true, activeDivisor)
                            : (cockpit ? g_cockpitScheduler.Advance(true, g_config.cockpit_screen_refresh_divisor)
                                       : g_scopeRefreshScheduler.Advance(false, 1));
    if (!cockpit)
        (void)g_cockpitScheduler.Advance(false, 1);
    // A refresh no longer drops the image before the pass runs;
    // a pass that refuses keeps the last good image (VR_EndScopeRaster drops a half-drawn one).
    return render;
}

bool VR_BeginScopeRaster()
{
    if (!g_gameSwapchain || !g_sceneColorRtv || !g_scopeCache || !g_scopeCacheRtv || !g_scopeCacheSrv ||
        g_scopeCacheDesc.Width != g_eyeCacheDesc.Width || g_scopeCacheDesc.Height != g_eyeCacheDesc.Height ||
        g_scopeCacheDesc.Format != g_eyeCacheDesc.Format)
        return false;
    g_scopeRedirected = false;
    g_scopePassCaptured = false;
    g_rasterScope.store(true, std::memory_order_release);
    return true;
}

void VR_CaptureScope()
{
    if (g_scopeRedirected && g_scopeCache)
    {
        g_scopeImageTitle = TitleAdapter_GetActiveTitle();
        g_scopeImageGeneration = TitleAdapter_GetGeneration(g_scopeImageTitle);
        g_scopeHasImage.store(true, std::memory_order_release);
        NoteScopeImageCaptured();
        g_scopePassCaptured = true;
        g_scopeImageSubrect = g_scopePendingSubrect; // this image's filled corner
        g_scopeImageSubrectSerial = g_scopeImageSerial.load(std::memory_order_acquire);
    }
    g_scopePendingSubrect = 1.0f;
}

float VR_ScopeRenderScale()
{
    g_scopeScalePassMs.store(GetTickCount64(), std::memory_order_release);
    const float diag = g_config.scope_render_scale_diag;
    const float scale = std::isfinite(diag) && diag >= 0.25f && diag < 0.999f
                            ? diag
                            : g_scopeAdaptiveScale.load(std::memory_order_acquire);
    // Diagnostic for automated test runs: a full-size pass costs scope_cost_diag_ms more.
    const float costMs = g_config.scope_cost_diag_ms;
    if (scale >= 0.999f && std::isfinite(costMs) && costMs > 0.0f)
    {
        static bool logged = false;
        if (!logged)
        {
            logged = true;
            LOG("scope cost diagnostic: full-size scope passes wait %.1f ms (scope_cost_diag_ms)", costMs);
        }
        LARGE_INTEGER start, now, freq;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        const LONGLONG ticks = static_cast<LONGLONG>(std::min(costMs, 20.0f) * 1e-3 * freq.QuadPart);
        do
        {
            YieldProcessor();
            QueryPerformanceCounter(&now);
        } while (now.QuadPart - start.QuadPart < ticks);
    }
    return scale;
}
void VR_SetScopeImageSubrect(float fraction)
{
    g_scopePendingSubrect = std::isfinite(fraction) && fraction >= 0.25f && fraction <= 1.0f ? fraction : 1.0f;
}

void VR_EndScopeRaster()
{
    g_rasterScope.store(false, std::memory_order_release);
    // Only a pass that drew into the cache without capturing drops it.
    if (!glass_reprojection::ImageAfterPass(
            g_scopeHasImage.load(std::memory_order_acquire), g_scopeRedirected, g_scopePassCaptured))
        g_scopeHasImage.store(false, std::memory_order_release);
    g_scopeRedirected = false;
    g_scopePassCaptured = false;
}

bool VR_GetScopeRenderAspect(float& outAspect)
{
    if (!g_scopeCacheDesc.Width || !g_scopeCacheDesc.Height)
        return false;
    outAspect = static_cast<float>(g_scopeCacheDesc.Width) / static_cast<float>(g_scopeCacheDesc.Height);
    return std::isfinite(outAspect) && outAspect > 0.0f;
}

void VR_InvalidateScopeImage()
{
    g_scopeHasImage.store(false, std::memory_order_release);
}
uint64_t VR_TakeAuthoredReticleOmReroutes()
{
    return g_authoredReticleOmReroutes.exchange(0, std::memory_order_relaxed);
}

uint64_t VR_TakeAuthoredReticleFramingReasserts()
{
    return g_authoredReticleFramingReasserts.exchange(0, std::memory_order_relaxed);
}
uint64_t VR_ZoomWeaponIdentity(GameTitle title, uint32_t generation) noexcept
{
    static GameTitle lastTitle = GameTitle::None;
    static uint32_t lastGeneration = 0;
    static uint64_t lastIdentity = 0, lastLiveMs = 0;
    if (title != lastTitle || generation != lastGeneration)
    {
        lastTitle = title;
        lastGeneration = generation;
        lastIdentity = 0;
    }
    const uint64_t now = GetTickCount64();
    const uint64_t live =
        VR_GetWeaponModelIdentity(title, generation, g_contactSpaceEpoch.load(std::memory_order_acquire), now);
    if (live)
    {
        lastIdentity = live;
        lastLiveMs = now;
    }
    // Bridge short gaps only: ~1 s of continuous zero is a real "no weapon"
    // (vehicle seat, dropped gun), so a stale zoom gun cannot arm the look gate.
    else if (lastIdentity && now - lastLiveMs > 1000)
        lastIdentity = 0;
    return lastIdentity;
}

void VR_PublishWeaponReticleRay(GameTitle title, uint8_t slot, const weapon_muzzle::Receipt& receipt,
    const contact_melee::TrackingToWorld& transform) noexcept
{
    if (!dual_reticle::BarrelRayTitle(title) || slot > 1)
        return;
    dual_reticle::ProjectedRay projected{};
    if (receipt.title == title && receipt.slot == slot)
        (void)dual_reticle::Project(receipt, transform, projected);
    (void)g_weaponReticleRays[static_cast<unsigned>(title)][slot].Publish(projected);
    // The scope camera's muzzle origin stays Halo 2/Halo 3 only (the barrel-line reticle moves
    // the reticle, not the scope pass; Halo 4 and Reach keep their lens-node origin).
    if (dual_reticle::Supported(title) && slot == 0 && receipt.title == title && static_cast<unsigned>(title) < 7)
    {
        (void)g_scopeLogMuzzle[static_cast<unsigned>(title)].Publish(receipt.ray);
        g_scopeMuzzleMs[static_cast<unsigned>(title)].store(GetTickCount64(), std::memory_order_release);
    }
}

bool VR_GetLastReticleRay(float origin[3], float direction[3], bool& barrel, bool& held, uint64_t& ageMs) noexcept
{
    auto& last = g_lastReticleRay;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const uint32_t before = last.sequence.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        for (int i = 0; i < 3; ++i)
        {
            origin[i] = last.origin[i].load(std::memory_order_relaxed);
            direction[i] = last.direction[i].load(std::memory_order_relaxed);
        }
        barrel = last.barrel.load(std::memory_order_relaxed);
        held = last.held.load(std::memory_order_relaxed);
        const uint64_t at = last.ms.load(std::memory_order_relaxed);
        if (last.sequence.load(std::memory_order_acquire) != before)
            continue;
        const uint64_t now = GetTickCount64();
        ageMs = now >= at ? now - at : 0;
        return true;
    }
    return false;
}

void VR_PublishWeaponLensRay(GameTitle title, uint8_t slot, const weapon_muzzle::Receipt& receipt,
    const contact_melee::TrackingToWorld& transform) noexcept
{
    if (!lens_anchor::Supported(title) || slot > 1)
        return;
    dual_reticle::ProjectedRay projected{};
    if (receipt.title == title && receipt.slot == slot)
        (void)lens_anchor::Project(receipt, transform, projected);
    (projected.source.identity ? g_lensPublishOk : g_lensPublishEmpty)[static_cast<unsigned>(title)].fetch_add(
        1, std::memory_order_relaxed);
    (void)g_weaponLensRays[static_cast<unsigned>(title)][slot].Publish(projected);
    if (slot == 0 && receipt.title == title && static_cast<unsigned>(title) < 7 && receipt.serial)
    {
        (void)g_scopeLogLens[static_cast<unsigned>(title)].Publish(receipt.ray);
        g_scopeLensMs[static_cast<unsigned>(title)].store(GetTickCount64(), std::memory_order_release);
    }
}

bool VR_ScopeBarrelForward(GameTitle title, float out[3], uint64_t& ageMs)
{
    // The slot-0 muzzle receipt the shot itself uses (published per shot frame
    // through VR_PublishWeaponReticleRay); fresh = within 150 ms, as for the origin.
    const unsigned t = static_cast<unsigned>(title);
    ageMs = 0;
    if (t >= 7)
        return false;
    const uint64_t at = g_scopeMuzzleMs[t].load(std::memory_order_acquire), now = GetTickCount64();
    weapon_muzzle::Ray muzzle{};
    if (!at || now < at || now - at > 150 || !g_scopeLogMuzzle[t].Read(muzzle))
        return false;
    ageMs = now - at;
    for (int i = 0; i < 3; ++i)
        out[i] = muzzle.direction[i];
    return true;
}

bool VR_ScopeCameraOrigin(GameTitle title, float unitsPerMeter, const float barrelForward[3], float out[3])
{
    const unsigned t = static_cast<unsigned>(title);
    int slot = title == GameTitle::Halo3       ? 0
               : title == GameTitle::Halo3ODST ? 1
               : title == GameTitle::HaloReach ? 2
               : title == GameTitle::Halo2     ? 3
               : title == GameTitle::Halo4     ? 4
                                               : -1;
    if (!g_config.scope_camera_at_muzzle || slot < 0 || t >= 7 || !std::isfinite(unitsPerMeter) || unitsPerMeter <= 0)
        return false;
    const uint64_t at = g_scopeMuzzleMs[t].load(std::memory_order_acquire), now = GetTickCount64();
    weapon_muzzle::Ray muzzle{};
    const float forwardWorld = g_config.scope_camera_forward_m[slot] * unitsPerMeter;
    int origin = at && now >= at && now - at <= 150 && g_scopeLogMuzzle[t].Read(muzzle) &&
                         ScopeOriginAtMuzzle(muzzle.position, muzzle.direction, forwardWorld, out)
                     ? 1
                     : 0;
    float lensToMuzzle = 0, lensToFront = 0;
    if (!origin && barrelForward)
    {
        // Reach, ODST and H4 publish no muzzle receipt, only the scope-lens node
        // (lens_anchor). The lens sits well behind the muzzle (Reach sniper ~1.4 m),
        // so move it forward along the bullet direction (not the lens ray, whose
        // axis may be the glass normal or a sideways marker) by the gun's own
        // lens-to-front distance: runtime model front bound minus the kit glass
        // centre, both in the model frame (world units).
        const uint64_t lensAt = g_scopeLensMs[t].load(std::memory_order_acquire);
        weapon_muzzle::Ray lensRay{};
        if (const auto* model =
                weapon_model::Find(title, VR_ZoomWeaponIdentity(title, TitleAdapter_GetGeneration(title))))
            if (const auto* kit = weapon_profile::KitLensRowFor(title, model->name))
                if (std::isfinite(model->maximum[0]) && model->maximum[0] > kit->cx)
                    lensToFront = model->maximum[0] - kit->cx;
        // Spec: camera at the lens. The model-frame front bound puts it ~1.4 m
        // ahead on the Reach sniper, so cap the travel (uncapped value logged).
        lensToMuzzle = CapLensTravel(lensToFront, g_config.scope_lens_forward_max_m * unitsPerMeter);
        origin = lensAt && now >= lensAt && now - lensAt <= 150 && g_scopeLogLens[t].Read(lensRay) &&
                         ScopeOriginAtMuzzle(lensRay.position, barrelForward, lensToMuzzle + forwardWorld, out)
                     ? 2
                     : 0;
    }
    // A receipt gap mid-zoom moves the camera between muzzle, lens and head;
    // log every switch while scoped (rate-limited) so test runs see it.
    static std::atomic<int> lastOrigin{-1};
    static std::atomic<uint64_t> lastSwitchLogMs{0};
    if (g_scopeActive.load(std::memory_order_acquire) && lastOrigin.exchange(origin) != origin &&
        now - lastSwitchLogMs.load(std::memory_order_relaxed) >= 250)
    {
        lastSwitchLogMs.store(now, std::memory_order_relaxed);
        LOG("scope cam origin switch: title=%u now %s (muzzle receipt age %llu ms, lens-to-muzzle %.3f m, uncapped %.3f m)",
            t,
            origin == 1   ? "muzzle"
            : origin == 2 ? "lens+forward"
                          : "head",
            static_cast<unsigned long long>(at && now >= at ? now - at : 0), lensToMuzzle / unitsPerMeter,
            lensToFront / unitsPerMeter);
    }
    return origin != 0;
}
constinit std::atomic<float> g_scopeHeadFovBefore{NAN}, g_scopeHeadFovAfter{NAN};
constinit std::atomic<uint64_t> g_scopeHeadFovMs{0};
void VR_NoteScopeHeadFov(float beforeRadians, float afterRadians)
{
    g_scopeHeadFovBefore.store(beforeRadians * 57.2957795f, std::memory_order_relaxed);
    g_scopeHeadFovAfter.store(afterRadians * 57.2957795f, std::memory_order_relaxed);
    g_scopeHeadFovMs.store(GetTickCount64(), std::memory_order_release);
}
void VR_NoteScopeCameraCE(const float* camera, const float* head, float unitsPerMeter) noexcept
{
    VR_NoteScopeCamera(GameTitle::HaloCE, camera, head, unitsPerMeter);
}
void VR_NoteScopeCamera(GameTitle title, const float cameraWorld[3], const float headWorld[3], float unitsPerMeter)
{
    // The "scope cam origin" line on engage, then once a second while the scope stays on.
    static std::atomic<uint64_t> lastLogMs{0}, loggedEngage{0};
    const uint64_t engaged = g_scopeEngagedMs.load(std::memory_order_acquire), now = GetTickCount64();
    if (!g_scopeActive.load(std::memory_order_acquire) || !cameraWorld || !headWorld || !std::isfinite(unitsPerMeter) ||
        unitsPerMeter <= 0 || static_cast<unsigned>(title) >= 7)
        return;
    if (engaged == loggedEngage.load(std::memory_order_relaxed) &&
        now - lastLogMs.load(std::memory_order_relaxed) < 1000)
        return;
    loggedEngage.store(engaged, std::memory_order_relaxed);
    lastLogMs.store(now, std::memory_order_relaxed);
    const float inv = 1.0f / unitsPerMeter;
    float cam[3], lens[3], muzzle[3], head = 0;
    for (int i = 0; i < 3; ++i)
    {
        cam[i] = cameraWorld[i] * inv;
        const float d = (cameraWorld[i] - headWorld[i]) * inv;
        head += d * d;
    }
    weapon_muzzle::Ray m{}, l{};
    const bool haveMuzzle = g_scopeLogMuzzle[static_cast<unsigned>(title)].Read(m);
    const bool haveLens = g_scopeLogLens[static_cast<unsigned>(title)].Read(l);
    for (int i = 0; i < 3; ++i)
    {
        muzzle[i] = m.position[i] * inv;
        lens[i] = l.position[i] * inv;
    }
    const float forward =
        haveMuzzle ? scope_log::ForwardOfMuzzle(cameraWorld, m.position, m.direction, unitsPerMeter) : NAN;
    char line[320];
    scope_log::FormatCamOrigin(line, sizeof line, static_cast<int>(title),
        static_cast<unsigned long long>(VR_ZoomWeaponIdentity(title, TitleAdapter_GetGeneration(title))), cam,
        haveLens ? lens : nullptr, haveMuzzle ? muzzle : nullptr, forward, std::sqrt(head),
        title == GameTitle::HaloReach && now - g_scopeHeadFovMs.load(std::memory_order_acquire) <= 1000
            ? g_scopeHeadFovBefore.load(std::memory_order_relaxed)
            : NAN,
        title == GameTitle::HaloReach && now - g_scopeHeadFovMs.load(std::memory_order_acquire) <= 1000
            ? g_scopeHeadFovAfter.load(std::memory_order_relaxed)
            : NAN);
    LOG("%s", line);
}
void VR_SetScopeActive(bool active)
{
    g_scopeActive.store(active, std::memory_order_release);
    if (!active)
        g_scopeGlassLive.store(false, std::memory_order_release);
    if (active)
        return;
    g_scopeResetRequested.store(true, std::memory_order_release);
    g_scopeHasImage.store(false, std::memory_order_release);
}

bool VR_ScopeGlassLive()
{
    return g_scopeGlassLive.load(std::memory_order_acquire);
}

bool VR_IsScopeActive()
{
    return g_scopeActive.load(std::memory_order_acquire);
}

void VR_RequestScopeToggle()
{
    g_scopeToggleSerial.fetch_add(1, std::memory_order_release);
}
float VR_GetScopeZoom()
{
    const float zoom = g_scopeRuntimeZoom.load(std::memory_order_acquire);
    if (!VR_VisorZoomHeadView())
        return zoom;
    // The visor window magnifies exactly `zoom` times (visor_zoom::ScopeZoomForWindow).
    float aspect = visor_zoom::kAspect;
    (void)VR_GetScopeRenderAspect(aspect);
    return visor_zoom::ScopeZoomForWindow(zoom, aspect, VisorWindowWidthM(), g_config.visor_zoom_distance_m);
}

bool VR_VisorZoomHeadView()
{
    return g_zoomTier.load(std::memory_order_acquire) == static_cast<uint8_t>(visor_zoom::Tier::Visor);
}

bool VR_CanPrepareAuthoredReticleResources()
{
    return g_authoredReticlePreparationReady.load(std::memory_order_acquire);
}

AuthoredReticlePreparationResult VR_PrepareAuthoredReticleResources()
{
    ReachExclusiveResourceLock lock(g_reachDisplayResourceLock);
    if (!VR_CanPrepareAuthoredReticleResources() || !g_device || !g_context || g_session == XR_NULL_HANDLE)
    {
        return AuthoredReticlePreparationResult::NotReady;
    }
    if (g_reticleChainFailed.load(std::memory_order_acquire))
        return AuthoredReticlePreparationResult::Failed;
    if (g_reticleChain == XR_NULL_HANDLE &&
        !CreateChain(kReticleSize, kReticleSize, g_reticleChain, g_reticleImages, g_reticleRtvs, "crosshair"))
    {
        g_reticleChainFailed.store(true, std::memory_order_release);
        return AuthoredReticlePreparationResult::Failed;
    }
    if (g_reticleImages.empty() || g_reticleRtvs.size() != g_reticleImages.size())
    {
        return AuthoredReticlePreparationResult::Failed;
    }
    // Capture and suppression render only into their private D3D targets.
    // The OpenXR swapchain RTV is needed later, after acquire identifies the
    // one image being uploaded. Creating every image RTV here eagerly meant
    // one refusal prevented both CUI hooks from installing for the whole level
    // even though this capture transaction never uses those views. Match the
    // accepted Halo 3/ODST lazy upload path and validate only the resources the
    // hot redirect actually consumes.
    return EnsureAuthoredReticleTexture() ? AuthoredReticlePreparationResult::Ready
                                          : AuthoredReticlePreparationResult::Failed;
}

void VR_InvalidatePreparedAuthoredReticleCapture()
{
    // Called only on the render thread before opening a new admitted title
    // transaction. Halo 3/ODST retain their accepted serial lifecycle.
    g_authoredReticleReady = false;
    g_authoredReticleSerial = 0;
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE)
        g_authoredReticleProbePending = false;
}

static bool BeginAuthoredReticleCaptureInternal(
    bool requirePreparedResources, bool requireCrosshairEnabled, bool publishAuthored)
{
    // `crosshair=0` means "do not show a VR crosshair". For Halo 3/ODST,
    // refusing here is the whole implementation: they hide their native
    // reticle through a separate visibility predicate, so declining the
    // redirect correctly leaves stock drawing alone.
    //
    // Reach has no such predicate, and its CHUD alpha field is inert when
    // written (proven: 1.000 across 860 live samples while the mod wrote 0).
    // This redirect is therefore the ONLY thing that keeps Reach's native
    // crosshair off the eye. Refusing it for `crosshair=0` meant the setting
    // that asks for no crosshair produced the stock flat one instead - and,
    // before the failure-isolation fix, tore VR down as well. Reach passes
    // requireCrosshairEnabled=false so the redirect always runs; whether the
    // captured art is ever SHOWN is decided separately by the compositor,
    // which still requires g_config.crosshair before uploading or submitting
    // the quad. So `crosshair=0` in Reach now means exactly what it says:
    // no crosshair anywhere, flat or VR.
    // Halo 4 follows the same prepared-only rule at its CUI subtree boundary;
    // its non-capture paths bind the distinct discard target below.
    if (!g_context || (requireCrosshairEnabled && !g_config.crosshair) ||
        !g_stereoEnabled.load(std::memory_order_relaxed) || !g_preparedFrame.begun || g_reticleCaptureState.active)
        return false;

    if (requirePreparedResources)
    {
        // A latched chain failure belongs only to this optional feature. Both
        // prepared Halo 4 paths must refuse before redirecting the CUI target,
        // allowing the unmodified native reticle to draw while stereo stays
        // armed.
        if (g_reticleChainFailed.load(std::memory_order_acquire) || g_reticleChain == XR_NULL_HANDLE ||
            g_reticleImages.empty() || g_reticleRtvs.size() != g_reticleImages.size() || !g_authoredReticleTexture ||
            !g_authoredReticleRtv ||
            (!publishAuthored && (!g_authoredReticleDiscardTexture || !g_authoredReticleDiscardRtv)))
        {
            return false;
        }
        // XR image views are intentionally lazy. Only UploadAuthoredReticle,
        // after xrAcquireSwapchainImage chooses an index, needs one.
    }
    else
    {
        // Preserve the accepted Halo 3/ODST lazy preparation path. Reach calls
        // only the prepared entry above, so allocation and logging never occur
        // from its HREK widget hook.
        if (!EnsureReticleChain() || !EnsureAuthoredReticleTexture())
            return false;
    }

    ID3D11RenderTargetView* const redirectRtv = publishAuthored ? g_authoredReticleRtv : g_authoredReticleDiscardRtv;
    if (!redirectRtv)
        return false;

    const GameTitle captureTitle = TitleAdapter_GetActiveTitle();
    auto& saved = g_reticleCaptureState;
    const bool ceCapture = captureTitle == GameTitle::HaloCE;
    if (ceCapture &&
        (!HaloCEHudLayout_CopyState(
             g_context, &saved.viewportCount, saved.viewports, &saved.scissorCount, saved.scissors) ||
            saved.viewportCount != 1 || !HaloCEHud_ReadTarget(g_context, saved.ceTarget) || !saved.ceTarget.count))
        return false;

    const uint64_t serial = g_preparedFrame.serial;
    if (publishAuthored && g_authoredReticleSerial != serial)
    {
        const float clear[4] = {0, 0, 0, 0};
        g_context->ClearRenderTargetView(g_authoredReticleRtv, clear);
        g_authoredReticleSerial = serial;
        g_authoredReticleReady = false;
        // New displayed frame: start the art-identity accumulation clean so a
        // static crosshair produces the same key every frame.
        Game_ResetAuthoredCrosshairKey();
    }

    if (!ceCapture)
    {
        g_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, saved.rtvs, &saved.dsv);
        saved.viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        g_context->RSGetViewports(&saved.viewportCount, saved.viewports);
        saved.scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        g_context->RSGetScissorRects(&saved.scissorCount, saved.scissors);
    }

    D3D11_VIEWPORT captureViewport{};
    // Halo 3/ODST/Reach hook a widget-scoped draw, so the viewport already
    // live when their capture begins IS that widget's own viewport - it is a
    // meaningful, consistent quantity. Halo 4 has no such narrow hook (its
    // whole CUI stream replays through here), so the live viewport at capture
    // entry is only ever whatever the PRECEDING unrelated pass left bound -
    // measured this session ranging from the full 4834x3486 raster down to a
    // 1209x872 slice depending on exact timing, non-deterministic capture to
    // capture. The reticle container's OWN measured transform base
    // (-halfRasterWidth/+halfRasterHeight, proven across two separate
    // sessions at two different raster sizes) is positioned directly in
    // raster-pixel space, so a fixed region centred on the raster is a
    // meaningful, timing-independent source for Halo 4 alone.
    if (captureTitle == GameTitle::Halo4)
    {
        // Use Halo 4's own measured CUI framing, not the rejected
        // square 2048x2048 guess. The draw hook derives raster aspect and the
        // accepted 2.5x scale from this immutable per-capture base.
        captureViewport.Width = kHalo4CuiCaptureWidth;
        captureViewport.Height = kHalo4CuiCaptureHeight;
        captureViewport.MinDepth = 0.0f;
        captureViewport.MaxDepth = 1.0f;
    }
    else if (saved.viewportCount)
        captureViewport = saved.viewports[0];
    else
    {
        captureViewport.Width = static_cast<float>(g_gameBackbufferDesc.Width);
        captureViewport.Height = static_cast<float>(g_gameBackbufferDesc.Height);
        captureViewport.MinDepth = 0.0f;
        captureViewport.MaxDepth = 1.0f;
    }
    // The outer quad retains the universal crosshair_size_deg and distance
    // sliders. Halo 3 and ODST share 4x internal authored-art occupancy so the
    // same slider values produce matching apparent size. Reach retains its
    // independently calibrated 2x occupancy. Halo 4 now uses that SAME proven
    // 4x ratio, baked directly into the fixed region above rather than
    // applied as a further multiplier, because a measurement showed that
    // shrinking the WHOLE raster (~9.4x this session) into the capture
    // produced a totally blank result - consistent with a thin/hollow
    // reticle outline falling below rasterizer coverage at that extreme a
    // minification. Scope zoom remains separate.
    const float authoredCaptureScale = captureTitle == GameTitle::Halo3 || captureTitle == GameTitle::Halo3ODST ? 4.0f
                                       : captureTitle == GameTitle::HaloReach                                   ? 2.0f
                                       : captureTitle == GameTitle::Halo4                                       ? 1.0f
                                                                                                                : 1.0f;
    captureViewport.Width *= authoredCaptureScale;
    captureViewport.Height *= authoredCaptureScale;
    // This symmetric centring is what maps the SOURCE's own centre - where any
    // FPS crosshair sits in its own normal full-screen render, independent of
    // whatever viewport is used to display it - to the capture texture's
    // centre, regardless of the chosen source width/height.
    captureViewport.TopLeftX = (static_cast<float>(kReticleSize) - captureViewport.Width) * 0.5f;
    captureViewport.TopLeftY = (static_cast<float>(kReticleSize) - captureViewport.Height) * 0.5f;
    const D3D11_RECT captureScissor{0, 0, static_cast<LONG>(kReticleSize), static_cast<LONG>(kReticleSize)};
    saved.publishesAuthored = publishAuthored;
    // Retained so a later Halo-4 scene-target rebind DURING this same capture
    // (see VR_RedirectRenderTargets) can put the SAME viewport back instead of
    // drawing with whatever the engine's own prior pass left bound.
    saved.captureViewport = captureViewport;
    saved.captureScissor = captureScissor;
    saved.framingCaptured = true;
    saved.active = true;
    saved.cePrivateRaster = ceCapture;
    saved.ceRestoring = false;
    saved.ceStateValid = ceCapture;
    if (saved.cePrivateRaster)
        HaloCEHudLayout_BeginPrivateRaster();
    // Publish the complete capture mode before the OM hook observes this bind;
    // otherwise its exact selected-target comparison would use the preceding
    // capture's authored/discard mode and miss the first required draw pin.
    g_context->OMSetRenderTargets(1, &redirectRtv, nullptr);
    g_context->RSSetViewports(1, &captureViewport);
    g_context->RSSetScissorRects(1, &captureScissor);
    return true;
}

bool VR_ShouldCaptureAuthoredReticleThisFrame()
{
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE)
        return halo_ce::ReticleCanReplaceCapture(g_authoredReticleProbePending);

    // Until valid art is held there is nothing to fall back on, so never skip.
    if (!g_reticleContainsAuthored)
        return true;

    // Do not overwrite the captured source while its prior coverage request is
    // in flight; SubmitPreparedFrame will poll it without blocking.
    if (g_authoredReticleProbePending)
        return false;

    // ODST's captured quad is static between weapon and state changes, so it
    // samples only often enough to notice one. Halo 3 and Halo 4's authored
    // crosshairs animate, so they sample at the cadence the player asked for -
    // also the cadence at which art can publish, avoiding discarded work.
    constexpr uint64_t kOdstCaptureSampleGapFrames = 30;
    uint64_t gapFrames = 0;
    switch (TitleAdapter_GetActiveTitle())
    {
    case GameTitle::Halo3:
        gapFrames = ResolveAuthoredAnimationGapFrames(g_config.crosshair_animation_frames);
        // Holding one image still needs a slow re-sample so a weapon swap is
        // eventually noticed; it just never animates between those.
        if (gapFrames == 0)
            gapFrames = kOdstCaptureSampleGapFrames;
        break;
    case GameTitle::Halo4:
        gapFrames = ResolveAuthoredAnimationGapFrames(g_config.crosshair_animation_frames);
        // H4EK's transform ID is not proven art identity. With animation off,
        // retain only a slow sample so a weapon swap can still replace art.
        if (gapFrames == 0)
            gapFrames = kOdstCaptureSampleGapFrames;
        break;
    case GameTitle::Halo3ODST:
        gapFrames = kOdstCaptureSampleGapFrames;
        break;
    default:
        return true;
    }

    // On a skipped frame the title predicate hides the flat widget without
    // redirecting a render target or drawing any authored crosshair pixels.
    static std::atomic<uint64_t> lastSampleSerial{0};
    const uint64_t serial = g_preparedFrame.serial;
    uint64_t last = lastSampleSerial.load(std::memory_order_relaxed);
    for (;;)
    {
        if (!ShouldSampleAuthoredCapture(gapFrames, last, serial))
            return false;
        if (serial == last)
            return true;
        if (lastSampleSerial.compare_exchange_weak(last, serial, std::memory_order_relaxed, std::memory_order_relaxed))
            return true;
    }
}

bool VR_BeginAuthoredReticleCapture()
{
    return BeginAuthoredReticleCaptureInternal(false, true, true);
}

// Reach's hide-the-widget entry: lazily creates whatever is missing (so a
// transient resource state can never fail it - that regression cost a build)
// and never refuses because the crosshair is switched off, because refusing
// is what lets Reach's flat crosshair through.
bool VR_BeginAuthoredReticleRedirect()
{
    return BeginAuthoredReticleCaptureInternal(false, false, true);
}

bool VR_BeginPreparedAuthoredReticleCapture()
{
    return BeginAuthoredReticleCaptureInternal(true, false, true);
}

#include "../titles/ce/haloce_reticle_redirect.inl"

static bool EndAuthoredReticleCaptureInternal(bool allowFirstCaptureLog, bool expectAuthored)
{
    auto& saved = g_reticleCaptureState;
    if (!saved.active || !g_context)
        return false;

    const bool ceCapture = saved.cePrivateRaster;
    bool restored = true;
    bool restoreEntryRaster = true;
    if (ceCapture)
    {
        // The native descriptor owns its views. Replay the verified binder
        // while this HUD callback still retains CE; no borrowed COM release.
        saved.ceRestoring = true;
        // The binder's native setters now publish its real numeric state.
        // Crosshair suspension prevents the outer HUD affine during restore.
        HaloCEHudLayout_EndPrivateRaster();
        const auto result = HaloCEHudTarget_Restore(saved.ceTarget);
        restored = result == CeHudTargetRestoreResult::Unchanged && saved.ceStateValid;
        restoreEntryRaster = result == CeHudTargetRestoreResult::Unchanged;
        if (result == CeHudTargetRestoreResult::Unavailable)
        {
            g_context->OMSetRenderTargets(0, nullptr, nullptr);
            HaloCEHudLayout_InvalidateState(g_context);
        }
    }
    else
        g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, saved.rtvs, saved.dsv);
    if (restoreEntryRaster && (saved.viewportCount || ceCapture))
        g_context->RSSetViewports(saved.viewportCount, saved.viewports);
    if (restoreEntryRaster && (saved.scissorCount || ceCapture))
        g_context->RSSetScissorRects(saved.scissorCount, saved.scissors);
    saved.cePrivateRaster = false;
    saved.ceRestoring = false;
    saved.ceStateValid = false;
    saved.ceTarget = {};

    if (!ceCapture)
    {
        for (auto*& rtv : saved.rtvs)
        {
            if (rtv)
                rtv->Release();
            rtv = nullptr;
        }
        if (saved.dsv)
        {
            saved.dsv->Release();
            saved.dsv = nullptr;
        }
    }
    saved.viewportCount = 0;
    saved.scissorCount = 0;
    saved.framingCaptured = false;
    const bool modeMatches = saved.publishesAuthored == expectAuthored;
    const bool publishedAuthored = saved.publishesAuthored;
    saved.active = false;
    saved.publishesAuthored = false;
    if (publishedAuthored && restored)
        g_authoredReticleReady = true;
    if (ceCapture && !restored)
        VR_InvalidatePreparedAuthoredReticleCapture();
    static bool logged = false;
    if (allowFirstCaptureLog && !logged)
    {
        LOG("Halo authored per-weapon crosshair redirected to VR aim quad");
        logged = true;
    }
    return modeMatches && restored;
}

void VR_EndAuthoredReticleCapture()
{
    (void)EndAuthoredReticleCaptureInternal(true, true);
}

bool VR_EndPreparedAuthoredReticleCapture()
{
    return EndAuthoredReticleCaptureInternal(false, true);
}

bool VR_PrepareAuthoredReticleSuppressionResources()
{
    ReachExclusiveResourceLock lock(g_reachDisplayResourceLock);
    // This entry is Halo 4's final cold install gate. Unlike the legacy lazy
    // title paths, its optional hook must not arm without the coverage probe:
    // otherwise a first blank capture could be indistinguishable from valid
    // authored art and replace the visible procedural fallback. Failure here
    // leaves Halo 4's native CUI reticle wholly stock.
    return !g_reticleChainFailed.load(std::memory_order_acquire) && VR_CanPrepareAuthoredReticleResources() &&
           g_device && g_context && g_authoredReticleProbeUsable && g_authoredReticleGoodTexture &&
           EnsureAuthoredReticleDiscardTexture();
}

bool VR_BeginPreparedAuthoredReticleSuppression()
{
    return BeginAuthoredReticleCaptureInternal(true, false, false);
}

bool VR_EndPreparedAuthoredReticleSuppression()
{
    return EndAuthoredReticleCaptureInternal(false, false);
}

void VR_SetReticleEnemy(bool enemy)
{
    g_reticleEnemy.store(enemy, std::memory_order_relaxed);
}
bool VR_SupportInvocationReceiptTrustedForReticle(
    GameTitle title, uint32_t generation, uint64_t serial, uint64_t raySupportEpoch, bool raySupportTrusted) noexcept
{
    // Only a wired title that publishes receipts is held to this rule; every
    // other title (and the whole feature while it is off) keeps its previous
    // receipt semantics exactly.
    if (!VR_SupportGripWiredForTitle(title) || !support_grip::TitlePublishesSupportInvocationReceipt(title))
        return true;
    support_grip::SupportInvocationReceipt invocation{};
    SupportGripRelationshipSnapshot relationship{};
    if (!ReadSupportInvocationReceiptInternal(title, invocation) || !ReadSupportGripRelationshipInternal(relationship))
        return false; // fail closed: no proven invocation receipt
    return support_grip::PresentedRayConsumableForInvocation(title, invocation, generation, serial,
        /*relationshipReadable=*/true, relationship.engaged, relationship.epoch,
        support_grip::OwnerTuple{relationship.title, relationship.generation, relationship.unit, relationship.weapon},
        raySupportEpoch, raySupportTrusted, /*raySerial=*/serial);
}
bool VR_GetPresentedReticleAimPoseWithSupportProvenance(float outQuat[4], float outPos[3], uint64_t& outSampleMs,
    uint64_t& outSupportEpoch, bool& outSupportTrusted, uint64_t& outSolveSerial)
{
    outSupportEpoch = 0;
    outSupportTrusted = false;
    outSolveSerial = 0;
    if (!outQuat || !outPos)
        return false;
    auto& published = g_presentedReticleAimPose;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = published.sequence.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        const bool valid = published.valid.load(std::memory_order_relaxed) != 0;
        const uint64_t sampleMs = published.sampleMs.load(std::memory_order_relaxed);
        const uint64_t supportEpoch = published.supportEpoch.load(std::memory_order_relaxed);
        const bool supportTrusted = published.supportTrusted.load(std::memory_order_relaxed) != 0;
        const uint64_t preparedSerial = published.preparedSerial.load(std::memory_order_relaxed);
        const float q[4] = {published.qx.load(std::memory_order_relaxed), published.qy.load(std::memory_order_relaxed),
            published.qz.load(std::memory_order_relaxed), published.qw.load(std::memory_order_relaxed)};
        const float p[3] = {published.px.load(std::memory_order_relaxed), published.py.load(std::memory_order_relaxed),
            published.pz.load(std::memory_order_relaxed)};
        if (published.sequence.load(std::memory_order_acquire) != before)
            continue;
        if (!valid || !sampleMs)
            return false;
        memcpy(outQuat, q, sizeof(q));
        memcpy(outPos, p, sizeof(p));
        outSampleMs = sampleMs;
        outSupportEpoch = supportEpoch;
        outSupportTrusted = supportTrusted;
        outSolveSerial = preparedSerial;
        return true;
    }
    return false;
}

bool VR_GetPresentedReticleAimPose(float outQuat[4], float outPos[3], uint64_t& outSampleMs)
{
    uint64_t supportEpoch = 0;
    uint64_t solveSerial = 0;
    bool supportTrusted = false;
    return VR_GetPresentedReticleAimPoseWithSupportProvenance(
        outQuat, outPos, outSampleMs, supportEpoch, supportTrusted, solveSerial);
}
