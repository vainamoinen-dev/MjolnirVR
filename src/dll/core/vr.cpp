// The OpenXR side of the mod: instance and session, eye swapchains, frame wait, prepare and submit (the frame path),
// present, the pacing worker, and the HUD and menu layers. Controllers, image composition, scope and telemetry are in
// vr_controllers.cpp, vr_compose.cpp, vr_scope.cpp and vr_telemetry.cpp; shared state in vr_internal.h.
// Over 8,000 lines by ruling (E2-VR 7b): the per-frame path stays in one unit so its small callees stay inlinable.
// Other code reaches it through vr.h: the D3D11 present hook (d3d11_hook.cpp), game.cpp, input.cpp, menu.cpp, dllmain.cpp
// and the per-title code under src/dll/titles. Pure decision rules live in src/common/*_logic.h, not here.
// It must never block the game's render thread on the OpenXR runtime. Several tools/ guards and fixture generators read its text.
#include "../../common/title_traits.h"
#include "../../common/pause_world_lock.h"
#include "../../common/weapon_model_observation.h"
#include "weapon_accessory_renderer.h"
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
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#include <atomic>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <share.h>
#include <cstring>
#include <limits>
#include <type_traits>
#include "menu.h"
#include "native_menu_pointer.h"
#include "native_subtitles.h"
#include "../../common/subtitle_logic.h"
#include "../../common/scope_render_scale_logic.h"
#include "../../common/game_menu_pointer.h"
#include "../../common/exclusive_input.h"
#include "../../common/game_menu_pointer_xr.h"
#include "../../common/titles/ce/haloce_pause_logic.h"
#include "../../common/titles/ce/haloce_reticle_logic.h"
#include "../titles/ce/haloce_first_person.h"
#include "../titles/ce/haloce_hud.h"
#include "d3d11_hook.h"
#include "d3d_state.h"
#include <../common/authored_reticle_logic.h>
#include "../../common/titles/reach/reach_chud_logic.h"
#include "../../common/frame_pacing_logic.h"
#include "../../common/titles/h4/halo4_render_logic.h"
#include "../../common/titles/h4/halo4_cui_reticle_logic.h"
#include "../../common/input_logic.h"
#include "../../common/scope_log.h"
#include "../../common/vehicle_comfort_logic.h"
#include "display_resource_lock.h"
#include "vr_internal.h"
#include "vr_frame.h"
#include "vr_compose.h"
#include "vr_controllers.h"
#include "vr_scope.h"
#include "vr_telemetry.h"
#include "vr_format_helpers.h"
#include "vr_inline_ops.h"
#include "guard_count.h"
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

// Mono "virtual cinema" fallback: every frame the game presents, we copy its backbuffer
// into an OpenXR swapchain and submit it as a world-locked quad layer (a flat
// rectangle floating in space). The ImGui menu is a second, smaller quad.
// The headset compositor does all the reprojection; there is no stereo yet.

namespace vr_core
{
    State g_state = State::Uninitialized;
} // namespace vr_core
namespace
{

    // The OpenXR instance/system are created on a background thread (see
    // VR_InitInstance) because xrCreateInstance can take 20+ seconds while
    // SteamVR spins up — doing that on the game's render thread freezes the
    // game and it gets killed as unresponsive. These flags let the render
    // thread know when the instance is ready without ever blocking on it.
    std::atomic<bool> g_instanceReady{false};
    std::atomic<bool> g_instanceFailed{false};

} // namespace
namespace vr_core
{
    // OpenXR core
    XrInstance g_instance = XR_NULL_HANDLE;
} // namespace vr_core
namespace
{
    XrSystemId g_systemId = XR_NULL_SYSTEM_ID;
} // namespace
namespace vr_core
{
    XrSession g_session = XR_NULL_HANDLE;
    XrSpace g_localSpace = XR_NULL_HANDLE; // world-fixed, origin = headset pose at session start
    std::atomic<int64_t> g_contactSpaceChangeAtNs{0};
    std::atomic<uint64_t> g_contactSpaceEpoch{1};
    virtual_stock::InverseNeckNeutralCaptureState g_inverseNeckNeutralCapture{};
} // namespace vr_core
namespace
{
    VrContactTrackingSnapshot g_contactTrackingSnapshots[2]{};
    std::atomic<uint32_t> g_contactTrackingStates[2]{};
    std::atomic<uint32_t> g_contactTrackingIndex{2};
    constexpr uint32_t kContactTrackingWriter = UINT32_MAX;
} // namespace
namespace vr_core
{
    XrSpace g_viewSpace = XR_NULL_HANDLE; // follows the headset; used for recentering
    XrAction g_hapticAction = XR_NULL_HANDLE;
    XrAction g_actMenu = XR_NULL_HANDLE;
    std::atomic<uint64_t> g_thumbrestDpadSampleMs{0};
    XrPath g_leftHandPath = XR_NULL_PATH;
    XrPath g_rightHandPath = XR_NULL_PATH;
    std::atomic<vr_mapping::Profile> g_controllerProfiles[2]{};
    std::atomic<uint32_t> g_controllerProfileEpoch{1};
    // Bumped ONLY by XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED (not by
    // session state changes): clears the native-layout cache.
    std::atomic<uint32_t> g_interactionProfileEpoch{1};
    bool g_touchProProfileEnabled = false;
} // namespace vr_core
namespace
{
    // The headset's real panel rate, read once from the runtime (0 = the runtime
    // does not expose it). Compared against the rate xrWaitFrame targets us at,
    // which is a FRACTION of this whenever the runtime reprojects. No rate is
    // ever assumed: 72, 80, 90, 120, 144 all come from these two numbers.
    bool g_refreshRateExtEnabled = false;
} // namespace
namespace vr_core
{
    std::atomic<float> g_panelRefreshHz{0.0f};
} // namespace vr_core
namespace
{
    XrEnvironmentBlendMode g_blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
} // namespace
namespace vr_core
{
    bool g_sessionRunning = false;
    XrSessionState g_sessionState = XR_SESSION_STATE_UNKNOWN;
    std::atomic<float> g_requestedHaptics{0.0f};
    // Peak-hold companion to g_requestedHaptics: the maximum amplitude requested
    // since the last applied VR frame, so a short gunfire pulse that arrives and
    // clears between two frame samples is not aliased to zero. See SampleHapticPeak.
    std::atomic<float> g_peakHaptics{0.0f};
    // Optional world-contact feedback is hand-specific. These are one-shot
    // peak latches raised by a title's cold collision worker and consumed by
    // the existing OpenXR frame path; no engine thread calls OpenXR directly.
    std::atomic<float> g_contactHaptics[2]{};
    WeaponHapticAdmission g_weaponHapticAdmission;
} // namespace vr_core
namespace
{
    void LogHeadsetPanelRate();
    bool StartFrameWaitThread();
    bool StopFrameWaitThread();

    // Stereo: per-eye recommended render size and per-eye pose/FOV.
    std::vector<XrViewConfigurationView> g_viewConfigs;
} // namespace
namespace vr_core
{
    std::vector<XrView> g_views;

    // D3D11 (the game's device — we never create our own for rendering)
    ID3D11Device* g_device = nullptr;
} // namespace vr_core
namespace
{
    IDXGIAdapter3* g_memoryAdapter = nullptr;
} // namespace
namespace vr_core
{
    ID3D11DeviceContext* g_context = nullptr;

    // XR swapchains + cached render target views of their images
    int64_t g_xrFormat = 0;
    XrSwapchain g_screenChain = XR_NULL_HANDLE;
    uint32_t g_screenW = 0, g_screenH = 0;
    std::vector<ID3D11Texture2D*> g_screenImages;
    std::vector<ID3D11RenderTargetView*> g_screenRtvs;
} // namespace vr_core
namespace
{
    XrSwapchain g_nativeCursorChain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> g_nativeCursorImages;
    std::vector<ID3D11RenderTargetView*> g_nativeCursorRtvs;
    bool g_nativeCursorReady = false;
    XrSwapchain g_nativeSubtitleChain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> g_nativeSubtitleImages;
    std::vector<ID3D11RenderTargetView*> g_nativeSubtitleRtvs;
    uint64_t g_nativeSubtitleUploaded = 0, g_nativeSubtitleRetryMs = 0;
    uint32_t g_nativeSubtitleAcquiredIndex = 0;
    bool g_nativeSubtitleAcquired = false, g_nativeSubtitleFailed = false;
} // namespace
namespace vr_core
{
    bool g_nativeTheatreCaptionReady = false;
    XrSwapchain g_dualReticleChain = XR_NULL_HANDLE;
} // namespace vr_core
namespace
{
    XrPosef g_secondaryReticlePose{};
    bool g_secondaryReticlePoseValid = false;
    uint64_t g_secondaryReticleSpace = 0;
    uint32_t g_secondaryReticleGeneration = 0;
    GameTitle g_secondaryReticleTitle = GameTitle::None;
} // namespace
namespace vr_core
{
    halo_ce::Snapshot<dual_reticle::ProjectedRay> g_weaponReticleRays[7][2];
} // namespace vr_core
namespace
{
    unsigned g_reticleRecentAccepts = 0; // compositor (render thread) only
} // namespace
namespace vr_core
{
    LastReticleRay g_lastReticleRay;
    halo_ce::Snapshot<dual_reticle::ProjectedRay> g_weaponLensRays[7][2]; // scope lens centre at the gun node
    std::atomic<uint64_t> g_scopeGlassHeldMs{0};                          // a scope-glass (Optic) gun held, lens tier
} // namespace vr_core
namespace
{
    bool g_sightLensAnchored = false; // raise gate measured the anchored lens (diagnostic)
} // namespace
namespace vr_core
{
    std::atomic<bool> g_scopeGlassLive{false}; // render the pass (g_scopeActive = engaged)
} // namespace vr_core
namespace
{
    // The lens quad last submitted (local space).
    float g_lensQuadPos[3]{}, g_lensQuadQ[4]{0, 0, 0, 1}, g_lensQuadW = 0, g_lensQuadH = 0;
} // namespace
namespace vr_core
{
    lens_anchor::ReadReport g_lensReadReport{}; // last consumer verdict (present thread only)
} // namespace vr_core
namespace
{
    XrSwapchain g_menuChain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> g_menuImages;
    std::vector<ID3D11RenderTargetView*> g_menuRtvs;
    XrSwapchain g_fadeChain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> g_fadeImages;
    std::vector<ID3D11RenderTargetView*> g_fadeRtvs;

} // namespace
namespace vr_core
{
    // Aim crosshair: a tiny static reticle image floated as a quad layer
    // along the weapon's true aim ray. Drawn once; the compositor keeps
    // re-showing the last released image, so it costs nothing per frame.
    XrSwapchain g_reticleChain = XR_NULL_HANDLE;
    std::atomic<bool> g_authoredReticlePreparationReady{false};
    // A strict world-swapchain failure aborts the current layer transaction and
    // enters the existing fatal session drain. Some failures (for example a
    // wait timeout) can leave an image acquired; other non-XR_SUCCESS results
    // (for example XR_SESSION_LOSS_PENDING from release) can still mean the
    // operation completed. In either case, submit no layers and pair a begun
    // frame with one empty xrEndFrame before session recovery.
    bool g_abortFrameForSwapchainFailure = false;
} // namespace vr_core
namespace
{

    // Universal gun scope. A private scene cache receives the refresh-limited
    // third render; an isolated upload pipeline center-crops it to the fixed
    // 1024x768 screen and paints the aiming mark without touching either eye.
    XrSwapchain g_scopeScreenChain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> g_scopeScreenImages;
    std::vector<ID3D11RenderTargetView*> g_scopeScreenRtvs;
    // Left half = left eye, right half = right eye (parallax/eyebox on).
    XrSwapchain g_scopeStereoChain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> g_scopeStereoImages;
    std::vector<ID3D11RenderTargetView*> g_scopeStereoRtvs;
} // namespace
namespace vr_core
{
    std::atomic<bool> g_scopeActive{false};
    // Look-through sighting, measured on the render thread where the
    // presented aim pose lives; read by the next scope-render decision.
    std::atomic<bool> g_scopeSighting{false};
    std::atomic<uint64_t> g_scopeSightingMs{0}; // when g_scopeSighting was last measured
    scope_sight::Detector g_scopeSightDetector;
    // Visor zoom: head-locked 4:3 window with a procedural HUD frame. Same
    // zoom render (g_scopeCache) as the lens; Halo CE fills that cache with a
    // crop of its eye image (CeVisorCrop).
    XrSwapchain g_visorScreenChain = XR_NULL_HANDLE;
    std::atomic<uint8_t> g_zoomTier{static_cast<uint8_t>(visor_zoom::Tier::Lens)};
    ID3D11Texture2D* g_scopeCache = nullptr;
    ID3D11RenderTargetView* g_scopeCacheRtv = nullptr;
    ID3D11ShaderResourceView* g_scopeCacheSrv = nullptr;
    D3D11_TEXTURE2D_DESC g_scopeCacheDesc{};
    std::atomic<bool> g_rasterScope{false};
    bool g_scopeRedirected = false;
    std::atomic<bool> g_scopeHasImage{false};
    GameTitle g_scopeImageTitle = GameTitle::None, g_scopeRequestTitle = GameTitle::None;
} // namespace vr_core
namespace
{
    // The adaptive scope pass scale (BeforePresent; scope_render_scale_logic.h).
    scope_render_scale::Controller g_scopeScaleController;
} // namespace
namespace vr_core
{
    std::atomic<float> g_scopeAdaptiveScale{1.0f};
    std::atomic<uint64_t> g_scopeScalePassMs{0}; // the last opted-in scope pass (GetTickCount64)
} // namespace vr_core
namespace
{
    GameTitle g_scopeScaleTitle = GameTitle::None;
    // Titles whose scope pass renders a scaled pass (VR_ScopeRenderScale + VR_SetScopeImageSubrect).
    bool ScopeAdaptiveTitle(GameTitle title)
    {
        return title == GameTitle::Halo3;
    }
} // namespace
namespace vr_core
{
    uint32_t g_scopeImageGeneration = 0, g_scopeRequestGeneration = 0;
    // The held gun's lens quad width/height (scope_window::GlassAspect; 1 = the
    // square glass). The quad keeps its height and widens; the picture is cropped at
    // this aspect and the CE scope camera widens only when the glass is wider than
    // its capture (GlassTangentScale), so the vertical view stays as before.
    std::atomic<float> g_scopeGlassAspect{1.0f};
} // namespace vr_core
namespace
{
    // Enclosed-cockpit screen: when an enclosed seat last asked for it, its own
    // (slower) refresh cadence, the seated-eye anchor and the shader mode.
    std::atomic<uint64_t> g_cockpitScreenMs{0};
    cockpit_screen::Anchor g_cockpitAnchor;
} // namespace
namespace vr_core
{
    bool g_visorCockpitMode = false;
    // Vehicle monitors: the view the title is rendering this frame, the
    // view of the scope image last captured, one swapchain per screen.
    // The view the next monitor render shows; it advances only when a scope
    // image is actually captured for it (titles may build the pose on frames
    // the refresh divisor then skips).
    std::atomic<int> g_monitorCurrentView{-1};
    std::atomic<int> g_monitorPendingView{-1};
    std::atomic<uint64_t> g_monitorViewSinceMs{0}; // when the current view was first asked for
    std::atomic<uint64_t> g_monitorRequestMs{0};
    std::atomic<const char*> g_monitorVehicle{nullptr};
    int g_scopeImageView = -1;                   // render thread, set at capture
    std::atomic<uint32_t> g_scopeImageSerial{0}; // +1 per captured scope image
    XrSwapchain g_monitorChains[vehicle_monitors::kViewCount]{};
    bool g_monitorHasImage[vehicle_monitors::kViewCount]{};
    std::atomic<uint64_t> g_monitorViewsRendered[vehicle_monitors::kViewCount]{};
} // namespace vr_core
namespace
{
    // Frame-time comparison: render-window ms while the monitors vs the single
    // cockpit screen are shown (the two "frame time" log lines compare them).
    struct MonitorFrameStats
    {
        double sum = 0;
        float max = 0;
        unsigned n = 0;
    };
    MonitorFrameStats g_monitorFrameStats[2]; // 0 single screen, 1 monitors
    int g_monitorFrameMode = -1;              // this frame: -1 none, 0 single, 1 monitors
} // namespace
namespace vr_core
{
    std::atomic<uint64_t> g_scopeImageCapturedMs{0}; // the newest image's capture time
    std::atomic<float> g_scopeRuntimeZoom{3.39f};
    std::atomic<float> g_scopeZoomStickY{0.0f};
    std::atomic<bool> g_scopeResetRequested{false};
    // The held weapon may not zoom at all (verified no-zoom row, or a CE
    // lens weapon whose look-through pass cannot run, i.e. Anniversary
    // graphics). The scope controller then stays disabled, so no visor window
    // and no button latch survive.
    std::atomic<bool> g_zoomWeaponBlocked{false};
    float g_reticlePaintedOpacity = -1.0f; // last painted opacity; sentinel forces first paint
    ID3D11RenderTargetView* g_authoredReticleRtv = nullptr;
    ID3D11RenderTargetView* g_authoredReticleDiscardRtv = nullptr;
    bool g_authoredReticleGoodValid = false;
    bool g_authoredReticleProbePending = false;
    uint32_t g_authoredReticleGoodInk = 0;
    uint32_t g_authoredReticleConsecutiveHolds = 0;
    uint32_t g_authoredReticleLastCoverage = 0;
    uint32_t g_authoredReticleBlankHeld = 0;
    // Set when an upload was deliberately withheld because the capture had no
    // visible art. The caller must tell this apart from a real upload failure,
    // which repaints the chain and would erase the crosshair itself.
    bool g_authoredReticleHeldBlank = false;
    bool g_authoredReticleReady = false;
    uint64_t g_authoredReticleSerial = 0;
    uint64_t g_authoredReticleUploadedSerial = 0;
    bool g_reticleContainsAuthored = false;
} // namespace vr_core
namespace
{
    std::atomic<uint64_t> g_reticleOwnerEpoch{1};
} // namespace
namespace vr_core
{
    ReticleCaptureState g_reticleCaptureState{};
    std::atomic<uint64_t> g_authoredReticleOmReroutes{0};
    std::atomic<uint64_t> g_authoredReticleFramingReasserts{0};
} // namespace vr_core
namespace
{

    // The CHUD steal-and-requad machinery (capture texture, shader
    // classifier, hand-HUD swapchain) was removed 2026-07-18: it removed the
    // native HUD from both eyes, never displayed its quad, and its
    // calibration retry loop cost ~30 fps. The native HUD renders untouched;
    // per-eye FP camera substitution in game.cpp gives it (and the gun)
    // stereo-correct rendering.

    // Eye targets. Each eye is a separate swapchain because the headset may
    // recommend a different size for each view. They are allocated now so the
    // later render hook can draw directly into them; the mono quad remains the
    // active layer until both contain genuine per-eye game renders.
    struct EyeChain
    {
        XrSwapchain chain = XR_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<ID3D11Texture2D*> images;
        std::vector<ID3D11RenderTargetView*> rtvs;
    };
    std::vector<EyeChain> g_eyeChains;
    XrSwapchain g_stereoChain = XR_NULL_HANDLE;
} // namespace
namespace vr_core
{
    uint32_t g_stereoW = 0, g_stereoH = 0;
} // namespace vr_core
namespace
{
    std::vector<ID3D11Texture2D*> g_stereoImages;
    std::vector<std::array<ID3D11RenderTargetView*, 2>> g_stereoRtvs;
} // namespace
namespace vr_core
{
    ID3D11Texture2D* g_eyeCache[2] = {nullptr, nullptr};
    ID3D11RenderTargetView* g_eyeCacheRtvs[2] = {nullptr, nullptr};
    D3D11_TEXTURE2D_DESC g_eyeCacheDesc{};
    // Removed: the per-eye post-process history machinery (cross-pass
    // discovery, frame-level blanking, and the learned scene-snapshot
    // pairs). Every one of them was disproven in a headset session, and
    // together they held two full-resolution shadow textures per learned
    // pair (~25 MB each) plus 96 AddRef'd candidate textures, and re-copied
    // them on every eye pass. Do not rebuild them without new evidence.
    std::atomic<bool> g_stereoEnabled{false};
} // namespace vr_core
namespace
{
    int g_renderEye = 0;
} // namespace
namespace vr_core
{
    bool g_eyeHasImage[2] = {false, false};
} // namespace vr_core
namespace
{
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
} // namespace
namespace vr_core
{
    bool g_stereoValidationDone = false;
    std::atomic<int> g_rasterEye{-1};
    std::atomic<DWORD> g_rasterOwnerThread{0};
    bool g_rasterRedirected[2] = {false, false};
    IDXGISwapChain* g_gameSwapchain = nullptr; // borrowed; owned by the game
    // The internal scene-color RTV is stable after the first eye render. Keep
    // one reference and use a pointer comparison in the OMSetRenderTargets
    // hook. The retired census path performed GetBuffer/GetResource/QI/GetDesc
    // plus a 128-entry linear scan on nearly every RTV bind and could collapse
    // stereo from 90 fps into the 20s.
    ID3D11RenderTargetView* g_sceneColorRtv = nullptr;
    DlssEyeState g_dlssEye[2];
    DlssStereoBatch g_dlssBatch;
    dlss::FrameResult<DlssKickedEye> g_dlssKicked[2];
    // The depth view the game binds together with the learned scene-colour
    // target. Kept alive with one reference exactly like g_sceneColorRtv.
    ID3D11DepthStencilView* g_dlssSceneDepthDsv = nullptr;
    bool g_dlssEyeSceneDepthSeen = false;
    DlssDepthViewEntry g_dlssDepthViews[kDlssDepthViewSlots]{};
    int g_dlssDepthViewCount = 0;
    bool g_dlssDepthViewTableFull = false;
    ID3D11DepthStencilView* g_dlssEyeLastFullRasterDsv = nullptr;
    // The eye whose DLSS depth window is open (Reach renders its eyes
    // outside the raster-eye scope); -1 otherwise. Read by the OM hook.
    std::atomic<int> g_dlssDepthWindowEye{-1};
    // Titles jitter their projection only after a frame in which both eyes
    // actually resolved through DLSS; a jittered image with no consumer would
    // just shake.
    bool g_dlssJitterArmed = false;
    uint32_t g_dlssEyesResolvedThisFrame = 0;
    bool g_dlssDepthSourceLogged = false;
    bool g_dlssResourcesLogged = false;
    uint32_t g_dlssBatchFailedFrames = 0;
    bool g_dlssBatchSuppressed = false;
    // Plain-language state for the F1 menu.
    SRWLOCK g_dlssStatusLock = SRWLOCK_INIT;
    char g_dlssStatusText[256] = "off";
    ID3D11Resource* g_sceneColorResource = nullptr;
    thread_local NativeHudEyeRouteState g_nativeHudEyeRoute{};
    std::atomic<unsigned> g_nativeHudPhaseScopes{0};
    std::atomic<unsigned> g_nativeHudProvenOmMatches{0};
    std::atomic<unsigned> g_nativeHudExactCopyScopes{0};
    std::atomic<unsigned> g_nativeHudCopySubstitutions{0};
    D3D11_TEXTURE2D_DESC g_gameBackbufferDesc{};
    bool g_gameBackbufferDescValid = false;
    // Retained immediately after Present using the flip chain's current buffer
    // index. ODST can copy it after each death-camera eye draw without COM
    // discovery in the hot render path.
    std::atomic<ID3D11Texture2D*> g_nextGameBackbuffer{nullptr};
} // namespace vr_core
namespace
{
    IDXGISwapChain* g_flipIndexOwner = nullptr;
    IDXGISwapChain3* g_flipIndexChain = nullptr;

} // namespace
namespace vr_core
{
    // Where the virtual screen sits: yaw-only orientation + head position,
    // captured once at start (and again on "re-center").
    XrQuaternionf g_centerRot{0, 0, 0, 1};
    XrVector3f g_centerPos{0, 0, 0};
    bool g_haveCenter = false;
} // namespace vr_core
namespace
{
    // Recenter requests can originate on the input/game camera threads. Only
    // Present owns g_haveCenter and the OpenXR locate used to replace it.
    std::atomic<bool> g_recenterRequested{false};
    // Bumped whenever the game's position neutral is re-zeroed (recenter chord,
    // seat settle); the cabin fade re-captures its seated eye on a change.
    std::atomic<uint32_t> g_positionRecenterEpoch{1};
    std::atomic<uint32_t> g_crouchCalibrationEpoch{1};

} // namespace
namespace vr_core
{
    // Screen placement while head tracking is on. World-locked (default) reads
    // as natural because turning your head shifts the screen in your view to
    // match your head motion; head-locked keeps it pinned in front but feels
    // disconnected. Toggle with F10.
    std::atomic<bool> g_screenFollow{false};
} // namespace vr_core
namespace
{
    // Input threads request a target; the render thread owns the 200 ms
    // fade-out, presentation switch, and fade-in.
    std::atomic<int> g_pauseRequest{-1};
} // namespace
namespace vr_core
{
    std::atomic<bool> g_pausePresentation{false};
} // namespace vr_core
namespace
{
    std::atomic<bool> g_pauseTarget{false};
    std::atomic<bool> g_cutsceneTheaterActive{false};
    CutsceneTheaterTransition g_cutsceneTheaterTransition;
    // Render-thread-only admission latches. A failed entry recenter blocks only
    // that one qualified cinematic; the next unqualified frame clears it.
    bool g_cutsceneTheaterWasQualified = false;
    bool g_cutsceneTheaterEntryBlocked = false;
} // namespace
namespace vr_core
{
    float g_cutsceneTheaterAuthoredAspect = 0.0f;

    // Latest head pose in the LOCAL space, captured every frame on the render
    // thread and read by the game camera hook on the game thread — hence
    // the lock. Orientation is a quaternion, position is in meters.
    CRITICAL_SECTION g_headCs;
    bool g_headCsInit = false;
    XrPosef g_headPose{{0, 0, 0, 1}, {0, 0, 0}};
    bool g_headPoseValid = false;
    hud_reveal::Publication g_hudReveal;
    // Virtual-stock coherence: true only after the current prepared frame has
    // both a successful controller/action sample and a successful HMD locate.
    // Cleared at new-frame admission (before a new sample can publish) and on
    // prepared-frame reset/abort/session-loss/teardown, so stock aim never
    // mixes a new hand sample with an older head sample. A single failed HMD
    // locate leaves this false and aim falls back to controller->controller.
    std::atomic<bool> g_stockAimFresh{false};
    // Optional support grip-pose endpoint freshness. Separate from HMD stock
    // freshness because the support endpoint experiment applies to ordinary
    // legacy two-hand aiming too.
    std::atomic<bool> g_supportGripPoseFresh{false};
    std::atomic<uint64_t> g_roomscaleHeadSampleMs{0};
    XrPosef g_rightAimPose{{0, 0, 0, 1}, {0, 0, 0}};
    bool g_rightAimPoseValid = false;
    // Title interfaces use right=primary, left=support/secondary roles.
    std::atomic<bool> g_capturedLeftHanded{false};
    XrPosef g_physicalControllerPose[2]{};
    bool g_physicalControllerValid[2]{};
    uint64_t g_physicalControllerAtMs = 0;
    XrVector3f g_rightAimLinearVelocity{};
    bool g_rightAimLinearVelocityValid = false;
    uint64_t g_rightAimLinearVelocityAtMs = 0;
    XrPosef g_leftAimPose{{0, 0, 0, 1}, {0, 0, 0}};
    bool g_leftAimPoseValid = false;
    XrVector3f g_supportGripPosePosition{0.0f, 0.0f, 0.0f};
    bool g_supportGripPoseValid = false;
    XrVector3f g_primaryGripPosePosition{0.0f, 0.0f, 0.0f};
    bool g_primaryGripPoseValid = false;
    XrVector3f g_leftAimLinearVelocity{};
    bool g_leftAimLinearVelocityValid = false;
    uint64_t g_leftAimLinearVelocityAtMs = 0;
} // namespace vr_core
namespace
{
    // Render-thread-only filtered copy for the compositor crosshair. Keeping it
    // separate is intentional: weapon steering and bullets stay on raw aim.
    XrPosef g_reticleAimPose{{0, 0, 0, 1}, {0, 0, 0}};
    bool g_reticleAimPoseValid = false;
    // Relationship epoch of the last presented (smoothed) ray. The receipt
    // travels with the smoothed result: the smoothing history reseeds across
    // an owner/trust or relationship-epoch discontinuity instead of blending
    // previous-owner geometry into the new ray. The trust bit of
    // that receipt is published on the ray itself; the epoch history is the
    // continuity state (a release or a bind always advances the epoch, so the
    // epoch comparison separates every supported/unsupported transition).
    uint64_t g_reticleSupportEpoch = 0;

} // namespace
namespace vr_core
{

    // Blit (copy-with-format-conversion) resources, created on demand
    ID3D11VertexShader* g_blitVs = nullptr;
    ID3D11SamplerState* g_blitSampler = nullptr;
    ID3D11RasterizerState* g_blitRasterizer = nullptr;
    ID3D11DepthStencilState* g_blitDepthOff = nullptr;
    ViewCacheTable<kSrcViewCacheCapacity> g_srcSrvCache;
    IntermediatePoolTable<kIntermediatePoolCapacity> g_intermediatePool;
    ID3D11Texture2D* g_intermediateTex[kIntermediatePoolCapacity]{};
    ID3D11ShaderResourceView* g_intermediateSrvs[kIntermediatePoolCapacity]{};
    // Steady-state creation must reach zero. These counters are what prove it
    // from one session instead of from an argument.
    uint64_t g_srcViewCreated = 0;
    uint64_t g_intermediateCreated = 0;
    // Theatre is rasterized into the ordinary two-view projection layer. The
    // default IQ configuration samples the title eye caches directly, so this
    // costs the same two output draws as immersive presentation. Non-default
    // IQ modes retain their existing resolve/AA/sharpen chain and use these two
    // private textures only for its final room-screen draw.
    ID3D11Texture2D* g_theaterResolved[2] = {nullptr, nullptr};
    ID3D11RenderTargetView* g_theaterResolvedRtv[2] = {nullptr, nullptr};
    ID3D11ShaderResourceView* g_theaterResolvedSrv[2] = {nullptr, nullptr};
    D3D11_TEXTURE2D_DESC g_theaterResolvedDesc{};
    // Subtitles are interface text, not a CHUD widget, and the title draws them
    // into its finished backbuffer after both eye captures - which is why they
    // reach the monitor and never the headset. Keep a copy of the lower band of
    // that backbuffer while theatre is active and let the projection shader put
    // the text back on the room-fixed screen at the position the title chose.
    // Every failure here leaves the existing stereo theatre exactly as it was.
    ID3D11Texture2D* g_theaterSubtitleBand = nullptr;
    ID3D11ShaderResourceView* g_theaterSubtitleBandSrv = nullptr;
    D3D11_TEXTURE2D_DESC g_theaterSubtitleBandDesc{};
    // First backbuffer V the band holds; negative means no band this frame.
    float g_theaterSubtitleBandStartV = -1.0f;
} // namespace vr_core
namespace
{
    static_assert(sizeof(TheaterProjectionParams) == 112);

} // namespace
namespace vr_core
{
    // Deliberately separate from the eye/menu blitter: scope upload failures
    // cannot poison the proven stereo presentation pipeline.
    ID3D11VertexShader* g_scopeUploadVs = nullptr;
    ID3D11SamplerState* g_scopeUploadSampler = nullptr;
    ID3D11RasterizerState* g_scopeUploadRasterizer = nullptr;
    ID3D11DepthStencilState* g_scopeUploadDepthOff = nullptr;
    const char* g_scopeShapeApplied = "circle"; // shape= (upload thread)
} // namespace vr_core
namespace
{

    // Status shown in the menu (only touched on the render thread)
    VrStatus g_status{};
    LARGE_INTEGER g_fpsTimer{};
    int g_fpsFrames = 0;

} // namespace
namespace vr_core
{
    PreparedFrame g_preparedFrame{};
} // namespace vr_core
namespace
{
    uint64_t g_nextPreparedSerial = 0;
} // namespace
namespace vr_core
{
    std::atomic<bool> g_preparedShouldRender{false};
    std::atomic<uint64_t> g_preparedViewSerialPublished{0};
} // namespace vr_core
namespace
{

    template <size_t N>
    struct TimingRing
    {
        std::array<double, N> values{};
        size_t next = 0;
        size_t count = 0;
        void Add(double value)
        {
            if (!std::isfinite(value) || value < 0.0)
                return;
            values[next] = value;
            next = (next + 1) % N;
            if (count < N)
                ++count;
        }
    };
    TimingRing<512> g_presentIntervalsMs;
    TimingRing<512> g_presentDurationsMs;
    TimingRing<512> g_waitDurationsMs;
    TimingRing<512> g_endFrameDurationsMs;
    TimingRing<512> g_renderWindowMs;
    TimingRing<512> g_predictionErrorMs;
    LARGE_INTEGER g_qpcFrequency{};
    LARGE_INTEGER g_lastBeforePresentQpc{};
    LARGE_INTEGER g_beginFrameQpc{};
    uint64_t g_timingLogStartMs = 0;
    XrTime g_lastPredictedDisplayTime = 0;
} // namespace
namespace vr_core
{
    // OpenXR's application-frame period, straight from xrWaitFrame. It is the
    // cadence the runtime is currently asking this app to submit at, and can be
    // a fraction of the separately queried physical panel refresh. No rate is
    // assumed: 72/80/90/120/144 and fractional cadences remain raw runtime data.
    std::atomic<uint64_t> g_displayPeriodNs{0};
} // namespace vr_core
namespace
{

    static_assert(std::is_trivially_copyable_v<FramePacingRecord>);

    // The raw transition capture produced the evidence it was added for. Keep
    // the implementation available for a future targeted investigation, but
    // compile its render-thread producers and worker consumer out of normal
    // builds. The independent wait-pipeline fault reporting remains active.
    constexpr bool kEnableFramePacingTransitionCapture = false;
    constexpr uint32_t kFramePacingQueueSize = 1024;
    std::array<FramePacingRecord, kFramePacingQueueSize> g_framePacingQueue{};
    std::atomic<uint32_t> g_framePacingQueueHead{0};
    std::atomic<uint32_t> g_framePacingQueueTail{0};
} // namespace
namespace vr_core
{
    std::atomic<uint64_t> g_framePacingQueueDrops{0};
} // namespace vr_core
namespace
{
    FramePacingRecord g_framePacingPending{};       // render thread only
    GameFramePerfCounters g_framePacingPerfStart{}; // render thread only
} // namespace
namespace vr_core
{
    std::atomic<uint32_t> g_framePacingSessionEpoch{0};
} // namespace vr_core
namespace
{

    void SubtractPerfCounters(
        const GameFramePerfCounters& after, const GameFramePerfCounters& before, GameFramePerfCounters& delta)
    {
        auto subtract = [](uint64_t end, uint64_t start) { return end >= start ? end - start : 0; };
        delta = {};
        delta.viewRenders = subtract(after.viewRenders, before.viewRenders);
        for (int eye = 0; eye < 3; ++eye)
        {
            delta.fpPaletteRequests[eye] = subtract(after.fpPaletteRequests[eye], before.fpPaletteRequests[eye]);
            delta.fpPaletteFullSolves[eye] = subtract(after.fpPaletteFullSolves[eye], before.fpPaletteFullSolves[eye]);
            delta.fpPaletteCacheHits[eye] = subtract(after.fpPaletteCacheHits[eye], before.fpPaletteCacheHits[eye]);
            delta.fpPaletteCacheStores[eye] =
                subtract(after.fpPaletteCacheStores[eye], before.fpPaletteCacheStores[eye]);
            delta.fpPaletteCacheFull[eye] = subtract(after.fpPaletteCacheFull[eye], before.fpPaletteCacheFull[eye]);
        }
        delta.zoomLogWrites = subtract(after.zoomLogWrites, before.zoomLogWrites);
        delta.viewRateLogWrites = subtract(after.viewRateLogWrites, before.viewRateLogWrites);
        delta.paletteRateLogWrites = subtract(after.paletteRateLogWrites, before.paletteRateLogWrites);
        delta.cameraRateLogWrites = subtract(after.cameraRateLogWrites, before.cameraRateLogWrites);
        delta.fpDriverRateLogWrites = subtract(after.fpDriverRateLogWrites, before.fpDriverRateLogWrites);
    }

    void PublishFramePacingRecord(const FramePacingRecord& record)
    {
        const uint32_t head = g_framePacingQueueHead.load(std::memory_order_relaxed);
        const uint32_t next = (head + 1) % kFramePacingQueueSize;
        if (next == g_framePacingQueueTail.load(std::memory_order_acquire))
        {
            g_framePacingQueueDrops.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        g_framePacingQueue[head] = record;
        g_framePacingQueueHead.store(next, std::memory_order_release);
    }

    bool ConsumeFramePacingRecord(FramePacingRecord& record)
    {
        const uint32_t tail = g_framePacingQueueTail.load(std::memory_order_relaxed);
        if (tail == g_framePacingQueueHead.load(std::memory_order_acquire))
            return false;
        record = g_framePacingQueue[tail];
        g_framePacingQueueTail.store((tail + 1) % kFramePacingQueueSize, std::memory_order_release);
        return true;
    }

    FramePacingWorkerState g_framePacingWorker{}; // title worker only

    bool IsEligiblePacingRecord(const FramePacingRecord& record)
    {
        return record.predictedDisplayPeriodNs > 0 && record.shouldRender && record.focused && record.stereo &&
               record.headTracking;
    }

    const FramePacingRecord* LastPacingHistory(const FramePacingWorkerState& worker)
    {
        if (!worker.historyCount)
            return nullptr;
        const size_t index = (worker.historyNext + kFramePacingPreFrames - 1) % kFramePacingPreFrames;
        return &worker.history[index];
    }

    void AddPacingHistory(FramePacingWorkerState& worker, const FramePacingRecord& record)
    {
        worker.history[worker.historyNext] = record;
        worker.historyNext = (worker.historyNext + 1) % kFramePacingPreFrames;
        if (worker.historyCount < kFramePacingPreFrames)
            ++worker.historyCount;
    }

    void ClearPacingHistory(FramePacingWorkerState& worker)
    {
        worker.historyNext = 0;
        worker.historyCount = 0;
    }

    void BeginPacingCapture(FramePacingWorkerState& worker, const FramePacingRecord& trigger, int64_t oldPeriodNs)
    {
        worker.captureCount = 0;
        worker.exact = true;
        const size_t first = (worker.historyNext + kFramePacingPreFrames - worker.historyCount) % kFramePacingPreFrames;
        for (size_t i = 0; i < worker.historyCount; ++i)
        {
            const FramePacingRecord& prior = worker.history[(first + i) % kFramePacingPreFrames];
            if (worker.captureCount && !IsExactWaitPair(worker.capture[worker.captureCount - 1], prior))
            {
                worker.exact = false;
            }
            worker.capture[worker.captureCount++] = prior;
            if (!IsExactWaitRecord(prior))
                worker.exact = false;
        }
        worker.triggerIndex = worker.captureCount;
        worker.capture[worker.captureCount++] = trigger;
        worker.postRemaining = kFramePacingPostFrames;
        worker.oldPeriodNs = oldPeriodNs;
        worker.newPeriodNs = trigger.predictedDisplayPeriodNs;
        worker.dropsAtStart = g_framePacingQueueDrops.load(std::memory_order_relaxed);
        worker.lastDataMs = GetTickCount64();
        worker.capturing = true;
        if (!IsExactWaitRecord(trigger) ||
            (worker.triggerIndex && !IsExactWaitPair(worker.capture[worker.triggerIndex - 1], trigger)))
        {
            worker.exact = false;
        }
    }

    void SeedHistoryFromCapture(FramePacingWorkerState& worker)
    {
        const size_t count = worker.captureCount;
        const size_t first = count > kFramePacingPreFrames ? count - kFramePacingPreFrames : 0;
        ClearPacingHistory(worker);
        for (size_t i = first; i < count; ++i)
        {
            const FramePacingRecord& record = worker.capture[i];
            const FramePacingRecord* previous = LastPacingHistory(worker);
            if (!IsEligiblePacingRecord(record) ||
                (previous && (previous->sessionEpoch != record.sessionEpoch || previous->serial + 1 != record.serial)))
            {
                ClearPacingHistory(worker);
                if (!IsEligiblePacingRecord(record))
                    continue;
            }
            AddPacingHistory(worker, record);
        }
    }

    void FinishPacingCapture(FramePacingWorkerState& worker, const char* completion)
    {
        LogPacingCapture(worker, completion);
        SeedHistoryFromCapture(worker);
        worker.capturing = false;
        worker.captureCount = 0;
        worker.postRemaining = 0;
        worker.exact = true;
    }

    // --- Frame-wait pipelining ------------------------------------------
    // xrWaitFrame blocks until the runtime's next display time. Calling it
    // inline on the game's render thread meant the game sat IDLE inside it:
    // measured 11.4ms of a 17.6ms frame while the real work (render + present
    // + blit) was only ~6ms, with prediction error collapsing to 0.03ms -- the
    // loop was locked onto every other display interval instead of merely being
    // slow. This thread absorbs that block so the wait overlaps the game's
    // rendering: by the time the render thread asks for a frame state, the wait
    // has already happened and it proceeds immediately.
    //
    // Pairing stays legal: after the render thread claims Wait(N), it releases
    // the worker immediately BEFORE Begin(N). OpenXR requires the subsequent
    // Wait(N+1) to block until Begin(N), so pacing remains on the worker and the
    // overlap covers exactly the game's own render window.
    //
    // NO refresh rate is referenced here. Whatever period the runtime reports
    // is what we use, so 72/80/90/120/144 and anything future behave the same.
    HANDLE g_waitThread = nullptr;
} // namespace
namespace vr_core
{
    HANDLE g_waitReadyEvent = nullptr;    // wait thread -> render thread
    HANDLE g_waitConsumedEvent = nullptr; // render thread -> wait thread
    HANDLE g_waitStartedEvent = nullptr;  // wait thread -> render thread
    std::atomic<bool> g_waitThreadStop{false};
} // namespace vr_core
namespace
{
    XrFrameState g_waitedFrameState{XR_TYPE_FRAME_STATE};
    std::atomic<bool> g_waitedStateValid{false};
    std::atomic<bool> g_waitedStateFailed{false};
    std::atomic<XrResult> g_waitedStateResult{XR_SUCCESS};
    std::atomic<uint64_t> g_waitPacketMisses{0};
    // Wakes on a leftover event credit with no packet (waited again).
    std::atomic<uint64_t> g_waitStaleWakes{0};
    // A parked wait worker means the game has not reached Present yet. Normal
    // parks are one frame period; this is the threshold above which the player
    // is looking at a frozen reprojected image and thinks the game crashed.
    constexpr uint64_t kFrameStallNoticeMs = 1000;
    std::atomic<uint64_t> g_frameStalls{0};
    std::atomic<uint64_t> g_frameStallWorstMs{0};
    // Read by the wait worker while the render thread is stuck inside the game,
    // so a stall report can say whether the headset was actually SHOWING us. It
    // usually was not: a stall recorded while the runtime has the session merely
    // synchronized (app not visible, shouldRender=0, zero layers submitted) is
    // not what any player saw, and reading one as if it were cost real time on
    // 2026-07-28. The old wording asserted the player was staring at a frozen
    // frame; these two let the line state what was true instead.
    std::atomic<int> g_sessionStateShared{XR_SESSION_STATE_UNKNOWN};
    std::atomic<int> g_lastShouldRenderShared{-1};
    std::atomic<uint64_t> g_waitFailuresObserved{0};
    std::atomic<uint64_t> g_waitEventSignalFailures{0};
    std::atomic<uint64_t> g_waitCallSequence{0};
    std::atomic<uint64_t> g_waitCallInFlight{0};
    std::atomic<uint64_t> g_waitConsumedSequence{0};
} // namespace
namespace vr_core
{
    std::atomic<bool> g_waitPipelineFaulted{false};
} // namespace vr_core
namespace
{
    std::atomic<uint64_t> g_waitedPacketSequence{0};
    std::atomic<uint64_t> g_waitedPacketVersion{0};
    std::atomic<uint32_t> g_waitedSessionEpochStart{0};
    std::atomic<uint32_t> g_waitedSessionEpochEnd{0};
    std::atomic<int64_t> g_waitedCallStartQpc{0};
    std::atomic<int64_t> g_waitedCallEndQpc{0};
    std::atomic<int64_t> g_waitedReadyQpc{0};
} // namespace
namespace vr_core
{
    // Render-thread owned. A fatal frame-loop error requests STOPPING first so
    // PollEvents can end the running session before Fail switches VR off.
    const char* g_frameWaitFatalExitReason = nullptr;
    bool g_frameWaitExitRequestAccepted = false;
    uint64_t g_frameWaitExitLastRequestMs = 0;
    uint64_t g_frameWaitExitLastLogMs = 0;
} // namespace vr_core
namespace
{

    uint64_t g_missedPredictions = 0;
    uint64_t g_duplicatePredictions = 0;
    uint64_t g_frameOrderFailures = 0;
} // namespace
namespace vr_core
{
    std::atomic<uint64_t> g_preparedSerialPublished{0};
} // namespace vr_core
namespace
{
    std::atomic<uint64_t> g_prepareQpcPublished{0};
    std::atomic<uint64_t> g_cameraSerialObserved{0};
    std::atomic<uint64_t> g_firstCameraDelayUs{0};

} // namespace
namespace vr_core
{
    AimContinuityLayerState g_aimContinuityLayer;
    AimContinuityPreparedOutput g_aimContinuityOutput;
    AimContinuityPublication g_aimContinuityPublication;

    // Dedicated raw Aim/Grip/head sample for the asynchronous XInput aim
    // consumer. This is intentionally separate from smoothing, continuity,
    // Lab, reticle and engine-presentation publications.
    committed_aim::Publication g_committedAimSample;
    TwoHandInputSmoothingLayerState g_twoHandInputSmoothingLayer;
    TwoHandInputSmoothingPreparedOutput g_twoHandInputSmoothingOutput;
} // namespace vr_core
namespace
{

    // Two-Hand Lab temporal layer: forward declaration so the
    // exceptional lifecycle paths above (abort, fatal drain, reference-space
    // change, session loss, handedness swap) mirror the VS invalidation even
    // though the layer itself is defined beside the Lab seam below. The
    // routine prepared-frame retire (ResetPreparedFrame) must NOT call it,
    // exactly like the VS layer.

    // Prepared-frame presentation publishers (contact/Halo 2/Halo 4/Reach/CE
    // tracking snapshots) run on the thread that advanced the layer, so they
    // read that serial's own output directly. Only the emitted orientation
    // changes; positions and every control path stay raw.
    XrQuaternionf PresentAimContinuity(XrQuaternionf orientation, uint64_t preparedSerial) noexcept
    {
        const AimContinuityLayerState& layer = g_aimContinuityLayer;
        if (!layer.transition.active || g_aimContinuityOutput.serial != preparedSerial)
            return orientation;
        return ComposeAimContinuityCorrection(orientation, layer.transition.correction);
    }

} // namespace
namespace vr_core
{
    // Orientation-only variant for the per-title snapshots that publish a whole
    // pose: the position is always the untouched solver output.
    XrPosef PresentedAimContinuityPose(XrPosef pose, uint64_t preparedSerial) noexcept
    {
        pose.orientation = PresentAimContinuity(pose.orientation, preparedSerial);
        return pose;
    }
} // namespace vr_core
namespace
{

    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    static_assert(std::atomic<uint32_t>::is_always_lock_free);

    // ---------------------------------------------------------------- utils

    double QpcMs(LONGLONG ticks)
    {
        if (!g_qpcFrequency.QuadPart)
            QueryPerformanceFrequency(&g_qpcFrequency);
        return ticks * 1000.0 / static_cast<double>(g_qpcFrequency.QuadPart);
    }

    const char* SessionStateName(XrSessionState s); // defined below

    template <size_t N>
    double TimingPercentile(const TimingRing<N>& ring, double percentile)
    {
        if (!ring.count)
            return 0.0;
        std::array<double, N> sorted{};
        for (size_t i = 0; i < ring.count; ++i)
            sorted[i] = ring.values[i];
        std::sort(sorted.begin(), sorted.begin() + ring.count);
        const size_t index =
            static_cast<size_t>(std::clamp(percentile, 0.0, 1.0) * static_cast<double>(ring.count - 1));
        return sorted[index];
    }

    const char* SessionStateName(XrSessionState s)
    {
        switch (s)
        {
        case XR_SESSION_STATE_IDLE:
            return "idle";
        case XR_SESSION_STATE_READY:
            return "ready";
        case XR_SESSION_STATE_SYNCHRONIZED:
            return "synchronized";
        case XR_SESSION_STATE_VISIBLE:
            return "visible";
        case XR_SESSION_STATE_FOCUSED:
            return "focused";
        case XR_SESSION_STATE_STOPPING:
            return "stopping";
        case XR_SESSION_STATE_LOSS_PENDING:
            return "loss pending";
        case XR_SESSION_STATE_EXITING:
            return "exiting";
        default:
            return "unknown";
        }
    }

    // ------------------------------------------------------------- blitting

    void ReleaseSourceViews()
    {
        // Drains every cached view and intermediate. A cached view holds a
        // strong reference on its source, so this is also what stops the table
        // pinning a resource the game is about to destroy - every caller below
        // runs on a resize or a detach, before the backbuffer goes away.
        while (void* view = g_srcSrvCache.TakeAny())
            static_cast<ID3D11ShaderResourceView*>(view)->Release();
        for (std::size_t i = 0; i < kIntermediatePoolCapacity; ++i)
        {
            if (g_intermediateSrvs[i])
            {
                g_intermediateSrvs[i]->Release();
                g_intermediateSrvs[i] = nullptr;
            }
            if (g_intermediateTex[i])
            {
                g_intermediateTex[i]->Release();
                g_intermediateTex[i] = nullptr;
            }
        }
        g_intermediatePool.Clear();
    }

    // Once per two seconds, say what the upload path actually reused. This is
    // the line that settles the change from one session: `created` must stop
    // rising once a level is up, and `miss` must fall to 0 per window. If
    // either keeps climbing, the working set is bigger than the table and the
    // capacity is wrong - which is a number to read, not to argue about.
    // `intermediates` is the only part that holds real VRAM, so its live slot
    // count and byte total are printed rather than assumed.
    void ReportUploadResourceReuse()
    {
        static uint64_t lastMs = 0;
        static uint64_t lastCreatedViews = 0;
        static uint64_t lastCreatedIntermediates = 0;
        const uint64_t nowMs = GetTickCount64();
        if (nowMs - lastMs < 2000)
            return;
        lastMs = nowMs;

        if (g_memoryAdapter)
        {
            DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
            if (SUCCEEDED(g_memoryAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)))
                LOG("VRAM: process local usage %.1f MiB / budget %.1f MiB; render %ux%u; DLSS %s, preset %s",
                    double(memory.CurrentUsage) / 1048576.0, double(memory.Budget) / 1048576.0,
                    g_gameBackbufferDesc.Width, g_gameBackbufferDesc.Height,
                    g_config.upscaler == dlss::kUpscalerDlss ? "on" : "off", dlss::PresetName(g_config.dlss_preset));
        }

        uint64_t intermediateBytes = 0;
        std::size_t intermediateLive = 0;
        for (std::size_t i = 0; i < kIntermediatePoolCapacity; ++i)
        {
            if (!g_intermediateTex[i])
                continue;
            ++intermediateLive;
            D3D11_TEXTURE2D_DESC d{};
            g_intermediateTex[i]->GetDesc(&d);
            intermediateBytes += static_cast<uint64_t>(d.Width) * d.Height * DxgiBytesPerPixel(d.Format);
        }

        const ViewCacheStats& views = g_srcSrvCache.Stats();
        LOG("upload reuse: views %zu/%zu resident, %llu hit / %llu miss / %llu "
            "evicted, %llu created (+%llu this window); intermediates %zu/%zu "
            "live = %llu KB, %llu created (+%llu this window)",
            g_srcSrvCache.Size(), kSrcViewCacheCapacity, static_cast<unsigned long long>(views.hits),
            static_cast<unsigned long long>(views.misses), static_cast<unsigned long long>(views.evictions),
            static_cast<unsigned long long>(g_srcViewCreated),
            static_cast<unsigned long long>(g_srcViewCreated - lastCreatedViews), intermediateLive,
            kIntermediatePoolCapacity, static_cast<unsigned long long>(intermediateBytes / 1024),
            static_cast<unsigned long long>(g_intermediateCreated),
            static_cast<unsigned long long>(g_intermediateCreated - lastCreatedIntermediates));
        lastCreatedViews = g_srcViewCreated;
        lastCreatedIntermediates = g_intermediateCreated;
        g_srcSrvCache.ResetStats();
    }

#include "../titles/ce/haloce_desktop_mirror.inl"

} // namespace
namespace vr_core
{
    IqTimerSlot g_iqTimer[kIqTimerDepth];
    GpuEyeTiming g_eyeGpuTiming;
    std::size_t g_iqTimerCursor = 0;  // which slot this frame is recording into
    uint32_t g_iqTimerStampIndex = 0; // stamps written so far this frame
    bool g_iqTimerArmed = false;
    bool g_iqTimerUsable = false;
} // namespace vr_core
namespace
{
    bool g_iqTimerTried = false;

    bool EnsureIqTimer()
    {
        if (g_iqTimerTried)
            return g_iqTimerUsable;
        g_iqTimerTried = true;
        if (!g_device)
            return false;
        D3D11_QUERY_DESC disjointDesc{};
        disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        D3D11_QUERY_DESC stampDesc{};
        stampDesc.Query = D3D11_QUERY_TIMESTAMP;
        for (auto& slot : g_iqTimer)
        {
            if (FAILED(g_device->CreateQuery(&disjointDesc, &slot.disjoint)))
                return false;
            for (auto*& stamp : slot.stamps)
            {
                if (FAILED(g_device->CreateQuery(&stampDesc, &stamp)))
                    return false;
            }
            for (auto*& stamp : slot.dlssWait)
                if (FAILED(g_device->CreateQuery(&stampDesc, &stamp)))
                    return false;
        }
        g_iqTimerUsable = true;
        g_eyeGpuTiming.Init(g_device);
        LOG("IQ TIMING: GPU timestamps armed; the eye publish now reports real "
            "GPU milliseconds, split resolve vs post");
        return true;
    }

    void ReleaseIqTimer()
    {
        g_eyeGpuTiming.Release();
        for (auto& slot : g_iqTimer)
        {
            if (slot.disjoint)
            {
                slot.disjoint->Release();
                slot.disjoint = nullptr;
            }
            for (auto*& stamp : slot.stamps)
            {
                if (stamp)
                {
                    stamp->Release();
                    stamp = nullptr;
                }
            }
            slot.written = 0;
            for (auto*& stamp : slot.dlssWait)
            {
                if (stamp)
                    stamp->Release();
                stamp = nullptr;
            }
            slot.waitMask = 0;
            slot.pending = false;
        }
        g_iqTimerArmed = false;
        g_iqTimerUsable = false;
        g_iqTimerTried = false;
    }

    void IqTimerBeginFrame()
    {
        g_iqTimerArmed = false;
        g_iqTimerStampIndex = 0;
        if (!EnsureIqTimer() || !g_context)
            return;
        CollectIqTimings();
        // Never overwrite a slot the GPU has not finished reporting.
        IqTimerSlot& slot = g_iqTimer[g_iqTimerCursor];
        if (slot.pending)
            return;
        slot.written = 0;
        slot.waitMask = 0;
        g_context->Begin(slot.disjoint);
        g_iqTimerArmed = true;
    }

    void IqTimerDlssWait(int eye, bool end)
    {
        if (!g_iqTimerArmed || eye < 0 || eye > 1)
            return;
        auto& slot = g_iqTimer[g_iqTimerCursor];
        const unsigned index = eye * 2 + (end ? 1 : 0);
        if (slot.waitMask & (1u << index))
            return;
        g_context->End(slot.dlssWait[index]);
        slot.waitMask |= 1u << index;
    }

    void IqTimerEndFrame()
    {
        if (!g_iqTimerArmed)
            return;
        IqTimerSlot& slot = g_iqTimer[g_iqTimerCursor];
        g_context->End(slot.disjoint);
        // A frame that produced no complete triple is not worth collecting.
        slot.pending = slot.written >= 3;
        g_iqTimerArmed = false;
        g_iqTimerCursor = (g_iqTimerCursor + 1) % kIqTimerDepth;
    }

    static_assert(sizeof(DlssMotionParams) == 14 * 16);

} // namespace
namespace vr_core
{
    const char* DlssTitleName(GameTitle title)
    {
        switch (title)
        {
        case GameTitle::Halo3:
            return "Halo 3";
        case GameTitle::Halo3ODST:
            return "Halo 3: ODST";
        case GameTitle::HaloReach:
            return "Halo: Reach";
        case GameTitle::Halo4:
            return "Halo 4";
        case GameTitle::HaloCE:
            return "Halo: CE";
        case GameTitle::Halo2:
            return "Halo 2";
        default:
            return "no title";
        }
    }
} // namespace vr_core
namespace
{

    void TickRenderPlan()
    {
        // A mode/model/size/debug change is an explicit retry. A persistent
        // failure otherwise remains at full resolution until the player makes
        // one of those changes or toggles DLSS off and on.
        static int lastUpscaler = g_config.upscaler;
        static int lastMode = g_config.dlss_mode;
        static int lastPreset = g_config.dlss_preset;
        static float lastScale = g_config.resolution_scale;
        static bool lastDebug = g_config.dlss_debug_view;
        const bool debugChanged = lastDebug != g_config.dlss_debug_view;
        const bool dlssSettingChanged = lastUpscaler != g_config.upscaler || lastMode != g_config.dlss_mode ||
                                        lastPreset != g_config.dlss_preset || lastScale != g_config.resolution_scale ||
                                        debugChanged;
        if (dlssSettingChanged)
        {
            D3D_RetryLiveResize();
            // Debug uses the old eye-local motion surfaces; normal operation
            // uses the compact atlas. Drop the other graph on either edge so
            // toggling the diagnostic cannot retain both allocations.
            if (debugChanged && g_config.upscaler == dlss::kUpscalerDlss)
                ReleaseDlssResources();
            if (g_dlssBatchSuppressed && g_config.upscaler == dlss::kUpscalerDlss)
            {
                LOG("DLSS: settings changed; retrying the eye resolve");
                SetDlssStatus("retrying the eye resolve after a settings change");
            }
            g_dlssBatchSuppressed = false;
            g_dlssBatchFailedFrames = 0;
            lastUpscaler = g_config.upscaler;
            lastMode = g_config.dlss_mode;
            lastPreset = g_config.dlss_preset;
            lastScale = g_config.resolution_scale;
            lastDebug = g_config.dlss_debug_view;
        }
        int nativeW = 0, nativeH = 0;
        nativeW = kNativeRenderWidth;
        nativeH = kNativeRenderHeight;
        unsigned planRenderW = 0, planRenderH = 0, planOutW = 0, planOutH = 0;
        bool planDlss = false, runtimePresent = false;
        D3D_GetRenderPlan(planRenderW, planRenderH, planOutW, planOutH, planDlss, runtimePresent);
        const dlss::RenderPlan world =
            dlss::PlanRender(nativeW, nativeH, g_config.resolution_scale, g_config.upscaler, g_config.dlss_mode,
                runtimePresent && !g_dlssBatchSuppressed &&
                    dlss::TitleHasCameraContract(TitleAdapter_GetActiveTitle()) && Dlss_EnsureInitialized(g_device));
        const RuntimeMode presentation = DlssPresentationMode();
        static GameTitle lastResizeTitle = GameTitle::None;
        static uint32_t lastResizeGeneration = 0;
        const auto resizeTitle = TitleAdapter_GetActiveTitle();
        const auto resizeGeneration = TitleAdapter_GetGeneration(resizeTitle);
        if (resizeTitle != lastResizeTitle || resizeGeneration != lastResizeGeneration)
        {
            D3D_RetryLiveResize();
            lastResizeTitle = resizeTitle;
            lastResizeGeneration = resizeGeneration;
        }
        static dlss::RenderPlan lastWant{};
        static uint64_t stableSinceMs = 0;
        const uint64_t nowMs = GetTickCount64();
        if (!dlss::CanChangeRenderPlan(presentation))
        {
            // Rejected transition-resize hold, retained dormant. The policy
            // currently admits all modes, preserving full-resolution menus.
            lastWant = {};
            stableSinceMs = nowMs;
            return;
        }
        const dlss::RenderPlan want = dlss::PlanPresentation(world, presentation);
        if (!want.outputW || !want.outputH)
            return;
        if (want.renderW != lastWant.renderW || want.renderH != lastWant.renderH || want.outputW != lastWant.outputW ||
            want.outputH != lastWant.outputH || want.dlss != lastWant.dlss)
        {
            lastWant = want;
            stableSinceMs = nowMs;
            return;
        }
        const bool differs = static_cast<unsigned>(want.renderW) != planRenderW ||
                             static_cast<unsigned>(want.renderH) != planRenderH ||
                             static_cast<unsigned>(want.outputW) != planOutW ||
                             static_cast<unsigned>(want.outputH) != planOutH || want.dlss != planDlss;
        if (!differs || nowMs - stableSinceMs < 500)
            return;
        const D3DRenderPlanResult result =
            D3D_RequestRenderPlan(static_cast<unsigned>(want.renderW), static_cast<unsigned>(want.renderH),
                static_cast<unsigned>(want.outputW), static_cast<unsigned>(want.outputH), want.dlss);
        if (result == D3DRenderPlanResult::Requested)
        {
            LOG("render plan queued: %s requests %dx%d canvas; world input %dx%d, output %dx%d",
                RuntimeModeName(presentation), want.renderW, want.renderH, world.renderW, world.renderH, world.outputW,
                world.outputH);
            Menu_PostLiveResize();
        }
    }

    // A completed eye belongs to the source eye, independent of whether its
    // consumer is immersive projection or a theatre screen. Consume it once.
    bool DlssTakeEyeOutput(int eye, DlssKickedEye& kicked)
    {
        if (eye < 0 || eye > 1 || !g_dlssKicked[eye].Take(g_preparedFrame.serial, kicked))
            return false;
        if (kicked.output)
        {
            IqTimerDlssWait(eye, false);
            Dlss_SyncD3D11(g_context, eye);
            IqTimerDlssWait(eye, true);
        }
        return true;
    }

    // At present: stretches one eye's finished upscale into the headset
    // image, after one GPU wait for that eye's evaluation (the first eye's
    // is normally long complete; the second's finishes while the first is
    // stretched). False means no upscale is in flight for this eye.
    bool DlssFinishEye(int eye, ID3D11Texture2D* dst, uint32_t dstW, uint32_t dstH, ID3D11RenderTargetView* dstRtv)
    {
        DlssKickedEye kicked{};
        if (!DlssTakeEyeOutput(eye, kicked))
            return false;
        if (!kicked.output)
            return true; // the motion-vector view already drew into dst
        if (!dst || !dstRtv || !dstW || !dstH)
            return false;
        // The same resolve as DLSS-off (linear, then the Sharpening slider).
        if (!BlitImageQuality(kicked.output, kicked.outputDesc, dst, dstW, dstH, dstRtv, true))
        {
            LOG("DLSS ERROR: the finished DLSS image could not be copied into "
                "the headset image");
            return false;
        }
        return true;
    }

    // The eye-end kick. Every title except Reach publishes its eye into
    // g_eyeCache by the time VR_EndRasterEye runs (that copy exists so the
    // other eye cannot overwrite it), so the upscale can start right here.
    // Reach's eye capture is read at present; it kicks there instead.
    void DlssKickEyeAtEyeEnd(int eye)
    {
        if (eye < 0 || eye > 1 || !VR_DlssWantsEyeCamera() || g_config.dlss_debug_view)
            return;
        if (TitleAdapter_GetActiveTitle() == GameTitle::HaloReach)
            return;
        if (!g_eyeCache[eye] || !g_eyeCacheDesc.Width || !g_eyeCacheDesc.Height || !g_stereoW || !g_stereoH)
            return;
        DlssKickEye(eye, g_eyeCache[eye], g_eyeCacheDesc, g_stereoW, g_stereoH, nullptr);
    }

    // (The HUD "capture-diff panel" machinery that lived here — per-eye pre-HUD
    // snapshots, ps_huddiff extraction, union blend, head-locked panel quad,
    // native-HUD erase — was headset-DISPROVEN 2026-07-19: the diff carried only
    // the objective text, the rest of the HUD vanished, and the capture copies
    // cost real GPU time every frame, so it was removed. The HUD
    // ships native and full-size; only the reticle element is hidden via the
    // verified 0x2EDF24 element hook.)

    bool TheaterProjectionCanSampleDirectly(const D3D11_TEXTURE2D_DESC& sourceDesc) noexcept
    {
        return g_config.upscale_filter == 0 && g_config.aa_mode == 0 && g_config.sharpness <= 0.001f &&
               (sourceDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 && sourceDesc.SampleDesc.Count == 1;
    }

    // ------------------------------------------------------- XR swapchains

    void DestroyChain(
        XrSwapchain& chain, std::vector<ID3D11Texture2D*>& images, std::vector<ID3D11RenderTargetView*>& rtvs)
    {
        for (auto* rtv : rtvs)
            if (rtv)
                rtv->Release();
        rtvs.clear();
        images.clear(); // owned by the runtime, not AddRef'd
        if (chain != XR_NULL_HANDLE)
        {
            xrDestroySwapchain(chain);
            chain = XR_NULL_HANDLE;
        }
    }

} // namespace
namespace vr_core
{
    bool CreateChain(uint32_t w, uint32_t h, XrSwapchain& chain, std::vector<ID3D11Texture2D*>& images,
        std::vector<ID3D11RenderTargetView*>& rtvs, const char* what)
    {
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                        XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        ci.format = g_xrFormat;
        ci.sampleCount = 1;
        ci.width = w;
        ci.height = h;
        ci.faceCount = 1;
        ci.arraySize = 1;
        ci.mipCount = 1;
        XrResult r = xrCreateSwapchain(g_session, &ci, &chain);
        if (XR_FAILED(r))
        {
            LOG("xrCreateSwapchain(%s, %ux%u) failed: %s", what, w, h, XrStr(r));
            return false;
        }
        uint32_t count = 0;
        r = xrEnumerateSwapchainImages(chain, 0, &count, nullptr);
        if (XR_FAILED(r) || !count)
        {
            LOG("xrEnumerateSwapchainImages(%s) count failed: %s", what, XrStr(r));
            DestroyChain(chain, images, rtvs);
            return false;
        }
        std::vector<XrSwapchainImageD3D11KHR> xrImages(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        r = xrEnumerateSwapchainImages(
            chain, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(xrImages.data()));
        if (XR_FAILED(r))
        {
            LOG("xrEnumerateSwapchainImages(%s) failed: %s", what, XrStr(r));
            DestroyChain(chain, images, rtvs);
            return false;
        }
        images.clear();
        rtvs.assign(count, nullptr);
        for (auto& img : xrImages)
            images.push_back(img.texture);
        LOG("XR swapchain '%s' created: %ux%u, %u images", what, w, h, count);
        return true;
    }

    // The held gun's glass shape (mask, kit rounded rectangle, wide flag) and the
    // glass aspect, resolved once per identity change. Called by the lens quad BEFORE
    // it sizes the quad (the first sniper quad was 0.040 x 0.040 for a frame because
    // the aspect only updated in the upload that follows) and by the upload itself.
    uint64_t g_scopeMaskIdentity = 0;
    float g_scopeKitAspect = 0;
    bool g_scopeMaskWide = false;
} // namespace vr_core
namespace
{

    bool g_lensDeliveredStereo = false; // this frame's lens image is the per-eye pair
} // namespace
namespace vr_core
{
    std::atomic<uint64_t> g_lensSubmittedMs{0}; // the lens quad last went out (monitor inset)
    ScopeImageProbe g_scopeImageProbe;
    bool PrepareScopeImageDelivery(const scope_parallax::Eye* eyes)
    {
        static glass_reprojection::RetryLatch creationFailed;
        if (!HeldScopeImageFresh())
            return false; // a scope pass that keeps refusing leaves no frozen glass
        if (creationFailed.Blocked(GetTickCount64()) || !g_scopeHasImage.load() ||
            g_scopeImageTitle != TitleAdapter_GetActiveTitle() ||
            g_scopeImageGeneration != TitleAdapter_GetGeneration(g_scopeImageTitle) || !g_scopeCache ||
            !g_scopeCacheSrv || !EnsureScopeUploadPipeline())
            return false;
        // One image per eye side by side; both off keeps the single image.
        static glass_reprojection::RetryLatch stereoCreationFailed;
        if (eyes && stereoCreationFailed.Blocked(GetTickCount64()))
            eyes = nullptr;
        if (eyes && g_scopeStereoChain == XR_NULL_HANDLE &&
            !CreateChain(2 * kScopeScreenWidth, kScopeScreenHeight, g_scopeStereoChain, g_scopeStereoImages,
                g_scopeStereoRtvs, "scope per-eye screen"))
        {
            stereoCreationFailed.Fail(GetTickCount64());
            eyes = nullptr;
        }
        if (!eyes && g_scopeScreenChain == XR_NULL_HANDLE &&
            !CreateChain(kScopeScreenWidth, kScopeScreenHeight, g_scopeScreenChain, g_scopeScreenImages,
                g_scopeScreenRtvs, "scope 4:3 screen"))
        {
            creationFailed.Fail(GetTickCount64());
            return false;
        }
        XrSwapchain chain = eyes ? g_scopeStereoChain : g_scopeScreenChain;
        std::vector<ID3D11Texture2D*>& chainImages = eyes ? g_scopeStereoImages : g_scopeScreenImages;
        std::vector<ID3D11RenderTargetView*>& chainRtvs = eyes ? g_scopeStereoRtvs : g_scopeScreenRtvs;
        g_lensDeliveredStereo = eyes != nullptr;

        uint32_t index = 0;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = 1000000000;
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};

        XrResult result = xrAcquireSwapchainImage(chain, &acquire, &index);
        if (XR_FAILED(result))
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                LOG("scope image acquire failed: %s", XrStr(result));
            }
            return false;
        }
        result = xrWaitSwapchainImage(chain, &wait);
        if (XR_FAILED(result))
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                LOG("scope image wait failed: %s", XrStr(result));
            }
            xrReleaseSwapchainImage(chain, &release);
            return false;
        }

        ID3D11RenderTargetView* rtv = nullptr;
        if (index < chainImages.size() && index < chainRtvs.size())
        {
            rtv = chainRtvs[index];
            if (!rtv)
            {
                D3D11_RENDER_TARGET_VIEW_DESC desc{};
                desc.Format = (DXGI_FORMAT)g_xrFormat;
                desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice = 0;
                const HRESULT hr = g_device->CreateRenderTargetView(chainImages[index], &desc, &chainRtvs[index]);
                if (FAILED(hr))
                {
                    static bool logged = false;
                    if (!logged)
                    {
                        logged = true;
                        LOG("scope image RTV creation failed: HRESULT 0x%08X format %d", (unsigned)hr, (int)g_xrFormat);
                    }
                }
                rtv = chainRtvs[index];
            }
        }

        const bool uploaded =
            eyes ? UploadScopeToRtv(rtv, &eyes[0], 0.0f) && UploadScopeToRtv(rtv, &eyes[1], (float)kScopeScreenWidth)
                 : UploadScopeToRtv(rtv);
        if (uploaded && index < chainImages.size())
            ProbeScopeImage(chainImages[index], eyes != nullptr);
        if (!uploaded)
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                LOG("scope image upload failed: no valid output RTV");
            }
        }

        const XrResult releaseResult = xrReleaseSwapchainImage(chain, &release);
        if (XR_FAILED(releaseResult))
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                LOG("scope image release failed: %s", XrStr(releaseResult));
            }
            return false;
        }
        return uploaded;
    }

    // The lens quad's pose for this frame and, with parallax/eyebox on, each
    // eye's picture shift and shadow, then the swapchain fill. Render thread.
    ScopeQuadTransform g_lensPlanTransform{};
    scope_parallax::Eye g_lensPlanEyes[2]{};

    bool EnsureScreenChain(uint32_t w, uint32_t h)
    {
        if (w == 0 || h == 0) // game momentarily has a 0x0 backbuffer (e.g. intro video / mode switch)
            return false;
        if (g_screenChain != XR_NULL_HANDLE && g_screenW == w && g_screenH == h)
            return true;
        DestroyChain(g_screenChain, g_screenImages, g_screenRtvs);
        if (!CreateChain(w, h, g_screenChain, g_screenImages, g_screenRtvs, "screen"))
            return false;
        g_screenW = w;
        g_screenH = h;
        g_status.gameWidth = w;
        g_status.gameHeight = h;
        return true;
    }
} // namespace vr_core
namespace
{

    // The eye image spans the title's symmetric cover, but only the native
    // per-eye FOV rect inside it is submitted. Sized at the recommended size,
    // that rect delivered only part of the pixels the headset asked for (one
    // test headset: 964x1433 of 1440x1584), which the compositor then stretched.
    // eye_full_density enlarges the swapchain so the SUBMITTED rect is about
    // the recommended size. The cover estimate is the titles' own: the larger
    // native half-angles, widened to the 2912:2100 raster aspect. What the
    // image covers is unchanged, so every consumer stays valid.
    void SizeStereoChain(const XrFovf* fov)
    {
        const auto& view = g_viewConfigs[0];
        g_stereoW = view.recommendedImageRectWidth;
        g_stereoH = view.recommendedImageRectHeight;
        if (!g_config.eye_full_density)
            return;
        if (!fov)
        {
            LOG("eye full density: views not located when the eye swapchain was created; "
                "kept the recommended %ux%u",
                g_stereoW, g_stereoH);
            return;
        }
        const float l = tanf(fov->angleLeft), r = tanf(fov->angleRight);
        const float u = tanf(fov->angleUp), d = tanf(fov->angleDown);
        const float coverY = std::max(std::fabs(u), std::fabs(d));
        const float coverX = std::max(
            std::max(std::fabs(l), std::fabs(r)), coverY * float(kNativeRenderWidth) / float(kNativeRenderHeight));
        const float fx = (r - l) / (2.0f * coverX), fy = (u - d) / (2.0f * coverY);
        if (!std::isfinite(fx) || !std::isfinite(fy) || fx <= 0.1f || fy <= 0.1f || fx > 1.0f || fy > 1.0f)
            return;
        auto grow = [](uint32_t recommended, float fraction, uint32_t maximum)
        {
            const float wanted = std::min(float(recommended) / fraction, 2.0f * float(recommended));
            return std::max(
                recommended, std::min(maximum ? maximum : recommended, static_cast<uint32_t>(std::lround(wanted))));
        };
        g_stereoW = grow(view.recommendedImageRectWidth, fx, view.maxImageRectWidth);
        g_stereoH = grow(view.recommendedImageRectHeight, fy, view.maxImageRectHeight);
        LOG("eye full density: recommended %ux%u, native rect is %.0f%%x%.0f%% of the cover -> eye "
            "swapchain %ux%u (submitted rect ~%.0fx%.0f)",
            view.recommendedImageRectWidth, view.recommendedImageRectHeight, fx * 100.0f, fy * 100.0f, g_stereoW,
            g_stereoH, g_stereoW * fx, g_stereoH * fy);
    }

    bool CreateStereoArrayChain(const XrFovf* fov)
    {
        if (g_viewConfigs.size() != 2)
            return false;
        SizeStereoChain(fov);
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        ci.format = g_xrFormat;
        ci.sampleCount = 1;
        ci.width = g_stereoW;
        ci.height = g_stereoH;
        ci.faceCount = 1;
        ci.arraySize = 2;
        ci.mipCount = 1;
        XrResult r = xrCreateSwapchain(g_session, &ci, &g_stereoChain);
        if (XR_FAILED(r))
        {
            LOG("xrCreateSwapchain(stereo array) failed: %s", XrStr(r));
            return false;
        }
        uint32_t count = 0;
        xrEnumerateSwapchainImages(g_stereoChain, 0, &count, nullptr);
        std::vector<XrSwapchainImageD3D11KHR> xrImages(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        r = xrEnumerateSwapchainImages(
            g_stereoChain, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(xrImages.data()));
        if (XR_FAILED(r))
            return false;
        g_stereoImages.clear();
        for (auto& image : xrImages)
            g_stereoImages.push_back(image.texture);
        g_stereoRtvs.resize(count);
        for (auto& pair : g_stereoRtvs)
            pair = {nullptr, nullptr};
        LOG("stereo 2-slice array swapchain created: %ux%u, %u images", g_stereoW, g_stereoH, count);
        return true;
    }

    ID3D11RenderTargetView* GetStereoRtv(uint32_t image, uint32_t eye)
    {
        if (image >= g_stereoImages.size() || eye >= 2)
            return nullptr;
        if (!g_stereoRtvs[image][eye])
        {
            D3D11_RENDER_TARGET_VIEW_DESC desc{};
            desc.Format = (DXGI_FORMAT)g_xrFormat;
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MipSlice = 0;
            desc.Texture2DArray.FirstArraySlice = eye;
            desc.Texture2DArray.ArraySize = 1;
            if (FAILED(g_device->CreateRenderTargetView(g_stereoImages[image], &desc, &g_stereoRtvs[image][eye])))
                return nullptr;
        }
        return g_stereoRtvs[image][eye];
    }

    void ValidateStereoImagesOnce()
    {
        if (g_stereoValidationDone || !g_eyeHasImage[0] || !g_eyeHasImage[1] || !g_eyeCache[0] || !g_eyeCache[1])
            return;
        if (g_eyeCacheDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
            g_eyeCacheDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        {
            LOG("eye validation: unsupported eye format %u", (unsigned)g_eyeCacheDesc.Format);
            g_stereoValidationDone = true;
            return;
        }

        D3D11_TEXTURE2D_DESC stagingDesc = g_eyeCacheDesc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        ID3D11Texture2D* staging[2]{};
        if (FAILED(g_device->CreateTexture2D(&stagingDesc, nullptr, &staging[0])) ||
            FAILED(g_device->CreateTexture2D(&stagingDesc, nullptr, &staging[1])))
        {
            if (staging[0])
                staging[0]->Release();
            if (staging[1])
                staging[1]->Release();
            LOG("eye validation: staging allocation failed");
            g_stereoValidationDone = true;
            return;
        }

        g_context->CopyResource(staging[0], g_eyeCache[0]);
        g_context->CopyResource(staging[1], g_eyeCache[1]);
        D3D11_MAPPED_SUBRESOURCE mapped[2]{};
        const HRESULT hr0 = g_context->Map(staging[0], 0, D3D11_MAP_READ, 0, &mapped[0]);
        const HRESULT hr1 = g_context->Map(staging[1], 0, D3D11_MAP_READ, 0, &mapped[1]);
        if (SUCCEEDED(hr0) && SUCCEEDED(hr1))
        {
            unsigned long long rgbDelta = 0;
            unsigned samples = 0, changed = 0;
            const unsigned step = 16;
            for (unsigned y = step / 2; y < g_eyeCacheDesc.Height; y += step)
            {
                const auto* left = static_cast<const unsigned char*>(mapped[0].pData) + y * mapped[0].RowPitch;
                const auto* right = static_cast<const unsigned char*>(mapped[1].pData) + y * mapped[1].RowPitch;
                for (unsigned x = step / 2; x < g_eyeCacheDesc.Width; x += step)
                {
                    const unsigned o = x * 4;
                    unsigned pixelDelta = 0;
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        const int d = (int)left[o + c] - (int)right[o + c];
                        pixelDelta += (unsigned)(d < 0 ? -d : d);
                    }
                    rgbDelta += pixelDelta;
                    if (pixelDelta > 12)
                        ++changed;
                    ++samples;
                }
            }
            const double meanChannelDelta = samples ? (double)rgbDelta / (samples * 3.0) : 0.0;
            const double changedPercent = samples ? (100.0 * changed / samples) : 0.0;
            LOG("eye validation: distinct eye pixels mean RGB delta=%.3f, changed samples=%.1f%% (%u/%u)",
                meanChannelDelta, changedPercent, changed, samples);
        }
        else
        {
            LOG("eye validation: staging map failed (0x%08X, 0x%08X)", (unsigned)hr0, (unsigned)hr1);
        }
        if (SUCCEEDED(hr0))
            g_context->Unmap(staging[0], 0);
        if (SUCCEEDED(hr1))
            g_context->Unmap(staging[1], 0);
        staging[0]->Release();
        staging[1]->Release();
        g_stereoValidationDone = true;
    }

    void ResetPreparedFrame()
    {
        g_preparedShouldRender.store(false, std::memory_order_release);
        g_preparedViewSerialPublished.store(0, std::memory_order_release);
        // A retired/aborted prepared frame cannot remain a valid
        // controller/head pairing afterwards.
        g_stockAimFresh.store(false, std::memory_order_release);
        g_supportGripPoseFresh.store(false, std::memory_order_release);
        // The grab/release transition layer deliberately SURVIVES the routine
        // prepared-frame retire: it is advanced once per prepared serial in
        // PrepareNextFrame, and SubmitPreparedFrame's unconditional
        // ResetPreparedFrame runs between every pair of those advances, so
        // invalidating here would make every frame a first observation and no
        // grab/release edge could ever seed. Only the genuine abort / session /
        // incoherence paths below invalidate the layer.
        g_halo2PreparedCadenceSerial.store(0, std::memory_order_release);
        g_preparedFrame.begun = false;
        g_preparedFrame.viewsValid = false;
        g_preparedFrame.viewCount = 0;
        g_preparedFrame.predictedDisplayDelta = 0;
    }

    void EndPreparedFrameWithoutLayers(const char* reason)
    {
        // Every caller of this abort path is exceptional (session stopping,
        // failed swapchain layer transactions, a failed next-wait dispatch
        // gate). Unlike the routine SubmitPreparedFrame retire, an aborted
        // frame must not leave a transition correction or history behind, so
        // both branches invalidate.
        InvalidateAimContinuityLayer();
        InvalidateTwoHandLabTemporal();
        if (!g_preparedFrame.begun || g_session == XR_NULL_HANDLE)
        {
            ResetPreparedFrame();
            return;
        }
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.displayTime = g_preparedFrame.state.predictedDisplayTime;
        end.environmentBlendMode = g_blendMode;
        const XrResult r = xrEndFrame(g_session, &end);
        if (XR_FAILED(r))
        {
            ++g_frameOrderFailures;
            LOG("timing: empty xrEndFrame during %s failed: %s", reason, XrStr(r));
        }
        ResetPreparedFrame();
    }

    // -------------------------------------------------------------- events

    void PollEvents()
    {
        XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(g_instance, &ev) == XR_SUCCESS)
        {
            switch (ev.type)
            {
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
            {
                const auto& change = *reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&ev);
                if (change.session == g_session && change.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL)
                {
                    // Inhibit contact motion until the new space actually
                    // takes effect; an epoch bump at notification time alone
                    // would allow a later origin jump to look like a punch.
                    g_contactSpaceChangeAtNs.store(change.changeTime, std::memory_order_release);
                    g_contactSpaceEpoch.fetch_add(1, std::memory_order_acq_rel);
                    // The stock anchor and any active transition belong to the
                    // old reference space; drop them before the new space is
                    // used. The seam also compares the epoch per serial.
                    InvalidateAimContinuityLayer();
                    InvalidateTwoHandLabTemporal();
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
            {
                auto& sc = *reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
                if (g_sessionState != sc.state)
                    g_controllerProfileEpoch.fetch_add(1, std::memory_order_acq_rel);
                g_sessionState = sc.state;
                g_sessionStateShared.store(static_cast<int>(sc.state), std::memory_order_relaxed);
                strcpy_s(g_status.sessionState, SessionStateName(sc.state));
                LOG("XR session state -> %s", SessionStateName(sc.state));
                if (sc.state != XR_SESSION_STATE_FOCUSED)
                {
                    g_thumbrestDpadSampleMs.store(0, std::memory_order_release);
                    g_weaponHapticAdmission.Invalidate();
                    StopControllerHaptics();
                }
                if (sc.state == XR_SESSION_STATE_READY)
                {
                    XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                    bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    XrResult r = xrBeginSession(g_session, &bi);
                    if (XR_SUCCEEDED(r))
                    {
                        g_framePacingSessionEpoch.fetch_add(1, std::memory_order_release);
                        if (StartFrameWaitThread())
                        {
                            g_sessionRunning = true;
                            // Publish only after xrBeginSession and the exclusive
                            // frame-wait worker are both live. SteamVR can accept
                            // earlier swapchains yet present them black.
                            g_authoredReticlePreparationReady.store(true, std::memory_order_release);
                        }
                        else
                        {
                            EnterFrameWaitFatalDrain("The exclusive OpenXR frame-wait worker could "
                                                     "not start");
                        }
                    }
                    else
                        LOG("xrBeginSession failed: %s", XrStr(r));
                }
                else if (sc.state == XR_SESSION_STATE_STOPPING)
                {
                    const char* fatalExitReason = g_frameWaitFatalExitReason;
                    g_weaponHapticAdmission.Invalidate();
                    StopControllerHaptics();
                    EndPreparedFrameWithoutLayers("session stopping");
                    g_authoredReticlePreparationReady.store(false, std::memory_order_release);
                    g_sessionRunning = false;
                    if (!StopFrameWaitThread())
                    {
                        Game_DetachForVrRuntimeFailure();
                        Fail("The OpenXR frame-wait worker did not stop; refusing "
                             "to end the session while a frame call may be active");
                        break;
                    }
                    const XrResult endResult = xrEndSession(g_session);
                    if (XR_FAILED(endResult))
                        LOG("xrEndSession failed: %s", XrStr(endResult));
                    if (fatalExitReason)
                    {
                        g_frameWaitFatalExitReason = nullptr;
                        g_frameWaitExitRequestAccepted = false;
                        g_frameWaitExitLastRequestMs = 0;
                        g_frameWaitExitLastLogMs = 0;
                        Game_DetachForVrRuntimeFailure();
                        Fail(fatalExitReason);
                    }
                }
                else if (sc.state == XR_SESSION_STATE_EXITING || sc.state == XR_SESSION_STATE_LOSS_PENDING)
                {
                    g_weaponHapticAdmission.Invalidate();
                    StopControllerHaptics();
                    // The session is ending: drop the transition layer with the
                    // prepared frame instead of letting it survive into a later
                    // session (the routine retire no longer does this).
                    InvalidateAimContinuityLayer();
                    InvalidateTwoHandLabTemporal();
                    ResetPreparedFrame();
                    g_authoredReticlePreparationReady.store(false, std::memory_order_release);
                    g_sessionRunning = false;
                    StopFrameWaitThread();
                    Fail("The VR runtime ended the session (headset off / SteamVR closed?)");
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                g_weaponHapticAdmission.Invalidate();
                StopControllerHaptics();
                InvalidateAimContinuityLayer();
                InvalidateTwoHandLabTemporal();
                ResetPreparedFrame();
                g_authoredReticlePreparationReady.store(false, std::memory_order_release);
                g_sessionRunning = false;
                StopFrameWaitThread();
                Fail("The OpenXR runtime is shutting down");
                break;
            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
            {
                auto logProfile = [](XrPath hand, const char* label, unsigned index)
                {
                    g_controllerProfiles[index].store(vr_mapping::Profile::Unknown, std::memory_order_release);
                    XrInteractionProfileState state{XR_TYPE_INTERACTION_PROFILE_STATE};
                    if (XR_FAILED(xrGetCurrentInteractionProfile(g_session, hand, &state)) ||
                        state.interactionProfile == XR_NULL_PATH)
                    {
                        LOG("controller profile %s: unavailable", label);
                        return;
                    }
                    char path[XR_MAX_PATH_LENGTH]{};
                    uint32_t written = 0;
                    if (XR_SUCCEEDED(xrPathToString(
                            g_instance, state.interactionProfile, (uint32_t)sizeof(path), &written, path)))
                    {
                        g_controllerProfiles[index].store(vr_mapping::DetectProfile(path), std::memory_order_release);
                        LOG("controller profile %s: %s", label, path);
                    }
                };
                logProfile(g_leftHandPath, "left", 0);
                logProfile(g_rightHandPath, "right", 1);
                g_controllerProfileEpoch.fetch_add(1, std::memory_order_acq_rel);
                g_interactionProfileEpoch.fetch_add(1, std::memory_order_acq_rel);
                if (g_actMenu != XR_NULL_HANDLE)
                {
                    XrBoundSourcesForActionEnumerateInfo info{XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO};
                    info.action = g_actMenu;
                    uint32_t count = 0;
                    if (XR_SUCCEEDED(xrEnumerateBoundSourcesForAction(g_session, &info, 0, &count, nullptr)) &&
                        count > 0)
                    {
                        std::vector<XrPath> sources(count);
                        if (XR_SUCCEEDED(
                                xrEnumerateBoundSourcesForAction(g_session, &info, count, &count, sources.data())))
                        {
                            for (XrPath source : sources)
                            {
                                char path[XR_MAX_PATH_LENGTH]{};
                                uint32_t written = 0;
                                if (XR_SUCCEEDED(
                                        xrPathToString(g_instance, source, (uint32_t)sizeof(path), &written, path)))
                                    LOG("Menu/Start bound source: %s", path);
                            }
                        }
                    }
                    else
                        LOG("Menu/Start bound source: none");
                }
                break;
            }
            default:
                break;
            }
            ev = {XR_TYPE_EVENT_DATA_BUFFER};
        }

        // A structural frame-loop failure disables new frame calls but keeps
        // event polling alive until the runtime accepts the exit request and
        // reports STOPPING. Retry a rejected request without flooding the log.
        if (g_state == State::Ready && g_frameWaitFatalExitReason && !g_frameWaitExitRequestAccepted &&
            g_sessionState != XR_SESSION_STATE_STOPPING && g_sessionState != XR_SESSION_STATE_EXITING &&
            g_sessionState != XR_SESSION_STATE_LOSS_PENDING)
        {
            const uint64_t nowMs = GetTickCount64();
            if (!g_frameWaitExitLastRequestMs || nowMs - g_frameWaitExitLastRequestMs >= 250)
            {
                g_frameWaitExitLastRequestMs = nowMs;
                const XrResult exitResult = xrRequestExitSession(g_session);
                if (XR_SUCCEEDED(exitResult))
                {
                    g_frameWaitExitRequestAccepted = true;
                }
                else if (!g_frameWaitExitLastLogMs || nowMs - g_frameWaitExitLastLogMs >= 2000)
                {
                    g_frameWaitExitLastLogMs = nowMs;
                    LOG("pacing: fatal frame-loop drain is retrying "
                        "xrRequestExitSession: %s",
                        XrStr(exitResult));
                }
            }
        }
    }

    bool TryRecenter(XrTime time)
    {
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        if (XR_FAILED(xrLocateSpace(g_viewSpace, g_localSpace, time, &loc)))
            return false;
        constexpr XrSpaceLocationFlags need =
            XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
        if ((loc.locationFlags & need) != need)
            return false;
        // Keep only the yaw (left/right) part of the head orientation so the
        // screen is level, straight ahead of wherever the player is facing.
        const XrVector3f fwd = Rotate(loc.pose.orientation, {0, 0, -1});
        float yaw = 0.0f;
        if (fabsf(fwd.x) > 1e-4f || fabsf(fwd.z) > 1e-4f)
            yaw = atan2f(-fwd.x, -fwd.z);
        g_centerRot = {0, sinf(yaw * 0.5f), 0, cosf(yaw * 0.5f)};
        g_centerPos = loc.pose.position;
        g_haveCenter = true;
        g_positionRecenterEpoch.fetch_add(1, std::memory_order_acq_rel);
        LOG("screen recentered (yaw %.1f deg)", yaw * 57.2958f);
        return true;
    }

} // namespace
namespace vr_core
{

    XrCompositionLayerQuad MakeQuad(XrSwapchain chain, int32_t imgW, int32_t imgH, float widthMeters, float distMeters,
        float yOffset, XrCompositionLayerFlags flags, bool headLocked, float xOffset)
    {
        XrCompositionLayerQuad q{XR_TYPE_COMPOSITION_LAYER_QUAD};
        q.layerFlags = flags;
        q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        q.subImage.swapchain = chain;
        q.subImage.imageRect = {{0, 0}, {imgW, imgH}};
        q.subImage.imageArrayIndex = 0;
        if (headLocked)
        {
            // Pinned in front of the head (VIEW space); the game camera looks around.
            q.space = g_viewSpace;
            q.pose.orientation = {0, 0, 0, 1};
            q.pose.position = {xOffset, yOffset, -distMeters};
        }
        else
        {
            q.space = g_localSpace;
            q.pose.orientation = g_centerRot;
            const XrVector3f off = Rotate(g_centerRot, {xOffset, yOffset, -distMeters});
            q.pose.position = {g_centerPos.x + off.x, g_centerPos.y + off.y, g_centerPos.z + off.z};
        }
        q.size = {widthMeters, widthMeters * (float)imgH / (float)imgW};
        return q;
    }
} // namespace vr_core
namespace
{

    // The paused game screen used to be pinned to the head (VIEW space), so it
    // followed the gaze. With pause_menu_world_lock it is placed once, where the
    // player looked when the pause began, and stays put in LOCAL space like the
    // main menu at launch; the pointer ray test reads the same quad.
    pause_world_lock::Anchor g_pauseWorldAnchor;
} // namespace
namespace vr_core
{
    void ApplyPauseWorldLock(XrCompositionLayerQuad& quad, bool paused, float distMeters)
    {
        const XrVector3f fwd = Rotate(g_headPose.orientation, {0, 0, -1});
        const float f[3]{fwd.x, fwd.y, fwd.z};
        const float p[3]{g_headPose.position.x, g_headPose.position.y, g_headPose.position.z};
        pause_world_lock::Pose pose;
        const bool anchored =
            g_localSpace != XR_NULL_HANDLE &&
            g_pauseWorldAnchor.Update(paused, g_config.pause_menu_world_lock, g_headPoseValid, f, p, distMeters, pose,
                g_positionRecenterEpoch.load(std::memory_order_acquire), GetTickCount64());
        {
            // Every change of the pause quad's mode, so automated tests can assert
            // "anchored" for a whole pause.
            static pause_world_lock::Mode lastMode = pause_world_lock::Mode::NotPaused;
            const auto mode = g_localSpace == XR_NULL_HANDLE && paused ? pause_world_lock::Mode::NoHeadPose
                                                                       : g_pauseWorldAnchor.LastMode();
            if (mode != lastMode)
            {
                lastMode = mode;
                LOG("pause quad: %s", pause_world_lock::ModeName(mode));
            }
        }
        if (!anchored)
            return;
        if (g_pauseWorldAnchor.TakeFresh())
            LOG("pause quad: world=(%.3f,%.3f,%.3f) yaw=%.1f", pose.position.x, pose.position.y, pose.position.z,
                2.0f * std::atan2(pose.orientation.y, pose.orientation.w) * 57.2957795f);
        quad.space = g_localSpace;
        quad.pose.orientation = {pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w};
        quad.pose.position = {pose.position.x, pose.position.y, pose.position.z};
    }
} // namespace vr_core
namespace
{

    bool PrepareNativeSubtitle(XrCompositionLayerQuad& quad)
    {
        const auto image = NativeSubtitles_Read();
        const GameTitle title = TitleAdapter_GetActiveTitle();
        const uint64_t now = GetTickCount64();
        if (g_nativeSubtitleFailed || !image ||
            !subtitles::Current(
                image->title, image->generation, image->expiresAtMs, title, TitleAdapter_GetGeneration(title), now) ||
            image->theatre != VR_IsCutsceneTheaterActive() ||
            (image->theatre ? !g_config.cutscene_theater_subtitles : !g_config.vr_gameplay_subtitles))
            return false;
        const bool format = g_xrFormat == DXGI_FORMAT_R8G8B8A8_UNORM || g_xrFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                            g_xrFormat == DXGI_FORMAT_B8G8R8A8_UNORM || g_xrFormat == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        if (!format || !image->rgba || image->rgba->size() != image->width * image->height * 4)
            return false;
        if (g_nativeSubtitleChain == XR_NULL_HANDLE)
        {
            if (now < g_nativeSubtitleRetryMs)
                return false;
            g_nativeSubtitleRetryMs = now + 5000;
            if (!CreateChain(image->width, image->height, g_nativeSubtitleChain, g_nativeSubtitleImages,
                    g_nativeSubtitleRtvs, "native subtitles"))
                return false;
        }
        if (g_nativeSubtitleUploaded != image->revision || g_nativeSubtitleAcquired)
        {
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            // Caption readiness must never delay or reject the working eye pair.
            // A timeout retains the acquire and retries its wait next frame.
            wait.timeout = 0;
            if (!g_nativeSubtitleAcquired)
            {
                const XrResult r =
                    xrAcquireSwapchainImage(g_nativeSubtitleChain, &acquire, &g_nativeSubtitleAcquiredIndex);
                if (r != XR_SUCCESS)
                    return false;
                g_nativeSubtitleAcquired = true;
            }
            const XrResult ready = xrWaitSwapchainImage(g_nativeSubtitleChain, &wait);
            if (ready == XR_TIMEOUT_EXPIRED)
                return false;
            if (ready != XR_SUCCESS)
            {
                g_nativeSubtitleFailed = true;
                LOG("Native subtitles: optional swapchain wait failed (%d); stock captions and VR retained", ready);
                return false;
            }
            const bool valid = g_nativeSubtitleAcquiredIndex < g_nativeSubtitleImages.size();
            if (valid)
                g_context->UpdateSubresource(g_nativeSubtitleImages[g_nativeSubtitleAcquiredIndex], 0, nullptr,
                    image->rgba->data(), image->width * 4, 0);
            const XrResult released = xrReleaseSwapchainImage(g_nativeSubtitleChain, &release);
            g_nativeSubtitleAcquired = false;
            if (!valid || released != XR_SUCCESS)
            {
                g_nativeSubtitleFailed = true;
                LOG("Native subtitles: optional swapchain release failed (%d, image=%d); stock captions and VR retained",
                    released, valid ? 1 : 0);
                return false;
            }
            g_nativeSubtitleUploaded = image->revision;
        }
        const float scale = image->theatre ? g_config.vr_theatre_subtitle_scale : g_config.vr_gameplay_subtitle_scale;
        const float x = image->theatre ? g_config.vr_theatre_subtitle_x : g_config.vr_gameplay_subtitle_x;
        const float y = image->theatre ? g_config.vr_theatre_subtitle_y : g_config.vr_gameplay_subtitle_y;
        const bool head =
            image->theatre ? g_config.vr_theatre_subtitle_anchor == 1 : g_config.vr_gameplay_subtitle_anchor == 0;
        const float distance = image->theatre ? g_config.cutscene_theater_distance_m : 2.0f;
        quad = MakeQuad(g_nativeSubtitleChain, image->width, image->height, 1.5f * scale, distance, -0.55f + y,
            XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT, head, x);
        g_nativeTheatreCaptionReady = image->theatre;
        return true;
    }

    float UpdatePauseTransition()
    {
        enum class Phase
        {
            Idle,
            FadeOut,
            FadeIn
        };
        static Phase phase = Phase::Idle;
        static bool targetPaused = false;
        static uint64_t phaseStartMs = 0;
        const uint64_t now = GetTickCount64();
        const int requested = g_pauseRequest.exchange(-1);
        if (requested >= 0)
        {
            targetPaused = requested != 0;
            phase = Phase::FadeOut;
            phaseStartMs = now;
            g_requestedHaptics = 0.0f;
            g_peakHaptics = 0.0f;
            g_contactHaptics[0] = 0.0f;
            g_contactHaptics[1] = 0.0f;
            LOG("pause transition: fade out -> %s", targetPaused ? "head-locked 2D" : "stereo 3D");
        }
        if (phase == Phase::Idle)
            return 0.0f;

        constexpr float fadeMs = 200.0f;
        float t = static_cast<float>(now - phaseStartMs) / fadeMs;
        if (phase == Phase::FadeOut)
        {
            if (t < 1.0f)
                return t;
            g_pausePresentation = targetPaused;
            if (!targetPaused)
            {
                Game_Recenter();
            }
            phase = Phase::FadeIn;
            phaseStartMs = now;
            LOG("pause transition: presentation switched to %s", targetPaused ? "head-locked 2D" : "stereo 3D");
            return 1.0f;
        }
        if (t < 1.0f)
            return 1.0f - t;
        phase = Phase::Idle;
        LOG("pause transition: comfort fade complete");
        return 0.0f;
    }

    // ---- Vehicle comfort layers: procedural, premultiplied ----
    XrSwapchain g_vignetteChain = XR_NULL_HANDLE, g_dashChain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> g_vignetteImages, g_dashImages;
    std::vector<ID3D11RenderTargetView*> g_vignetteRtvs, g_dashRtvs;
    constexpr uint32_t kVignetteSize = 256, kDashWidth = 512, kDashHeight = 32;
} // namespace
namespace vr_core
{
    ID3D11PixelShader* g_vignettePs = nullptr;
    ID3D11PixelShader* g_dashPs = nullptr;
} // namespace vr_core
namespace
{

    // Turn vignette (head-locked) and the vehicle-fixed dash line.
    float TurnStickX(); // defined with g_padState below
    // When a VR smooth turn last changed the view yaw (turn code, any title).
    std::atomic<uint64_t> g_smoothTurnAppliedMs{0};
    float AppendVehicleComfort(XrCompositionLayerQuad& vignetteQuad, XrCompositionLayerQuad& dashQuad,
        std::vector<XrCompositionLayerBaseHeader*>& layers)
    {
        static vehicle_comfort::YawRate rate;
        static vehicle_comfort::Envelope envelope;
        static vehicle_comfort::DashPose dash;
        static bool wasFollowing = false;
        static float drawnStrength = -1.0f;
        static bool dashDrawn = false;
        const uint64_t now = GetTickCount64();
        // A title change (or a new map) starts the vignette from scratch.
        static GameTitle comfortTitle = GameTitle::None;
        static uint32_t comfortGeneration = 0;
        const GameTitle activeTitle = TitleAdapter_GetActiveTitle();
        const uint32_t activeGeneration = TitleAdapter_GetGeneration(activeTitle);
        if (activeTitle != comfortTitle || activeGeneration != comfortGeneration)
        {
            rate.Reset();
            envelope.Reset();
            dash = {};
            wasFollowing = false;
            drawnStrength = -1.0f;
            comfortTitle = activeTitle;
            comfortGeneration = activeGeneration;
        }
        float yawTotal = 0.0f;
        const bool following = g_config.vehicle_first_person && Game_VehicleFollowYaw(yawTotal) && !Menu_IsOpen() &&
                               !VR_IsPausePresentation();
        const float degPerSec = rate.Update(now, following, yawTotal);
        // On-foot smooth turns get their own strength (turn_vignette); the larger wins.
        // Only while a VR smooth turn is actually turning the view (not a stick
        // the seat, a wheel or a native turn owns).
        const uint64_t turnedAt = g_smoothTurnAppliedMs.load(std::memory_order_relaxed);
        const bool turnFree = !Menu_IsOpen() && !VR_IsPausePresentation() && !VR_IsCutsceneTheaterActive() &&
                              turnedAt && now >= turnedAt && now - turnedAt <= 100;
        const float turnRate =
            turnFree ? vehicle_comfort::SmoothTurnRate(g_config.turn_smooth, TurnStickX(), g_config.turn_smooth_deg_s)
                     : 0.0f;
        const float strength =
            envelope.Update(now, std::max(vehicle_comfort::VignetteTarget(degPerSec, g_config.vehicle_vignette),
                                     vehicle_comfort::VignetteTarget(turnRate, g_config.turn_vignette)));
        static vehicle_comfort::CabinFade cabin;
        float cabinAlpha = 0.0f;
        if (following && !wasFollowing)
        {
            float hq[4]{}, hp[3]{};
            dash = {};
            cabin.Reset();
            if (VR_GetHeadPose(hq, hp))
            {
                cabin.Capture(hp, g_positionRecenterEpoch.load(std::memory_order_acquire)); // seated eye on entry
                const float x = hq[0], y = hq[1], z = hq[2], w = hq[3];
                const float fx = -2.0f * (w * y + x * z);
                const float fz = -(1.0f - 2.0f * (x * x + y * y));
                dash = vehicle_comfort::CaptureDash(hp, atan2f(-fx, -fz));
            }
        }
        if (!following)
        {
            dash = {};
            cabin.Reset();
        }
        else if (cabin.Valid())
        {
            float hq[4]{}, hp[3]{};
            if (VR_GetHeadPose(hq, hp))
                cabinAlpha = cabin.Update(
                    now, hp, g_config.vehicle_cabin_fade_m, g_positionRecenterEpoch.load(std::memory_order_acquire));
        }
        wasFollowing = following;
        if (!EnsureComfortPipeline())
            return cabinAlpha;
        if (strength > 0.005f)
        {
            if (g_vignetteChain == XR_NULL_HANDLE && !CreateChain(kVignetteSize, kVignetteSize, g_vignetteChain,
                                                         g_vignetteImages, g_vignetteRtvs, "turn vignette"))
                return cabinAlpha;
            if (std::fabs(strength - drawnStrength) > 0.004f)
                if (DrawComfortImage(g_vignetteChain, g_vignetteImages, g_vignetteRtvs, kVignetteSize, kVignetteSize,
                        g_vignettePs, strength))
                    drawnStrength = strength;
            vignetteQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            vignetteQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            vignetteQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            vignetteQuad.space = g_viewSpace;
            vignetteQuad.subImage.swapchain = g_vignetteChain;
            vignetteQuad.subImage.imageRect = {{0, 0}, {(int32_t)kVignetteSize, (int32_t)kVignetteSize}};
            vignetteQuad.pose = {{0, 0, 0, 1}, {0, 0, -0.5f}};
            vignetteQuad.size = {2.4f, 2.4f}; // wider than any headset FOV at 0.5 m
            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&vignetteQuad));
        }
        else
            drawnStrength = -1.0f;
        if (g_config.vehicle_dash_cue && dash.valid)
        {
            if (g_dashChain == XR_NULL_HANDLE &&
                !CreateChain(kDashWidth, kDashHeight, g_dashChain, g_dashImages, g_dashRtvs, "dash line"))
                return cabinAlpha;
            if (!dashDrawn)
                dashDrawn =
                    DrawComfortImage(g_dashChain, g_dashImages, g_dashRtvs, kDashWidth, kDashHeight, g_dashPs, 1.0f);
            dashQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            dashQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            dashQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            dashQuad.space = g_localSpace; // the vehicle frame while the view follows the hull
            dashQuad.subImage.swapchain = g_dashChain;
            dashQuad.subImage.imageRect = {{0, 0}, {(int32_t)kDashWidth, (int32_t)kDashHeight}};
            dashQuad.pose.orientation = {0, std::sin(dash.yaw * 0.5f), 0, std::cos(dash.yaw * 0.5f)};
            dashQuad.pose.position = {dash.position[0], dash.position[1], dash.position[2]};
            dashQuad.size = {vehicle_comfort::kDashWidthM, vehicle_comfort::kDashHeightM};
            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&dashQuad));
        }
        return cabinAlpha;
    }

    bool AppendComfortFade(
        float alpha, XrCompositionLayerQuad& quad, std::vector<XrCompositionLayerBaseHeader*>& layers)
    {
        if (alpha <= 0.001f)
            return true;
        if (g_fadeChain == XR_NULL_HANDLE && !CreateChain(4, 4, g_fadeChain, g_fadeImages, g_fadeRtvs, "comfort fade"))
            return false;

        uint32_t idx = 0;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = 1000000000;
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if (XR_FAILED(xrAcquireSwapchainImage(g_fadeChain, &acquire, &idx)) ||
            XR_FAILED(xrWaitSwapchainImage(g_fadeChain, &wait)))
            return false;
        const float black[4] = {0.0f, 0.0f, 0.0f, std::clamp(alpha, 0.0f, 1.0f)};
        g_context->ClearRenderTargetView(GetRtv(g_fadeImages, g_fadeRtvs, idx), black);
        xrReleaseSwapchainImage(g_fadeChain, &release);
        quad = MakeQuad(g_fadeChain, 4, 4, 20.0f, 0.25f, 0.0f,
            XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT, true);
        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&quad));
        return true;
    }

    // ---------------------------------------------------------------- init

    // Part 1 (background thread): create the OpenXR instance and find the
    // headset. No D3D device needed here, so it can run while the game loads.
    bool InitInstance()
    {
        uint32_t extensionCount = 0;
        std::vector<XrExtensionProperties> availableExtensions;
        if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr)) &&
            extensionCount > 0)
        {
            availableExtensions.resize(extensionCount);
            for (auto& extension : availableExtensions)
                extension.type = XR_TYPE_EXTENSION_PROPERTIES;
            xrEnumerateInstanceExtensionProperties(
                nullptr, extensionCount, &extensionCount, availableExtensions.data());
        }
        auto hasExtension = [&](const char* name)
        {
            return std::any_of(availableExtensions.begin(), availableExtensions.end(),
                [&](const XrExtensionProperties& extension) { return strcmp(extension.extensionName, name) == 0; });
        };

        std::vector<const char*> enabledExtensions{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
        g_touchProProfileEnabled = hasExtension(XR_FB_TOUCH_CONTROLLER_PRO_EXTENSION_NAME);
        if (g_touchProProfileEnabled)
            enabledExtensions.push_back(XR_FB_TOUCH_CONTROLLER_PRO_EXTENSION_NAME);
        // Read-only: lets us log the headset's REAL panel rate. xrWaitFrame's
        // predictedDisplayPeriod reports the rate the runtime is TARGETING the
        // app at, which is a fraction of the panel rate whenever SteamVR engages
        // reprojection (120 panel -> 60 or 40 targeted). Logging both side by
        // side is the only way to tell "the headset is slow" apart from "the
        // runtime halved us", which look identical from the period alone.
        g_refreshRateExtEnabled = hasExtension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
        if (g_refreshRateExtEnabled)
            enabledExtensions.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);

        XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(ici.applicationInfo.applicationName, "HaloMCCVR");
        ici.applicationInfo.applicationVersion = 1;
        strcpy_s(ici.applicationInfo.engineName, "MjolnirVR");
        ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        ici.enabledExtensionCount = (uint32_t)enabledExtensions.size();
        ici.enabledExtensionNames = enabledExtensions.data();
        LOG("creating OpenXR instance (this can take a while as SteamVR starts)...");
        XrResult r = xrCreateInstance(&ici, &g_instance);
        if (XR_FAILED(r))
        {
            Fail("No OpenXR runtime available. Is SteamVR installed and set as the\n"
                 "default OpenXR runtime? (SteamVR -> Settings -> OpenXR)",
                r);
            return false;
        }

        XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
        xrGetInstanceProperties(g_instance, &ip);
        snprintf(g_status.runtime, sizeof(g_status.runtime), "%s %u.%u.%u", ip.runtimeName,
            XR_VERSION_MAJOR(ip.runtimeVersion), XR_VERSION_MINOR(ip.runtimeVersion),
            XR_VERSION_PATCH(ip.runtimeVersion));
        LOG("OpenXR runtime: %s", g_status.runtime);
        LOG("Quest Touch Pro interaction profile: %s",
            g_touchProProfileEnabled ? "enabled" : "not advertised by runtime");

        XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
        sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        r = xrGetSystem(g_instance, &sgi, &g_systemId);
        // The launcher injects the mod at process start, before the game window
        // exists, so the runtime and headset are often still coming online the
        // first time we ask. XR_ERROR_FORM_FACTOR_UNAVAILABLE means "no HMD yet",
        // which is transient, so poll for up to ~60s instead of giving up on the
        // first race. This runs on the background init thread and never blocks
        // the game; a headset connected shortly after launch still brings up VR.
        for (int waited = 0; r == XR_ERROR_FORM_FACTOR_UNAVAILABLE && waited < 60; ++waited)
        {
            if (waited % 10 == 0)
                LOG("waiting for a headset on OpenXR runtime '%s' "
                    "(connect it now if it is off)...",
                    g_status.runtime);
            Sleep(1000);
            r = xrGetSystem(g_instance, &sgi, &g_systemId);
        }
        if (XR_FAILED(r))
        {
            char msg[320];
            snprintf(msg, sizeof(msg),
                "No headset found on the active OpenXR runtime '%s' after 60s.\n\n"
                "The mod uses whichever runtime owns OpenXR. If your headset runs\n"
                "through SteamVR (PSVR2, Index, Vive, etc.), set SteamVR -> Settings\n"
                "-> OpenXR -> Set SteamVR as OpenXR Runtime. If you stream with\n"
                "Virtual Desktop, connect it before launching.",
                g_status.runtime);
            Fail(msg, r);
            return false;
        }
        // Record WHICH headset this is, not just which runtime. SteamVR fronts
        // PSVR2, Index, Vive, Virtual Desktop, Steam Link and ALVR alike, so
        // "OpenXR runtime: SteamVR/OpenXR" identifies almost nothing. On
        // 2026-07-28 that cost hours: preserved logs were attributed to a
        // headset from a code comment rather than evidence, and two theories
        // were built on the wrong attribution before the mistake was caught. A log
        // that cannot tell its own sessions apart cannot settle an argument.
        XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
        if (XR_SUCCEEDED(xrGetSystemProperties(g_instance, g_systemId, &systemProperties)))
        {
            LOG("headset: '%s' (vendor 0x%04X) on runtime %s", systemProperties.systemName, systemProperties.vendorId,
                g_status.runtime);
        }
        else
        {
            // Not fatal and not silent: every session must still say something
            // about which headset produced it.
            LOG("headset: model NOT reported by runtime %s; identify this "
                "session by its per-eye FOV instead",
                g_status.runtime);
        }
        LogStreamingHosts();
        LOG("OpenXR instance ready; headset found on runtime %s", g_status.runtime);
        return true;
    }

} // namespace
namespace vr_core
{
    VrPadState g_padState{};
} // namespace vr_core
namespace
{
    float TurnStickX()
    {
        return g_padState.valid ? g_padState.turnX : 0.0f;
    }
} // namespace
namespace vr_core
{
    ControllerFingerSample g_controllerFingerSample{};
    float g_nativePointerTrigger = 0;
    // Save & Quit dialog keys: when the game ended a pause without a resume press, and the last resume press.
    std::atomic<uint64_t> g_pauseEndedByGameMs{0}, g_lastResumeInputMs{0};
    std::atomic<uint64_t> g_lastPauseEndMs{0}; // any pause -> unpause (pad-feed diagnostic window)

    // Two-handed aim state. `latched` = two-hand engaged this frame; decided
    // ONCE per frame in UpdateTwoHandLatch (edge detection can't live in the
    // multi-call aim getter). `active` mirrors it for the menu indicator.
    std::atomic<bool> g_twoHandLatched{false};
    std::atomic<bool> g_twoHandActive{false};
    weapon_interaction::State g_weaponInteraction;
} // namespace vr_core
namespace
{
    weapon_model::Observations g_weaponModels;
} // namespace
namespace vr_core
{
    weapon_interaction::ReloadTargets g_reloadTargets;
} // namespace vr_core
namespace
{
    WeaponAccessoryRenderer g_weaponAccessoryRenderer;
} // namespace
namespace vr_core
{
    weapon_accessory::Presentation g_weaponAccessory; // compositor thread only
    std::atomic<HRESULT> g_weaponAccessoryResult{S_FALSE};
    std::array<RecentSecondaryWeaponPresentation, kTitleRuntimeSlotCount> g_secondaryWeaponPresentation;
} // namespace vr_core
namespace
{

    two_hand_lab::Settings SnapshotTwoHandLabSettings() noexcept
    {
        return SnapshotTwoHandLabSettingsWithGeneration().settings;
    }

} // namespace
namespace vr_core
{
    // pistol_two_hand_visual_only: the held weapon is a OneHanded class in its
    // weapon profile (unknown weapons keep today's two-hand aim).
    bool PistolTwoHandVisualOnly() noexcept
    {
        if (!g_config.pistol_two_hand_visual_only)
            return false;
        const GameTitle title = TitleAdapter_GetActiveTitle();
        const uint64_t identity = VR_GetWeaponModelIdentity(title, TitleAdapter_GetGeneration(title),
            g_contactSpaceEpoch.load(std::memory_order_acquire), GetTickCount64());
        return identity && weapon_profile::WeaponClassFor(title, identity) == weapon_profile::SizeClass::OneHanded;
    }

    AimPoseInputs CurrentAimPoseInputs(
        bool rightValid, const XrPosef& right, bool leftValid, const XrPosef& left) noexcept
    {
        AimPoseInputs inputs{};
        inputs.rightValid = rightValid;
        inputs.right = right;
        inputs.leftValid = leftValid;
        inputs.left = left;
        inputs.supportPosition = left.position;
        inputs.twoHandEnabled = g_config.two_handed_aim && !SecondaryWeaponPresentationActive();
        inputs.twoHandLatched = g_twoHandLatched.load();
        inputs.twoHandToggle = g_config.two_hand_toggle;
        // Visual hand seating must not change the two-controller aiming line.
        inputs.leftHandForwardM = 0.0f;
        inputs.leftGripForwardM = 0.0f;
        inputs.gunYawDeg = g_config.gun_yaw_deg;
        inputs.gunPitchDeg = g_config.gun_pitch_deg;
        inputs.gunRollDeg = g_config.gun_roll_deg;
        inputs.twoHandExactLine = g_config.two_hand_exact_line;
        inputs.twoHandAimSuppressed = PistolTwoHandVisualOnly();
        return inputs;
    }

    // Per-thread, per-invocation override set by a title hook that proved the
    // currently executing weapon invocation is not the durable relationship
    // owner. Meaningful only while `persistent_support_grip` is on: with the
    // feature off it is never consulted (PG-off parity). It never mutates the
    // durable relationship.
    thread_local bool g_supportInvocationUntrusted = false;
} // namespace vr_core
namespace
{

    // Defined with the durable relationship publication further below; the
    // stock-aware aim assembly above it (virtual_stock_aim.inl) needs the
    // coherent reader before that point.

    // The OpenXR frame thread owns these globals at snapshot publication. The
    // public getter below instead passes one copy taken under g_headCs.

} // namespace
namespace vr_core
{
    TwoHandLabTemporalPreparedOutput g_twoHandLabTemporalOutput;
    TwoHandLabTemporalPublication g_twoHandLabTemporalPublication;
} // namespace vr_core
namespace
{

    // Frame-thread presentation publishers (contact/Halo 2/Halo 4/Reach/CE
    // tracking snapshots) run on the thread that advanced the layer, so they
    // read that serial's own output directly. Called AFTER the existing VS
    // presentation at each site; at most one correction is ever active, and
    // both inactive is identity. Only the emitted orientation changes;
    // positions and every control path stay raw.
    XrQuaternionf PresentTwoHandLabTemporal(
        XrQuaternionf orientation, uint64_t preparedSerial, uint64_t labGeneration) noexcept
    {
        const TwoHandLabTemporalPreparedOutput& output = g_twoHandLabTemporalOutput;
        if (!output.temporalActive || output.serial != preparedSerial || output.labGeneration != labGeneration ||
            !output.presentedValid || !output.baseValid)
            return orientation;
        // Own-solve match: the correction is local to the product-presented
        // orientation. A consumer whose own orientation is anything else
        // fails open instead of composing a mismatched correction.
        virtual_stock::Quat4 consumer{};
        if (!virtual_stock::TryNormalizeQuaternion(
                virtual_stock::Quat4{orientation.x, orientation.y, orientation.z, orientation.w}, consumer) ||
            consumer.x != output.base.x || consumer.y != output.base.y || consumer.z != output.base.z ||
            consumer.w != output.base.w)
            return orientation;
        return ComposeAimContinuityCorrection(orientation, output.correction);
    }

} // namespace
namespace vr_core
{
    // Orientation-only variant for the per-title snapshots that publish a
    // whole pose: the position is always the untouched solver output.
    XrPosef PresentedTwoHandLabTemporalPose(XrPosef pose, uint64_t preparedSerial, uint64_t labGeneration) noexcept
    {
        pose.orientation = PresentTwoHandLabTemporal(pose.orientation, preparedSerial, labGeneration);
        return pose;
    }
} // namespace vr_core
namespace
{

    // Title-native invocation receipt. Single writer: the title's FP
    // interpolate hook on the game render thread. A plain odd/even seqlock is
    // sufficient for the same reason as the relationship publication, and no
    // lock, allocation or logging happens on this path. The receipt is only
    // ever published for a wired title while the feature is on; with the
    // feature off nothing is written and no consumer consults it (PG-off
    // parity).
    struct SupportInvocationReceiptState
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint8_t> title{static_cast<uint8_t>(GameTitle::None)};
        std::atomic<uint32_t> generation{0};
        std::atomic<uint64_t> serial{0};
        std::atomic<uint8_t> evidence{0};
        std::atomic<uint8_t> producerAgreement{0};
        std::atomic<uint32_t> ownerTitle{static_cast<uint32_t>(GameTitle::None)};
        std::atomic<uint32_t> ownerGeneration{0};
        std::atomic<uint32_t> ownerUnit{0xFFFFFFFFu};
        std::atomic<uint32_t> ownerWeapon{0xFFFFFFFFu};
        std::atomic<uint8_t> relationshipReadable{0};
        std::atomic<uint8_t> relationshipEngaged{0};
        std::atomic<uint64_t> relationshipEpoch{0};
    };
    SupportInvocationReceiptState g_supportInvocationReceiptState;

    void PublishSupportInvocationReceiptInternal(const support_grip::SupportInvocationReceipt& receipt) noexcept
    {
        auto& published = g_supportInvocationReceiptState;
        published.sequence.fetch_add(1, std::memory_order_acq_rel);
        published.title.store(static_cast<uint8_t>(receipt.title), std::memory_order_relaxed);
        published.generation.store(receipt.generation, std::memory_order_relaxed);
        published.serial.store(receipt.serial, std::memory_order_relaxed);
        published.evidence.store(static_cast<uint8_t>(receipt.evidence), std::memory_order_relaxed);
        published.producerAgreement.store(receipt.producerAgreement ? 1u : 0u, std::memory_order_relaxed);
        published.ownerTitle.store(static_cast<uint32_t>(receipt.owner.title), std::memory_order_relaxed);
        published.ownerGeneration.store(receipt.owner.generation, std::memory_order_relaxed);
        published.ownerUnit.store(receipt.owner.unit, std::memory_order_relaxed);
        published.ownerWeapon.store(receipt.owner.weapon, std::memory_order_relaxed);
        published.relationshipReadable.store(receipt.relationshipReadable ? 1u : 0u, std::memory_order_relaxed);
        published.relationshipEngaged.store(receipt.relationshipEngaged ? 1u : 0u, std::memory_order_relaxed);
        published.relationshipEpoch.store(receipt.relationshipEpoch, std::memory_order_relaxed);
        published.sequence.fetch_add(1, std::memory_order_release);
    }

} // namespace
namespace vr_core
{
    bool ReadSupportInvocationReceiptInternal(GameTitle title, support_grip::SupportInvocationReceipt& receipt) noexcept
    {
        receipt = support_grip::SupportInvocationReceipt{};
        const SupportInvocationReceiptState& published = g_supportInvocationReceiptState;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = published.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            support_grip::SupportInvocationReceipt candidate{};
            candidate.title = static_cast<GameTitle>(published.title.load(std::memory_order_relaxed));
            candidate.generation = published.generation.load(std::memory_order_relaxed);
            candidate.serial = published.serial.load(std::memory_order_relaxed);
            candidate.evidence =
                static_cast<support_grip::OwnerEvidence>(published.evidence.load(std::memory_order_relaxed));
            candidate.producerAgreement = published.producerAgreement.load(std::memory_order_relaxed) != 0;
            candidate.owner.title = static_cast<GameTitle>(published.ownerTitle.load(std::memory_order_relaxed));
            candidate.owner.generation = published.ownerGeneration.load(std::memory_order_relaxed);
            candidate.owner.unit = published.ownerUnit.load(std::memory_order_relaxed);
            candidate.owner.weapon = published.ownerWeapon.load(std::memory_order_relaxed);
            candidate.relationshipReadable = published.relationshipReadable.load(std::memory_order_relaxed) != 0;
            candidate.relationshipEngaged = published.relationshipEngaged.load(std::memory_order_relaxed) != 0;
            candidate.relationshipEpoch = published.relationshipEpoch.load(std::memory_order_relaxed);
            if (published.sequence.load(std::memory_order_acquire) != before)
                continue; // torn: retry once, then report unavailable
            if (candidate.title != title || candidate.generation == 0)
                return false; // another lifecycle's record
            candidate.resolved = true;
            receipt = candidate;
            return true;
        }
        return false;
    }
} // namespace vr_core
namespace
{

    // Optional native-menu input has no effect on any eye/camera transaction.
    // Publish only after a real stock-screen quad was successfully submitted.
    struct NativePointerFrame
    {
        bool active = false, pressed = false;
        float u = 0, v = 0;
    } g_nativePointerFrame;

    [[maybe_unused]] void UpdateNativeMenuPointerLegacy(const XrCompositionLayerQuad& quad)
    {
        // Native pointing failed in headset testing. This optional behavior stays
        // disabled until it is corrected; all existing camera/F1 code is kept.
        return;
        static bool pressed = false;
        const bool headLocked = quad.space == g_viewSpace;
        if (!g_config.game_menu_pointer || Menu_IsOpen() || g_sessionState != XR_SESSION_STATE_FOCUSED ||
            !g_padState.valid || GetTickCount64() - g_physicalControllerAtMs > 200 || !g_rightAimPoseValid ||
            (headLocked && !g_headPoseValid) ||
            !game_menu_pointer::MenuMode(
                TitleAdapter_GetRuntimeMode(), VR_IsPausePresentation() && VR_IsPausePresentationTarget()) ||
            VR_IsCutsceneTheaterActive())
        {
            pressed = false;
            return;
        }
        if (g_nativePointerTrigger >= 0.65f)
            pressed = true;
        else if (g_nativePointerTrigger <= 0.35f)
            pressed = false;

        XrVector3f origin = g_rightAimPose.position;
        XrVector3f direction = Rotate(g_rightAimPose.orientation, {0, 0, -1});
        if (headLocked)
        {
            const XrQuaternionf inverseHead{-g_headPose.orientation.x, -g_headPose.orientation.y,
                -g_headPose.orientation.z, g_headPose.orientation.w};
            origin = Rotate(inverseHead,
                {origin.x - g_headPose.position.x, origin.y - g_headPose.position.y, origin.z - g_headPose.position.z});
            direction = Rotate(inverseHead, direction);
        }
        const XrQuaternionf inverseQuad{
            -quad.pose.orientation.x, -quad.pose.orientation.y, -quad.pose.orientation.z, quad.pose.orientation.w};
        origin = Rotate(inverseQuad,
            {origin.x - quad.pose.position.x, origin.y - quad.pose.position.y, origin.z - quad.pose.position.z});
        direction = Rotate(inverseQuad, direction);
        if (direction.z >= -1e-5f || origin.z <= 0)
            return;
        // Translate the quad-local z=0 plane to the existing helper's z=-1.
        const float o[]{origin.x, origin.y, origin.z - 1.0f};
        const float d[]{direction.x, direction.y, direction.z};
        const auto hit = IntersectMenuQuad(o, d, 1.0f, quad.size.width, quad.size.height, 0);
        g_nativePointerFrame = {hit.hit, pressed, hit.u, hit.v};
    }

    void UpdateNativeMenuPointer(const XrCompositionLayerQuad& quad)
    {
        static bool pressed = false;
        const bool headLocked = quad.space == g_viewSpace;
        if (!g_config.game_menu_pointer || Menu_IsOpen() || g_padState.thumbrestDpad)
        {
            NativeMenuPointer_FrameStatus("disabled-F1-or-Dpad");
            pressed = false;
            return;
        }
        if (!game_menu_pointer::MenuMode(
                TitleAdapter_GetRuntimeMode(), VR_IsPausePresentation() && VR_IsPausePresentationTarget()) ||
            VR_IsCutsceneTheaterActive())
        {
            NativeMenuPointer_FrameStatus("non-menu-mode");
            pressed = false;
            return;
        }
        NativeMenuPointer_DialogKeys(g_padState.a, g_padState.b,
            std::fabs(g_padState.moveX) > std::fabs(g_padState.dpadX) ? g_padState.moveX : g_padState.dpadX);
        if (g_sessionState != XR_SESSION_STATE_FOCUSED || !g_padState.valid ||
            GetTickCount64() - g_physicalControllerAtMs > 200 || !g_rightAimPoseValid ||
            (headLocked && !g_headPoseValid))
        {
            NativeMenuPointer_FrameStatus("focus-or-tracking");
            pressed = false;
            return;
        }
        if (g_nativePointerTrigger >= 0.65f)
            pressed = true;
        else if (g_nativePointerTrigger <= 0.35f)
            pressed = false;
        float u = 0, v = 0;
        const bool hit = game_menu_pointer::RayHit(quad, g_rightAimPose, g_headPose, headLocked, u, v);
        g_nativePointerFrame = {hit, pressed, u, v};
        NativeMenuPointer_FrameStatus(hit ? "hit" : "ray-miss");
    }

    void UpdateMenuPointer(bool headLocked)
    {
        static bool triggerPressed = false;
        static bool hadHit = false;
        static float smoothU = 0.5f, smoothV = 0.5f;
        static uint64_t lastDiagMs = 0;
        static bool dragging = false;
        static float grabOffsetX = 0.0f, grabOffsetY = 0.0f;
        if (!g_rightAimPoseValid || (headLocked && !g_headPoseValid) || (!headLocked && !g_haveCenter))
        {
            triggerPressed = false;
            hadHit = false;
            if (dragging)
            {
                // Tracking dropped mid-drag. Keep wherever the panel got to
                // rather than leaving the handle stuck to a stale ray.
                dragging = false;
                Menu_SetPanelDragging(false);
                ConfigSave();
            }
            Menu_ClearVrPointer();
            return;
        }

        const XrPosef anchor = headLocked ? g_headPose : XrPosef{g_centerRot, g_centerPos};
        const XrQuaternionf inverseAnchor{
            -anchor.orientation.x, -anchor.orientation.y, -anchor.orientation.z, anchor.orientation.w};
        const XrVector3f relative{g_rightAimPose.position.x - anchor.position.x,
            g_rightAimPose.position.y - anchor.position.y, g_rightAimPose.position.z - anchor.position.z};
        const XrVector3f localOrigin = Rotate(inverseAnchor, relative);
        const XrVector3f worldDirection = Rotate(g_rightAimPose.orientation, {0.0f, 0.0f, -1.0f});
        const XrVector3f localDirection = Rotate(inverseAnchor, worldDirection);
        const float origin[3] = {localOrigin.x, localOrigin.y, localOrigin.z};
        const float direction[3] = {localDirection.x, localDirection.y, localDirection.z};
        const MenuPointerHit hit = IntersectMenuQuad(origin, direction, g_config.menu_distance_m, g_config.menu_width_m,
            g_config.menu_width_m * MENU_H / MENU_W, g_config.menu_height_m, g_config.menu_side_m);

        if (hit.hit)
        {
            if (!hadHit)
            {
                smoothU = hit.u;
                smoothV = hit.v;
            }
            else
            {
                smoothU += (hit.u - smoothU) * 0.35f;
                smoothV += (hit.v - smoothV) * 0.35f;
            }
        }
        hadHit = hit.hit;

        const bool wasTriggerPressed = triggerPressed;
        if (!triggerPressed && g_padState.trigR >= 0.65f)
            triggerPressed = true;
        else if (triggerPressed && g_padState.trigR <= 0.35f)
            triggerPressed = false;
        const float stickY = std::fabs(g_padState.turnY) > 0.25f ? g_padState.turnY : 0.0f;

        // Panel drag. The trigger only starts a drag when it goes down on the
        // grab handle; anywhere else on the panel it is an ordinary click, so
        // nothing moves by accident. While dragging, the panel keeps the offset
        // it had when grabbed, and the stick pushes it further away or pulls it
        // in -- the ray recomputes at the new distance, so it stays under the
        // pointer.
        auto planeHit = [&](float distance, float& outX, float& outY) -> bool
        {
            if (std::fabs(direction[2]) < 1e-5f)
                return false;
            const float t = (-distance - origin[2]) / direction[2];
            if (t <= 0.0f)
                return false;
            outX = origin[0] + direction[0] * t;
            outY = origin[1] + direction[1] * t;
            return true;
        };
        if (!dragging && !wasTriggerPressed && triggerPressed && hit.hit && Menu_PointerOverGrabHandle())
        {
            float hx = 0.0f, hy = 0.0f;
            if (planeHit(g_config.menu_distance_m, hx, hy))
            {
                grabOffsetX = g_config.menu_side_m - hx;
                grabOffsetY = g_config.menu_height_m - hy;
                dragging = true;
                Menu_SetPanelDragging(true);
                LOG("menu panel: grabbed at distance %.2f m", g_config.menu_distance_m);
            }
        }
        if (dragging)
        {
            if (stickY != 0.0f)
            {
                // ~0.6 m/s at full deflection, frame-rate independent.
                static uint64_t lastPushMs = 0;
                const uint64_t nowPush = GetTickCount64();
                const float dt = lastPushMs ? (float)(nowPush - lastPushMs) * 0.001f : 0.0f;
                lastPushMs = nowPush;
                if (dt > 0.0f && dt < 0.25f)
                    g_config.menu_distance_m =
                        std::clamp(g_config.menu_distance_m - stickY * 0.6f * dt, kMenuDistanceMin, kMenuDistanceMax);
            }
            float hx = 0.0f, hy = 0.0f;
            if (planeHit(g_config.menu_distance_m, hx, hy))
            {
                g_config.menu_side_m = std::clamp(hx + grabOffsetX, -kMenuOffsetLimit, kMenuOffsetLimit);
                g_config.menu_height_m = std::clamp(hy + grabOffsetY, -kMenuOffsetLimit, kMenuOffsetLimit);
            }
            if (!triggerPressed)
            {
                dragging = false;
                Menu_SetPanelDragging(false);
                ConfigSave();
                LOG("menu panel: released at distance %.2f m, side %.2f m, height %.2f m", g_config.menu_distance_m,
                    g_config.menu_side_m, g_config.menu_height_m);
            }
            // Swallow the click and the scroll so dragging never also drives a
            // widget underneath the handle.
            Menu_SetVrPointer(hit.hit, smoothU, smoothV, false, 0.0f);
            return;
        }

        const float scroll = stickY * 0.12f;
        Menu_SetVrPointer(hit.hit, smoothU, smoothV, triggerPressed, scroll);

        const uint64_t now = GetTickCount64();
        if (now - lastDiagMs >= 2000)
        {
            lastDiagMs = now;
            LOG("menu pointer: hit=%d uv=(%.3f,%.3f) origin=(%.2f,%.2f,%.2f) "
                "dir=(%.2f,%.2f,%.2f) trigger=%.2f stickY=%.2f headLocked=%d",
                hit.hit ? 1 : 0, hit.u, hit.v, origin[0], origin[1], origin[2], direction[0], direction[1],
                direction[2], g_padState.trigR, g_padState.turnY, headLocked ? 1 : 0);
        }
    }

    // Part 2 (render thread, first frame): now that we have the game's D3D
    // device, create the session and everything that hangs off it. This is
    // fast (~100 ms), so running it on the render thread is fine.
    bool InitSession(IDXGISwapChain* sc)
    {
        // The game's device is the one we hand to OpenXR: the runtime then
        // reads our textures without any cross-device copying.
        if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&g_device)))
        {
            Fail("Could not get the game's D3D11 device");
            return false;
        }
        g_device->GetImmediateContext(&g_context);
        IDXGIDevice* memoryDevice = nullptr;
        IDXGIAdapter* memoryAdapter = nullptr;
        if (SUCCEEDED(g_device->QueryInterface(IID_PPV_ARGS(&memoryDevice))))
        {
            if (SUCCEEDED(memoryDevice->GetAdapter(&memoryAdapter)))
            {
                if (g_memoryAdapter)
                {
                    g_memoryAdapter->Release();
                    g_memoryAdapter = nullptr;
                }
                memoryAdapter->QueryInterface(IID_PPV_ARGS(&g_memoryAdapter));
                memoryAdapter->Release();
            }
            memoryDevice->Release();
        }

        DXGI_SWAP_CHAIN_DESC scd{};
        sc->GetDesc(&scd);
        LOG("game swapchain: %ux%u fmt %d windowed=%d swapeffect=%d bufcount=%u hwnd %p", scd.BufferDesc.Width,
            scd.BufferDesc.Height, (int)scd.BufferDesc.Format, (int)scd.Windowed, (int)scd.SwapEffect, scd.BufferCount,
            (void*)scd.OutputWindow);

        XrResult r;
        // Required call before creating a D3D11 session; also tells us which
        // GPU the runtime wants (must match the game's).
        PFN_xrGetD3D11GraphicsRequirementsKHR pfnReq = nullptr;
        xrGetInstanceProcAddr(
            g_instance, "xrGetD3D11GraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&pfnReq));
        XrGraphicsRequirementsD3D11KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        if (!pfnReq || XR_FAILED(pfnReq(g_instance, g_systemId, &req)))
        {
            Fail("The OpenXR runtime does not support D3D11");
            return false;
        }
        IDXGIDevice* dxgiDev = nullptr;
        if (SUCCEEDED(g_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev)))
        {
            IDXGIAdapter* adapter = nullptr;
            if (SUCCEEDED(dxgiDev->GetAdapter(&adapter)))
            {
                DXGI_ADAPTER_DESC ad{};
                adapter->GetDesc(&ad);
                if (memcmp(&ad.AdapterLuid, &req.adapterLuid, sizeof(LUID)) != 0)
                    LOG("WARNING: game GPU differs from the headset's GPU; session may fail");
                adapter->Release();
            }
            dxgiDev->Release();
        }

        XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
        binding.device = g_device;
        XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
        sci.next = &binding;
        sci.systemId = g_systemId;
        r = xrCreateSession(g_instance, &sci, &g_session);
        if (XR_FAILED(r))
        {
            Fail("Could not create the VR session", r);
            return false;
        }

        XrReferenceSpaceCreateInfo rsci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        rsci.poseInReferenceSpace.orientation.w = 1.0f;
        if (XR_FAILED(xrCreateReferenceSpace(g_session, &rsci, &g_localSpace)))
        {
            Fail("Could not create the LOCAL reference space");
            return false;
        }
        g_contactSpaceChangeAtNs.store(0, std::memory_order_release);
        g_contactSpaceEpoch.fetch_add(1, std::memory_order_acq_rel);
        rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        if (XR_FAILED(xrCreateReferenceSpace(g_session, &rsci, &g_viewSpace)))
        {
            Fail("Could not create the VIEW reference space");
            return false;
        }

        // Controller actions are optional: failure must never take
        // down the already-working headset/stereo path.
        if (!CreateControllerActions())
            LOG("controller tracking unavailable; head/stereo remain enabled");

        uint32_t modeCount = 0;
        xrEnumerateEnvironmentBlendModes(
            g_instance, g_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 1, &modeCount, &g_blendMode);

        // The headset's two eyes — recommended per-eye render size. We'll
        // render the game once per eye into swapchains of this size.
        uint32_t viewCount = 0;
        xrEnumerateViewConfigurationViews(
            g_instance, g_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
        g_viewConfigs.assign(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        xrEnumerateViewConfigurationViews(g_instance, g_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount,
            &viewCount, g_viewConfigs.data());
        g_views.assign(viewCount, {XR_TYPE_VIEW});
        for (uint32_t i = 0; i < viewCount; i++)
            LOG("eye %u recommended render size %ux%u", i, g_viewConfigs[i].recommendedImageRectWidth,
                g_viewConfigs[i].recommendedImageRectHeight);

        // Pick the image format for our XR swapchains, preferring sRGB
        // variants so colors in the headset match the monitor.
        uint32_t fmtCount = 0;
        xrEnumerateSwapchainFormats(g_session, 0, &fmtCount, nullptr);
        std::vector<int64_t> formats(fmtCount);
        xrEnumerateSwapchainFormats(g_session, fmtCount, &fmtCount, formats.data());
        const int64_t preferred[] = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
            DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM};
        g_xrFormat = 0;
        for (int64_t want : preferred)
        {
            for (int64_t have : formats)
                if (have == want)
                {
                    g_xrFormat = want;
                    break;
                }
            if (g_xrFormat)
                break;
        }
        if (!g_xrFormat && fmtCount > 0)
            g_xrFormat = formats[0];
        if (!g_xrFormat)
        {
            Fail("The runtime offered no usable swapchain formats");
            return false;
        }
        LOG("XR swapchain format: %d", (int)g_xrFormat);

        // Menu: fixed-size texture on its own quad. Its render target uses the
        // non-sRGB sibling format so a raw GPU copy lands with correct gamma.
        if (!CreateChain(MENU_W, MENU_H, g_menuChain, g_menuImages, g_menuRtvs, "menu"))
        {
            Fail("Could not create the menu swapchain");
            return false;
        }
        if (!Menu_Init(scd.OutputWindow, g_device, g_context, UnormSibling((DXGI_FORMAT)g_xrFormat)))
            LOG("WARNING: menu failed to initialize; F1 menu unavailable");

        // Optional cursor uploads once. Reuse its released image each frame;
        // any initialization failure leaves the native input and VR core alive.
        g_nativeCursorReady = false;
        const bool cursorFormat =
            g_xrFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || g_xrFormat == DXGI_FORMAT_R8G8B8A8_UNORM ||
            g_xrFormat == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || g_xrFormat == DXGI_FORMAT_B8G8R8A8_UNORM;
        if (cursorFormat &&
            CreateChain(32, 32, g_nativeCursorChain, g_nativeCursorImages, g_nativeCursorRtvs, "game menu cursor"))
        {
            uint32_t index = 0;
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait.timeout = 1000000000;
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            if (xrAcquireSwapchainImage(g_nativeCursorChain, &acquire, &index) == XR_SUCCESS &&
                xrWaitSwapchainImage(g_nativeCursorChain, &wait) == XR_SUCCESS)
            {
                if (index < g_nativeCursorImages.size() && g_nativeCursorImages[index])
                {
                    uint32_t pixels[32 * 32];
                    for (int y = 0; y < 32; ++y)
                        for (int x = 0; x < 32; ++x)
                            pixels[y * 32 + x] = game_menu_pointer::CursorPixel(x, y);
                    g_context->UpdateSubresource(g_nativeCursorImages[index], 0, nullptr, pixels, 32 * 4, 0);
                    g_nativeCursorReady = xrReleaseSwapchainImage(g_nativeCursorChain, &release) == XR_SUCCESS;
                }
                else
                    xrReleaseSwapchainImage(g_nativeCursorChain, &release);
            }
        }
        if (!g_nativeCursorReady)
            DestroyChain(g_nativeCursorChain, g_nativeCursorImages, g_nativeCursorRtvs);
        LOG("game menu cursor: %s; optional visual only", g_nativeCursorReady ? "ready" : "unavailable");

        strcpy_s(g_status.sessionState, "starting");
        LogHeadsetPanelRate();
        LOG("OpenXR session created");
        return true;
    }

    // Read the headset's REAL panel rate once, so the log can separate two very
    // different things that look identical from the frame period alone:
    //   - the headset is genuinely running slow, versus
    //   - the runtime is TARGETING us at a fraction of the panel (reprojection),
    //     which pins us to panel/2 or panel/3 no matter what the GPU could do.
    // Works for any rate (72, 80, 90, 120, 144); nothing here assumes a number.
    void LogHeadsetPanelRate()
    {
        if (!g_refreshRateExtEnabled)
        {
            LOG("headset: this runtime does not expose %s, so the panel rate is "
                "unknown; only the rate it targets us at can be read",
                XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
            return;
        }
        PFN_xrGetDisplayRefreshRateFB getRate = nullptr;
        if (XR_FAILED(xrGetInstanceProcAddr(g_instance, "xrGetDisplayRefreshRateFB", (PFN_xrVoidFunction*)&getRate)) ||
            !getRate)
        {
            LOG("headset: xrGetDisplayRefreshRateFB missing though the extension "
                "is enabled; panel rate unknown");
            return;
        }
        float hz = 0.0f;
        const XrResult r = getRate(g_session, &hz);
        if (XR_FAILED(r) || hz <= 0.0f)
        {
            LOG("headset: panel rate query failed (%s); panel rate unknown", XrStr(r));
            return;
        }
        g_panelRefreshHz.store(hz, std::memory_order_relaxed);
        LOG("headset: panel is running at %.1fHz", hz);
    }

    // --------------------------------------------------------------- frame

    // Log the first few frames step by step so if anything dies on the render
    // thread we can see the exact call it died on. Silent afterward.
    int g_frameNo = 0;
} // namespace
namespace vr_core
{
    void FLog(const char* step)
    {
        if (g_frameNo <= 3)
            LOG("frame %d: %s", g_frameNo, step);
    }
} // namespace vr_core
namespace
{

    bool LocateViewsForUpcomingRender(XrTime displayTime);

    void PublishContactTrackingSnapshot(uint64_t serial, bool trackingFresh) noexcept
    {
        VrContactTrackingSnapshot next{};
        next.serial = serial;
        next.timeNs = g_preparedFrame.state.predictedDisplayTime;
        next.headValid = trackingFresh && g_headPoseValid;
        next.locomotionBlocked = g_weaponInteraction.HasClaimedGrip() || exclusive_input::Active();
        if (next.headValid)
        {
            const float p[]{g_headPose.position.x, g_headPose.position.y, g_headPose.position.z};
            const float q[]{
                g_headPose.orientation.x, g_headPose.orientation.y, g_headPose.orientation.z, g_headPose.orientation.w};
            memcpy(next.headPosition, p, sizeof(p));
            memcpy(next.headOrientation, q, sizeof(q));
        }
        const int64_t pending = g_contactSpaceChangeAtNs.load(std::memory_order_acquire);
        if (!pending || next.timeNs >= pending)
            next.referenceEpoch = g_contactSpaceEpoch.load(std::memory_order_acquire);
        const bool rightFresh = trackingFresh && g_rightAimPoseValid;
        const bool leftFresh = trackingFresh && g_leftAimPoseValid;
        next.rawPrimaryValid = rightFresh;
        if (rightFresh)
        {
            const auto& q = g_rightAimPose.orientation;
            const auto& p = g_rightAimPose.position;
            const float orientation[]{q.x, q.y, q.z, q.w}, position[]{p.x, p.y, p.z};
            memcpy(next.rawPrimaryOrientation, orientation, sizeof(orientation));
            memcpy(next.rawPrimaryPosition, position, sizeof(position));
        }
        const AimPoseInputs contactAimInputs =
            CurrentFrameStockAimPoseInputs(rightFresh, g_rightAimPose, leftFresh, g_leftAimPose);
        const AimPoseResult aim = ComputeAimPose(contactAimInputs);
        next.twoHandAimActive = aim.valid && aim.twoHandActive;
        next.leftHanded = g_capturedLeftHanded.load(std::memory_order_acquire);
        SelectControllerFingerInputs(g_controllerFingerSample, serial, next.timeNs, next.referenceEpoch,
            next.leftHanded, trackingFresh, next.controllerFingers);
        next.handAlignment = next.leftHanded && g_config.experimental_hand_alignment;
        next.primaryAimValid = aim.valid;
        if (aim.valid)
        {
            XrQuaternionf presented = PresentAimContinuity(aim.pose.orientation, serial);
            // Two-Hand Lab temporal (VS-off only): runs after the VS
            // presentation; at most one correction is ever active.
            presented = PresentTwoHandLabTemporal(presented, serial, contactAimInputs.twoHandLabGeneration);
            const float q[]{presented.x, presented.y, presented.z, presented.w};
            memcpy(next.primaryAimOrientation, q, sizeof(q));
        }
        auto physicalInputs = CurrentAimPoseInputs(rightFresh, g_rightAimPose, leftFresh, g_leftAimPose);
        physicalInputs.twoHandEnabled = false;
        const AimPoseResult physical = ComputeAimPose(physicalInputs);
        const XrPosef poses[2]{g_leftAimPose, physical.pose};
        next.hands[0].valid = leftFresh;
        next.hands[1].valid = physical.valid;
        for (unsigned hand = 0; hand < 2; ++hand)
        {
            if (!next.hands[hand].valid)
                next.controllerFingers[hand] = {};
            if (!next.hands[hand].valid)
                continue;
            const auto& pose = poses[hand];
            const float q[]{pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w};
            const float p[]{pose.position.x, pose.position.y, pose.position.z};
            memcpy(next.hands[hand].orientation, q, sizeof(q));
            memcpy(next.hands[hand].position, p, sizeof(p));
        }
        const uint32_t current = g_contactTrackingIndex.load(std::memory_order_seq_cst);
        const uint32_t target = current < 2 ? 1 - current : 0;
        uint32_t expected = 0;
        if (!g_contactTrackingStates[target].compare_exchange_strong(
                expected, kContactTrackingWriter, std::memory_order_seq_cst))
            return;
        g_contactTrackingSnapshots[target] = next;
        g_contactTrackingIndex.store(target, std::memory_order_seq_cst);
        g_contactTrackingStates[target].store(0, std::memory_order_seq_cst);
    }

    // The wait thread: block in xrWaitFrame here instead of on the game's render
    // thread, then hand the state over. Once the render thread claims Wait(N),
    // it releases this worker immediately before Begin(N). The next Wait(N+1)
    // is dispatched at that boundary and then overlaps the game's render work.
    DWORD WINAPI FrameWaitThread(LPVOID)
    {
        while (!g_waitThreadStop.load(std::memory_order_acquire))
        {
            XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
            XrFrameState state{XR_TYPE_FRAME_STATE};
            const uint64_t waitSequence = g_waitCallSequence.fetch_add(1, std::memory_order_relaxed) + 1;
            const uint32_t waitSessionEpoch = g_framePacingSessionEpoch.load(std::memory_order_acquire);
            LARGE_INTEGER waitStart{}, waitEnd{};
            if constexpr (kEnableFramePacingTransitionCapture)
                QueryPerformanceCounter(&waitStart);
            g_waitCallInFlight.store(waitSequence, std::memory_order_release);
            if (!SetEvent(g_waitStartedEvent))
                g_waitEventSignalFailures.fetch_add(1, std::memory_order_relaxed);
            const XrResult r = xrWaitFrame(g_session, &waitInfo, &state);
            if constexpr (kEnableFramePacingTransitionCapture)
                QueryPerformanceCounter(&waitEnd);
            const uint32_t returnSessionEpoch = g_framePacingSessionEpoch.load(std::memory_order_acquire);
            g_waitCallInFlight.store(0, std::memory_order_release);
            if (g_waitThreadStop.load(std::memory_order_acquire))
                break;
            if (XR_FAILED(r))
            {
                // Don't spin on a dead session. Report the failed worker wait;
                // the render thread never substitutes its own xrWaitFrame.
                g_waitedStateValid.store(false, std::memory_order_release);
                g_waitedStateResult.store(r, std::memory_order_relaxed);
                g_waitedStateFailed.store(true, std::memory_order_release);
                SetEvent(g_waitReadyEvent);
                Sleep(4);
                continue;
            }
            LARGE_INTEGER ready{};
            if constexpr (kEnableFramePacingTransitionCapture)
                QueryPerformanceCounter(&ready);
            // Version the existing packet handoff so a consumed-event timeout
            // and overwrite cannot be mislabeled as an exact diagnostic row.
            g_waitedPacketVersion.fetch_add(1, std::memory_order_acq_rel);
            g_waitedFrameState = state;
            if constexpr (kEnableFramePacingTransitionCapture)
            {
                g_waitedCallStartQpc.store(waitStart.QuadPart, std::memory_order_relaxed);
                g_waitedCallEndQpc.store(waitEnd.QuadPart, std::memory_order_relaxed);
                g_waitedReadyQpc.store(ready.QuadPart, std::memory_order_relaxed);
            }
            g_waitedSessionEpochStart.store(waitSessionEpoch, std::memory_order_relaxed);
            g_waitedSessionEpochEnd.store(returnSessionEpoch, std::memory_order_relaxed);
            g_waitedPacketSequence.store(waitSequence, std::memory_order_release);
            g_waitedPacketVersion.fetch_add(1, std::memory_order_release);
            g_waitedStateFailed.store(false, std::memory_order_release);
            g_waitedStateValid.store(true, std::memory_order_release);
            SetEvent(g_waitReadyEvent);
            // The event is only a wakeup; the exact sequence acknowledgement is
            // the permit. A timeout or stale event must never authorize another
            // xrWaitFrame, because concurrent waits are undefined by OpenXR.
            //
            // Parking here means the render thread has not claimed this packet,
            // which means the game has not reached its Present. A park of a few
            // milliseconds is the normal cadence; seconds of it is the freeze
            // that reads as a crash to the player. Time it and name it, because
            // nothing else in the log can: `status:` is emitted on the game's
            // render thread and the XInput diagnostic is driven by the game's
            // own polling, so both simply stop and leave an unexplained gap.
            // This is observation only - it never claims the packet, and no
            // frame submission behaviour changes.
            const uint64_t parkStartMs = GetTickCount64();
            bool parkStallReported = false;
            for (;;)
            {
                if (g_waitThreadStop.load(std::memory_order_acquire))
                    return 0;
                const FrameWaitPermit permit =
                    ClassifyFrameWaitPermit(waitSequence, g_waitConsumedSequence.load(std::memory_order_acquire));
                if (permit == FrameWaitPermit::StartNextWait)
                    break;
                if (permit == FrameWaitPermit::Fault)
                {
                    g_waitPipelineFaulted.store(true, std::memory_order_release);
                    SetEvent(g_waitReadyEvent);
                    return 0;
                }
                const DWORD wake = WaitForSingleObject(g_waitConsumedEvent, 50);
                if (wake == WAIT_FAILED)
                {
                    g_waitPipelineFaulted.store(true, std::memory_order_release);
                    SetEvent(g_waitReadyEvent);
                    return 0;
                }
                if (!parkStallReported && GetTickCount64() - parkStartMs >= kFrameStallNoticeMs)
                {
                    parkStallReported = true;
                    g_frameStalls.fetch_add(1, std::memory_order_relaxed);
                    // Say what was true, not what is usually true. A stall while
                    // the runtime has us not-visible is real but invisible: no
                    // player experienced it, and it must never be quoted as
                    // evidence of an in-headset freeze.
                    const int shouldRender = g_lastShouldRenderShared.load(std::memory_order_relaxed);
                    const char* stateName = SessionStateName(
                        static_cast<XrSessionState>(g_sessionStateShared.load(std::memory_order_relaxed)));
                    if (shouldRender == 1)
                        LOG("STALL: the game has not presented for %llums while "
                            "VISIBLE (session %s) - the headset is holding the "
                            "last frame we submitted and the player IS seeing a "
                            "freeze (this is the game, not the VR runtime)",
                            static_cast<unsigned long long>(kFrameStallNoticeMs), stateName);
                    else
                        LOG("STALL: the game has not presented for %llums while "
                            "NOT VISIBLE (session %s, shouldRender=%d) - we were "
                            "submitting no layers, so the headset was not showing "
                            "the game at all. Nobody saw this one; do not read it "
                            "as an in-headset freeze",
                            static_cast<unsigned long long>(kFrameStallNoticeMs), stateName, shouldRender);
                }
            }
            if (parkStallReported)
            {
                const uint64_t stalledMs = GetTickCount64() - parkStartMs;
                if (stalledMs > g_frameStallWorstMs.load(std::memory_order_relaxed))
                    g_frameStallWorstMs.store(stalledMs, std::memory_order_relaxed);
                LOG("STALL ENDED: the game resumed presenting after %llums",
                    static_cast<unsigned long long>(stalledMs));
            }
        }
        return 0;
    }

    bool StartFrameWaitThread()
    {
        if (g_waitThread)
            return true;
        g_waitThreadStop.store(false, std::memory_order_release);
        g_waitedStateValid.store(false, std::memory_order_release);
        g_waitedStateFailed.store(false, std::memory_order_release);
        g_waitedStateResult.store(XR_SUCCESS, std::memory_order_release);
        g_waitPacketMisses.store(0, std::memory_order_release);
        g_waitFailuresObserved.store(0, std::memory_order_release);
        g_waitEventSignalFailures.store(0, std::memory_order_release);
        g_waitCallSequence.store(0, std::memory_order_release);
        g_waitCallInFlight.store(0, std::memory_order_release);
        g_waitConsumedSequence.store(0, std::memory_order_release);
        g_waitPipelineFaulted.store(false, std::memory_order_release);
        g_abortFrameForSwapchainFailure = false;
        g_waitedPacketSequence.store(0, std::memory_order_release);
        g_waitedPacketVersion.store(0, std::memory_order_release);
        g_waitReadyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_waitConsumedEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_waitStartedEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_waitReadyEvent || !g_waitConsumedEvent || !g_waitStartedEvent)
        {
            LOG("pacing: could not create frame-wait events (%lu); XR frame "
                "preparation will remain disabled (no inline wait)",
                static_cast<unsigned long>(GetLastError()));
            if (g_waitReadyEvent)
                CloseHandle(g_waitReadyEvent);
            if (g_waitConsumedEvent)
                CloseHandle(g_waitConsumedEvent);
            if (g_waitStartedEvent)
                CloseHandle(g_waitStartedEvent);
            g_waitReadyEvent = nullptr;
            g_waitConsumedEvent = nullptr;
            g_waitStartedEvent = nullptr;
            return false;
        }
        g_waitThread = CreateThread(nullptr, 0, &FrameWaitThread, nullptr, 0, nullptr);
        if (!g_waitThread)
        {
            LOG("pacing: could not start the frame-wait thread (%lu); XR frame "
                "preparation will remain disabled (no inline wait)",
                static_cast<unsigned long>(GetLastError()));
            CloseHandle(g_waitReadyEvent);
            CloseHandle(g_waitConsumedEvent);
            CloseHandle(g_waitStartedEvent);
            g_waitReadyEvent = nullptr;
            g_waitConsumedEvent = nullptr;
            g_waitStartedEvent = nullptr;
            return false;
        }
        LOG("pacing: frame-wait thread started; the game's render thread no "
            "longer blocks in xrWaitFrame (whatever rate the runtime reports)");
        return true;
    }

    bool StopFrameWaitThread()
    {
        if (!g_waitThread)
            return true;

        g_waitThreadStop.store(true, std::memory_order_release);
        if (g_waitConsumedEvent)
            SetEvent(g_waitConsumedEvent);
        if (g_waitReadyEvent)
            SetEvent(g_waitReadyEvent);
        if (g_waitStartedEvent)
            SetEvent(g_waitStartedEvent);
        const DWORD joined = WaitForSingleObject(g_waitThread, 2000);
        if (joined != WAIT_OBJECT_0)
        {
            g_waitPipelineFaulted.store(true, std::memory_order_release);
            LOG("pacing: frame-wait worker did not stop cleanly (wait=%lu)", static_cast<unsigned long>(joined));
            return false;
        }

        CloseHandle(g_waitThread);
        g_waitThread = nullptr;
        if (g_waitReadyEvent)
            CloseHandle(g_waitReadyEvent);
        if (g_waitConsumedEvent)
            CloseHandle(g_waitConsumedEvent);
        if (g_waitStartedEvent)
            CloseHandle(g_waitStartedEvent);
        g_waitReadyEvent = nullptr;
        g_waitConsumedEvent = nullptr;
        g_waitStartedEvent = nullptr;
        g_waitedStateValid.store(false, std::memory_order_release);
        g_waitedStateFailed.store(false, std::memory_order_release);
        g_waitCallInFlight.store(0, std::memory_order_release);
        return true;
    }

    void PrepareNextFrame()
    {
        if (g_preparedFrame.begun)
            return;
        // Resource creation belongs at the frame preparation boundary, never
        // inside a title's optional third-camera callback.
        if (g_config.scope_enabled)
        {
            D3D11_TEXTURE2D_DESC scopeSource = g_eyeCacheDesc;
            if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
                scopeSource = g_halo2ScopeSourceDesc;
            if (TitleAdapter_GetActiveTitle() == GameTitle::HaloReach)
            {
                scopeSource = {};
                g_reachCaptureUsers.fetch_add(1, std::memory_order_seq_cst);
                if (g_reachCaptureEnabled.load(std::memory_order_seq_cst))
                    scopeSource = g_reachCaptureDesc;
                g_reachCaptureUsers.fetch_sub(1, std::memory_order_seq_cst);
            }
            (void)EnsureScopeCache(scopeSource);
        }

        if constexpr (kEnableFramePacingTransitionCapture)
            g_framePacingPending = {};
        ++g_frameNo;
        FLog("xrWaitFrame after DXGI Present");
        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        bool haveState = false;
        bool workerPacketClaimed = false;
        bool waitPacketCoherent = true;
        uint64_t waitSequence = 0;
        uint64_t overlappingWorkerWaitSequence = 0;
        uint32_t waitSessionEpoch = 0;
        int64_t waitCallStartQpc = 0;
        int64_t waitCallEndQpc = 0;
        int64_t waitReadyQpc = 0;
        LARGE_INTEGER consumedSignal{};
        LARGE_INTEGER nextWaitDispatchGateStart{}, nextWaitDispatchGateEnd{};
        DWORD eventWaitResult = WAIT_OBJECT_0;
        DWORD nextWaitEventResult = WAIT_OBJECT_0;
        uint64_t nextWaitDispatchSequence = 0;
        bool nextWaitDispatchObservedBeforeBegin = false;
        bool nextWaitDispatchGateFailed = false;
        LARGE_INTEGER waitStart{}, waitEnd{};
        QueryPerformanceCounter(&waitStart);
        if (g_waitThread && !g_waitPipelineFaulted.load(std::memory_order_acquire))
        {
            // Events are wakeup hints only. Always inspect packet status before
            // and after waiting so a coalesced or just-missed signal cannot
            // strand a valid packet while the exact-sequence worker is parked.
            if (!g_waitedStateValid.load(std::memory_order_acquire) &&
                !g_waitedStateFailed.load(std::memory_order_acquire))
            {
                // Normally already signalled. The timeout is only a bounded
                // observation point; it never transfers Wait ownership.
                // A packet claimed without waiting (the frame came
                // after the worker published) leaves the auto-reset event
                // signalled. The next frame then woke at once, found no packet
                // and skipped XR unpaced (H2A gameplay, renderWindow ~8-11 ms:
                // ~36 such frames/s, 128 fps against a 90 Hz panel). A wake
                // without a packet, failure or fault waits again, inside the
                // same 1000 ms bound.
                const uint64_t waitDeadlineMs = GetTickCount64() + 1000;
                for (;;)
                {
                    const uint64_t nowMs = GetTickCount64();
                    eventWaitResult = WaitForSingleObject(
                        g_waitReadyEvent, nowMs < waitDeadlineMs ? static_cast<DWORD>(waitDeadlineMs - nowMs) : 0);
                    if (eventWaitResult != WAIT_OBJECT_0 || g_waitedStateValid.load(std::memory_order_acquire) ||
                        g_waitedStateFailed.load(std::memory_order_acquire) ||
                        g_waitPipelineFaulted.load(std::memory_order_acquire) ||
                        // Stop sets this and signals once; never re-wait on an
                        // event (or a worker handle) the stop path may close.
                        g_waitThreadStop.load(std::memory_order_acquire) ||
                        WaitForSingleObject(g_waitThread, 0) == WAIT_OBJECT_0 || GetTickCount64() >= waitDeadlineMs)
                        break;
                    g_waitStaleWakes.fetch_add(1, std::memory_order_relaxed);
                }
            }
            // A successful packet supersedes any stale failure notification.
            if (g_waitedStateValid.exchange(false, std::memory_order_acq_rel))
            {
                g_waitedStateFailed.store(false, std::memory_order_release);
                const uint64_t versionBefore = g_waitedPacketVersion.load(std::memory_order_acquire);
                const uint64_t sequenceBefore = g_waitedPacketSequence.load(std::memory_order_acquire);
                frameState = g_waitedFrameState;
                if constexpr (kEnableFramePacingTransitionCapture)
                {
                    waitCallStartQpc = g_waitedCallStartQpc.load(std::memory_order_relaxed);
                    waitCallEndQpc = g_waitedCallEndQpc.load(std::memory_order_relaxed);
                    waitReadyQpc = g_waitedReadyQpc.load(std::memory_order_relaxed);
                }
                const uint32_t epochAtStart = g_waitedSessionEpochStart.load(std::memory_order_relaxed);
                const uint32_t epochAtEnd = g_waitedSessionEpochEnd.load(std::memory_order_relaxed);
                const uint64_t sequenceAfter = g_waitedPacketSequence.load(std::memory_order_acquire);
                const uint64_t versionAfter = g_waitedPacketVersion.load(std::memory_order_acquire);
                overlappingWorkerWaitSequence = g_waitCallInFlight.load(std::memory_order_acquire);
                const uint32_t currentEpoch = g_framePacingSessionEpoch.load(std::memory_order_acquire);
                waitSequence = sequenceAfter;
                waitSessionEpoch = epochAtStart;
                waitPacketCoherent = sequenceBefore != 0 && sequenceBefore == sequenceAfter &&
                                     versionBefore == versionAfter && (versionBefore & 1u) == 0 && epochAtStart != 0 &&
                                     epochAtStart == epochAtEnd && epochAtStart == currentEpoch &&
                                     overlappingWorkerWaitSequence == 0;
                haveState = true;
                workerPacketClaimed = waitPacketCoherent;
            }
            else if (g_waitedStateFailed.exchange(false, std::memory_order_acq_rel))
            {
                QueryPerformanceCounter(&waitEnd);
                g_waitDurationsMs.Add(QpcMs(waitEnd.QuadPart - waitStart.QuadPart));
                ++g_frameOrderFailures;
                g_waitFailuresObserved.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        if (haveState && !waitPacketCoherent)
        {
            // Do not acknowledge an incoherent or cross-session packet. The
            // worker remains parked, so drain and reset the running session.
            ++g_frameOrderFailures;
            EnterFrameWaitFatalDrain("The OpenXR wait worker published an incoherent or stale "
                                     "frame packet");
            return;
        }
        if (!haveState)
        {
            if (g_waitPipelineFaulted.load(std::memory_order_acquire) ||
                (g_waitThread && WaitForSingleObject(g_waitThread, 0) == WAIT_OBJECT_0))
            {
                ++g_frameOrderFailures;
                EnterFrameWaitFatalDrain("The exclusive OpenXR wait-worker pipeline stopped");
                return;
            }
            // The worker is the sole xrWaitFrame owner. Never substitute an
            // inline wait: it can race an in-flight worker wait, and it restores
            // the exact render-thread pacing stall this pipeline exists to fix.
            QueryPerformanceCounter(&waitEnd);
            g_waitDurationsMs.Add(QpcMs(waitEnd.QuadPart - waitStart.QuadPart));
            g_waitPacketMisses.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        QueryPerformanceCounter(&waitEnd);
        g_waitDurationsMs.Add(QpcMs(waitEnd.QuadPart - waitStart.QuadPart));

        // A subsequent xrWaitFrame is explicitly legal before this Begin and
        // must block until this frame has begun. Dispatch it now so the
        // runtime applies cadence delay on the worker, not inside xrBeginFrame
        // on Halo's render thread. Publish the exact claimed sequence before
        // waking; stale auto-reset event credits cannot advance the worker. Do
        // not rely on scheduler luck alone: observe the exact Wait(N+1)
        // dispatch boundary before entering Begin(N), then use cross-frame
        // timing to verify the runtime-call relationship in the headset.
        if (ShouldReleaseFrameWaitWorkerBeforeBegin(g_waitThread != nullptr, workerPacketClaimed))
        {
            if constexpr (kEnableFramePacingTransitionCapture)
                QueryPerformanceCounter(&consumedSignal);
            g_waitConsumedSequence.store(waitSequence, std::memory_order_release);
            if (!SetEvent(g_waitConsumedEvent))
            {
                ++g_frameOrderFailures;
                g_waitEventSignalFailures.fetch_add(1, std::memory_order_relaxed);
            }

            if constexpr (kEnableFramePacingTransitionCapture)
                QueryPerformanceCounter(&nextWaitDispatchGateStart);
            const uint64_t dispatchDeadlineMs = GetTickCount64() + 1000;
            for (;;)
            {
                nextWaitDispatchSequence = g_waitCallInFlight.load(std::memory_order_acquire);
                if (IsExpectedNextFrameWaitDispatch(waitSequence, nextWaitDispatchSequence))
                {
                    nextWaitDispatchObservedBeforeBegin = true;
                    break;
                }
                if (nextWaitDispatchSequence != 0 || g_waitPipelineFaulted.load(std::memory_order_acquire) ||
                    g_waitThreadStop.load(std::memory_order_acquire) ||
                    g_waitedStateFailed.load(std::memory_order_acquire))
                {
                    break;
                }
                const uint64_t dispatchNowMs = GetTickCount64();
                if (dispatchNowMs >= dispatchDeadlineMs)
                {
                    nextWaitEventResult = WAIT_TIMEOUT;
                    break;
                }
                nextWaitEventResult =
                    WaitForSingleObject(g_waitStartedEvent, static_cast<DWORD>(dispatchDeadlineMs - dispatchNowMs));
                if (nextWaitEventResult == WAIT_FAILED)
                    break;
            }
            nextWaitDispatchSequence = g_waitCallInFlight.load(std::memory_order_acquire);
            nextWaitDispatchObservedBeforeBegin =
                IsExpectedNextFrameWaitDispatch(waitSequence, nextWaitDispatchSequence);
            if constexpr (kEnableFramePacingTransitionCapture)
                QueryPerformanceCounter(&nextWaitDispatchGateEnd);
            if (!nextWaitDispatchObservedBeforeBegin)
            {
                ++g_frameOrderFailures;
                nextWaitDispatchGateFailed = true;
            }
        }

        FLog("xrBeginFrame before Halo render");
        XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
        LARGE_INTEGER beginStart{}, beginEnd{};
        if constexpr (kEnableFramePacingTransitionCapture)
            QueryPerformanceCounter(&beginStart);
        const XrResult beginResult = xrBeginFrame(g_session, &beginInfo);
        QueryPerformanceCounter(&beginEnd);
        if (XR_FAILED(beginResult))
        {
            ++g_frameOrderFailures;
            LOG("timing: xrBeginFrame failed: %s", XrStr(beginResult));
            EnterFrameWaitFatalDrain("xrBeginFrame failed after the exclusive wait worker was "
                                     "released");
            return;
        }

        g_beginFrameQpc = beginEnd;

        g_preparedFrame.state = frameState;
        g_preparedFrame.begun = true;
        g_preparedFrame.serial = ++g_nextPreparedSerial;
        // Invalidate stock coherence BEFORE a new controller/action sample can
        // replace the previous frame's poses. It becomes true again only after
        // this same prepared frame completes both action sync and HMD locate.
        g_stockAimFresh.store(false, std::memory_order_release);
        g_supportGripPoseFresh.store(false, std::memory_order_release);
        g_preparedFrame.predictedDisplayDelta = 0;
        g_preparedShouldRender.store(frameState.shouldRender == XR_TRUE, std::memory_order_release);

        if constexpr (kEnableFramePacingTransitionCapture)
        {
            FramePacingRecord& pacing = g_framePacingPending;
            pacing.serial = g_preparedFrame.serial;
            pacing.waitSequence = waitSequence;
            pacing.overlappingWorkerWaitSequence = overlappingWorkerWaitSequence;
            pacing.nextWaitDispatchSequence = nextWaitDispatchSequence;
            pacing.sessionEpoch = waitSessionEpoch;
            pacing.predictedDisplayTimeNs = frameState.predictedDisplayTime;
            pacing.predictedDisplayPeriodNs = frameState.predictedDisplayPeriod;
            pacing.waitCallStartQpc = waitCallStartQpc;
            pacing.waitCallEndQpc = waitCallEndQpc;
            pacing.waitReadyQpc = waitReadyQpc;
            pacing.renderWaitStartQpc = waitStart.QuadPart;
            pacing.renderWaitEndQpc = waitEnd.QuadPart;
            pacing.nextWaitDispatchGateStartQpc = nextWaitDispatchGateStart.QuadPart;
            pacing.nextWaitDispatchGateEndQpc = nextWaitDispatchGateEnd.QuadPart;
            pacing.beginStartQpc = beginStart.QuadPart;
            pacing.beginEndQpc = beginEnd.QuadPart;
            pacing.consumedSignalQpc = consumedSignal.QuadPart;
            pacing.waitResult = XR_SUCCESS;
            pacing.beginResult = beginResult;
            pacing.eventWaitResult = eventWaitResult;
            pacing.nextWaitEventResult = nextWaitEventResult;
            pacing.title = static_cast<uint8_t>(TitleAdapter_GetActiveTitle());
            pacing.waitPacketCoherent = waitPacketCoherent;
            pacing.nextWaitDispatchObservedBeforeBegin = nextWaitDispatchObservedBeforeBegin;
            pacing.shouldRender = frameState.shouldRender == XR_TRUE;
            pacing.focused = g_sessionState == XR_SESSION_STATE_FOCUSED;
            pacing.stereo = g_stereoEnabled.load(std::memory_order_relaxed);
            pacing.headTracking = Game_IsHeadTrackingApplied();
            pacing.scopeActive = g_scopeActive.load(std::memory_order_relaxed);
            Game_ReadFramePerfCounters(g_framePacingPerfStart);
        }

        if (nextWaitDispatchGateFailed)
        {
            EndPreparedFrameWithoutLayers("next wait dispatch gate failed");
            EnterFrameWaitFatalDrain("The exclusive OpenXR wait worker did not dispatch the exact "
                                     "next wait before xrBeginFrame");
            return;
        }

        if (g_lastPredictedDisplayTime)
        {
            const XrDuration delta = frameState.predictedDisplayTime - g_lastPredictedDisplayTime;
            const XrDuration period = frameState.predictedDisplayPeriod;
            if (delta > 0)
                g_preparedFrame.predictedDisplayDelta = delta;
            if (period > 0)
                g_displayPeriodNs.store(static_cast<uint64_t>(period), std::memory_order_relaxed);
            if (delta <= 0)
                ++g_duplicatePredictions;
            else if (period > 0)
            {
                g_predictionErrorMs.Add(std::fabs(static_cast<double>(delta - period)) / 1000000.0);
                if (delta > period + period / 2)
                    g_missedPredictions += static_cast<uint64_t>(std::max<XrDuration>(1, delta / period - 1));
            }
        }
        g_lastPredictedDisplayTime = frameState.predictedDisplayTime;

        // Input first, then views/head as late as possible. Every locate uses
        // the exact predicted time associated with this begun frame.
        const bool upcomingPadFresh = CaptureRightControllerPose(frameState.predictedDisplayTime);
        const bool upcomingViewsValid = LocateViewsForUpcomingRender(frameState.predictedDisplayTime);
        g_preparedFrame.viewsValid = upcomingViewsValid;
        g_preparedFrame.viewCount = upcomingViewsValid ? static_cast<uint32_t>(g_views.size()) : 0;
        g_preparedViewSerialPublished.store(upcomingViewsValid ? g_preparedFrame.serial : 0, std::memory_order_release);
        const ResolvedVirtualStockAimSettings resolvedStockSettings = CurrentResolvedVirtualStockAimSettings();
        const int64_t referenceChangeAt = g_contactSpaceChangeAtNs.load(std::memory_order_acquire);
        const uint64_t contactSpaceEpoch = g_contactSpaceEpoch.load(std::memory_order_acquire);
        const virtual_stock::InverseNeckNeutralCaptureInput neutralInput{
            VirtualStockHeadTurnCorrectionFamilyActive(resolvedStockSettings.settings), Menu_IsOpen(), false,
            referenceChangeAt != 0 && frameState.predictedDisplayTime < referenceChangeAt, g_preparedFrame.serial,
            contactSpaceEpoch, {}};
        const bool upcomingHeadValid = CaptureHeadPose(frameState.predictedDisplayTime, upcomingPadFresh, neutralInput);
        if (!upcomingHeadValid)
        {
            EnterCriticalSection(&g_headCs);
            virtual_stock::AdvanceInverseNeckNeutralCapture(g_inverseNeckNeutralCapture, neutralInput);
            LeaveCriticalSection(&g_headCs);
        }
        // Grab/release transition continuity: advance the Virtual Stock layer
        // exactly once for this prepared serial. It sits after the input and
        // head captures above (so UpdateTwoHandLatch and the coherent head
        // sample already describe this serial) and before every publication
        // below (telemetry, contact snapshot, Halo 2/Halo 4/Reach/CE snapshots,
        // reticle). The prepared-frame display delta is the same dt the rest of
        // the frame pipeline uses; an unavailable or non-positive delta makes
        // the module skip the advance, exactly as if the layer were off.
        {
            const float preparedDtSeconds = g_preparedFrame.predictedDisplayDelta > 0
                                                ? static_cast<float>(g_preparedFrame.predictedDisplayDelta) * 1.0e-9f
                                                : 0.0f;
            AdvanceTwoHandInputSmoothingForPreparedFrame(g_preparedFrame.serial, upcomingPadFresh, preparedDtSeconds);
            AdvanceAimContinuityForPreparedFrame(upcomingPadFresh, preparedDtSeconds);
            AdvanceTwoHandLabForPreparedFrame(upcomingPadFresh, preparedDtSeconds);
        }
        CapturePreparedFrameTelemetry(frameState, upcomingPadFresh, upcomingViewsValid, upcomingHeadValid);
        {
            halo_ce::Tracking ce{};
            const bool enabled = TitleAdapter_GetActiveTitle() == GameTitle::HaloCE && upcomingViewsValid &&
                                 upcomingHeadValid && g_views.size() == 2 && frameState.shouldRender &&
                                 g_stereoEnabled.load() && Game_IsHeadTracking() && !g_pausePresentation.load() &&
                                 !VR_IsPausePresentationTarget();
            if (enabled)
            {
                ce.serial = g_preparedFrame.serial;
                ce.generation = TitleAdapter_GetGeneration(GameTitle::HaloCE);
                ce.spaceEpoch = g_contactSpaceEpoch.load(std::memory_order_acquire);
                ce.predictedDisplayTimeNs = frameState.predictedDisplayTime;
                ce.motionBlur = g_config.motion_blur;
                ce.disableAnniversaryLensFlares = g_config.ce_anniversary_disable_lens_flares;
                ce.hud = {g_config.hud_size, g_config.hud_aspect, g_config.hud_curvature, g_config.hud_vertical_offset,
                    VR_HudHidden()};
                {
                    const auto& row = g_config.hud_place[hud_element::SetHaloCE];
                    ce.hud.perElement = g_config.hud_per_element;
                    ce.hud.weaponX = row.x[hud_element::Weapon];
                    ce.hud.weaponY = row.y[hud_element::Weapon];
                    ce.hud.motionX = row.x[hud_element::Motion];
                    ce.hud.motionY = row.y[hud_element::Motion];
                }
                ce.headPosition = {g_headPose.position.x, g_headPose.position.y, g_headPose.position.z};
                ce.headOrientation = {g_headPose.orientation.x, g_headPose.orientation.y, g_headPose.orientation.z,
                    g_headPose.orientation.w};
                // Same action-sync, role routing, mount calibration and support
                // grip as H2/Reach/H4, frozen with the eyes for native hot hooks.
                // Missing controller tracking affects hands/aim only.
                auto& rig = ce.controllers;
                rig.leftHanded = g_capturedLeftHanded.load(std::memory_order_acquire);
                rig.handAlignment = rig.leftHanded && g_config.experimental_hand_alignment;
                rig.turnSmooth = g_config.turn_smooth;
                rig.vehicleSmoothTurn = g_config.vehicle_smooth_turn;
                rig.turnSnapDeg = g_config.turn_snap_deg;
                rig.turnSmoothDegS = g_config.turn_smooth_deg_s;
                rig.vehicleMotion = g_config.vehicle_motion;
                rig.vehicleViewFollow = g_config.vehicle_view_follow;
                // Freeze the player's setting with this CE tracking frame;
                // the camera consumer separately proves native on-foot state.
                rig.roomscaleEnabled = g_config.roomscale_movement;
                rig.physicalRunningEnabled = g_config.physical_running;
                rig.controlsPresentationBlocked = !Game_CeControllerFeaturesRequested() || Menu_IsOpen() ||
                                                  VR_IsPausePresentation() || VR_IsPausePresentationTarget() ||
                                                  VR_IsCutsceneTheaterActive();
                rig.armIk = g_config.arm_ik;
                rig.floatingHands = g_config.floating_hands;
                rig.shoulderLevel = g_config.shoulder_level;
                rig.gunScale = g_config.gun_scale;
                rig.supportScale = g_config.left_hand_scale;
                rig.supportForwardM = g_config.left_hand_forward_m;
                rig.visualLeftHandOffset[0] = g_config.left_hand_mesh_x_m;
                rig.visualLeftHandOffset[1] = g_config.left_hand_mesh_y_m;
                rig.visualLeftHandOffset[2] = g_config.left_hand_mesh_z_m;
                rig.visualRightHandOffset[0] = g_config.right_hand_mesh_x_m;
                rig.visualRightHandOffset[1] = g_config.right_hand_mesh_y_m;
                rig.visualRightHandOffset[2] = g_config.right_hand_mesh_z_m;
                rig.visualPitchDeg = g_config.barrel_pitch_deg;
                rig.gunBarrelAim = g_config.gun_barrel_aim;
                rig.visualYawDeg = g_config.barrel_yaw_deg;
                rig.visualRollDeg = g_config.barrel_roll_deg;
                rig.supportMountPitchDeg = g_config.gun_pitch_deg;
                rig.supportMountYawDeg = g_config.gun_yaw_deg;
                rig.supportMountRollDeg = g_config.gun_roll_deg;
                rig.gunForwardM = g_config.gun_forward_m;
                rig.gunRightM = g_config.gun_right_m;
                rig.gunUpM = g_config.gun_up_m;
                rig.shoulderBackM = g_config.shoulder_back_m;
                rig.primaryShoulderDrop = g_config.right_shoulder_drop;
                const auto pose = [](const XrPosef& source, bool valid)
                {
                    halo_ce::ControllerPose result{};
                    if (!valid)
                        return result;
                    result.position = {source.position.x, source.position.y, source.position.z};
                    result.orientation = {
                        source.orientation.x, source.orientation.y, source.orientation.z, source.orientation.w};
                    result.valid = halo_ce::Finite(result.position) && halo_ce::Valid(result.orientation);
                    return result;
                };
                auto aimInputs = CurrentFrameStockAimPoseInputs(upcomingPadFresh && g_rightAimPoseValid, g_rightAimPose,
                    upcomingPadFresh && g_leftAimPoseValid, g_leftAimPose);
                const auto aim = ComputeAimPose(aimInputs);
                // CE's visible weapon/hand rig consumes the presented aim:
                // orientation-only continuity, position untouched. The
                // independent solve below stays raw. The Lab temporal
                // presentation (VS-off only) runs after the VS one.
                rig.primaryAim =
                    pose(PresentedTwoHandLabTemporalPose(PresentedAimContinuityPose(aim.pose, g_preparedFrame.serial),
                             g_preparedFrame.serial, aimInputs.twoHandLabGeneration),
                        aim.valid);
                rig.twoHandAimActive = rig.primaryAim.valid && aim.twoHandActive;
                // Persistent support grip: the frozen pose's provenance and the
                // base presentation value. The CE first-person prepare freezes
                // the per-invocation relationship decision into its copied
                // context, so with the feature off (or an unwired title) these
                // stay exactly `twoHandAimActive` and the palette is unchanged.
                rig.primaryAimSupportDerived = support_grip::AimSupportDerived(aim.valid, aim.twoHandActive);
                rig.supportGripAttached = rig.twoHandAimActive;
                aimInputs.twoHandEnabled = false;
                const auto independent = ComputeAimPose(aimInputs);
                rig.independentPrimaryAim = pose(independent.pose, independent.valid);
                rig.support = pose(g_leftAimPose, upcomingPadFresh && g_leftAimPoseValid);
                for (int hand = 0; hand < 2; ++hand)
                    rig.physical[hand] =
                        pose(g_physicalControllerPose[hand], upcomingPadFresh && g_physicalControllerValid[hand]);
                SelectControllerFingerInputs(g_controllerFingerSample, ce.serial, ce.predictedDisplayTimeNs,
                    ce.spaceEpoch, rig.leftHanded, upcomingPadFresh, ce.controllerFingers);
                const bool supportPhysicalFresh = rig.leftHanded ? (upcomingPadFresh && g_rightAimPoseValid)
                                                                 : (upcomingPadFresh && g_leftAimPoseValid);
                if (!supportPhysicalFresh)
                    ce.controllerFingers[0] = {};
                rig.padValid = upcomingPadFresh && g_padState.valid;
                if (rig.padValid)
                {
                    rig.turnX = g_padState.turnX;
                    rig.moveX = g_padState.moveX;
                    rig.moveY = g_padState.moveY;
                }
                for (int eye = 0; eye < 2; ++eye)
                {
                    const auto& eyePose = g_views[eye].pose;
                    const auto& fov = g_views[eye].fov;
                    ce.eyes[eye].offset = {eyePose.position.x - ce.headPosition.x,
                        eyePose.position.y - ce.headPosition.y, eyePose.position.z - ce.headPosition.z};
                    ce.eyes[eye].orientation = {
                        eyePose.orientation.x, eyePose.orientation.y, eyePose.orientation.z, eyePose.orientation.w};
                    ce.eyes[eye].fov[0] = fov.angleLeft;
                    ce.eyes[eye].fov[1] = fov.angleRight;
                    ce.eyes[eye].fov[2] = fov.angleUp;
                    ce.eyes[eye].fov[3] = fov.angleDown;
                }
            }
            // Classic look-through scope request for the render this sample
            // drives (third pass); zoom tangent from the shared lens projection.
            const bool ceVisorPass = CeVisorFullResActive();
            ce.scopeWanted = g_scopeGlassLive.load(std::memory_order_acquire) && HaloCE_ClassicScopeReady() &&
                             ((g_config.ce_classic_scope && g_zoomTier.load(std::memory_order_acquire) ==
                                                                static_cast<uint8_t>(visor_zoom::Tier::Lens)) ||
                                 ceVisorPass);
            // Idle cadence: a live glass that is not raised (not engaged) renders its
            // extra world pass every scope_idle_cadence samples; raised = every sample.
            ce.scopeWanted = g_ceScopeRequestScheduler.Advance(
                ce.scopeWanted, ceVisorPass || g_scopeActive.load(std::memory_order_acquire)
                                    ? 1
                                    : std::clamp(g_config.scope_idle_cadence, 1, 8));
            ce.scopeHeadView = ceVisorPass;
            if (ce.scopeWanted)
            {
                const float passAspect = g_ceScopeAspect.load(std::memory_order_relaxed);
                float passZoom = g_scopeRuntimeZoom.load(std::memory_order_acquire);
                if (ceVisorPass)
                    passZoom = visor_zoom::ScopeZoomForWindow(
                        passZoom, visor_zoom::kAspect, VisorWindowWidthM(), g_config.visor_zoom_distance_m);
                const ScopeProjectionTangents lens = ComputeScopeProjectionTangents(passZoom, passAspect);
                // A glass wider than the capture renders wider so its vertical view stays the same.
                const float glass = g_scopeGlassAspect.load(std::memory_order_acquire);
                ce.scopeTanHalfVertical =
                    ceVisorPass
                        ? lens.vertical
                        : lens.vertical * scope_window::GlassTangentScale(glass, passAspect) *
                              scope_window::GlassShiftMargin(glass, passAspect, g_config.scope_parallax_max_shift);
                ce.scopeGlassAspect = ceVisorPass ? 0.0f : glass;
                ce.scopeForwardM = g_config.ce_scope_forward_m;
                ce.scopeLensKnown = false;
                ce.scopeReceiptValid = false;
                if (!ceVisorPass)
                    CeFillGunCameraSource(ce);
                static int loggedLens = -1;
                const int lensState = ce.scopeReceiptValid ? 2 : ce.scopeLensKnown ? 1 : 0;
                if (loggedLens != lensState)
                {
                    loggedLens = lensState;
                    LOG("CE scope camera: %s (+%.2f m along the %s)",
                        lensState == 2   ? "at the gun's lens node (receipt)"
                        : lensState == 1 ? "at the gun's kit glass"
                                         : "ahead of the hand (no kit glass)",
                        ce.scopeForwardM, lensState == 2 ? "gun" : "aim");
                }
                ce.scopeWanted = std::isfinite(lens.vertical) && lens.vertical > 0.0f;
            }
            // CE enclosed driver/pilot seat: the third pass renders the
            // hull camera (the single cockpit screen, or one monitor view).
            if (!ce.scopeWanted)
                CeRequestHullCamera(ce);
            CeAnniversaryMonitorFrame(ce);
            HaloCE_PublishTracking(ce, enabled);
        }
        if (upcomingViewsValid && upcomingHeadValid && TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
        {
            PublishHalo2RenderSnapshot(g_preparedFrame.serial, upcomingPadFresh);
        }
        // MUST stay below CaptureHeadPose. That call is the only writer of the
        // head pose this snapshot carries, so publishing above it would hand
        // Halo 4 the PREVIOUS frame's head while its eye offsets, its FOV and
        // the layer pose submitted later all describe THIS frame - a full frame
        // of head latency plus a render/layer pose mismatch the compositor
        // reprojects against, which reads as the world swimming when you turn.
        // Reach's publish sits below it for the same reason.
        //
        // Deliberately NOT gated on upcomingHeadValid, unlike Reach: for Halo 4
        // the head pose is optional. Without it the snapshot simply carries
        // headPoseValid=false and the eyes still render from the engine's own
        // camera, so a tracking dropout costs head tracking and not stereo.
        if (upcomingViewsValid && TitleAdapter_GetActiveTitle() == GameTitle::Halo4)
        {
            PublishHalo4RenderSnapshot(g_preparedFrame.serial, upcomingPadFresh);
        }
        if (upcomingViewsValid && upcomingHeadValid)
            PublishReachRenderSnapshot(g_preparedFrame.serial, upcomingPadFresh);

        PublishContactTrackingSnapshot(
            g_preparedFrame.serial, upcomingPadFresh && upcomingHeadValid && upcomingViewsValid);
        LARGE_INTEGER preparedAt{};
        QueryPerformanceCounter(&preparedAt);
        g_prepareQpcPublished.store(static_cast<uint64_t>(preparedAt.QuadPart), std::memory_order_release);
        if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
        {
            PublishHalo2PreparedCadence(g_preparedFrame.serial, g_preparedFrame.state.predictedDisplayPeriod,
                g_preparedFrame.predictedDisplayDelta);
        }
        else
        {
            g_halo2PreparedCadenceSerial.store(0, std::memory_order_release);
        }
        g_preparedSerialPublished.store(g_preparedFrame.serial, std::memory_order_release);
        // Publish only after this prepared serial is visible and only after
        // this frame successfully captured both controller actions and head.
        // Leave the last committed sample untouched on a failed capture; its
        // serial/identity/age gates make it unavailable rather than serving it
        // as stale tracking. The frame thread owns these pose writes, so this
        // fixed atomic publication needs no critical section or XR sampling.
        if (upcomingPadFresh && upcomingHeadValid)
        {
            committed_aim::Sample sample{};
            sample.preparedSerial = g_preparedFrame.serial;
            sample.contactSpaceEpoch = g_contactSpaceEpoch.load(std::memory_order_acquire);
            sample.sessionEpoch = g_framePacingSessionEpoch.load(std::memory_order_acquire);
            sample.title = TitleAdapter_GetActiveTitle();
            sample.titleGeneration = TitleAdapter_GetGeneration(sample.title);
            sample.commitTimeMs = GetTickCount64();
            sample.values.okR = g_rightAimPoseValid;
            sample.values.okL = g_leftAimPoseValid;
            sample.values.okH = g_headPoseValid && g_stockAimFresh.load(std::memory_order_acquire);
            sample.values.supportGripValid =
                g_supportGripPoseValid && g_supportGripPoseFresh.load(std::memory_order_acquire);
            sample.values.primaryGripValid = g_primaryGripPoseValid;
            sample.values.rightAimPose = g_rightAimPose;
            sample.values.leftAimPose = g_leftAimPose;
            sample.values.headPose = g_headPose;
            sample.values.supportGripPosition = g_supportGripPosePosition;
            sample.values.primaryGripPosition = g_primaryGripPosePosition;
            sample.values.capturedLeftHanded = g_capturedLeftHanded.load(std::memory_order_acquire);
            sample.values.inverseNeckNeutralValid = g_inverseNeckNeutralCapture.neutralValid;
            sample.values.inverseNeckNeutralOrientation = {g_inverseNeckNeutralCapture.neutralOrientation.x,
                g_inverseNeckNeutralCapture.neutralOrientation.y, g_inverseNeckNeutralCapture.neutralOrientation.z,
                g_inverseNeckNeutralCapture.neutralOrientation.w};
            sample.values.inverseNeckNeutralCaptureSerial = g_inverseNeckNeutralCapture.captureSerial;
            sample.values.inverseNeckNeutralCaptureContactSpaceEpoch =
                g_inverseNeckNeutralCapture.captureContactSpaceEpoch;
            g_committedAimSample.Publish(sample);
        }
        if (g_frameNo == 1)
            LOG("timing: exact OpenXR pipeline active; headset smoothing %.1f%%",
                std::clamp(g_config.headset_smoothing, 0.0f, 0.10f) * 100.0f);
        if constexpr (kEnableFramePacingTransitionCapture)
        {
            LARGE_INTEGER prepareEnd{};
            QueryPerformanceCounter(&prepareEnd);
            g_framePacingPending.prepareEndQpc = prepareEnd.QuadPart;
        }
    }

    bool LocateViewsForUpcomingRender(XrTime displayTime)
    {
        if (g_views.empty())
            return false;
        XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};
        info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        info.displayTime = displayTime;
        info.space = g_localSpace;
        XrViewState state{XR_TYPE_VIEW_STATE};
        uint32_t count = 0;
        const XrResult result =
            xrLocateViews(g_session, &info, &state, static_cast<uint32_t>(g_views.size()), &count, g_views.data());
        return XR_SUCCEEDED(result) && count == g_views.size() &&
               (state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
               (state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);
    }

    // Titles whose captured crosshair art is not yet usable as the held authored
    // layer: CE until its procedural bootstrap has measured real art, and Halo 4
    // while its CUI reticle is on the procedural fallback.
    bool TitleReticleNeedsProceduralArt(GameTitle title, bool titleCapturesArt)
    {
        if (title == GameTitle::HaloCE)
            return halo_ce::ReticleNeedsProceduralBootstrap(
                titleCapturesArt, g_authoredReticleGoodValid, g_reticleContainsAuthored);
        if (title == GameTitle::Halo4)
            return Halo4CuiReticleUsesProceduralFallback(titleCapturesArt, g_config.crosshair, g_config.kill_reticle);
        return false;
    }

    // Halo 4 uses kill_reticle=0 as an explicit request for the stock face-centred
    // CUI reticle; never add a held authored gun-ray quad on top of it.
    bool TitleAllowsReticleQuad(GameTitle title)
    {
        return title != GameTitle::Halo4 || g_config.kill_reticle;
    }

    // The half-FOV cover the prepared frame was rendered with: CE's leased pair
    // carries its own covers; every other title publishes them for this serial.
    // (CE never consults Game_GetRenderHalfFovs.)
    bool PreparedFrameRenderHalfFovs(
        bool ceTitle, bool ceImages, const halo_ce::EyeCache::Completed& cePair, float halfX[2], float halfY[2])
    {
        const bool valid = ceTitle ? ceImages : Game_GetRenderHalfFovs(g_preparedFrame.serial, halfX, halfY);
        if (ceImages)
            for (int eye = 0; eye < 2; ++eye)
            {
                halfX[eye] = cePair.covers[eye].halfX;
                halfY[eye] = cePair.covers[eye].halfY;
            }
        return valid;
    }

} // namespace
namespace haloce_frame_bridge
{

    // CE desktop: show one eye of the borrowed pair on the monitor; a failed copy
    // keeps the native output and never touches XR. Logged on change (failures
    // repeat every 2 s).
    void MirrorCeDesktopAndReport(
        const halo_ce::EyeCache::Completed& pair, ID3D11Texture2D* backbuffer, const D3D11_TEXTURE2D_DESC& bd)
    {
        const bool mirrored = MirrorCeDesktop(pair, backbuffer, bd);
        static int lastCeMirrorState = -1;
        static uint64_t lastCeMirrorLogMs = 0;
        const uint64_t nowMs = GetTickCount64();
        if (lastCeMirrorState != static_cast<int>(mirrored) || (!mirrored && nowMs - lastCeMirrorLogMs >= 2000))
        {
            lastCeMirrorState = static_cast<int>(mirrored);
            lastCeMirrorLogMs = nowMs;
            LOG("CE desktop mirror: %s; left eye %ux%u -> desktop %ux%u; XR pair unchanged",
                mirrored ? "one view" : "copy failed; native output retained", pair.descriptor.Width,
                pair.descriptor.Height, bd.Width, bd.Height);
        }
    }

} // namespace haloce_frame_bridge
namespace vr_core
{
    // Anniversary graphics: steal one tracking sample in N, only while the
    // head is nearly still, to render one extra view in place of the eye pair
    // (the previous pair is shown again). The visor zoom wins over the hull
    // camera. Counters are logged every 5 s.
    std::atomic<uint64_t> g_annivStolen{0}, g_annivStolenVisor{0}, g_annivMovingSkips{0}, g_annivStolenMoving{0};
    std::atomic<int> g_annivHullMoving{-1}; // last hull decision: 0 stopped, 1 moving
    // When a hull / visor monitor frame was last wanted: a held image is shown
    // only while its request is live (a held hull image must not
    // outlive the seat and block the visor crop).
    std::atomic<uint64_t> g_annivHullWantedMs{0}, g_annivVisorWantedMs{0};
    visor_zoom::ZoomBoost g_annivZoomBoost; // render thread (tracking sample)
    // Gun camera (mode 3): the glass request, its held images and their
    // camera source (2 lens receipt, 1 kit glass, 0 aim only), for the 1 s line.
    std::atomic<uint64_t> g_annivGlassWantedMs{0}, g_annivGlassImageMs{0}, g_annivStolenGlass{0};
    std::atomic<unsigned> g_annivGlassImages{0};
} // namespace vr_core
namespace
{

    const WorldSwapchainMessages& SelectWorldSwapchainMessages(bool ceTitle, bool halo2Title, bool halo4Title)
    {
        return ceTitle      ? kCeWorldMessages
               : halo2Title ? kHalo2WorldMessages
               : halo4Title ? kHalo4WorldMessages
                            : kReachWorldMessages;
    }

    // The images one frame's two eyes are uploaded from.
    struct WorldEyeSource
    {
        ID3D11Texture2D* const* eyes = nullptr; // per-eye textures, or none
        const D3D11_TEXTURE2D_DESC* desc = nullptr;
        bool titleOwned = false;    // a title-owned pair: both eyes present by contract
        bool cacheAdmitted = false; // shared eye cache: each eye still needs g_eyeHasImage
    };

    // Per-title authored-crosshair data lives in common/title_traits.h; every
    // listed title reports its upload counters, an unlisted title uses
    // IdentityAndColorState and reports nothing.
    using TitleReticleTraits = TitleTraits;

    const TitleReticleTraits* FindTitleReticleTraits(GameTitle title)
    {
        return FindTitleTraits(title);
    }

    AuthoredReticleRefreshPolicy TitleReticleRefreshPolicy(const TitleReticleTraits* traits)
    {
        if (!traits)
            return AuthoredReticleRefreshPolicy::IdentityAndColorState;
        if (traits->crosshairAnimatesWhenConfigured &&
            ResolveAuthoredAnimationGapFrames(g_config.crosshair_animation_frames) != 0)
            return AuthoredReticleRefreshPolicy::BoundedAnimation;
        return traits->crosshairRefresh;
    }

    void SubmitPreparedFrame(IDXGISwapChain* sc)
    {
        if (!g_preparedFrame.begun)
            return;
        const XrFrameState fs = g_preparedFrame.state;
        HaloCE_SetDlssRequested(TitleAdapter_GetActiveTitle() == GameTitle::HaloCE &&
                                g_config.upscaler == dlss::kUpscalerDlss && Dlss_IsAvailable());
        HaloCE_PresentResources(g_device, g_context);
        struct CeLease
        {
            halo_ce::EyeCache::Completed pair;
            ~CeLease()
            {
                for (auto& eye : g_dlssEye)
                    eye.borrowedCeDepth = nullptr;
                if (pair.borrowId)
                    HaloCE_ReleasePair(pair.borrowId);
            }
        } ceLease{};
        const bool ceTitle = TitleAdapter_GetActiveTitle() == GameTitle::HaloCE;
        const bool ceImages = ceTitle && HaloCE_AcquirePair(g_context, g_preparedFrame.serial,
                                             g_contactSpaceEpoch.load(std::memory_order_acquire), ceLease.pair);
        const bool ceOwned = ceTitle && HaloCE_OwnsPresentation();
        if (ceTitle)
        {
            static uint64_t previousSerial = 0, previousEpoch = 0, previousSpace = 0;
            static uint32_t previousGeneration = 0;
            const bool depthPair = ceImages && ceLease.pair.depthHistoryEpoch && ceLease.pair.depthViews[0] &&
                                   ceLease.pair.depthViews[1] && ceLease.pair.depthCameras[0].valid &&
                                   ceLease.pair.depthCameras[1].valid;
            for (int eye = 0; eye < 2; ++eye)
            {
                auto& state = g_dlssEye[eye];
                state.published = state.depthValid = depthPair;
                if (depthPair)
                {
                    state.current = ceLease.pair.depthCameras[eye];
                    state.borrowedCeDepth = ceLease.pair.depthViews[eye];
                    state.depthCopyDesc = ceLease.pair.depthDescriptor;
                    state.resetPending |= previousSerial >= ceLease.pair.key.serial ||
                                          previousEpoch != ceLease.pair.depthHistoryEpoch ||
                                          previousSpace != ceLease.pair.key.spaceEpoch ||
                                          previousGeneration != ceLease.pair.key.generation;
                }
            }
            if (depthPair)
            {
                previousSerial = ceLease.pair.key.serial;
                previousEpoch = ceLease.pair.depthHistoryEpoch;
                previousSpace = ceLease.pair.key.spaceEpoch;
                previousGeneration = ceLease.pair.key.generation;
            }
        }
        float comfortFadeAlpha = UpdatePauseTransition();

        // Per-eye pose + field of view for this frame (foundation for
        // stereo rendering — not used to render yet).
        bool viewsValid = false;
        uint32_t locatedViewCount = 0;
        if (!g_views.empty())
        {
            XrViewLocateInfo vli{XR_TYPE_VIEW_LOCATE_INFO};
            vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            vli.displayTime = fs.predictedDisplayTime;
            vli.space = g_localSpace;
            XrViewState vs{XR_TYPE_VIEW_STATE};
            if (XR_SUCCEEDED(
                    xrLocateViews(g_session, &vli, &vs, (uint32_t)g_views.size(), &locatedViewCount, g_views.data())) &&
                locatedViewCount == g_views.size() && (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
                (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT))
            {
                viewsValid = true;
                static bool loggedEyes = false;
                if (!loggedEyes)
                {
                    for (uint32_t i = 0; i < locatedViewCount; i++)
                        LOG("eye %u pose(%.3f,%.3f,%.3f) fov L%.1f R%.1f U%.1f D%.1f deg", i,
                            g_views[i].pose.position.x, g_views[i].pose.position.y, g_views[i].pose.position.z,
                            g_views[i].fov.angleLeft * 57.2958f, g_views[i].fov.angleRight * 57.2958f,
                            g_views[i].fov.angleUp * 57.2958f, g_views[i].fov.angleDown * 57.2958f);
                    // Interpupillary distance = horizontal gap between the eye poses.
                    if (locatedViewCount >= 2)
                        LOG("eye separation (IPD) = %.1f mm",
                            (g_views[1].pose.position.x - g_views[0].pose.position.x) * 1000.0f);
                    loggedEyes = true;
                }
            }
        }

        XrCompositionLayerQuad screenQuad, menuQuad, reticleQuad, secondaryReticleQuad, scopeQuad, scopeQuadRight,
            fadeQuad, nativeCursorQuad, nativeSubtitleQuad;
        XrCompositionLayerQuad vignetteQuad, dashQuad;
        XrCompositionLayerQuad theaterQuads[2]{};
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        XrCompositionLayerProjection theaterProjection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        XrCompositionLayerProjectionView theaterProjectionViews[2]{
            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        // Reused across frames (Frame runs only on the render thread) so the
        // per-frame layer assembly allocates nothing in steady state.
        static std::vector<XrCompositionLayerProjectionView> projectionViews;
        static std::vector<XrCompositionLayerBaseHeader*> layers;
        projectionViews.clear();
        layers.clear();
        g_nativeTheatreCaptionReady = false;
        const bool nativeSubtitleReady =
            fs.shouldRender && comfortFadeAlpha <= 0.01f && !Menu_IsOpen() && PrepareNativeSubtitle(nativeSubtitleQuad);
        bool halo2ProjectionQueued = false;
        bool halo2StrictStockScreenQueued = false;
        // Count a headset-visible Halo 4 pair only after the frame that queues
        // its projection layer is accepted by xrEndFrame.
        bool halo4ProjectionQueued = false;

        // Build the descriptors every frame from the predicted eye poses/FOV.
        // A later render hook only needs to fill/release both swapchain
        // images and replace the mono quad in `layers` with `projection`.
        if (viewsValid && g_stereoChain != XR_NULL_HANDLE && locatedViewCount == 2)
        {
            projection.space = g_localSpace;
            projectionViews.assign(locatedViewCount, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});

            // RenderViewHook positions and rotates each raster camera from the
            // same per-eye view offsets (VR_GetEyeViewOffset), so the images
            // are canted the way PSVR2 reports its views — submit the real
            // per-eye orientations. Submitting a shared midpoint orientation
            // here under-covers the outward-angled lens edge and shows as a
            // black border at the outer edge of each eye.
            // The current compositor contract accepts a symmetric raster cover.
            // Established title paths render one wide enough to contain the
            // headset's asymmetric per-eye angles; H4 instead admits only
            // a runtime-confirmed zero-center native projection.
            //
            // We used to submit the cover itself as the layer FOV over the whole
            // slice. That is legal OpenXR and SteamVR's compositor resolves it
            // correctly, but ALVR does not: it lens-corrects and reprojects using
            // its own view parameters rather than the layer's, so a layer whose
            // FOV is not the native view FOV gets sampled with the wrong frustum -
            // warped, stretched, and wrong differently per eye, which the viewer
            // cannot fuse. That is alvr-org/ALVR#1306, open since 2022: "ALVR does
            // not account for the missing space in the frame when the FOV is any
            // lower than 100%".
            //
            // Measured on one Quest 3 with one build and identical reported optics
            // (left -54.0, right 40.0, up 44.0, down -55.0 degrees; IPD 69.3 mm): clean
            // through Virtual Desktop and Steam Link, doubled through ALVR at both
            // 90 and 120 Hz, and the image snaps correct the instant the SteamVR
            // dashboard composites it.
            //
            // So submit the runtime's OWN per-eye FOV and let imageRect select the
            // sub-rectangle of the cover that corresponds to it. Every other VR
            // application submits the FOV the runtime handed it; doing the same
            // costs no resolution, because the compositor was already cropping to
            // exactly this region, and it makes the layer correct for any
            // compositor rather than only for those that honour a custom FOV.
            float haloHalfX[2] = {atanf(1.091595f), atanf(1.091595f)};
            float haloHalfY[2] = {atanf(1.114286f), atanf(1.114286f)};
            const bool renderFovsValid =
                PreparedFrameRenderHalfFovs(ceTitle, ceImages, ceLease.pair, haloHalfX, haloHalfY);
            if (ceImages && renderFovsValid)
                CeVisorCrop(ceLease.pair, haloHalfX, haloHalfY);
            bool nativeFovValid = renderFovsValid;
            for (uint32_t i = 0; nativeFovValid && i < locatedViewCount; ++i)
            {
                // Tangent space: the cover maps linearly across the slice, x from
                // -coverX (left edge) to +coverX, y from +coverY (TOP row) down to
                // -coverY. Solve for the native frustum's edges in pixels.
                const XrFovf native = ceImages ? CeTrackedEyeFov(ceLease.pair, i) : g_views[i].fov;
                const float coverX = tanf(haloHalfX[i]);
                const float coverY = tanf(haloHalfY[i]);
                const float nl = tanf(native.angleLeft);
                const float nr = tanf(native.angleRight);
                const float nu = tanf(native.angleUp);
                const float nd = tanf(native.angleDown);
                const float w = static_cast<float>(g_stereoW);
                const float h = static_cast<float>(g_stereoH);
                if (!std::isfinite(coverX) || !std::isfinite(coverY) || coverX <= 0.0f || coverY <= 0.0f || w <= 0.0f ||
                    h <= 0.0f || !std::isfinite(nl) || !std::isfinite(nr) || !std::isfinite(nu) || !std::isfinite(nd) ||
                    nr <= nl || nu <= nd ||
                    // The cover must actually contain the native frustum. If it
                    // does not, the raster is missing pixels the headset wants and
                    // cropping to it would show a hard edge, so keep the whole
                    // slice for this frame instead of inventing coverage.
                    nl < -coverX || nr > coverX || nu > coverY || nd < -coverY)
                {
                    nativeFovValid = false;
                    break;
                }

                auto clampToSlice = [](long value, long lo, long hi) -> int32_t
                { return static_cast<int32_t>(value < lo ? lo : (value > hi ? hi : value)); };
                int32_t x0 = clampToSlice(lroundf(w * (nl + coverX) / (2.0f * coverX)), 0, (long)g_stereoW - 1);
                int32_t x1 = clampToSlice(lroundf(w * (nr + coverX) / (2.0f * coverX)), x0 + 1, (long)g_stereoW);
                int32_t y0 = clampToSlice(lroundf(h * (coverY - nu) / (2.0f * coverY)), 0, (long)g_stereoH - 1);
                int32_t y1 = clampToSlice(lroundf(h * (coverY - nd) / (2.0f * coverY)), y0 + 1, (long)g_stereoH);

                // Rounding to whole pixels moves the edges by up to half a pixel,
                // so derive the submitted FOV back from the integer rect. The pair
                // then describes the same frustum exactly, which is the property
                // the compositor relies on.
                projectionViews[i].pose = g_views[i].pose;
                if (ceImages)
                    projectionViews[i].pose = CeTrackedEyePose(ceLease.pair, i);
                projectionViews[i].fov = {atanf(coverX * (2.0f * x0 / w - 1.0f)),
                    atanf(coverX * (2.0f * x1 / w - 1.0f)), atanf(coverY * (1.0f - 2.0f * y0 / h)),
                    atanf(coverY * (1.0f - 2.0f * y1 / h))};
                projectionViews[i].subImage.swapchain = g_stereoChain;
                projectionViews[i].subImage.imageRect = {{x0, y0}, {x1 - x0, y1 - y0}};
                projectionViews[i].subImage.imageArrayIndex = i;

                // Log on the first frame and again whenever the cover or
                // the rect changes (a graphics-mode switch, a new title),
                // at most every two seconds; a once-ever line could not
                // describe Halo 2's two renderers.
                static float loggedCoverX = -1.0f, loggedCoverY = -1.0f;
                static int32_t loggedRectW = -1, loggedRectH = -1;
                static uint64_t loggedNativeFovMs = 0;
                const uint64_t nativeFovNowMs = GetTickCount64();
                const bool nativeFovChanged = loggedCoverX != coverX || loggedCoverY != coverY ||
                                              loggedRectW != x1 - x0 || loggedRectH != y1 - y0;
                if (i + 1 == locatedViewCount && nativeFovChanged && nativeFovNowMs - loggedNativeFovMs >= 2000)
                {
                    loggedCoverX = coverX;
                    loggedCoverY = coverY;
                    loggedRectW = x1 - x0;
                    loggedRectH = y1 - y0;
                    loggedNativeFovMs = nativeFovNowMs;
                    LOG("submitting native per-eye FOV; eye %u cover "
                        "%.1f/%.1f deg -> rect (%d,%d)+%dx%d of %ux%u, fov "
                        "L%.1f R%.1f U%.1f D%.1f deg",
                        i, haloHalfX[i] * 57.2958f, haloHalfY[i] * 57.2958f, x0, y0, x1 - x0, y1 - y0, g_stereoW,
                        g_stereoH, projectionViews[i].fov.angleLeft * 57.2958f,
                        projectionViews[i].fov.angleRight * 57.2958f, projectionViews[i].fov.angleUp * 57.2958f,
                        projectionViews[i].fov.angleDown * 57.2958f);
                }
            }
            if (renderFovsValid && !nativeFovValid)
            {
                // Loud, never silent: fall back to submitting the full cover, which
                // is what shipped through 0.3.0 and is correct on SteamVR.
                for (uint32_t i = 0; i < locatedViewCount; ++i)
                {
                    projectionViews[i].pose = g_views[i].pose;
                    projectionViews[i].fov = {-haloHalfX[i], haloHalfX[i], haloHalfY[i], -haloHalfY[i]};
                    projectionViews[i].subImage.swapchain = g_stereoChain;
                    projectionViews[i].subImage.imageRect = {{0, 0}, {(int32_t)g_stereoW, (int32_t)g_stereoH}};
                    projectionViews[i].subImage.imageArrayIndex = i;
                }
                static bool loggedCoverFallback = false;
                if (!loggedCoverFallback)
                {
                    loggedCoverFallback = true;
                    LOG("WARNING: the symmetric raster cover does not contain "
                        "the headset's native per-eye frustum, so the whole slice "
                        "is submitted at the cover FOV. Compositors that ignore a "
                        "custom layer FOV (ALVR) will show a doubled image.");
                }
            }
            if (renderFovsValid)
            {
                // Also cover the symmetric-FOV fallback: asynchronous CE
                // preparation always submits the pose that drew these pixels.
                if (ceImages)
                {
                    for (int index = 0; index < 2; ++index)
                        projectionViews[index].pose = CeTrackedEyePose(ceLease.pair, index);
                    NoteCeSubmittedPair(ceLease.pair, projectionViews.data());
                }
                projection.viewCount = locatedViewCount;
                projection.views = projectionViews.data();
            }
        }

        if (fs.shouldRender)
        {
            ID3D11Texture2D* backbuffer = nullptr;
            const HRESULT backbufferResult = sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backbuffer);
            (void)backbufferResult;
            if (backbuffer)
            {
                D3D11_TEXTURE2D_DESC bd{};
                backbuffer->GetDesc(&bd);
                HaloCE_RecordPresentationTexture(backbuffer, bd);
                if (g_recenterRequested.exchange(false, std::memory_order_acq_rel))
                {
                    g_haveCenter = false;
                }
                if (!g_haveCenter)
                    TryRecenter(fs.predictedDisplayTime);
                // SteamVR accepted eye swapchains made before xrBeginSession
                // but presented them black. Create them lazily only once the
                // session is running, matching the known-good screen chain.
                // With eye_full_density the chain waits for located views (it is
                // sized from the native FOV, once); stereo needs them anyway.
                if (g_stereoEnabled.load() && g_stereoChain == XR_NULL_HANDLE &&
                    (viewsValid || !g_config.eye_full_density))
                {
                    if (!CreateStereoArrayChain(viewsValid ? &g_views[0].fov : nullptr))
                    {
                        LOG("eye swapchain allocation failed; returning to mono screen");
                        g_stereoEnabled = false;
                        Game_SetStereoEye(-1);
                    }
                }
                TickRenderPlan();
                const bool pausedPresentation = g_pausePresentation.load();
                const bool stereo = !pausedPresentation && g_stereoEnabled.load() && viewsValid &&
                                    g_stereoChain != XR_NULL_HANDLE && Game_IsHeadTracking();
                const Halo2FrameDecision halo2Frame =
                    DecideHalo2Frame(stereo, pausedPresentation, projection.viewCount == 2, projectionViews);
                const bool halo2Title = halo2Frame.title;
                const uint32_t halo2Generation = halo2Frame.generation;
                float halo2HalfX[2]{halo2Frame.halfX[0], halo2Frame.halfX[1]};
                float halo2HalfY[2]{halo2Frame.halfY[0], halo2Frame.halfY[1]};
                const bool halo2LiveExactPair = halo2Frame.liveExactPair;
                const bool halo2Images = halo2Frame.images;
                const bool halo2StrictStockScreen = halo2Frame.strictStockScreen;
                const bool stereoWorldFrame = halo2Frame.stereoWorldFrame;
                const bool allowStockScreenFrame = halo2Frame.allowStockScreenFrame;
                float requestedTheaterAspect = 0.0f;
                const bool theaterQualified =
                    stereoWorldFrame && Game_GetCutsceneTheaterPresentation(requestedTheaterAspect);
                if (theaterQualified)
                    g_cutsceneTheaterAuthoredAspect = requestedTheaterAspect;
                if (theaterQualified && !g_cutsceneTheaterWasQualified)
                {
                    g_cutsceneTheaterEntryBlocked = !TryRecenter(fs.predictedDisplayTime);
                    if (g_cutsceneTheaterEntryBlocked)
                    {
                        LOG("cutscene theatre: current head pose unavailable; "
                            "keeping this cinematic immersive");
                    }
                }
                else if (!theaterQualified)
                {
                    g_cutsceneTheaterEntryBlocked = false;
                }
                g_cutsceneTheaterWasQualified = theaterQualified;

                const CutsceneTheaterTransitionOutput theaterTransition =
                    g_cutsceneTheaterTransition.Update(GetTickCount64(),
                        theaterQualified && !g_cutsceneTheaterEntryBlocked, CutsceneFadeMs(g_config.cutscene_fade_ms));
                g_cutsceneTheaterActive.store(theaterTransition.active, std::memory_order_release);
                comfortFadeAlpha = std::max(comfortFadeAlpha, theaterTransition.fadeAlpha);
                // A VR world <-> flat screen switch the game makes this frame
                // (e.g. a camera the stereo path cannot drive) fades back in.
                static PresentationSwitchFade switchFade;
                comfortFadeAlpha = std::max(comfortFadeAlpha,
                    switchFade.Update(GetTickCount64(), stereoWorldFrame, !Menu_IsOpen() && !pausedPresentation,
                        CutsceneFadeMs(g_config.cutscene_fade_ms)));
                if (theaterTransition.switched)
                {
                    Game_OnCutsceneTheaterPresentationChanged();
                    if (theaterTransition.active)
                    {
                        LOG("cutscene theatre: entered room-fixed stereo presentation");
                        // The authored aspect is the shape the title actually
                        // rasterized this cutscene into, so report it beside
                        // the cine bars it produced. A report can then say
                        // which of the two is wrong without another build.
                        const CutsceneTheaterMatte entryMatte =
                            ComputeCutsceneTheaterMatte(g_cutsceneTheaterAuthoredAspect,
                                g_config.cutscene_theater_matte_aspect, g_config.cutscene_theater_matte_offset);
                        if (entryMatte.active)
                        {
                            LOG("cutscene theatre: authored frame %.3f:1, cine "
                                "bars leave %.3f:1 showing (V %.3f..%.3f, "
                                "%.1f%% hidden top, %.1f%% bottom)",
                                g_cutsceneTheaterAuthoredAspect, g_config.cutscene_theater_matte_aspect,
                                entryMatte.vMin, entryMatte.vMax, entryMatte.vMin * 100.0f,
                                (1.0f - entryMatte.vMax) * 100.0f);
                        }
                        else
                        {
                            LOG("cutscene theatre: authored frame %.3f:1, no "
                                "cine bars (requested %.3f:1)",
                                g_cutsceneTheaterAuthoredAspect, g_config.cutscene_theater_matte_aspect);
                        }
                    }
                    else
                    {
                        LOG("cutscene theatre: returned to immersive presentation");
                        ReleaseTheaterResolvedResources();
                        ReleaseTheaterSubtitleBand();
                        Game_Recenter();
                    }
                }
                const bool theaterPresentation = theaterTransition.active && g_haveCenter;
                bool theaterProjectionAttempted = false;
                bool theaterProjectionReady = false;
                if (theaterPresentation && viewsValid && locatedViewCount == 2 && g_stereoChain != XR_NULL_HANDLE)
                {
                    // This descriptor is intentionally independent from the
                    // title-authored raster projection above. The rejected
                    // earlier path reused that narrow cutscene FOV here, then
                    // asked the runtime to reinterpret it as the headset view.
                    // Project and submit against the native runtime views
                    // instead, with one complete array slice per physical eye.
                    theaterProjection.space = g_localSpace;
                    theaterProjection.viewCount = 2;
                    theaterProjection.views = theaterProjectionViews;
                    for (uint32_t eye = 0; eye < 2; ++eye)
                    {
                        theaterProjectionViews[eye].pose = g_views[eye].pose;
                        theaterProjectionViews[eye].fov = g_views[eye].fov;
                        theaterProjectionViews[eye].subImage.swapchain = g_stereoChain;
                        theaterProjectionViews[eye].subImage.imageRect = {
                            {0, 0}, {static_cast<int32_t>(g_stereoW), static_cast<int32_t>(g_stereoH)}};
                        theaterProjectionViews[eye].subImage.imageArrayIndex = eye;
                    }
                }
                if (stereoWorldFrame && (!ceTitle || ceImages))
                {
                    // The exact-current H2 pair and projection descriptor were
                    // admitted together immediately above. This branch never
                    // sees a missing, late, partial, or N-1 H2 pair.
                    ReachFrameAccess reachFrame = BeginReachFrameAccess();
                    ReachVrRenderAccess& reachAccess = reachFrame.access;
                    bool reachImages = reachFrame.images;
                    const bool reachTitle = reachFrame.title;
                    const bool halo4Title = TitleAdapter_GetActiveTitle() == GameTitle::Halo4;
                    const bool halo4Images = !halo4Title || Halo4PreparedFrameImagesReady();
                    // Halo 2 has its own bounded source-learning validator;
                    // running the generic full-resolution check as well is a
                    // redundant synchronous GPU stall.
                    if (!reachTitle && !halo2Title && !ceTitle)
                        ValidateStereoImagesOnce();
                    if (halo2Title && halo2LiveExactPair)
                        ValidateHalo2EyePairPeriodic();
                    // Once per frame, but it only writes a line every two
                    // seconds; the same shape every other steady report here
                    // uses. All three titles reach this point.
                    ReportUploadResourceReuse();
                    uint32_t idx = 0;
                    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                    XrSwapchainImageWaitInfo swi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                    swi.timeout = 1000000000;
                    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                    bool reachStereoUploadComplete = !reachTitle;
                    const D3D11_TEXTURE2D_DESC& capturedEyeDesc = ceImages     ? ceLease.pair.descriptor
                                                                  : reachTitle ? g_reachCaptureDesc
                                                                               : g_eyeCacheDesc;
                    const bool theaterDirectSampling =
                        theaterPresentation && TheaterProjectionCanSampleDirectly(capturedEyeDesc);
                    theaterProjectionAttempted =
                        theaterPresentation && theaterProjection.viewCount == 2 &&
                        EnsureTheaterProjectionResources(g_stereoW, g_stereoH, !theaterDirectSampling);
                    // The backbuffer in hand is the frame the title just
                    // finished, subtitles included. Copy the band before the
                    // theatre composite reads it; a failure only clears the
                    // band and leaves the stereo image alone.
                    g_theaterSubtitleBandStartV = -1.0f;
                    if (theaterProjectionAttempted && g_config.cutscene_theater_subtitles &&
                        !CaptureTheaterSubtitleBand(backbuffer, bd))
                    {
                        g_theaterSubtitleBandStartV = -1.0f;
                    }
                    // A synchronous H2 frame without two exact-current-serial
                    // images owns no world swapchain operation at all. It is a
                    // clean frame drop, not an opportunity to upload N-1.
                    const bool attemptStereoAcquire = !halo2Title || halo2Images;
                    XrResult stereoAcquire = XR_ERROR_CALL_ORDER_INVALID;
                    if (attemptStereoAcquire)
                    {
                        stereoAcquire = xrAcquireSwapchainImage(g_stereoChain, &ai, &idx);
                    }
                    const bool exactStereoTransaction = reachTitle || halo4Title || halo2Title || ceTitle;
                    const WorldSwapchainMessages& worldMessages =
                        SelectWorldSwapchainMessages(ceTitle, halo2Title, halo4Title);
                    // An ordinary failed acquire owns no image and is therefore
                    // a frame-local miss. An exact-title positive non-success
                    // may nevertheless have acquired one (for example session
                    // loss pending), so enter the named runtime-recovery path
                    // rather than guessing at its call-order state. This keeps
                    // Reach's established acquire behavior byte-for-behavior.
                    const bool stereoAcquired =
                        attemptStereoAcquire &&
                        (exactStereoTransaction ? stereoAcquire == XR_SUCCESS : XR_SUCCEEDED(stereoAcquire));
                    if (attemptStereoAcquire && exactStereoTransaction && stereoAcquire != XR_SUCCESS &&
                        XR_SUCCEEDED(stereoAcquire))
                    {
                        (void)RequireExactSwapchainCompletion(stereoAcquire, worldMessages.acquire);
                    }
                    bool stereoWaitCompleted = false;
                    if (stereoAcquired)
                    {
                        const XrResult stereoWait = xrWaitSwapchainImage(g_stereoChain, &swi);
                        stereoWaitCompleted = exactStereoTransaction
                                                  ? RequireExactSwapchainCompletion(stereoWait, worldMessages.wait)
                                                  : XR_SUCCEEDED(stereoWait);
                    }
                    bool everyEyeUploaded = false;
                    bool stereoReleased = false;
                    if (stereoAcquired && stereoWaitCompleted)
                    {
                        // Bracket only the two-eye publish. Everything the GPU
                        // does between these two calls is what this mod adds on
                        // top of the game's own rendering.
                        IqTimerBeginFrame();
                        bool everyReachEyeUploaded = reachImages;
                        everyEyeUploaded = true;
                        // Which images this frame's eyes come from: a title-owned
                        // pair (CE lease, Reach render access) or the shared eye
                        // cache, which only counts once every title gate admits it.
                        WorldEyeSource eyeSource{};
                        if (ceImages)
                            eyeSource = {ceLease.pair.eyes, &ceLease.pair.descriptor, true, false};
                        else if (reachImages)
                            eyeSource = {reachAccess.eyes, &g_reachCaptureDesc, true, false};
                        else
                            eyeSource = {reachTitle ? nullptr : g_eyeCache, &g_eyeCacheDesc, false,
                                !reachTitle && (!halo4Title || halo4Images) && (!halo2Title || halo2Images)};
                        // A stereo batch needs both fresh sources before either
                        // target eye is presented. Eye-end normally completed
                        // this already; Reach publishes its retained sources
                        // here, so prepare both ahead of the presentation loop.
                        Microsoft::WRL::ComPtr<ID3D11CommandList> accessoryCommands[2];
                        HRESULT accessoryPairResult = S_FALSE;
                        for (uint32_t targetEye = 0; targetEye < 2; ++targetEye)
                        {
                            const uint32_t sourceEye =
                                theaterProjectionAttempted
                                    ? CutsceneTheaterImageIndex(targetEye, g_config.cutscene_theater_flip_depth)
                                    : targetEye;
                            const bool haveImage =
                                eyeSource.titleOwned || (eyeSource.cacheAdmitted && g_eyeHasImage[sourceEye]);
                            ID3D11Texture2D* source = eyeSource.eyes ? eyeSource.eyes[sourceEye] : nullptr;
                            D3D11_TEXTURE2D_DESC sourceDesc = *eyeSource.desc;
                            bool eyeUploaded = false;
                            bool sourceReconstructed = false;
                            if (haveImage && source)
                            {
                                // Select the reconstructed surface BEFORE selecting
                                // the presentation. Previously theatre paid for NGX
                                // at eye end, then displayed the low-resolution cache
                                // and discarded both completed outputs at EndFrame.
                                if (VR_DlssWantsEyeCamera() && !g_config.dlss_debug_view)
                                {
                                    const int dlssEye = static_cast<int>(sourceEye);
                                    if (g_dlssKicked[dlssEye].Ready(g_preparedFrame.serial) ||
                                        DlssKickEye(dlssEye, source, sourceDesc, g_stereoW, g_stereoH, nullptr))
                                    {
                                        DlssKickedEye reconstructed{};
                                        if (DlssTakeEyeOutput(dlssEye, reconstructed) && reconstructed.output)
                                        {
                                            source = reconstructed.output;
                                            sourceDesc = reconstructed.outputDesc;
                                            sourceReconstructed = true;
                                        }
                                    }
                                }
                                ID3D11RenderTargetView* targetRtv = GetStereoRtv(idx, targetEye);
                                if (theaterProjectionAttempted && targetRtv)
                                {
                                    if (theaterDirectSampling)
                                    {
                                        ID3D11ShaderResourceView* sourceSrv = AcquireSrcSrv(source, sourceDesc);
                                        eyeUploaded = CompositeTheaterProjectionEye(
                                            targetEye, sourceSrv, sourceDesc, false, g_views[targetEye], targetRtv);
                                    }
                                    else if (g_theaterResolved[targetEye] && g_theaterResolvedRtv[targetEye] &&
                                             g_theaterResolvedSrv[targetEye])
                                    {
                                        const bool resolved = BlitImageQuality(source, sourceDesc,
                                            g_theaterResolved[targetEye], g_stereoW, g_stereoH,
                                            g_theaterResolvedRtv[targetEye], sourceReconstructed);
                                        eyeUploaded =
                                            resolved &&
                                            CompositeTheaterProjectionEye(targetEye, g_theaterResolvedSrv[targetEye],
                                                g_theaterResolvedDesc, true, g_views[targetEye], targetRtv);
                                    }
                                }
                                else if (!theaterProjectionAttempted && targetRtv)
                                {
                                    // DLSS first when selected; any reason it
                                    // cannot run this eye falls back to the
                                    // mod's own resolve (fail-open per eye).
                                    if (VR_DlssWantsEyeCamera() && g_config.dlss_debug_view)
                                    {
                                        const int dlssEye = static_cast<int>(sourceEye);
                                        eyeUploaded = DlssFinishEye(dlssEye, g_stereoImages[idx], g_stereoW, g_stereoH,
                                                          targetRtv) ||
                                                      (DlssKickEye(dlssEye, source, sourceDesc, g_stereoW, g_stereoH,
                                                           targetRtv) &&
                                                          DlssFinishEye(dlssEye, g_stereoImages[idx], g_stereoW,
                                                              g_stereoH, targetRtv));
                                    }
                                    if (!eyeUploaded)
                                        eyeUploaded = BlitImageQuality(source, sourceDesc, g_stereoImages[idx],
                                            g_stereoW, g_stereoH, targetRtv, sourceReconstructed);
                                    // Optional accessory failure never changes eyeUploaded,
                                    // ownership, XR submission or native weapon rendering.
                                    const uint64_t now = GetTickCount64();
                                    const GameTitle title = TitleAdapter_GetActiveTitle();
                                    const uint32_t generation = TitleAdapter_GetGeneration(title);
                                    const uint64_t space = g_contactSpaceEpoch.load(std::memory_order_acquire);
                                    if (eyeUploaded && targetEye < projectionViews.size() &&
                                        weapon_accessory::Current(g_weaponAccessory, title, generation, space,
                                            VR_GetWeaponModelIdentity(title, generation, space, now), now,
                                            g_config.manual_reload,
                                            g_sessionState == XR_SESSION_STATE_FOCUSED &&
                                                TitleAdapter_GetRuntimeMode() == RuntimeMode::Gameplay &&
                                                !Menu_IsOpen() && !VR_IsPausePresentation() &&
                                                !VR_IsPausePresentationTarget() &&
                                                !SecondaryWeaponPresentationActive()))
                                    {
                                        if (g_weaponAccessory.model == &weapon_model::kGenericReloadModel &&
                                            !g_config.weapon_unknown_reload_visual)
                                            g_weaponAccessory = {};
                                        const auto& view = projectionViews[targetEye];
                                        const auto& p = view.pose.position;
                                        const auto& q = view.pose.orientation;
                                        weapon_accessory::Constants constants{};
                                        if (weapon_accessory::Projection(g_weaponAccessory,
                                                {{p.x, p.y, p.z}, {q.x, q.y, q.z, q.w}}, view.fov.angleLeft,
                                                view.fov.angleRight, view.fov.angleDown, view.fov.angleUp, constants))
                                        {
                                            const auto& rect = view.subImage.imageRect;
                                            const D3D11_VIEWPORT viewport{static_cast<float>(rect.offset.x),
                                                static_cast<float>(rect.offset.y),
                                                static_cast<float>(rect.extent.width),
                                                static_cast<float>(rect.extent.height), 0, 1};
                                            const HRESULT accessoryResult = g_weaponAccessoryRenderer.Draw(g_device,
                                                g_context, targetRtv, g_stereoW, g_stereoH, viewport, g_weaponAccessory,
                                                constants, &accessoryCommands[targetEye]);
                                            if (!FAILED(accessoryPairResult))
                                                accessoryPairResult = accessoryResult;
                                        }
                                    }
                                }
                            }
                            everyEyeUploaded = everyEyeUploaded && eyeUploaded;
                            if (reachTitle)
                                everyReachEyeUploaded = everyReachEyeUploaded && eyeUploaded;
                        }
                        // Commit both optional eyes together, only after all
                        // world copies and accessory draws were prepared.
                        // A failed accessory leaves the complete world pair intact.
                        if (everyEyeUploaded && accessoryCommands[0] && accessoryCommands[1])
                        {
                            g_context->ExecuteCommandList(accessoryCommands[0].Get(), TRUE);
                            g_context->ExecuteCommandList(accessoryCommands[1].Get(), TRUE);
                        }
                        if (accessoryPairResult != S_FALSE)
                            g_weaponAccessoryResult.store(accessoryPairResult, std::memory_order_release);
                        IqTimerEndFrame();
                        ReportIqGpuTiming();
                        theaterProjectionReady = theaterProjectionAttempted && everyEyeUploaded;
                        const XrResult stereoRelease = xrReleaseSwapchainImage(g_stereoChain, &ri);
                        stereoReleased = exactStereoTransaction
                                             ? RequireExactSwapchainCompletion(stereoRelease, worldMessages.release)
                                             : XR_SUCCEEDED(stereoRelease);
                        if (theaterProjectionAttempted && !theaterProjectionReady)
                        {
                            static uint64_t lastProjectionSkipLogMs = 0;
                            const uint64_t nowMs = GetTickCount64();
                            if (nowMs - lastProjectionSkipLogMs >= 2000)
                            {
                                lastProjectionSkipLogMs = nowMs;
                                LOG("cutscene theatre frame skipped: the "
                                    "headset-agnostic projection did not "
                                    "complete; immersive VR remains armed");
                            }
                        }
                        if (reachTitle)
                            reachStereoUploadComplete = everyReachEyeUploaded && stereoReleased &&
                                                        (!theaterProjectionAttempted || theaterProjectionReady);
                    }
                    const bool halo4StereoUploadComplete =
                        !halo4Title || Halo4XrPairUploadComplete(
                                           stereoAcquired, stereoWaitCompleted, everyEyeUploaded, stereoReleased);
                    const bool halo2StereoUploadComplete =
                        !halo2Title || (stereoAcquired && stereoWaitCompleted && everyEyeUploaded && stereoReleased);

                    // A Reach frame that cannot expose a complete stereo pair
                    // is SKIPPED, not fatal. Revoke this frame's copied eye
                    // serials so nothing stale or partial is queued, then let
                    // the next frame try again.
                    //
                    // This used to call Game_RejectReachAuthoredReticle, which
                    // disarms the core and unhooks the mod. projection.viewCount
                    // is only set to 2 once Game_GetRenderHalfFovs matches the
                    // prepared frame serial, and Reach publishes those FOVs
                    // while its eyes render -- so an ordinary ordering gap on a
                    // single frame permanently destroyed Reach VR. That is the
                    // teardown behind every unexplained "stereo OFF" on
                    // 2026-07-26. Halo 3 and ODST skip such a frame and recover.
                    if (reachTitle && reachImages && (!reachStereoUploadComplete || projection.viewCount != 2))
                        DropIncompleteReachFrame(reachStereoUploadComplete);
                    if (halo4Title && halo4Images && (!halo4StereoUploadComplete || projection.viewCount != 2))
                        DropIncompleteHalo4Frame(halo4StereoUploadComplete);
                    if (halo2Title && halo2Images && (!halo2StereoUploadComplete || projection.viewCount != 2))
                        DropIncompleteHalo2Frame(halo2Generation, halo2StereoUploadComplete);
                    const bool projectionImagesReady =
                        ceTitle
                            ? ceImages && stereoAcquired && stereoWaitCompleted && everyEyeUploaded && stereoReleased
                        : reachTitle ? reachImages && reachStereoUploadComplete
                        : halo2Title ? halo2Images && halo2StereoUploadComplete && g_eyeHasImage[0] && g_eyeHasImage[1]
                        : halo4Title ? halo4Images && halo4StereoUploadComplete && g_eyeHasImage[0] && g_eyeHasImage[1]
                                     : g_eyeHasImage[0] && g_eyeHasImage[1];
                    if (projectionImagesReady && projection.viewCount == 2)
                    {
                        // Use the same predicted OpenXR controller pose captured
                        // for this displayed frame. Routing the quad through
                        // Halo's virtual-stick aim introduced visible catch-up
                        // lag and placed its ray origin at the player's head.
                        // Start from the same aim pose as bullet steering, then
                        // optionally stabilize ONLY the displayed reticle. At
                        // 0% it is the exact current bullet ray; higher values
                        // deliberately trade visual reticle response for calm.
                        // The solve-time support receipt rides along so the
                        // presented-ray publication is qualified from the
                        // producing solve, never from a live durable re-read.
                        float aimQ[4], aimP[3];
                        VrAimSupportReceipt aimSupportReceipt{};
                        const bool haveAim = VR_GetAimPoseWithSupportProvenance(aimQ, aimP, aimSupportReceipt);
                        const bool authoredReticleThisFrame =
                            g_authoredReticleReady && g_authoredReticleSerial == g_preparedFrame.serial;
                        // Reachability proof for the whole block below. If the
                        // quad heartbeat is silent while THIS is alive, the
                        // difference is the `reachTitle` test; if both are
                        // silent, the compositor is not reaching Reach's
                        // authored path at all and every conclusion drawn from
                        // that silence is void.
                        {
                            static uint64_t lastAliveMs = 0;
                            const uint64_t aliveNow = GetTickCount64();
                            if (aliveNow - lastAliveMs >= 2000)
                            {
                                lastAliveMs = aliveNow;
                                LOG("REACHHUD: compositor alive "
                                    "(reachTitle=%d reachImages=%d "
                                    "ownsReach=%d authoredThisFrame=%d "
                                    "heldArt=%d key=%016llX color=%X)",
                                    reachTitle ? 1 : 0, reachImages ? 1 : 0, Game_OwnsReachAuthoredReticle() ? 1 : 0,
                                    authoredReticleThisFrame ? 1 : 0, g_reticleContainsAuthored ? 1 : 0,
                                    static_cast<unsigned long long>(Game_GetAuthoredCrosshairKey()),
                                    Game_GetAuthoredCrosshairColorState());
                            }
                        }
                        const bool reachAuthoredReticleThisFrame =
                            reachTitle && reachImages && authoredReticleThisFrame && Game_OwnsReachAuthoredReticle();
                        // Reach reaches the shared aim-ray quad through the
                        // PROCEDURAL path only. The authored branch below --
                        // and the transaction rejection inside its failure
                        // handler -- stays Halo 3 / ODST only, so a crosshair
                        // problem can never disarm Reach's camera core.
                        // ODST does not consult the capability system for its
                        // reticle either -- it uses a direct ownership check
                        // (Game_IsCameraOnlyBringup), which is why it worked
                        // immediately. Reach gets the same treatment: ask the
                        // Reach core directly whether it owns this frame.
                        // Game_HasTitleCapability keeps returning false for
                        // Reach because the shared runtime snapshot spends much
                        // of its time "pending", which reports zero
                        // capabilities by design.
                        const bool reticleTitleAdmitted =
                            Game_HasTitleCapability(TitleCapability_ControllerAim) ||
                            (ceImages && HaloCEFirstPerson_AimArmed() && HaloCEHud_CapturedCrosshair()) ||
                            Game_OwnsReachAuthoredReticle();
                        const bool titlePositionsNativeReticle = Game_TitlePositionsNativeCrosshair();
                        const bool reticleUploadAdmitted = reticleTitleAdmitted && !titlePositionsNativeReticle &&
                                                           g_config.crosshair && !VR_HudHidden() && haveAim &&
                                                           EnsureReticleChain();
                        // Reach uploads its captured widget art exactly like
                        // Halo 3 and ODST. The previous "!reachTitle" excluded
                        // Reach from ever publishing the art it captures, which
                        // is why its crosshair stayed procedural even once the
                        // capture worked. The exclusion dated from when the
                        // Reach CHUD hook could not run at all.
                        // Re-uploading the authored reticle costs an OpenXR
                        // swapchain acquire, a BLOCKING wait, a copy and a
                        // release - every frame, on a second swapchain, on top
                        // of Reach's own stereo swapchain. Reach's crosshair art
                        // is static between weapon and state changes, so that
                        // work is almost always spent re-publishing an image
                        // identical to the one already in the swapchain. Skip it
                        // while the captured art's identity is unchanged; the
                        // released image stays valid and the quad keeps showing
                        // it. The refresh policy below is title-specific.
                        static AuthoredReticleRefreshState s_refreshState{};
                        static uint64_t s_uploadsDone = 0;
                        static uint64_t s_uploadsSkipped = 0;
                        static uint64_t s_lastUploadLogMs = 0;
                        // Every authored title would pay a blocking swapchain
                        // upload here unless its refresh policy proves the held
                        // image is still current.
                        const bool titleCapturesArt = Game_TitleCapturesAuthoredCrosshair();
                        const uint64_t authoredCrosshairKey = titleCapturesArt ? Game_GetAuthoredCrosshairKey() : 0;
                        const uint32_t authoredColorState =
                            titleCapturesArt ? Game_GetAuthoredCrosshairColorState() : 0;
                        const uint32_t authoredCrosshairDraws =
                            titleCapturesArt ? Game_GetAuthoredCrosshairDrawCount() : 0;
                        const GameTitle reticleTitle = TitleAdapter_GetActiveTitle();
                        const TitleReticleTraits* reticleTraits = FindTitleReticleTraits(reticleTitle);
                        const AuthoredReticleRefreshPolicy refreshPolicy = TitleReticleRefreshPolicy(reticleTraits);
                        // Halo 3 and Halo 4's authored crosshairs animate - they
                        // kick on fire and carry target/hit colour state - and
                        // none of that necessarily changes WHICH pieces drew.
                        // Sampling and publishing on the same bounded cadence
                        // keeps the animation live without paying the blocking
                        // swapchain upload every frame.
                        // With crosshair_animation_frames=0, Halo 4 keeps a
                        // slow 30-frame sample so a fixed-key weapon swap can
                        // replace held art; it does not animate every 6 frames.
                        // ODST's proven identity key also folds its native
                        // alternate-path state, so it publishes only when that
                        // key changes and has no steady blocking upload. Reach
                        // retains its working bounded animation cadence.
                        // Scope zoom is a separate pipeline and is not a trigger.
                        const uint64_t reticleOwnerEpoch = g_reticleOwnerEpoch.load(std::memory_order_acquire);
                        const bool shouldUploadAuthoredReticle = ShouldUploadAuthoredReticle(refreshPolicy,
                            authoredReticleThisFrame, reticleUploadAdmitted, authoredCrosshairKey, authoredColorState,
                            authoredCrosshairDraws, g_preparedFrame.serial, reticleOwnerEpoch, s_refreshState);
                        const bool authoredArtAlreadyPublished =
                            authoredReticleThisFrame && reticleUploadAdmitted && !shouldUploadAuthoredReticle;
                        const bool reticleProbePending = g_authoredReticleProbePending;
                        // A different crosshair, not the same one redrawn: the
                        // coverage bar must not judge a new weapon's reticle
                        // against the old one's ink. Reach is excluded because
                        // it folds its key per DRAWN widget, so its documented
                        // thinning case also changes the key and still needs
                        // the coverage guard behind it.
                        const bool authoredIdentityChanged =
                            authoredCrosshairKey != 0 && authoredCrosshairKey != s_refreshState.lastPublishedKey &&
                            !(reticleTraits && reticleTraits->crosshairKeyPerDrawnWidget);
                        bool authoredUploadFailed = false;
                        if ((shouldUploadAuthoredReticle || reticleProbePending) &&
                            UploadAuthoredReticle(false, authoredIdentityChanged))
                        {
                            if (authoredCrosshairKey != 0)
                            {
                                MarkAuthoredReticleUploaded(s_refreshState, authoredCrosshairKey, authoredColorState,
                                    authoredCrosshairDraws, g_preparedFrame.serial);
                            }
                            ++s_uploadsDone;
                        }
                        else if ((shouldUploadAuthoredReticle || reticleProbePending) && !g_authoredReticleHeldBlank)
                        {
                            // Never expose stale or undefined swapchain
                            // contents: repaint the chain, exactly as the
                            // accepted Halo 3 / ODST path does. A crosshair
                            // upload failure must not disarm Reach's camera
                            // core or drop its world projection - failure
                            // isolation, per AGENTS.md. It is reported loudly
                            // rather than passing silently.
                            g_authoredReticleReady = false;
                            EnsureReticleChain();
                            static std::atomic<bool> loggedUploadLoss{false};
                            if (!loggedUploadLoss.exchange(true, std::memory_order_relaxed))
                                LOG("authored reticle upload FAILED; the VR "
                                    "reticle is showing its procedural art "
                                    "this frame instead of the game's widget");
                        }
                        // Halo 3 reports the same counters as Reach: a headset
                        // report about the animated crosshair is only
                        // actionable next to the rate it actually published at.
                        // ODST is included too: its ink has never been measured
                        // in a headset session, and a stale-crosshair report is
                        // only actionable next to the coverage the guard saw.
                        if (reachTitle || reticleTraits)
                        {
                            if (authoredArtAlreadyPublished)
                                ++s_uploadsSkipped;
                            const uint64_t nowMs = GetTickCount64();
                            if (nowMs - s_lastUploadLogMs >= 2000)
                            {
                                s_lastUploadLogMs = nowMs;
                                // `pieces` is what makes a "the crosshair
                                // vanished" report actionable: held < pieces
                                // means the capture thinned out and the
                                // completeness guard held the good art.
                                // `art` is the measured maximum alpha of the
                                // capture and `blankHeld` counts the publishes
                                // refused because it had none. Together they
                                // say, without another round trip, whether the
                                // engine stopped painting a visible crosshair
                                // and whether the guard caught it.
                                LOG("%s reticle upload: %llu uploaded, %llu "
                                    "skipped in the last window (key %llX, "
                                    "pieces %u, held %u, art %u, blankHeld %u)",
                                    reachTitle ? "Reach" : reticleTraits->crosshairLogName,
                                    static_cast<unsigned long long>(s_uploadsDone),
                                    static_cast<unsigned long long>(s_uploadsSkipped),
                                    static_cast<unsigned long long>(authoredCrosshairKey), authoredCrosshairDraws,
                                    s_refreshState.lastPublishedDraws, g_authoredReticleLastCoverage,
                                    g_authoredReticleBlankHeld);
                                g_authoredReticleBlankHeld = 0;
                                s_uploadsDone = 0;
                                s_uploadsSkipped = 0;
                            }
                        }
                        const bool liveReachOwnerAfterUpload = !reachTitle || Game_OwnsReachAuthoredReticle();
                        if (reachTitle && !liveReachOwnerAfterUpload)
                        {
                            // A title/lifecycle transition can complete while
                            // OpenXR waits for the authored upload. Revoke the
                            // pair again after that wait and age out any uploaded
                            // Reach art so the next title clears it before use.
                            g_reachEyeSerial[0].store(0, std::memory_order_release);
                            g_reachEyeSerial[1].store(0, std::memory_order_release);
                            if (reachAuthoredReticleThisFrame)
                            {
                                g_authoredReticleReady = false;
                                g_authoredReticleSerial = 0;
                            }
                        }
                        const bool reachProjectionAdmitted = ReachCanSubmitCompleteProjection(
                            reachTitle, reachStereoUploadComplete, authoredUploadFailed, liveReachOwnerAfterUpload);
                        const bool reticleOwnerAdmitted = reticleTitleAdmitted &&
                                                          // Reach shows its crosshair only alongside its own
                                                          // admitted world projection.
                                                          (!reachTitle || reachProjectionAdmitted);
                        if (reachProjectionAdmitted)
                        {
                            if (theaterPresentation)
                            {
                                if (theaterProjectionReady)
                                {
                                    layers.push_back(
                                        reinterpret_cast<XrCompositionLayerBaseHeader*>(&theaterProjection));
                                }
                                else if (!theaterProjectionAttempted)
                                {
                                    // Core-spec fallback if the private
                                    // projection resources could not be made.
                                    // It preserves the previously accepted
                                    // SteamVR path and names the degradation.
                                    for (uint32_t eye = 0; eye < 2; ++eye)
                                    {
                                        theaterQuads[eye] = MakeQuad(g_stereoChain, static_cast<int32_t>(g_stereoW),
                                            static_cast<int32_t>(g_stereoH), g_config.cutscene_theater_width_m,
                                            g_config.cutscene_theater_distance_m, 0.0f, 0, false);
                                        theaterQuads[eye].eyeVisibility =
                                            eye == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
                                        theaterQuads[eye].subImage.imageArrayIndex =
                                            CutsceneTheaterImageIndex(eye, g_config.cutscene_theater_flip_depth);
                                        theaterQuads[eye].size.height = CutsceneTheaterHeightFromAspect(
                                            g_config.cutscene_theater_width_m, g_cutsceneTheaterAuthoredAspect);
                                        // This path has no shader of ours, so
                                        // the cine bars become the sub-rect the
                                        // quad samples. Same retained window,
                                        // same picture scale.
                                        const CutsceneTheaterMatte quadMatte = ComputeCutsceneTheaterMatte(
                                            g_cutsceneTheaterAuthoredAspect, g_config.cutscene_theater_matte_aspect,
                                            g_config.cutscene_theater_matte_offset);
                                        if (quadMatte.active)
                                        {
                                            const int32_t top =
                                                static_cast<int32_t>(quadMatte.vMin * static_cast<float>(g_stereoH));
                                            int32_t height = static_cast<int32_t>(
                                                (quadMatte.vMax - quadMatte.vMin) * static_cast<float>(g_stereoH));
                                            if (height > 0 && top + height <= static_cast<int32_t>(g_stereoH))
                                            {
                                                theaterQuads[eye].subImage.imageRect.offset.y = top;
                                                theaterQuads[eye].subImage.imageRect.extent.height = height;
                                                theaterQuads[eye].size.height =
                                                    CutsceneTheaterHeightFromAspect(g_config.cutscene_theater_width_m,
                                                        g_config.cutscene_theater_matte_aspect);
                                            }
                                        }
                                        layers.push_back(
                                            reinterpret_cast<XrCompositionLayerBaseHeader*>(&theaterQuads[eye]));
                                    }
                                    static bool loggedFallback = false;
                                    if (!loggedFallback)
                                    {
                                        loggedFallback = true;
                                        LOG("cutscene theatre: native-FOV projection "
                                            "unavailable; using the core "
                                            "eye-selective quad fallback");
                                    }
                                }
                            }
                            else
                            {
                                layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&projection));
                                if (halo2Title)
                                    halo2ProjectionQueued = true;
                                if (halo2Title && halo2LiveExactPair && projectionViews.size() >= 2)
                                    RememberHalo2Pair(halo2Generation, projectionViews, halo2HalfX, halo2HalfY);
                                if (halo4Title)
                                    halo4ProjectionQueued = true;
                            }
                        }
                        const bool reticleChainAdmitted =
                            reticleUploadAdmitted &&
                            AuthoredReticleLayerHasContent(
                                titleCapturesArt && !TitleReticleNeedsProceduralArt(reticleTitle, titleCapturesArt),
                                g_reticleContainsAuthored);
                        XrPosef secondaryAim{};
                        float secondaryQ[4]{}, secondaryP[3]{};
                        const bool secondaryTracked =
                            dual_reticle::Supported(reticleTitle) && VR_GetLeftControllerPose(secondaryQ, secondaryP) &&
                            Halo2BuildMirroredLeftAimOrientation(secondaryQ, g_config.gun_yaw_deg,
                                g_config.gun_pitch_deg, g_config.gun_roll_deg, secondaryQ);
                        secondaryAim = {{secondaryQ[0], secondaryQ[1], secondaryQ[2], secondaryQ[3]},
                            {secondaryP[0], secondaryP[1], secondaryP[2]}};
                        const bool dualReticles =
                            dual_reticle::Eligible(reticleTitle, SecondaryWeaponPresentationActive(), haveAim,
                                secondaryTracked, g_config.crosshair, VR_HudHidden(), theaterPresentation) &&
                            PrepareDualReticleImage();
                        const uint64_t reticleSpace = g_contactSpaceEpoch.load(std::memory_order_acquire);
                        const uint32_t reticleGeneration = TitleAdapter_GetGeneration(reticleTitle);
                        if (!dualReticles || g_secondaryReticleSpace != reticleSpace ||
                            g_secondaryReticleGeneration != reticleGeneration ||
                            g_secondaryReticleTitle != reticleTitle)
                            g_secondaryReticlePoseValid = false;
                        g_secondaryReticleSpace = reticleSpace;
                        g_secondaryReticleGeneration = reticleGeneration;
                        g_secondaryReticleTitle = reticleTitle;
                        const bool reticleQuadSubmitted =
                            reticleOwnerAdmitted && !titlePositionsNativeReticle && g_config.crosshair &&
                            !VR_HudHidden() && TitleAllowsReticleQuad(reticleTitle) && haveAim &&
                            (reticleChainAdmitted || dualReticles) && !theaterPresentation;
                        if (ceTitle)
                            ReportCeCrosshairPresentation(reticleQuadSubmitted, haveAim, titleCapturesArt);
                        // The 2026-07-27 objective blackout is proven NOT to be
                        // the capture, the class resolution, or the redirect:
                        // during it Reach draws every other HUD widget at its
                        // normal rate and simply emits no crosshair, while the
                        // authored art stays held in the swapchain and the
                        // repaint guard leaves it alone. So the art exists and
                        // the quad still stops reaching the eye - which means
                        // one of these gates is refusing. Name it rather than
                        // guess a third time. Throttled state-change only, on
                        // the compositor thread that already logs status lines.
                        if (reachTitle)
                            ReportReachReticleGate(reticleQuadSubmitted, reticleOwnerAdmitted, reachProjectionAdmitted,
                                haveAim, reticleChainAdmitted, authoredReticleThisFrame);
                        if (reticleQuadSubmitted)
                        {
                            XrPosef rawAim{{aimQ[0], aimQ[1], aimQ[2], aimQ[3]}, {aimP[0], aimP[1], aimP[2]}};
                            const float smoothing = std::clamp(g_config.aim_stabilization, 0.0f, 0.95f);
                            // Persistent support grip: the smoothing
                            // history reseeds across an owner/trust or
                            // relationship-epoch discontinuity, so a denied
                            // invocation (or a changed owner epoch) can never
                            // blend previous support geometry into the new ray.
                            // PG off reports an all-zero receipt, which is
                            // continuous by construction: the expression below
                            // is then byte-identical to the base smoothing.
                            //
                            // For a title that publishes a title-native
                            // invocation receipt (Reach), the solve-time receipt
                            // is not permission on its own. The frame's own FP
                            // invocation must also have proven the same owner and
                            // relationship epoch for this prepared serial; a
                            // missing/mismatched receipt makes the ray
                            // support-untrusted, which reseeds the smoothing
                            // history here and is refused by every consumer.
                            const bool invocationReceiptOk =
                                !g_config.persistent_support_grip ||
                                VR_SupportInvocationReceiptTrustedForReticle(reticleTitle, reticleGeneration,
                                    aimSupportReceipt.solveSerial, aimSupportReceipt.supportEpoch,
                                    aimSupportReceipt.supportTrusted);
                            const bool supportTrusted = aimSupportReceipt.supportTrusted && invocationReceiptOk;
                            const bool supportOwnerSafe = !g_config.persistent_support_grip ||
                                                          !aimSupportReceipt.relationshipEngaged || supportTrusted;
                            const bool supportDiscontinuity = support_grip::ReticleSmoothingDiscontinuity(
                                supportOwnerSafe, aimSupportReceipt.supportEpoch, g_reticleSupportEpoch);
                            g_reticleAimPose = g_reticleAimPoseValid && smoothing > 0.0f && !supportDiscontinuity
                                                   ? SmoothTrackedPose(rawAim, g_reticleAimPose, smoothing)
                                                   : rawAim;
                            g_reticleAimPoseValid = true;
                            // The receipt travels with the smoothed result.
                            g_reticleSupportEpoch = aimSupportReceipt.supportEpoch;
                            PublishPresentedReticleAimPose(&g_reticleAimPose, aimSupportReceipt.supportEpoch,
                                supportTrusted, aimSupportReceipt.solveSerial);
                            const XrVector3f aimRay = Rotate(g_reticleAimPose.orientation, {0.0f, 0.0f, -1.0f});
                            float aimDir[3] = {aimRay.x, aimRay.y, aimRay.z};
                            weapon_muzzle::Ray committedPrimary{}, committedSecondary{};
                            VrContactTrackingSnapshot reticleTracking{};
                            bool primaryBarrel = false, secondaryBarrel = false, primaryHeld = false;
                            if (g_config.gun_barrel_aim && dual_reticle::BarrelRayTitle(reticleTitle) &&
                                VR_GetContactTrackingSnapshot(reticleTracking))
                            {
                                for (unsigned slot = 0; slot < 2; ++slot)
                                {
                                    dual_reticle::ProjectedRay projected{};
                                    const uint64_t reticleNow = GetTickCount64();
                                    if (!g_weaponReticleRays[static_cast<unsigned>(reticleTitle)][slot].Read(projected))
                                        continue;
                                    const bool current = dual_reticle::Current(projected, reticleTitle,
                                        reticleGeneration, reticleTracking.referenceEpoch, reticleTracking.serial,
                                        reticleTracking.timeNs, reticleNow, slot, g_config.left_handed);
                                    const bool recent =
                                        !current &&
                                        dual_reticle::Recent(projected, reticleTitle, reticleGeneration,
                                            reticleTracking.referenceEpoch, reticleNow, slot, g_config.left_handed);
                                    if (recent)
                                        ++g_reticleRecentAccepts; // frames the hold carried
                                    if (current || recent)
                                    {
                                        if (slot == 0)
                                        {
                                            committedPrimary = projected.tracking;
                                            primaryBarrel = true;
                                            primaryHeld = recent;
                                        }
                                        else
                                        {
                                            committedSecondary = projected.tracking;
                                            secondaryBarrel = true;
                                        }
                                    }
                                }
                            }
                            // Once a second, how many frames the 50 ms hold carried the
                            // barrel line (the missed-commit rate the old flip pairs showed).
                            // Only when non-zero, so titles that never miss stay quiet.
                            {
                                static uint64_t reticleRecentLogMs = 0;
                                const uint64_t now = GetTickCount64();
                                if (now - reticleRecentLogMs >= 1000)
                                {
                                    reticleRecentLogMs = now;
                                    if (g_reticleRecentAccepts)
                                    {
                                        LOG("reticle recent-ray accepts: %u in the last second (title %d)",
                                            g_reticleRecentAccepts, static_cast<int>(reticleTitle));
                                        g_reticleRecentAccepts = 0;
                                    }
                                }
                            }
                            if (primaryBarrel)
                                std::copy(std::begin(committedPrimary.direction), std::end(committedPrimary.direction),
                                    aimDir);
                            // A vehicle seat bounds the weapon to a cone around
                            // the hull, so a hand outside it asks for an angle
                            // the engine will never reach. While that limit is
                            // holding, show where the gun ACTUALLY points: the
                            // reticle is meant to be the truth, and this is the
                            // one case where the hand ray is not.
                            float clampedAim[3];
                            if (Game_GetClampedAimDirection(clampedAim))
                            {
                                aimDir[0] = clampedAim[0];
                                aimDir[1] = clampedAim[1];
                                aimDir[2] = clampedAim[2];
                            }
                            const float dist = g_config.crosshair_distance_m;
                            const float yaw = atan2f(aimDir[0], -aimDir[2]);
                            const float sp = fminf(fmaxf(aimDir[1], -1.0f), 1.0f);
                            const float pitch = asinf(sp);
                            // Orientation whose local -Z runs along the ray
                            // (quad faces the player): global yaw about +Y
                            // (angle -yaw, same convention as TryRecenter),
                            // then local pitch about +X.
                            const XrQuaternionf qy{0, sinf(-yaw * 0.5f), 0, cosf(-yaw * 0.5f)};
                            const XrQuaternionf qp{sinf(pitch * 0.5f), 0, 0, cosf(pitch * 0.5f)};
                            const XrQuaternionf q{qy.w * qp.x + qy.x * qp.w + qy.y * qp.z - qy.z * qp.y,
                                qy.w * qp.y - qy.x * qp.z + qy.y * qp.w + qy.z * qp.x,
                                qy.w * qp.z + qy.x * qp.y - qy.y * qp.x + qy.z * qp.w,
                                qy.w * qp.w - qy.x * qp.x - qy.y * qp.y - qy.z * qp.z};
                            reticleQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
                            reticleQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                                                     XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                            reticleQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                            reticleQuad.space = g_localSpace;
                            reticleQuad.subImage.swapchain = dualReticles ? g_dualReticleChain : g_reticleChain;
                            reticleQuad.subImage.imageRect = {{0, 0}, {(int32_t)kReticleSize, (int32_t)kReticleSize}};
                            reticleQuad.subImage.imageArrayIndex = 0;
                            reticleQuad.pose.orientation = q;
                            XrVector3f reticleOrigin = g_reticleAimPose.position;
                            if (primaryBarrel)
                                reticleOrigin = {committedPrimary.position[0], committedPrimary.position[1],
                                    committedPrimary.position[2]};
                            reticleQuad.pose.position = {reticleOrigin.x + aimDir[0] * dist,
                                reticleOrigin.y + aimDir[1] * dist, reticleOrigin.z + aimDir[2] * dist};
                            {
                                // Publish the drawn ray for the shot lines.
                                auto& last = g_lastReticleRay;
                                last.sequence.fetch_add(1, std::memory_order_acq_rel);
                                const float o[3]{reticleOrigin.x, reticleOrigin.y, reticleOrigin.z};
                                for (int i = 0; i < 3; ++i)
                                {
                                    last.origin[i].store(o[i], std::memory_order_relaxed);
                                    last.direction[i].store(aimDir[i], std::memory_order_relaxed);
                                }
                                last.barrel.store(primaryBarrel, std::memory_order_relaxed);
                                last.held.store(primaryHeld, std::memory_order_relaxed);
                                last.ms.store(GetTickCount64(), std::memory_order_relaxed);
                                last.sequence.fetch_add(1, std::memory_order_release);
                            }
                            const float w = 2.0f * dist * tanf(g_config.crosshair_size_deg * 0.5f * 0.0174533f);
                            // Two independent glyphs stay small and thin (dual_reticle_scale).
                            const float quadSize = dualReticles ? ConfigDualReticleSize(g_config, w) : w;
                            reticleQuad.size = {quadSize, quadSize};
                            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&reticleQuad));
                            if (dualReticles)
                            {
                                g_secondaryReticlePose =
                                    g_secondaryReticlePoseValid && smoothing > 0
                                        ? SmoothTrackedPose(secondaryAim, g_secondaryReticlePose, smoothing)
                                        : secondaryAim;
                                g_secondaryReticlePoseValid = true;
                                XrVector3f direction = Rotate(g_secondaryReticlePose.orientation, {0, 0, -1});
                                XrVector3f origin = g_secondaryReticlePose.position;
                                if (secondaryBarrel)
                                {
                                    direction = {committedSecondary.direction[0], committedSecondary.direction[1],
                                        committedSecondary.direction[2]};
                                    origin = {committedSecondary.position[0], committedSecondary.position[1],
                                        committedSecondary.position[2]};
                                }
                                const float secondaryYaw = atan2f(direction.x, -direction.z);
                                const float secondaryPitch = asinf(std::clamp(direction.y, -1.f, 1.f));
                                const XrQuaternionf sy{0, sinf(-secondaryYaw * .5f), 0, cosf(-secondaryYaw * .5f)};
                                const XrQuaternionf pitchQ{
                                    sinf(secondaryPitch * .5f), 0, 0, cosf(secondaryPitch * .5f)};
                                secondaryReticleQuad = reticleQuad;
                                secondaryReticleQuad.pose.orientation = {
                                    sy.w * pitchQ.x, sy.y * pitchQ.w, -sy.y * pitchQ.x, sy.w * pitchQ.w};
                                secondaryReticleQuad.pose.position = {origin.x + direction.x * dist,
                                    origin.y + direction.y * dist, origin.z + direction.z * dist};
                                layers.push_back(
                                    reinterpret_cast<XrCompositionLayerBaseHeader*>(&secondaryReticleQuad));
                            }
                            // Which line the primary reticle is on, once per change and title.
                            static int loggedBarrelTitle = -1;
                            static bool loggedBarrel = false;
                            if (loggedBarrelTitle != static_cast<int>(reticleTitle) || loggedBarrel != primaryBarrel)
                            {
                                loggedBarrelTitle = static_cast<int>(reticleTitle);
                                loggedBarrel = primaryBarrel;
                                LOG("reticle: %s (title %d)",
                                    primaryBarrel ? "on the barrel receipt ray" : "on the hand aim ray",
                                    loggedBarrelTitle);
                            }
                            static bool loggedDual = false;
                            if (loggedDual != dualReticles)
                            {
                                loggedDual = dualReticles;
                                LOG("Dual-weapon crosshairs: %s; single-glyph pair follows primary/secondary weapon roles; barrel receipts=%d/%d",
                                    dualReticles ? "two independent symbols" : "ordinary authored reticle",
                                    primaryBarrel, secondaryBarrel);
                            }
                        }
                        else
                        {
                            // Never blend from a stale pose after tracking or
                            // the crosshair is restored.
                            g_reticleAimPoseValid = false;
                            g_reticleSupportEpoch = 0;
                            g_secondaryReticlePoseValid = false;
                            PublishPresentedReticleAimPose(nullptr, 0, false, 0);
                        }

                        {
                            // Eye close behind the lens and lined up with the barrel?
                            bool sightValid = false;
                            float eye[3]{}, lens[3]{}, barrel[3]{};
                            float headQ[4]{}, headP[3]{};
                            bool ceOpticNoLens = false;
                            // Which zoom tier this weapon uses (lens or visor).
                            {
                                const GameTitle zoomTitle = TitleAdapter_GetActiveTitle();
                                const uint32_t zoomGeneration = TitleAdapter_GetGeneration(zoomTitle);
                                const uint64_t zoomWeapon = VR_ZoomWeaponIdentity(zoomTitle, zoomGeneration);
                                // The weapon_profile row decides: Visor weapons open the visor
                                // window, Optic ones use the lens; unknown weapons keep the lens.
                                const weapon_profile::ZoomKind rowZoom =
                                    zoomWeapon ? weapon_profile::ZoomKindFor(zoomTitle, zoomWeapon)
                                               : weapon_profile::ZoomKind::None;
                                const visor_zoom::Kind kind = visor_zoom::KindOf(
                                    rowZoom == weapon_profile::ZoomKind::Visor   ? visor_zoom::ProfileZoom::Visor
                                    : rowZoom == weapon_profile::ZoomKind::Optic ? visor_zoom::ProfileZoom::Optic
                                                                                 : visor_zoom::ProfileZoom::None);
                                // CE: a weapon with a sight (Optic) gets the Classic
                                // look-through scope when that pass can run; every
                                // other CE zoom is the visor crop.
                                const bool ceOptic = zoomTitle == GameTitle::HaloCE && g_config.ce_classic_scope &&
                                                     HaloCE_ClassicScopeReady() && zoomWeapon &&
                                                     weapon_profile::ZoomKindFor(GameTitle::HaloCE, zoomWeapon) ==
                                                         weapon_profile::ZoomKind::Optic;
                                // Found in headset testing (Anniversary: a head-locked visor picture stuck in the
                                // centre): a CE lens weapon without the look-through pass gets no
                                // zoom, never the visor crop.
                                // With borrowed frames on, Anniversary gives the lens gun its glass, fed
                                // by the gun camera (monitor frame mode 3).
                                const bool ceAnnivGlass = zoomTitle == GameTitle::HaloCE && !ceOptic && zoomWeapon &&
                                                          g_config.ce_anniv_monitor_frames &&
                                                          !HaloCE_ClassicScopeReady() && HaloCE_AnniversaryActive() &&
                                                          weapon_profile::ZoomKindFor(GameTitle::HaloCE, zoomWeapon) ==
                                                              weapon_profile::ZoomKind::Optic;
                                g_ceAnnivGlass.store(ceAnnivGlass, std::memory_order_release);
                                ceOpticNoLens = zoomTitle == GameTitle::HaloCE && zoomWeapon && !ceOptic &&
                                                !ceAnnivGlass &&
                                                weapon_profile::ZoomKindFor(GameTitle::HaloCE, zoomWeapon) ==
                                                    weapon_profile::ZoomKind::Optic;
                                g_zoomTier.store(
                                    static_cast<uint8_t>(zoomTitle == GameTitle::HaloCE
                                                             ? (ceOptic || ceAnnivGlass ? visor_zoom::Tier::Lens
                                                                                        : visor_zoom::Tier::Visor)
                                                             : visor_zoom::Choose(kind, g_config.visor_zoom_mode)),
                                    std::memory_order_release);
                                // A known scope-glass gun (kit lens node) on the lens tier keeps its glass live.
                                const bool glassGun = (zoomTitle == GameTitle::HaloCE
                                                              ? ceOptic || ceAnnivGlass
                                                              : weapon_profile::LiveGlassGun(zoomTitle, zoomWeapon)) &&
                                                      g_zoomTier.load(std::memory_order_relaxed) ==
                                                          static_cast<uint8_t>(visor_zoom::Tier::Lens);
                                if (glassGun)
                                    g_scopeGlassHeldMs.store(GetTickCount64(), std::memory_order_release);
                                // A known gun without a glass (a swap to
                                // the AR) ends the glass at once; the 250 ms grace is only for an
                                // unidentified gun (a tracking or identity hiccup).
                                else if (zoomWeapon)
                                    g_scopeGlassHeldMs.store(0, std::memory_order_release);
                            }
                            const bool visorTier = g_zoomTier.load(std::memory_order_acquire) ==
                                                   static_cast<uint8_t>(visor_zoom::Tier::Visor);
                            // No zoom on the held weapon (verified row): no lens, no visor.
                            bool noZoomWeapon = false;
                            {
                                static uint64_t cachedWeapon = ~0ull;
                                static GameTitle cachedTitle = GameTitle::None;
                                static bool cachedNoZoom = false;
                                const GameTitle zt = TitleAdapter_GetActiveTitle();
                                const uint64_t zw = VR_ZoomWeaponIdentity(zt, TitleAdapter_GetGeneration(zt));
                                if (zw != cachedWeapon || zt != cachedTitle)
                                {
                                    cachedWeapon = zw;
                                    cachedTitle = zt;
                                    // CE identifies every weapon from its own palette, so an
                                    // unidentified CE weapon does not arm (found in headset testing: CE zoomed on every gun).
                                    cachedNoZoom = zw ? weapon_profile::KnownNoZoom(zt, zw) : zt == GameTitle::HaloCE;
                                    if (cachedNoZoom)
                                        LOG("scope via look: off for weapon %016llX (no zoom on this weapon)",
                                            static_cast<unsigned long long>(zw));
                                }
                                noZoomWeapon = cachedNoZoom;
                                // A button-latched zoom ends when the held weapon changes.
                                static uint64_t latchedWeapon = 0;
                                if (zw && latchedWeapon && zw != latchedWeapon)
                                    g_scopeResetRequested.store(true, std::memory_order_release);
                                if (zw)
                                    latchedWeapon = zw;
                                static bool loggedBlocked = false;
                                if (ceOpticNoLens != loggedBlocked)
                                {
                                    loggedBlocked = ceOpticNoLens;
                                    if (ceOpticNoLens)
                                        LOG("scope: off for CE weapon %016llX (lens weapon without the look-through pass or borrowed frames; no visor fallback)",
                                            static_cast<unsigned long long>(zw));
                                }
                                g_zoomWeaponBlocked.store(noZoomWeapon || ceOpticNoLens, std::memory_order_release);
                            }
                            {
                                // Why the look gate cannot run, once per title (found in headset testing: BR never
                                // raise-zoomed in H2/ODST/H4).
                                static uint32_t loggedNoRenderer = 0;
                                const unsigned bit = 1u << (static_cast<unsigned>(TitleAdapter_GetActiveTitle()) & 31u);
                                if (haveAim && g_config.scope_enabled && g_config.scope_activation != 0 &&
                                    !Game_HasScopeRenderer() && !(loggedNoRenderer & bit))
                                {
                                    loggedNoRenderer |= bit;
                                    LOG("scope via look: unavailable, title %u has no scope renderer",
                                        static_cast<unsigned>(TitleAdapter_GetActiveTitle()));
                                }
                            }
                            // A live glass engages on the raise in every activation mode, so
                            // the raise is measured for it even when scope_activation = 0.
                            const bool glassLiveNow = g_scopeGlassLive.load(std::memory_order_acquire);
                            if (haveAim && !noZoomWeapon && g_config.scope_enabled &&
                                (g_config.scope_activation != 0 || glassLiveNow) && Game_HasScopeRenderer() &&
                                !Menu_IsOpen() && VR_GetHeadPose(headQ, headP))
                            {
                                ScopeQuadTransform lensTransform = ComputeScopeQuadTransform(aimQ, aimP,
                                    g_config.left_handed ? -g_config.scope_screen_right_m
                                                         : g_config.scope_screen_right_m,
                                    g_config.scope_screen_up_m, g_config.scope_screen_forward_m,
                                    g_config.scope_screen_width_m);
                                {
                                    float anchored[3], aimDerived[3]{lensTransform.position[0],
                                                           lensTransform.position[1], lensTransform.position[2]};
                                    const bool isAnchored = ReadAnchoredLens(anchored);
                                    if (isAnchored)
                                        for (int i = 0; i < 3; ++i)
                                            lensTransform.position[i] = anchored[i];
                                    g_sightLensAnchored = isAnchored;
                                    LogLensProof(projectionViews, lensTransform.position, aimDerived, isAnchored);
                                }
                                const XrQuaternionf aq{aimQ[0], aimQ[1], aimQ[2], aimQ[3]};
                                const XrQuaternionf hq{headQ[0], headQ[1], headQ[2], headQ[3]};
                                const XrVector3f f = Rotate(aq, {0, 0, -1});
                                const XrVector3f r = Rotate(hq, {1, 0, 0});
                                const float side = scope_sight::EyeSide(g_config.scope_eye, g_config.left_handed) *
                                                   scope_sight::kEyeOffsetM;
                                for (int i = 0; i < 3; ++i)
                                    lens[i] = lensTransform.position[i];
                                // A visor-tier gun with an anchored glass (the CE sniper in Anniversary graphics,
                                // where the CE lens ray still publishes) is raised by that glass, like a lens
                                // gun: the virtual point 0.25 m ahead of the hand kept a raised sniper at
                                // d=0.53-0.77 m (headset session 6 22:44:56-45:01), so it never engaged.
                                if (visor_zoom::RaiseUsesVirtualSight(visorTier, g_sightLensAnchored))
                                {
                                    // Raise to eye: a virtual sight point ahead of the grip.
                                    lens[0] = aimP[0] + f.x * visor_zoom::kVirtualSightAheadM;
                                    lens[1] = aimP[1] + f.y * visor_zoom::kVirtualSightAheadM;
                                    lens[2] = aimP[2] + f.z * visor_zoom::kVirtualSightAheadM;
                                }
                                barrel[0] = f.x;
                                barrel[1] = f.y;
                                barrel[2] = f.z;
                                eye[0] = headP[0] + r.x * side;
                                eye[1] = headP[1] + r.y * side;
                                eye[2] = headP[2] + r.z * side;
                                sightValid = true;
                            }
                            scope_sight::Settings sight{};
                            sight.minDistance = g_config.scope_sight_min_distance_m;
                            sight.maxDistance =
                                visorTier ? g_config.visor_raise_distance_m : g_config.scope_sight_distance_m;
                            sight.pitchDeg =
                                visorTier ? g_config.visor_raise_angle_deg : g_config.scope_sight_angle_deg;
                            const uint64_t sightNow = GetTickCount64();
                            const bool sightingNow =
                                g_scopeSightDetector.Update(sightNow, sightValid, eye, lens, barrel, sight);
                            {
                                // Edges are counted; at most one line a second, with the count.
                                static bool wasSighting = false;
                                static uint64_t lastLogMs = 0;
                                static unsigned edges = 0;
                                if (sightingNow != wasSighting)
                                {
                                    ++edges;
                                    wasSighting = sightingNow;
                                    if (sightNow - lastLogMs >= 1000)
                                    {
                                        // d/pitch: what the gate measured at this edge (nan = untracked/gap).
                                        LOG("scope via look: %s (%s tier) d=%.3f pitch=%+.1f (scope %.2f-%.2f m from the eye, gun within %.0f deg of level, %u edge(s))",
                                            sightingNow ? "engaged" : "released", visorTier ? "visor" : "lens",
                                            g_scopeSightDetector.LastDistance(), g_scopeSightDetector.LastPitchDeg(),
                                            sight.minDistance, sight.maxDistance, sight.pitchDeg, edges);
                                        lastLogMs = sightNow;
                                        edges = 0;
                                    }
                                }
                            }
                            {
                                // Diagnostic: a live glass that is not raised says why, once a second
                                // (the "scope cam origin" line only prints once raised/engaged).
                                static uint64_t lastWhyMs = 0, lastRaisedMs = 0;
                                if (sightingNow && !visorTier && sightNow - lastRaisedMs >= 1000)
                                {
                                    // The held pose (calibration): what the gate measures while raised.
                                    lastRaisedMs = sightNow;
                                    char raisedLine[96];
                                    scope_log::FormatRaised(raisedLine, sizeof raisedLine,
                                        g_scopeSightDetector.LastDistance(), g_scopeSightDetector.LastPitchDeg(),
                                        scope_sight::EyeSide(g_config.scope_eye, g_config.left_handed) > 0 ? 'R' : 'L');
                                    LOG("%s", raisedLine);
                                }
                                if (!sightingNow)
                                    lastRaisedMs = 0;
                                if (glassLiveNow && !sightingNow && sightNow - lastWhyMs >= 1000)
                                {
                                    lastWhyMs = sightNow;
                                    const float d = g_scopeSightDetector.LastDistance(),
                                                pitch = g_scopeSightDetector.LastPitchDeg();
                                    const char* why = !sightValid ? "not measured (no aim/head pose, menu or scope off)"
                                                      : !std::isfinite(d)                 ? "untracked frame"
                                                      : d < sight.minDistance             ? "lens too close to the eye"
                                                      : d > sight.maxDistance             ? "lens too far from the eye"
                                                      : std::fabs(pitch) > sight.pitchDeg ? "barrel not level enough"
                                                                                          : "holding (dwell)";
                                    LOG("scope glass not raised: %s; d=%.3f m pitch=%+.1f deg (raise = %.2f-%.2f m and within %.0f deg of level, %s lens, %s eye)",
                                        why, d, pitch, sight.minDistance, sight.maxDistance, sight.pitchDeg,
                                        g_sightLensAnchored ? "anchored" : "aim-derived",
                                        scope_sight::EyeSide(g_config.scope_eye, g_config.left_handed) > 0 ? "right"
                                                                                                           : "left");
                                }
                            }
                            g_scopeSighting.store(sightingNow, std::memory_order_release);
                            g_scopeSightingMs.store(sightNow, std::memory_order_release);
                        }
                        // F1 Weapon tuning: with the scope page open, show where the lens will sit
                        // (the crosshair glyph at the placement size) so it can be nudged onto the glass.
                        if (VR_LensMarkerActive() && Menu_IsOpen() && haveAim && g_reticleChain != XR_NULL_HANDLE)
                        {
                            ScopeQuadTransform markerTransform = ComputeScopeQuadTransform(aimQ, aimP,
                                g_config.left_handed ? -g_config.scope_screen_right_m : g_config.scope_screen_right_m,
                                g_config.scope_screen_up_m, g_config.scope_screen_forward_m,
                                g_config.scope_screen_width_m);
                            {
                                float anchored[3];
                                if (ReadAnchoredLens(anchored))
                                    for (int i = 0; i < 3; ++i)
                                        markerTransform.position[i] = anchored[i];
                            }
                            scopeQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
                            scopeQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                                                   XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                            scopeQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                            scopeQuad.space = g_localSpace;
                            scopeQuad.subImage.swapchain = g_reticleChain;
                            scopeQuad.subImage.imageRect = {{0, 0}, {(int32_t)kReticleSize, (int32_t)kReticleSize}};
                            scopeQuad.subImage.imageArrayIndex = 0;
                            scopeQuad.pose.orientation = {aimQ[0], aimQ[1], aimQ[2], aimQ[3]};
                            scopeQuad.pose.position = {
                                markerTransform.position[0], markerTransform.position[1], markerTransform.position[2]};
                            scopeQuad.size = {g_config.scope_screen_width_m, g_config.scope_screen_width_m};
                            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&scopeQuad));
                        }
                        // Enclosed-cockpit screen: a framed quad fixed in the cockpit (the seated
                        // eye captured when it appears), showing the hull camera's second render.
                        bool cockpitShown = false;
                        bool lensSubmitted = false;
                        if (VR_CockpitScreenActive() && !theaterPresentation && !Menu_IsOpen())
                        {
                            float chq[4]{}, chp[3]{};
                            if (VR_GetHeadPose(chq, chp))
                            {
                                const XrVector3f cf = Rotate(XrQuaternionf{chq[0], chq[1], chq[2], chq[3]}, {0, 0, -1});
                                g_cockpitAnchor.Update(
                                    chp, atan2f(-cf.x, -cf.z), g_positionRecenterEpoch.load(std::memory_order_acquire));
                            }
                            // Vehicle monitors: three screens, each from its own chain, when a
                            // title asked for a monitor view in the last 250 ms.
                            const uint64_t monitorAt = g_monitorRequestMs.load(std::memory_order_acquire);
                            const uint64_t monitorNow = GetTickCount64();
                            if (g_config.vehicle_monitors && monitorAt && monitorNow >= monitorAt &&
                                monitorNow - monitorAt <= 250 && g_cockpitAnchor.Valid())
                            {
                                (void)DeliverMonitorImage();
                                static XrCompositionLayerQuad monitorQuads[vehicle_monitors::kViewCount]{};
                                const bool on[vehicle_monitors::kViewCount]{g_config.vehicle_monitor_forward,
                                    g_config.vehicle_monitor_left, g_config.vehicle_monitor_right};
                                int shown = 0;
                                for (int v = 0; v < vehicle_monitors::kViewCount; ++v)
                                {
                                    if (!on[v] || !g_monitorHasImage[v] || g_monitorChains[v] == XR_NULL_HANDLE)
                                        continue;
                                    const cockpit_screen::Quad mq = vehicle_monitors::Place(g_cockpitAnchor.Eye(),
                                        g_cockpitAnchor.Yaw(), static_cast<vehicle_monitors::View>(v),
                                        g_config.vehicle_monitor_width_m, g_config.vehicle_monitor_distance_m);
                                    if (!mq.valid)
                                        continue;
                                    auto& q = monitorQuads[v];
                                    q = {XR_TYPE_COMPOSITION_LAYER_QUAD};
                                    q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                                    q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                                    q.space = g_localSpace;
                                    q.subImage.swapchain = g_monitorChains[v];
                                    q.subImage.imageRect = {
                                        {0, 0}, {(int32_t)kVisorScreenWidth, (int32_t)kVisorScreenHeight}};
                                    q.pose.orientation = {
                                        mq.orientation[0], mq.orientation[1], mq.orientation[2], mq.orientation[3]};
                                    q.pose.position = {mq.position[0], mq.position[1], mq.position[2]};
                                    q.size = {mq.width, mq.height};
                                    layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&q));
                                    ++shown;
                                }
                                if (shown)
                                {
                                    cockpitShown = true;
                                    g_monitorFrameMode = 1;
                                    static const char* loggedVehicle = nullptr;
                                    const char* vehicle = g_monitorVehicle.load(std::memory_order_acquire);
                                    if (vehicle != loggedVehicle && shown == vehicle_monitors::kViewCount)
                                    {
                                        loggedVehicle = vehicle;
                                        LOG("vehicle monitors: %s 3 screens at %.2f m (%.2f m wide, sides at +-%.0f deg)",
                                            vehicle ? vehicle : "unnamed seat", g_config.vehicle_monitor_distance_m,
                                            g_config.vehicle_monitor_width_m, vehicle_monitors::kSideDeg);
                                    }
                                }
                            }
                            g_visorCockpitMode = true;
                            const bool cockpitReady = !cockpitShown && PrepareVisorImageDelivery();
                            g_visorCockpitMode = false;
                            const cockpit_screen::Quad cq =
                                g_cockpitAnchor.Valid()
                                    ? cockpit_screen::Place(g_cockpitAnchor.Eye(), g_cockpitAnchor.Yaw(),
                                          g_config.cockpit_screen_width_m, g_config.cockpit_screen_distance_m,
                                          g_config.cockpit_screen_offset_y_m)
                                    : cockpit_screen::Quad{};
                            if (cockpitReady && cq.valid)
                            {
                                scopeQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
                                scopeQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                                scopeQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                                scopeQuad.space = g_localSpace;
                                scopeQuad.subImage.swapchain = g_visorScreenChain;
                                scopeQuad.subImage.imageRect = {
                                    {0, 0}, {(int32_t)kVisorScreenWidth, (int32_t)kVisorScreenHeight}};
                                scopeQuad.subImage.imageArrayIndex = 0;
                                scopeQuad.pose.orientation = {
                                    cq.orientation[0], cq.orientation[1], cq.orientation[2], cq.orientation[3]};
                                scopeQuad.pose.position = {cq.position[0], cq.position[1], cq.position[2]};
                                scopeQuad.size = {cq.width, cq.height};
                                layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&scopeQuad));
                                cockpitShown = true;
                                g_monitorFrameMode = 0;
                                static bool loggedCockpit = false;
                                if (!loggedCockpit)
                                {
                                    loggedCockpit = true;
                                    LOG("cockpit screen submitted: %.2f m wide at %.2f m, fixed in the cockpit",
                                        cq.width, g_config.cockpit_screen_distance_m);
                                }
                            }
                        }
                        else
                            g_cockpitAnchor.Reset();
                        if (!cockpitShown && reachProjectionAdmitted && !theaterPresentation &&
                            Game_HasScopeRenderer() && g_config.scope_enabled && g_scopeActive.load() &&
                            !Menu_IsOpen() && haveAim &&
                            g_zoomTier.load(std::memory_order_acquire) ==
                                static_cast<uint8_t>(visor_zoom::Tier::Visor) &&
                            // Never a second (helmet) window while a scope-glass gun is held: its
                            // glass is the zoom (CE Classic showed both when the tier wobbled).
                            [&]
                            {
                                const uint64_t heldAt = g_scopeGlassHeldMs.load(std::memory_order_acquire),
                                               heldNow = GetTickCount64();
                                const bool glassHeld = heldAt && heldNow >= heldAt && heldNow - heldAt <= 250;
                                static bool logged = false;
                                if (glassHeld && !logged)
                                {
                                    logged = true;
                                    LOG("visor zoom window: not shown while a scope-glass gun is held (its glass is the zoom)");
                                }
                                return !glassHeld;
                            }() &&
                            PrepareVisorImageDelivery())
                        {
                            // Head-locked visor window from its four edges.
                            const visor_zoom::EdgeWindow edgeWindow = VisorWindow();
                            const visor_zoom::Layout window = edgeWindow.quad;
                            scopeQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
                            scopeQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                            scopeQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                            scopeQuad.space = g_viewSpace;
                            scopeQuad.subImage.swapchain = g_visorScreenChain;
                            scopeQuad.subImage.imageRect = VisorImageRect(edgeWindow);
                            scopeQuad.subImage.imageArrayIndex = 0;
                            scopeQuad.pose.orientation = {
                                0, std::sin(window.yaw * 0.5f), 0, std::cos(window.yaw * 0.5f)};
                            scopeQuad.pose.position = {window.x, window.y, window.z};
                            scopeQuad.size = {window.width, window.height};
                            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&scopeQuad));
                            static bool loggedVisor = false;
                            if (!loggedVisor)
                            {
                                loggedVisor = true;
                                LOG("visor zoom window submitted: %.3fm x %.3fm at %.2fm, offset x %+.3f y %+.3f",
                                    window.width, window.height, -window.z, window.x, window.y);
                            }
                        }
                        else if (!cockpitShown && reachProjectionAdmitted && !theaterPresentation &&
                                 Game_HasScopeRenderer() && g_config.scope_enabled && g_scopeGlassLive.load() &&
                                 !Menu_IsOpen() && haveAim &&
                                 g_zoomTier.load(std::memory_order_acquire) !=
                                     static_cast<uint8_t>(visor_zoom::Tier::Visor) &&
                                 PrepareLensQuad(aimQ, aimP, viewsValid))
                        {
                            const ScopeQuadTransform transform = g_lensPlanTransform;
                            scopeQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
                            scopeQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                            scopeQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                            scopeQuad.space = g_localSpace;
                            scopeQuad.subImage.swapchain = g_scopeScreenChain;
                            scopeQuad.subImage.imageRect = {
                                {0, 0}, {(int32_t)kScopeScreenWidth, (int32_t)kScopeScreenHeight}};
                            scopeQuad.subImage.imageArrayIndex = 0;
                            scopeQuad.pose.orientation = {aimQ[0], aimQ[1], aimQ[2], aimQ[3]};
                            scopeQuad.pose.position = {
                                transform.position[0], transform.position[1], transform.position[2]};
                            scopeQuad.size = {transform.width, transform.height};
                            for (int i = 0; i < 3; ++i)
                                g_lensQuadPos[i] = transform.position[i];
                            g_lensQuadQ[0] = aimQ[0];
                            g_lensQuadQ[1] = aimQ[1];
                            g_lensQuadQ[2] = aimQ[2];
                            g_lensQuadQ[3] = aimQ[3];
                            g_lensQuadW = transform.width;
                            g_lensQuadH = transform.height;
                            if (g_lensDeliveredStereo)
                            {
                                // Same glass, one image per eye.
                                scopeQuad.subImage.swapchain = g_scopeStereoChain;
                                scopeQuad.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
                                scopeQuadRight = scopeQuad;
                                scopeQuadRight.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;
                                scopeQuadRight.subImage.imageRect = {{(int32_t)kScopeScreenWidth, 0},
                                    {(int32_t)kScopeScreenWidth, (int32_t)kScopeScreenHeight}};
                                layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&scopeQuad));
                                layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&scopeQuadRight));
                            }
                            else
                                layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&scopeQuad));
                            lensSubmitted = true;
                            g_lensSubmittedMs.store(GetTickCount64(), std::memory_order_release);
                            static bool logged = false;
                            if (!logged)
                            {
                                logged = true;
                                LOG("scope 4:3 zoom screen submitted: %.3fm x %.3fm at local offsets %.3f/%.3f/%.3f",
                                    transform.width, transform.height, g_config.scope_screen_right_m,
                                    g_config.scope_screen_up_m, g_config.scope_screen_forward_m);
                            }
                        }
                        // Lens delivery per CE frame with a live glass (the 1 s glass line).
                        if (ceTitle && g_scopeGlassLive.load(std::memory_order_acquire))
                        {
                            const int raised = g_scopeSighting.load(std::memory_order_acquire) ? 1 : 0;
                            ++g_ceLensFrames[raised];
                            if (lensSubmitted)
                                ++g_ceLensSubmitted[raised];
                        }
                        {
                            // Once per look engage: either the lens layer went out, or why not
                            // (found in headset testing: Reach sniper engaged the lens tier but no glass appeared).
                            static bool reported = false;
                            static uint64_t engagedAtMs = 0;
                            // A live glass reports from the first frame it is held, not from the raise.
                            const bool engaged = (g_scopeSighting.load(std::memory_order_acquire) ||
                                                     g_scopeGlassLive.load(std::memory_order_acquire)) &&
                                                 g_zoomTier.load(std::memory_order_acquire) !=
                                                     static_cast<uint8_t>(visor_zoom::Tier::Visor);
                            const uint64_t reportNow = GetTickCount64();
                            {
                                // A weapon swap while the glass stays live reports the new gun too.
                                static uint64_t reportedWeapon = 0;
                                const GameTitle reportTitle = TitleAdapter_GetActiveTitle();
                                const uint64_t weaponNow =
                                    VR_ZoomWeaponIdentity(reportTitle, TitleAdapter_GetGeneration(reportTitle));
                                if (weaponNow != reportedWeapon)
                                {
                                    reportedWeapon = weaponNow;
                                    reported = false;
                                    engagedAtMs = 0;
                                }
                                // ...and each raise to the eye reports again (test runs time the lens report per raise).
                                static bool reportedRaised = false;
                                const bool raisedNow = g_scopeSighting.load(std::memory_order_acquire);
                                if (raisedNow && !reportedRaised)
                                {
                                    reported = false;
                                    engagedAtMs = 0;
                                }
                                reportedRaised = raisedNow;
                            }
                            if (!engaged)
                            {
                                reported = false;
                                engagedAtMs = 0;
                            }
                            else if (!engagedAtMs)
                                engagedAtMs = reportNow;
                            // The scope toggle and its second render follow the engage by a
                            // frame or more: report the first submit, or the gates 500 ms in.
                            // The lens report again 1 s into a held raise, tagged hold=1 (the settled pose, not mid-travel).
                            static uint64_t holdDueMs = 0;
                            const bool raisedForHold = g_scopeSighting.load(std::memory_order_acquire);
                            if (!raisedForHold)
                                holdDueMs = 0;
                            bool holdReport = false;
                            if (holdDueMs && reportNow >= holdDueMs && lensSubmitted)
                            {
                                holdReport = true;
                                holdDueMs = 0;
                            }
                            if ((engaged && !reported && (lensSubmitted || reportNow - engagedAtMs >= 500)) ||
                                holdReport)
                            {
                                if (!holdReport)
                                {
                                    reported = true;
                                    if (raisedForHold)
                                        holdDueMs = reportNow + 1000;
                                }
                                if (lensSubmitted)
                                {
                                    if (!holdReport)
                                        LOG("scope lens: submitted layer=%zu size=%dx%d", layers.size() - 1,
                                            (int)kScopeScreenWidth, (int)kScopeScreenHeight);
                                    // The quad in the head frame, and how far it is turned
                                    // from facing the eye (0 = square to the line of sight).
                                    const XrQuaternionf hq = g_headPose.orientation;
                                    const XrQuaternionf hqInv{-hq.x, -hq.y, -hq.z, hq.w};
                                    const XrVector3f d{g_lensQuadPos[0] - g_headPose.position.x,
                                        g_lensQuadPos[1] - g_headPose.position.y,
                                        g_lensQuadPos[2] - g_headPose.position.z};
                                    const XrVector3f local = Rotate(hqInv, d);
                                    const XrVector3f normal = Rotate(
                                        XrQuaternionf{g_lensQuadQ[0], g_lensQuadQ[1], g_lensQuadQ[2], g_lensQuadQ[3]},
                                        {0, 0, 1});
                                    const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                                    const float cosAngle =
                                        dl > 1e-5f ? -(normal.x * d.x + normal.y * d.y + normal.z * d.z) / dl : 1.0f;
                                    const float offset[3]{local.x, local.y, local.z};
                                    const GameTitle quadTitle = TitleAdapter_GetActiveTitle();
                                    char quadLine[200];
                                    scope_log::FormatQuad(quadLine, sizeof quadLine, offset, g_lensQuadW, g_lensQuadH,
                                        std::acos(std::clamp(cosAngle, -1.0f, 1.0f)) * 57.2957795f,
                                        static_cast<unsigned long long>(
                                            VR_ZoomWeaponIdentity(quadTitle, TitleAdapter_GetGeneration(quadTitle))));
                                    char eyeLines[2][96]{};
                                    if (g_lensDeliveredStereo)
                                        for (int eye = 0; eye < 2; ++eye)
                                            scope_log::FormatQuadEye(eyeLines[eye], sizeof eyeLines[eye],
                                                eye ? 'R' : 'L', g_lensPlanEyes[eye].angleDeg,
                                                g_lensPlanEyes[eye].shiftU, g_lensPlanEyes[eye].shiftV,
                                                g_lensPlanEyes[eye].k);
                                    char shapeField[32], holdField[16];
                                    scope_log::FormatQuadShape(shapeField, sizeof shapeField, g_scopeShapeApplied);
                                    scope_log::FormatQuadHold(holdField, sizeof holdField, holdReport);
                                    LOG("%s%s%s%s%s glass=%.2f:1 image=%ux%u", quadLine, shapeField, eyeLines[0],
                                        eyeLines[1], holdField, g_scopeGlassAspect.load(std::memory_order_acquire),
                                        kScopeScreenWidth, kScopeScreenHeight);
                                }
                                else
                                    LOG("scope lens: NOT submitted (cockpit=%d reachAdmitted=%d theater=%d renderer=%d "
                                        "scopeActive=%d menu=%d aim=%d image=%d imageTitle=%u/%u imageGen=%u/%u cache=%d reachStage=%u)",
                                        cockpitShown ? 1 : 0, reachProjectionAdmitted ? 1 : 0,
                                        theaterPresentation ? 1 : 0, Game_HasScopeRenderer() ? 1 : 0,
                                        g_scopeActive.load() ? 1 : 0, Menu_IsOpen() ? 1 : 0, haveAim ? 1 : 0,
                                        g_scopeHasImage.load() ? 1 : 0, static_cast<unsigned>(g_scopeImageTitle),
                                        static_cast<unsigned>(TitleAdapter_GetActiveTitle()), g_scopeImageGeneration,
                                        TitleAdapter_GetGeneration(g_scopeImageTitle), g_scopeCache ? 1 : 0,
                                        g_reachScopeStage.load());
                            }
                        }
                    }
                    if (reachAccess.active)
                        VR_ReachEndRenderAccess(reachAccess);
                }
                else if (halo_ce::AllowStockScreen(allowStockScreenFrame, ceOwned, pausedPresentation))
                {
                    if (halo2StrictStockScreen)
                    {
                        SubmitHalo2StrictStockScreen(
                            backbuffer, bd, pausedPresentation, screenQuad, layers, halo2StrictStockScreenQueued);
                    }
                    else if (g_haveCenter && EnsureScreenChain(bd.Width, bd.Height))
                    {
                        uint32_t idx = 0;
                        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                        XrSwapchainImageWaitInfo wi2{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                        wi2.timeout = 1000000000; // 1 second in ns; never hang the render thread
                        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                        FLog("acquire+wait screen image");
                        if (XR_SUCCEEDED(xrAcquireSwapchainImage(g_screenChain, &ai, &idx)) &&
                            XR_SUCCEEDED(xrWaitSwapchainImage(g_screenChain, &wi2)))
                        {
                            FLog("blit backbuffer -> screen");
                            Blit(backbuffer, bd, g_screenImages[idx], g_screenW, g_screenH,
                                GetRtv(g_screenImages, g_screenRtvs, idx));
                            xrReleaseSwapchainImage(g_screenChain, &ri);
                            FLog("screen image released");
                            const bool headLock =
                                pausedPresentation || (g_screenFollow.load() && Game_IsHeadTracking());
                            screenQuad = MakeQuad(g_screenChain, (int32_t)g_screenW, (int32_t)g_screenH,
                                g_config.screen_width_m, g_config.screen_distance_m, 0.0f, 0, headLock);
                            ApplyPauseWorldLock(screenQuad, pausedPresentation, g_config.screen_distance_m);
                            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&screenQuad));
                        }
                    }
                }

                // H3/H2's native final output already contains one full eye.
                // CE leaves its packed native atlas here instead. Present one
                // completed eye only after all headset/screen consumers above
                // have read their inputs, while the same pair borrow is held.
                // A desktop failure is independent of XR ownership/submission.
                if (ceImages && ceOwned && stereoWorldFrame)
                    MirrorCeDesktopAndReport(ceLease.pair, backbuffer, bd);
                // The monitor never shows the XR layers: put the scope glass picture
                // in the bottom-right corner (after every XR consumer read the backbuffer).
                DrawScopeInset(backbuffer, bd);

                if (g_abortFrameForSwapchainFailure)
                {
                    // A later exact swapchain transaction can invalidate the
                    // already-queued H2 world layer. The shared fatal drain
                    // ends this frame; quarantine also closes H2 admission
                    // immediately so recovery cannot claim another frame in
                    // the same title generation.
                    if (halo2ProjectionQueued)
                        QuarantineHalo2PreparedFrame(Halo2StereoQuarantineReason::VrClaimedSwapchainFailed);
                    // Do not acquire menu/fade images or submit any partial
                    // exact-title layer set. xrBeginFrame still requires a matching
                    // xrEndFrame; an empty layer list references no potentially
                    // outstanding swapchain image.
                    Menu_ClearVrPointer();
                    backbuffer->Release();
                    EndPreparedFrameWithoutLayers("world swapchain transaction failed");
                    return;
                }

                // One-shot welcome page for this launch. The latch is a
                // process-lifetime static that is never cleared, so quitting a
                // level or returning to MCC's menu can never bring the page
                // back -- that is the entire "startup only" mechanism, and it
                // needs no level-load detection.
                //
                // The first focused frame is MCC's own shell menu on any real
                // launch, because the session is created during MCC's startup
                // Present. Deliberately NOT also gated on RuntimeMode or head
                // tracking: a title that failed to publish a lifecycle would
                // then silently never show the message, and a predictable rule
                // beats a "sometimes it doesn't appear" mode.
                static bool welcomeConsidered = false;
                if (!welcomeConsidered && g_sessionState == XR_SESSION_STATE_FOCUSED)
                {
                    welcomeConsidered = true;
                    if (g_config.show_welcome)
                        Menu_OpenWelcome();
                    else
                        LOG("menu: welcome page suppressed by show_welcome=0");
                }

                // The menu is submitted in BOTH modes (it used to live only in
                // the mono-screen branch, which made F1 invisible in stereo).
                // In stereo it head-locks so it is always in front of you.
                if (Menu_IsOpen())
                {
                    const bool menuHeadLocked =
                        pausedPresentation || stereo || (g_screenFollow.load() && Game_IsHeadTracking());
                    UpdateMenuPointer(menuHeadLocked);
                    if (ID3D11Texture2D* menuTex = Menu_Render())
                    {
                        uint32_t idx = 0;
                        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                        XrSwapchainImageWaitInfo wi2{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                        wi2.timeout = 1000000000;
                        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                        D3D11_TEXTURE2D_DESC md{};
                        menuTex->GetDesc(&md);
                        if (XR_SUCCEEDED(xrAcquireSwapchainImage(g_menuChain, &ai, &idx)) &&
                            XR_SUCCEEDED(xrWaitSwapchainImage(g_menuChain, &wi2)))
                        {
                            Blit(menuTex, md, g_menuImages[idx], MENU_W, MENU_H, GetRtv(g_menuImages, g_menuRtvs, idx));
                            xrReleaseSwapchainImage(g_menuChain, &ri);
                            // Placement comes from the config, which the grab
                            // handle writes. UpdateMenuPointer above raycasts
                            // against the same four values, so the pointer can
                            // never drift off the visible panel.
                            menuQuad = MakeQuad(g_menuChain, MENU_W, MENU_H, g_config.menu_width_m,
                                g_config.menu_distance_m, g_config.menu_height_m,
                                XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                                    XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT,
                                menuHeadLocked, g_config.menu_side_m);
                            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&menuQuad));
                        }
                    }
                }
                else
                {
                    Menu_ClearVrPointer();
                }
                backbuffer->Release();
            }
            else
            {
                // The exact H2 eye pair can be complete even if DXGI no longer
                // yields buffer 0 at submission. Its durable Claim/Complete
                // still owns this serial, so drop it once and close admission
                // before the next render instead of producing a black run.
                if (!QuarantineHalo2PreparedFrame(Halo2StereoQuarantineReason::VrClaimedPresentationUnavailable) &&
                    TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
                {
                    LOG("Halo 2 pre-stereo screen GetBuffer(0) failed: "
                        "HRESULT 0x%08X, backbuffer=%p",
                        static_cast<unsigned>(backbufferResult), static_cast<void*>(backbuffer));
                    HandleHalo2StockScreenFailure(
                        "DXGI GetBuffer(0) returned no stock backbuffer", Halo2StockScreenXrOwnership::None);
                    EndPreparedFrameWithoutLayers("Halo 2 stock-screen backbuffer unavailable");
                    return;
                }
            }
        }
        if (nativeSubtitleReady && !layers.empty())
            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&nativeSubtitleQuad));
        // Evaluate against this frame's actual native quad before composition.
        // The cursor follows the last successfully delivered smoothed mouse UV.
        if (comfortFadeAlpha <= 0.01f &&
            std::find(layers.begin(), layers.end(), reinterpret_cast<XrCompositionLayerBaseHeader*>(&screenQuad)) !=
                layers.end())
        {
            UpdateNativeMenuPointer(screenQuad);
            float u = 0, v = 0;
            bool pressed = false;
            if (g_nativePointerFrame.active && g_nativeCursorReady && NativeMenuPointer_ReadVisual(u, v, pressed))
            {
                nativeCursorQuad = game_menu_pointer::CursorQuad(screenQuad, g_nativeCursorChain, u, v, pressed);
                layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&nativeCursorQuad));
            }
        }
        if (fs.shouldRender)
        {
            comfortFadeAlpha = std::max(comfortFadeAlpha, AppendVehicleComfort(vignetteQuad, dashQuad, layers));
            AppendComfortFade(comfortFadeAlpha, fadeQuad, layers);
        }

        // Heartbeat: log the session state + whether the runtime wants us to
        // render + how many layers we submitted, on any change and at least
        // every couple of seconds. This shows whether we ever go VISIBLE.
        {
            static XrSessionState lastState = XR_SESSION_STATE_UNKNOWN;
            static int lastShould = -1;
            static LARGE_INTEGER last{}, freq{};
            LARGE_INTEGER now;
            if (freq.QuadPart == 0)
                QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&now);
            // Publish for the wait worker, which reports stalls while this
            // thread is stuck inside the game and cannot speak for itself.
            g_lastShouldRenderShared.store((int)fs.shouldRender, std::memory_order_relaxed);
            const bool changed = g_sessionState != lastState || (int)fs.shouldRender != lastShould;
            const bool tick = last.QuadPart == 0 || (now.QuadPart - last.QuadPart) >= 2 * freq.QuadPart;
            if (changed || tick)
            {
                LOG("status: session=%s shouldRender=%d layers=%u", SessionStateName(g_sessionState),
                    (int)fs.shouldRender, (unsigned)layers.size());
                lastState = g_sessionState;
                lastShould = (int)fs.shouldRender;
                last = now;
            }
        }

        FLog("xrEndFrame before DXGI Present");
        XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
        ei.displayTime = fs.predictedDisplayTime;
        ei.environmentBlendMode = g_blendMode;
        ei.layerCount = (uint32_t)layers.size();
        ei.layers = layers.data();
        // Preserve the legacy rolling submit timer. Transition diagnosis uses
        // the correlated raw-QPC record above; this aggregate cannot identify
        // which logical frame changed cadence.
        LARGE_INTEGER endStart{}, endEnd{};
        QueryPerformanceCounter(&endStart);
        if (g_beginFrameQpc.QuadPart)
            g_renderWindowMs.Add(QpcMs(endStart.QuadPart - g_beginFrameQpc.QuadPart));
        if (g_beginFrameQpc.QuadPart && g_monitorFrameMode >= 0)
        {
            // Render-window time while the monitors vs the single screen show.
            const float ms = static_cast<float>(QpcMs(endStart.QuadPart - g_beginFrameQpc.QuadPart));
            auto& st = g_monitorFrameStats[g_monitorFrameMode];
            if (std::isfinite(ms))
            {
                st.sum += ms;
                st.max = std::max(st.max, ms);
                ++st.n;
            }
        }
        g_monitorFrameMode = -1;
        {
            static uint64_t nextMonitorLogMs = 0;
            const uint64_t nowMs = GetTickCount64();
            if (nowMs >= nextMonitorLogMs)
            {
                nextMonitorLogMs = nowMs + 5000;
                for (int m = 0; m < 2; ++m)
                {
                    auto& st = g_monitorFrameStats[m];
                    if (!st.n)
                        continue;
                    LOG("vehicle monitors: frame time mode=%s render window avg %.2f ms max %.2f ms over %u frames (5 s)",
                        m ? "monitors" : "single", st.sum / st.n, st.max, st.n);
                    st = {};
                }
                {
                    uint64_t captured = 0, refused = 0;
                    HaloCE_AnnivMonitorStats(captured, refused);
                    uint64_t why[5]{};
                    HaloCE_AnnivMonitorRefusals(why);
                    const uint64_t stolen = g_annivStolen.exchange(0), visor = g_annivStolenVisor.exchange(0),
                                   glass = g_annivStolenGlass.exchange(0), moving = g_annivMovingSkips.exchange(0),
                                   hullMovingFrames = g_annivStolenMoving.exchange(0);
                    const int hullMode = g_annivHullMoving.load(std::memory_order_relaxed);
                    if (stolen || moving || refused)
                        LOG("CE anniv monitor frames: stolen=%llu (visor %llu, glass %llu, hull %llu, hull while moving %llu) captured=%llu "
                            "refused=%llu (finish %llu, receipt %llu, camera %llu, begin %llu, other %llu) skipped-moving=%llu mode=%s every=%d (stopped) / %d (moving, 0 = off) "
                            "still<=%.2f deg (stopped = <=1 m/s); eye pair repeated=%llu (5 s)",
                            static_cast<unsigned long long>(stolen), static_cast<unsigned long long>(visor),
                            static_cast<unsigned long long>(glass),
                            static_cast<unsigned long long>(stolen - visor - glass),
                            static_cast<unsigned long long>(hullMovingFrames),
                            static_cast<unsigned long long>(captured), static_cast<unsigned long long>(refused),
                            static_cast<unsigned long long>(why[0]), static_cast<unsigned long long>(why[1]),
                            static_cast<unsigned long long>(why[2]), static_cast<unsigned long long>(why[3]),
                            static_cast<unsigned long long>(why[4]), static_cast<unsigned long long>(moving),
                            hullMode == 1   ? "moving"
                            : hullMode == 0 ? "stopped"
                                            : "none",
                            g_config.ce_anniv_monitor_every, g_config.ce_anniv_hull_frames_moving_every,
                            g_config.ce_anniv_monitor_still_deg, static_cast<unsigned long long>(captured + refused));
                }
                const uint64_t f = g_monitorViewsRendered[0].exchange(0), l = g_monitorViewsRendered[1].exchange(0),
                               r = g_monitorViewsRendered[2].exchange(0);
                if (f || l || r)
                    LOG("vehicle monitors: views rendered forward=%llu left=%llu right=%llu (5 s)",
                        static_cast<unsigned long long>(f), static_cast<unsigned long long>(l),
                        static_cast<unsigned long long>(r));
            }
        }
        XrResult r = xrEndFrame(g_session, &ei);
        if (r != XR_SUCCESS)
        {
            if (std::find(layers.begin(), layers.end(),
                    reinterpret_cast<XrCompositionLayerBaseHeader*>(&nativeCursorQuad)) != layers.end())
            {
                // Drop this frame, then retire only the optional cursor layer.
                // Never let a rejected extra layer repeatedly poison core VR.
                g_nativeCursorReady = false;
                NativeMenuPointer_CursorFailed();
            }
            g_nativePointerFrame = {};
            NativeMenuPointer_FrameStatus("submission-failed");
        }
        QueryPerformanceCounter(&endEnd);
        g_endFrameDurationsMs.Add(QpcMs(endEnd.QuadPart - endStart.QuadPart));
        FinishHalo2Submission(halo2StrictStockScreenQueued, halo2ProjectionQueued, r);
        if (Halo4XrPairSubmissionAccepted(halo4ProjectionQueued, r == XR_SUCCESS))
        {
            g_halo4XrPairsSubmitted.fetch_add(1, std::memory_order_relaxed);
        }
        if constexpr (kEnableFramePacingTransitionCapture)
        {
            if (g_framePacingPending.serial == g_preparedFrame.serial)
            {
                g_framePacingPending.endStartQpc = endStart.QuadPart;
                g_framePacingPending.endEndQpc = endEnd.QuadPart;
                g_framePacingPending.endResult = r;
                g_framePacingPending.layerCount = static_cast<uint32_t>(layers.size());
                g_framePacingPending.submitted = true;
            }
        }
        ResetPreparedFrame();
        if (XR_FAILED(r))
        {
            ++g_frameOrderFailures;
            static bool logged = false;
            if (!logged)
            {
                LOG("xrEndFrame failed: %s", XrStr(r));
                logged = true;
            }
        }
        FLog("frame complete");
        if (g_frameNo == 3)
            LOG("first 3 frames submitted OK; going quiet now");
    }
} // namespace

namespace two_hand_lab_runtime
{
    void SetSettings(const two_hand_lab::Settings& settings) noexcept
    {
        PublishTwoHandLabSettings(settings);
    }

    two_hand_lab::Settings GetSettings() noexcept
    {
        return SnapshotTwoHandLabSettings();
    }

    void ResetSettings() noexcept
    {
        PublishTwoHandLabSettings(two_hand_lab::DefaultSettings());
    }

} // namespace two_hand_lab_runtime

void VR_InitInstance()
{
    if (!g_headCsInit)
    {
        InitializeCriticalSection(&g_headCs);
        g_headCsInit = true;
    }
    // Runs on the DLL's background init thread, in parallel with the game
    // loading. Never touches the render thread or the game's D3D device.
    if (InitInstance())
        g_instanceReady = true;
    else
        g_instanceFailed = true; // Fail() already showed a message
}

namespace
{

}

static void UpdateScopeAdaptiveScale(float intervalMs); // below, with the scope capture

void VR_BeforePresent(IDXGISwapChain* sc)
{
    // Diagnostic-only frame-boundary marker (no gameplay effect).
    PublishWeaponOrderMarker(WeaponOrderEventKind::PresentBegin);
    // DLSS frame retirement belongs to Present, not to successful stereo
    // upload. This covers pause/loading, shouldRender=false and every early
    // return, including failed XR acquisition, without altering VR ownership.
    struct DlssFrameCleanup
    {
        ~DlssFrameCleanup() { DlssEndFrame(); }
    } dlssFrameCleanup;
    g_nativePointerFrame = {};
    NativeMenuPointer_FrameStatus("no-native-screen");
    struct PublishNativePointerOnExit
    {
        ~PublishNativePointerOnExit()
        {
            NativeMenuPointer_Publish(g_nativePointerFrame.active, g_nativePointerFrame.u, g_nativePointerFrame.v,
                g_nativePointerFrame.pressed);
        }
    } publishNativePointerOnExit;
    if constexpr (kEnableFramePacingTransitionCapture)
    {
        LARGE_INTEGER pacingBeforePresent{};
        QueryPerformanceCounter(&pacingBeforePresent);
        if (g_framePacingPending.serial)
        {
            g_framePacingPending.beforePresentQpc = pacingBeforePresent.QuadPart;
            g_framePacingPending.scopeActive = g_scopeActive.load(std::memory_order_relaxed);
            GameFramePerfCounters perfEnd{};
            Game_ReadFramePerfCounters(perfEnd);
            SubtractPerfCounters(perfEnd, g_framePacingPerfStart, g_framePacingPending.perf);
        }
    }
    g_gameSwapchain = sc;
    // This observation is intentionally before every OpenXR/session early
    // return. It rejects unrelated Presents with one raw engine-swapchain
    // identity read, then at most every 250 ms captures a fixed-storage retained
    // buffer/device snapshot at the exact Reach Present boundary. It performs
    // no file/signature scan, resource allocation, lock, or logging.
    ObserveReachPresentSwapchain(sc);
    // Presentation cleanup must not depend on a healthy/running OpenXR session.
    // The D3D Present hook still reaches this path when instance/session setup
    // failed, so it can acknowledge ODST teardown and restore shared title
    // policy without leaving camera-only ownership latched indefinitely.
    Game_ProcessPresentationDetachRequest();
    if (!g_gameBackbufferDescValid && sc)
    {
        ID3D11Texture2D* backbuffer = nullptr;
        if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer))) && backbuffer)
        {
            backbuffer->GetDesc(&g_gameBackbufferDesc);
            g_gameBackbufferDescValid = true;
            backbuffer->Release();
        }
    }
    if (g_state == State::Failed)
        return;
    if (g_state == State::Uninitialized)
    {
        // Wait for the background thread to finish creating the OpenXR
        // instance. Until then, do nothing so the game renders normally to
        // the monitor instead of freezing.
        if (g_instanceFailed)
        {
            g_state = State::Failed;
            return;
        }
        if (!g_instanceReady)
            return;
        static bool announced = false;
        if (!announced)
        {
            LOG("instance ready; creating VR session on the render thread");
            announced = true;
        }
        if (!InitSession(sc))
            return; // state is Failed, message shown
        g_state = State::Ready;
    }

    // FPS counter for the menu status line
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    if (g_lastBeforePresentQpc.QuadPart)
    {
        const float intervalMs = static_cast<float>(QpcMs(now.QuadPart - g_lastBeforePresentQpc.QuadPart));
        g_presentIntervalsMs.Add(intervalMs);
        UpdateScopeAdaptiveScale(intervalMs);
    }
    g_lastBeforePresentQpc = now;
    g_fpsFrames++;
    if (g_fpsTimer.QuadPart == 0)
        g_fpsTimer = now;
    else if (now.QuadPart - g_fpsTimer.QuadPart >= freq.QuadPart)
    {
        g_status.fps = (float)g_fpsFrames * freq.QuadPart / (float)(now.QuadPart - g_fpsTimer.QuadPart);
        g_fpsFrames = 0;
        g_fpsTimer = now;
        // Every 10 s, put the frame rate in the log so performance reports
        // ("fps has been lower") can be tied to a session and a build.
        static int fpsLogCountdown = 10;
        if (--fpsLogCountdown <= 0)
        {
            fpsLogCountdown = 10;
            LOG("fps %.0f (stereo %s); app cadence %.1fHz "
                "(xrWaitFrame period), panel %.1fHz",
                g_status.fps, g_stereoEnabled.load() ? "on" : "off", VR_HeadsetRefreshHz(),
                g_panelRefreshHz.load(std::memory_order_relaxed));
        }
    }

    const uint64_t timingNowMs = GetTickCount64();
    if (!g_timingLogStartMs)
        g_timingLogStartMs = timingNowMs;
    else if (timingNowMs - g_timingLogStartMs >= 10000)
    {
        // Backward-compatible rolling summaries. These are the latest 512
        // samples (not a fixed 10-second window) and are intentionally not used
        // to attribute a transition; the PACING capture correlates exact rows.
        // p50 tells a steady cost from a 1-in-N hitch (with every 4th frame
        // heavy, p95 and p99 both sit in the heavy group; the median does not).
        LOG("timing: frame interval p95 %.2fms p99 %.2fms p50 %.2fms; "
            "renderWindow p95 %.2fms; xrEndFrame p95 %.2fms; "
            "DXGI Present p95 %.2fms; wait handoff p95 %.2fms; "
            "prediction error p95 %.3fms; missed=%llu duplicate=%llu "
            "orderFailures=%llu firstCamera=%.3fms stalls=%llu worstStall=%llums",
            TimingPercentile(g_presentIntervalsMs, 0.95), TimingPercentile(g_presentIntervalsMs, 0.99),
            TimingPercentile(g_presentIntervalsMs, 0.50), TimingPercentile(g_renderWindowMs, 0.95),
            TimingPercentile(g_endFrameDurationsMs, 0.95), TimingPercentile(g_presentDurationsMs, 0.95),
            TimingPercentile(g_waitDurationsMs, 0.95), TimingPercentile(g_predictionErrorMs, 0.95),
            static_cast<unsigned long long>(g_missedPredictions),
            static_cast<unsigned long long>(g_duplicatePredictions),
            static_cast<unsigned long long>(g_frameOrderFailures),
            g_firstCameraDelayUs.load(std::memory_order_relaxed) / 1000.0,
            static_cast<unsigned long long>(g_frameStalls.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_frameStallWorstMs.load(std::memory_order_relaxed)));
        // Frame-cap probe: per title, whether the frames wait for the
        // headset (xrWaitFrame) or for something else (the title's own limiter: frame
        // interval ~16.7 ms with ~0 wait). It has shown ODST's limiter (Reach's pair) and
        // Halo 2's second limiter; after "set to unlimited" it should show the headset's wait.
        {
            const GameTitle probeTitle = TitleAdapter_GetActiveTitle();
            if (probeTitle == GameTitle::Halo3ODST || probeTitle == GameTitle::Halo2 ||
                probeTitle == GameTitle::Halo4 || probeTitle == GameTitle::HaloReach || probeTitle == GameTitle::Halo3)
                LOG("frame cap probe (%s): frame interval p50 %.2fms (%.1f fps), xrWaitFrame wait p50 %.2fms, "
                    "renderWindow p50 %.2fms, headset %.1f Hz",
                    DlssTitleName(probeTitle), TimingPercentile(g_presentIntervalsMs, 0.50),
                    TimingPercentile(g_presentIntervalsMs, 0.50) > 0
                        ? 1000.0 / TimingPercentile(g_presentIntervalsMs, 0.50)
                        : 0.0,
                    TimingPercentile(g_waitDurationsMs, 0.50), TimingPercentile(g_renderWindowMs, 0.50),
                    VR_HeadsetRefreshHz());
        }
        // Exception guards (guard_count.h): the first block always, so every log
        // shows the counters ran, then only when the totals change.
        {
            static bool guardSummaryShown = false;
            static GuardTotals guardSummaryLast{};
            char guardSummary[1536];
            const GuardTotals guardNow = GuardSummaryFormat(guardSummary, sizeof(guardSummary));
            if (!guardSummaryShown || guardNow.sites != guardSummaryLast.sites ||
                guardNow.hits != guardSummaryLast.hits)
            {
                LOG("guard summary: %s", guardSummary);
                guardSummaryShown = true;
                guardSummaryLast = guardNow;
            }
        }
        g_timingLogStartMs = timingNowMs;
    }

    PollEvents();
    if (g_state != State::Ready || !g_sessionRunning)
        return;

    // Auto-enter/exit VR when a level loads/unloads (no F2/F11 needed).
    Game_AutoVrTick();
    // Allocate/recreate the two exact-output caches at Present, before the next
    // prepared frame can reach Halo 2's render hooks. BeginPair is therefore a
    // pointer-only proof and never performs COM work or allocation.
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2 && g_stereoEnabled.load(std::memory_order_acquire) &&
        g_gameBackbufferDescValid && g_device)
    {
        (void)EnsureEyeCaches(g_gameBackbufferDesc);
    }
    if (!g_config.scope_enabled || !Game_IsHeadTracking())
        VR_SetScopeActive(false);

    // The held gun's profile (alignment and lens row) is applied here,
    // on the render thread, before the frame that gates the scope glass on it.
    VR_UpdateWeaponAlignment();
    SubmitPreparedFrame(sc);
}

void VR_AfterPresent(IDXGISwapChain* sc, int64_t presentStartQpc, int64_t presentEndQpc, HRESULT presentResult)
{
    if (presentEndQpc >= presentStartQpc && presentStartQpc)
        g_presentDurationsMs.Add(QpcMs(presentEndQpc - presentStartQpc));

    if constexpr (kEnableFramePacingTransitionCapture)
    {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        if (g_framePacingPending.serial && g_framePacingPending.submitted)
        {
            g_framePacingPending.presentStartQpc = presentStartQpc;
            g_framePacingPending.presentEndQpc = presentEndQpc;
            g_framePacingPending.afterPresentQpc = now.QuadPart;
            g_framePacingPending.presentResult = presentResult;
            PublishFramePacingRecord(g_framePacingPending);
        }
        g_framePacingPending = {};
    }

    // Present has advanced the flip-model chain. Retain its reported current
    // buffer outside all camera/render hooks for ODST's direct death capture.
    if (sc)
    {
        UINT currentIndex = 0;
        if (g_flipIndexOwner != sc)
        {
            if (g_flipIndexChain)
                g_flipIndexChain->Release();
            g_flipIndexChain = nullptr;
            g_flipIndexOwner = sc;
            sc->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&g_flipIndexChain));
        }
        if (g_flipIndexChain)
            currentIndex = g_flipIndexChain->GetCurrentBackBufferIndex();
        ID3D11Texture2D* next = nullptr;
        if (SUCCEEDED(sc->GetBuffer(currentIndex, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&next))) && next)
        {
            ID3D11Texture2D* previous = g_nextGameBackbuffer.exchange(next, std::memory_order_acq_rel);
            if (previous)
                previous->Release();
        }
    }

    if (g_state != State::Ready || !g_sessionRunning)
        return;
    // Diagnostic-only marker immediately before next-frame preparation.
    PublishWeaponOrderMarker(WeaponOrderEventKind::AfterPresentBeforePrepare);
    PrepareNextFrame();
}

void VR_FramePacingWorkerPoll()
{
    NativeMenuPointer_ReportFailures();
    // Keep exceptional pipeline reporting off the game's render path. Packet
    // misses are counted there with one atomic increment and summarized here.
    static uint64_t reportedPacketMisses = 0;
    static uint64_t reportedWaitFailures = 0;
    static uint64_t reportedSignalFailures = 0;
    static uint64_t lastPacketMissReportMs = 0;
    static uint64_t lastWaitFailureReportMs = 0;
    static uint64_t lastSignalFailureReportMs = 0;
    static bool pipelineFaultReported = false;
    const uint64_t workerNowMs = GetTickCount64();
    static uint64_t lastHalo2XrPairReportMs = 0;
    if (!lastHalo2XrPairReportMs || workerNowMs - lastHalo2XrPairReportMs >= 2000)
    {
        const uint64_t submitted = g_halo2XrPairsSubmitted.exchange(0, std::memory_order_relaxed);
        const uint64_t dropped = g_halo2XrPairsDropped.exchange(0, std::memory_order_relaxed);
        const uint64_t repeated = g_halo2XrPairsRepeated.exchange(0, std::memory_order_relaxed);
        uint32_t reason = g_halo2LastDropReason.exchange(0, std::memory_order_relaxed);
        static const char* const kDropReasons[] = {
            "none", "render-target redirect incomplete", "eye upload incomplete", "projection incomplete"};
        if (reason >= sizeof(kDropReasons) / sizeof(kDropReasons[0]))
            reason = 0;
        if (submitted || dropped || TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
        {
            LOG("Halo 2 synchronous XR publish: %llu exact-current-"
                "serial pairs submitted, %llu last-pair repeats (game below "
                "panel rate), %llu recoverable frame drops in 2s; last drop: "
                "%s (two renders/frame; no cross-frame eye mixing; drops "
                "keep the core/session armed)",
                static_cast<unsigned long long>(submitted), static_cast<unsigned long long>(repeated),
                static_cast<unsigned long long>(dropped), kDropReasons[reason]);
        }
        lastHalo2XrPairReportMs = workerNowMs;
    }
    static uint64_t lastHalo4XrPairReportMs = 0;
    if (!lastHalo4XrPairReportMs || workerNowMs - lastHalo4XrPairReportMs >= 2000)
    {
        const uint64_t submitted = g_halo4XrPairsSubmitted.exchange(0, std::memory_order_relaxed);
        const uint64_t dropped = g_halo4XrPairsDropped.exchange(0, std::memory_order_relaxed);
        uint32_t reason = g_halo4LastXrPairDropReason.exchange(0, std::memory_order_relaxed);
        static const char* const kDropReasons[] = {"none", "eye upload incomplete", "projection incomplete"};
        if (reason >= sizeof(kDropReasons) / sizeof(kDropReasons[0]))
            reason = 0;
        if (submitted || dropped || TitleAdapter_GetActiveTitle() == GameTitle::Halo4)
        {
            LOG("Halo 4 XR publish: %llu pairs submitted, %llu "
                "recoverable frame drops in 2s; last drop: %s (recoverable "
                "drops keep the core/session armed)",
                static_cast<unsigned long long>(submitted), static_cast<unsigned long long>(dropped),
                kDropReasons[reason]);
        }
        lastHalo4XrPairReportMs = workerNowMs;
    }
    const uint64_t packetMisses = g_waitPacketMisses.load(std::memory_order_relaxed);
    if (!packetMisses)
    {
        reportedPacketMisses = 0;
        lastPacketMissReportMs = 0;
    }
    else if (packetMisses != reportedPacketMisses &&
             (!lastPacketMissReportMs || workerNowMs - lastPacketMissReportMs >= 1000))
    {
        LOG("pacing: wait-worker packet unavailable; skipped XR preparation "
            "without an inline wait (%llu misses total)",
            static_cast<unsigned long long>(packetMisses));
        reportedPacketMisses = packetMisses;
        lastPacketMissReportMs = workerNowMs;
    }
    {
        // Each of these would be an unpaced frame without the wait-again rule.
        static uint64_t reportedStaleWakes = 0, lastStaleWakeReportMs = 0;
        const uint64_t staleWakes = g_waitStaleWakes.load(std::memory_order_relaxed);
        if (staleWakes != reportedStaleWakes && workerNowMs - lastStaleWakeReportMs >= 10000)
        {
            LOG("pacing: %llu wake-ups found no wait packet and waited again for the headset "
                "(each would otherwise have been an unpaced, unsubmitted frame; %llu total)",
                static_cast<unsigned long long>(staleWakes - reportedStaleWakes),
                static_cast<unsigned long long>(staleWakes));
            reportedStaleWakes = staleWakes;
            lastStaleWakeReportMs = workerNowMs;
        }
    }

    const uint64_t waitFailures = g_waitFailuresObserved.load(std::memory_order_relaxed);
    if (!waitFailures)
    {
        reportedWaitFailures = 0;
        lastWaitFailureReportMs = 0;
    }
    else if (waitFailures != reportedWaitFailures &&
             (!lastWaitFailureReportMs || workerNowMs - lastWaitFailureReportMs >= 1000))
    {
        LOG("timing: worker xrWaitFrame failure observed (%llu total): %s",
            static_cast<unsigned long long>(waitFailures), XrStr(g_waitedStateResult.load(std::memory_order_acquire)));
        reportedWaitFailures = waitFailures;
        lastWaitFailureReportMs = workerNowMs;
    }

    const uint64_t signalFailures = g_waitEventSignalFailures.load(std::memory_order_relaxed);
    if (!signalFailures)
    {
        reportedSignalFailures = 0;
        lastSignalFailureReportMs = 0;
    }
    else if (signalFailures != reportedSignalFailures &&
             (!lastSignalFailureReportMs || workerNowMs - lastSignalFailureReportMs >= 1000))
    {
        LOG("pacing: frame-wait event signalling failed (%llu failures total); "
            "exact sequence predicates remain authoritative",
            static_cast<unsigned long long>(signalFailures));
        reportedSignalFailures = signalFailures;
        lastSignalFailureReportMs = workerNowMs;
    }

    const bool pipelineFaulted = g_waitPipelineFaulted.load(std::memory_order_acquire);
    if (pipelineFaulted && !pipelineFaultReported)
    {
        LOG("pacing: exact wait-worker pipeline faulted; no render-thread "
            "xrWaitFrame will be substituted");
        pipelineFaultReported = true;
    }
    else if (!pipelineFaulted)
    {
        pipelineFaultReported = false;
    }

    if constexpr (!kEnableFramePacingTransitionCapture)
        return;
    else
    {
        FramePacingWorkerState& worker = g_framePacingWorker;
        FramePacingRecord record{};
        while (ConsumeFramePacingRecord(record))
        {
            if (worker.capturing)
            {
                const FramePacingRecord& previous = worker.capture[worker.captureCount - 1];
                if (record.sessionEpoch != previous.sessionEpoch || !IsEligiblePacingRecord(record))
                {
                    FinishPacingCapture(worker, "eligibility-ended");
                }
                else
                {
                    if (record.serial != previous.serial + 1 || !IsExactWaitPair(previous, record))
                    {
                        worker.exact = false;
                    }
                    if (worker.captureCount < worker.capture.size())
                        worker.capture[worker.captureCount++] = record;
                    else
                        worker.exact = false;
                    worker.lastDataMs = GetTickCount64();
                    if (worker.postRemaining)
                        --worker.postRemaining;
                    if (!worker.postRemaining || worker.captureCount == worker.capture.size())
                    {
                        FinishPacingCapture(worker, "post-roll-complete");
                    }
                    continue;
                }
            }

            if (!IsEligiblePacingRecord(record))
            {
                ClearPacingHistory(worker);
                continue;
            }

            const FramePacingRecord* previous = LastPacingHistory(worker);
            if (previous && (previous->sessionEpoch != record.sessionEpoch || previous->serial + 1 != record.serial))
            {
                ClearPacingHistory(worker);
                previous = nullptr;
            }
            if (previous && IsMaterialFramePeriodTransition(static_cast<uint64_t>(previous->predictedDisplayPeriodNs),
                                static_cast<uint64_t>(record.predictedDisplayPeriodNs)))
            {
                BeginPacingCapture(worker, record, previous->predictedDisplayPeriodNs);
            }
            else
            {
                AddPacingHistory(worker, record);
            }
        }

        if (worker.capturing && worker.lastDataMs && GetTickCount64() - worker.lastDataMs >= 3000)
        {
            worker.exact = false;
            FinishPacingCapture(worker, "post-roll-timeout");
        }
    }
}

void VR_NotifyCameraTransform()
{
    const uint64_t serial = g_preparedSerialPublished.load(std::memory_order_acquire);
    if (!serial)
        return;
    uint64_t observed = g_cameraSerialObserved.load(std::memory_order_relaxed);
    if (observed == serial ||
        !g_cameraSerialObserved.compare_exchange_strong(observed, serial, std::memory_order_acq_rel))
        return;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const uint64_t prepared = g_prepareQpcPublished.load(std::memory_order_acquire);
    if (prepared && static_cast<uint64_t>(now.QuadPart) >= prepared)
    {
        const double delayMs = QpcMs(now.QuadPart - static_cast<LONGLONG>(prepared));
        g_firstCameraDelayUs.store(static_cast<uint64_t>(delayMs * 1000.0), std::memory_order_relaxed);
    }
}

bool VR_OnResizeBuffers(IDXGISwapChain* sc)
{
    HaloCE_ForgetPresentationTexture();
    ReleaseCeDesktopMirror();
    ReleaseScopeInset();
    if (g_scopeImageProbe.staging)
    {
        g_scopeImageProbe.staging->Release();
        g_scopeImageProbe = {};
    }
    VR_ResetHalo2SynchronousStereo();
    // Resize is rare and must exclude the worker while DXGI owns the buffer
    // transition. VR_AfterResizeBuffers releases this lock after the real call.
    AcquireSRWLockExclusive(&g_reachDisplayResourceLock);
    g_reachResizeActive.store(true, std::memory_order_release);
    InvalidateReachPresentAdmission();
    DiscardReachDisplaySnapshots(true);
    // The game is about to destroy its backbuffer; anything of ours that
    // references it must go first or the resize fails. The tracked history
    // targets are resolution-dependent too — drop and re-learn them.
    ReleaseSourceViews();
    ReleaseTheaterProjectionResources();
    // DLSS holds references to the game's depth views and eye-shaped
    // textures; they go with the scene target they describe.
    ReleaseDlssResources();
    if (g_sceneColorRtv)
    {
        g_sceneColorRtv->Release();
        g_sceneColorRtv = nullptr;
    }
    // Halo 4 must re-learn its scene target from scratch for the next level:
    // the retained candidate pointers belong to resources that are going away.
    Halo4ResetSceneTargetDiscovery();
    if (g_sceneColorResource)
    {
        g_sceneColorResource->Release();
        g_sceneColorResource = nullptr;
    }
    g_gameBackbufferDesc = {};
    g_gameBackbufferDescValid = false;
    // Buffer 0 and the engine's record-0 views are invalid at this edge. A
    // copied readiness token must fail immediately even if COM reuses every
    // pointer value after ResizeBuffers.
    // Reach retains buffer 0 in its capture lease. Release that graph before
    // DXGI resizes the active chain, after the existing bounded reader drain.
    const bool activeReachSwapchain =
        sc && (g_reachCaptureProof.continuity.swapchainIdentity == reinterpret_cast<uintptr_t>(sc) ||
                  (TitleAdapter_GetActiveTitle() == GameTitle::HaloReach &&
                      ReachPresentMatchesEngineSwapchain(g_reachDisplayEpoch, sc)));
    const bool resourcesReleased = ResetReachDisplayCandidateLocked(false, activeReachSwapchain);
    if (ID3D11Texture2D* retained = g_nextGameBackbuffer.exchange(nullptr, std::memory_order_acq_rel))
        retained->Release();
    return resourcesReleased;
}

void VR_NoteCockpitScreen(bool wanted)
{
    g_cockpitScreenMs.store(wanted ? GetTickCount64() : 0, std::memory_order_release);
}
bool VR_CockpitScreenActive()
{
    const uint64_t at = g_cockpitScreenMs.load(std::memory_order_acquire), now = GetTickCount64();
    // vehicle_first_person is the live seat-view flag; the game's own
    // third-person vehicle camera shows no cockpit screen or monitors.
    return g_config.vehicle_cockpit_screen && g_config.vehicle_first_person && at && now >= at && now - at <= 250;
}
void VR_NotePositionRecenter()
{
    g_positionRecenterEpoch.fetch_add(1, std::memory_order_acq_rel);
}
void VR_RequestRecenter()
{
    VR_RecalibratePhysicalCrouch();
    g_recenterRequested.store(true, std::memory_order_release);
}

void VR_RequestPausePresentation(bool paused)
{
    const bool previous = g_pauseTarget.exchange(paused);
    if (paused)
        g_pauseEndedByGameMs.store(0, std::memory_order_release);
    else if (previous)
    {
        g_lastPauseEndMs.store(GetTickCount64(), std::memory_order_release);
        // MCC's Save & Quit confirmation clears the title's pause flag while it is
        // still on screen; without a resume press this is that dialog (or a game-side resume).
        const uint64_t endNow = GetTickCount64();
        if (game_menu_pointer::PauseEndedByGame(
                endNow, g_lastResumeInputMs.load(std::memory_order_acquire), NativeMenuPointer_LastClickMs()))
        {
            g_pauseEndedByGameMs.store(endNow, std::memory_order_release);
            if (g_config.menu_dialog_keys)
                LOG("menu keys: the game ended the pause without a resume press; A = Enter, stick = Left/Right until the first trigger pull or step (max 20 s; a Save & Quit dialog may be up)");
        }
    }
    if (previous != paused || g_pausePresentation.load() != paused)
        g_pauseRequest = paused ? 1 : 0;
}

bool VR_IsPausePresentation()
{
    return g_pausePresentation.load();
}

bool VR_IsPausePresentationTarget()
{
    return g_pauseTarget.load();
}

void VR_ToggleScreenFollow()
{
    const bool on = !g_screenFollow.load();
    g_screenFollow = on;
    LOG("screen-follow %s", on ? "on (screen follows head)" : "off (screen world-locked)");
}

void VR_ToggleStereo()
{
    const bool on = !g_stereoEnabled.load();
    g_stereoEnabled = on;
    if (on && !Game_IsStereoGeometryOnlyBringup() && !Game_UsesTitleOwnedHeadTracking())
        Game_ForcePositional();
    g_renderEye = 0;
    g_eyeHasImage[0] = g_eyeHasImage[1] = false;
    VR_ResetHalo2SynchronousStereo();
    g_halo4EyeSerial[0].store(0, std::memory_order_release);
    g_halo4EyeSerial[1].store(0, std::memory_order_release);
    Game_SetStereoEye(on ? 0 : -1);
    if (on && Game_IsStereoGeometryOnlyBringup())
        LOG("Halo 2 temporal stereo presentation ON (stock head pose; "
            "one render per frame, adjacent-eye pair)");
    else if (on && TitleAdapter_GetActiveTitle() == GameTitle::HaloCE)
        LOG("CE Original/Anniversary stereo + positional 6DoF presentation ON");
    else if (on && Game_UsesTitleOwnedHeadTracking())
        LOG("Halo 2 synchronous stereo+6DOF presentation ON "
            "(title-owned headset pose; exact-current-serial eye pair)");
    else
        LOG("alternate-eye stereo %s%s", on ? "ON" : "OFF",
            on && !Game_IsHeadTracking() ? " (enable head tracking with F2)" : "");
}

bool VR_IsStereoEnabled()
{
    return g_stereoEnabled.load();
}

void VR_NoteSmoothTurnApplied()
{
    g_smoothTurnAppliedMs.store(GetTickCount64(), std::memory_order_relaxed);
}
bool VR_IsCutsceneTheaterActive()
{
    return g_cutsceneTheaterActive.load(std::memory_order_acquire);
}

bool VR_ShouldRenderPreparedFrame()
{
    return g_preparedShouldRender.load(std::memory_order_acquire);
}

float VR_HeadsetRefreshHz()
{
    const uint64_t period = g_displayPeriodNs.load(std::memory_order_relaxed);
    return period ? (float)(1000000000.0 / (double)period) : 0.0f;
}

bool VR_IsFramePacingOwned()
{
    // True once the runtime is driving our cadence through xrWaitFrame. Until
    // then the desktop present must keep the game's own timing untouched.
    return g_state == State::Ready && g_sessionRunning && g_displayPeriodNs.load(std::memory_order_relaxed) != 0;
}

void VR_DetachGamePresentation()
{
    // Game_AutoVrTick calls this from Present, after Halo has stopped issuing
    // camera renders and before this frame is submitted to OpenXR. Do not tear
    // down the session or shared MCC D3D hooks: the flat shell still needs
    // them. Only disarm Halo's per-eye work and release its retained render
    // target so a different MCC engine can own the shared device cleanly.
    g_cutsceneTheaterActive.store(false, std::memory_order_release);
    g_cutsceneTheaterTransition.Reset(false);
    g_cutsceneTheaterWasQualified = false;
    g_cutsceneTheaterEntryBlocked = false;
    g_cutsceneTheaterAuthoredAspect = 0.0f;
    g_authoredReticleReady = false;
    g_authoredReticleSerial = 0;
    g_authoredReticleUploadedSerial = 0;
    g_reticleContainsAuthored = false;
    // Known-good art belongs to the title that captured it; a new title must
    // measure its own before anything is held on its behalf.
    g_authoredReticleGoodValid = false;
    g_authoredReticleProbePending = false;
    g_authoredReticleHeldBlank = false;
    g_authoredReticleGoodInk = 0;
    g_authoredReticleConsecutiveHolds = 0;
    g_reticlePaintedOpacity = -1.0f;
    Game_ResetAuthoredCrosshairKey();
    g_reticleOwnerEpoch.fetch_add(1, std::memory_order_acq_rel);
    if (g_stereoEnabled.load())
        VR_ToggleStereo();
    else
        Game_SetStereoEye(-1);
    g_renderEye = 0;
    g_eyeHasImage[0] = g_eyeHasImage[1] = false;
    VR_ResetHalo2SynchronousStereo();
    g_halo4EyeSerial[0].store(0, std::memory_order_release);
    g_halo4EyeSerial[1].store(0, std::memory_order_release);
    g_stereoValidationDone = false;
    g_rasterOwnerThread.store(0, std::memory_order_release);
    g_rasterEye = -1;
    g_rasterRedirected[0] = g_rasterRedirected[1] = false;
    g_rasterScope = false;
    g_scopeRedirected = false;
    g_scopeActive = false;
    g_scopeGlassLive = false;
    g_scopeHasImage = false;
    g_scopeResetRequested = true;
    // The worker owns gate/resource mutation. Present only revokes the
    // admission stamp so a proof racing this detach cannot publish.
    InvalidateReachPresentAdmission();
    HaloCE_ForgetPresentationTexture();
    ReleaseCeDesktopMirror();
    ReleaseScopeInset();
    if (g_scopeImageProbe.staging)
    {
        g_scopeImageProbe.staging->Release();
        g_scopeImageProbe = {};
    }
    ReleaseSourceViews();
    ReleaseIqChain();
    ReleaseIqTimer();
    ReleaseTheaterProjectionResources();
    // DLSS holds references to the game's depth views and eye-shaped
    // textures; they go with the scene target they describe.
    ReleaseDlssResources();
    if (g_sceneColorRtv)
    {
        g_sceneColorRtv->Release();
        g_sceneColorRtv = nullptr;
    }
    // Halo 4 must re-learn its scene target from scratch for the next level:
    // the retained candidate pointers belong to resources that are going away.
    Halo4ResetSceneTargetDiscovery();
    if (g_sceneColorResource)
    {
        g_sceneColorResource->Release();
        g_sceneColorResource = nullptr;
    }
}

void VR_EndRasterEye()
{
    const int completedEye = g_rasterEye.load(std::memory_order_relaxed);
    const bool halo4CompletedEye =
        (completedEye == 0 || completedEye == 1) && TitleAdapter_GetActiveTitle() == GameTitle::Halo4;
    // This deliberately writes back only after the scene target was
    // already proven before this eye began.  The eye that first latches the
    // target remains a learning eye; treating it as steady state would copy an
    // eye cache that was not produced by the redirect transaction.
    const bool halo4HadSceneTarget = halo4CompletedEye && g_sceneColorRtv != nullptr;
    if (halo4CompletedEye)
        Halo4ResolveSceneTargetAtEyeEnd();

    // Once the real Halo 4 scene target is known,
    // put the completed per-eye image back into the texture behind it.  The
    // engine consumes that texture after render_view returns for the desktop
    // mirror and damage effects.  This is intentionally in addition to the VR
    // eye cache, not an alternative capture path.
    if (VrHalo4SceneWritebackEligible(halo4CompletedEye, completedEye, g_context != nullptr,
            halo4HadSceneTarget && g_sceneColorRtv != nullptr,
            halo4CompletedEye && g_eyeCache[completedEye] != nullptr))
    {
        ID3D11Resource* sceneResource = nullptr;
        g_sceneColorRtv->GetResource(&sceneResource);
        if (sceneResource)
        {
            g_context->CopyResource(sceneResource, g_eyeCache[completedEye]);
            sceneResource->Release();
        }
    }
    Halo2SynchronousEyeScope& halo2Scope = g_halo2SynchronousEyeScope;
    if (halo2Scope.active && halo2Scope.eye >= 0 && halo2Scope.eye <= 1)
    {
        const uint64_t reserved = Halo2ReservedSynchronousSerial(halo2Scope.preparedSerial);
        if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) == reserved &&
            g_halo2SynchronousPairGeneration.load(std::memory_order_relaxed) == halo2Scope.generation &&
            g_halo2SynchronousPairResourceEpoch.load(std::memory_order_relaxed) == halo2Scope.resourceEpoch)
        {
            // render_view has returned, so the engine's own pipeline for
            // this eye - world pass, resolve, postprocess, final output -
            // has all been issued into its own backbuffer. Copy that
            // finished frame. Nothing of the engine's was redirected.
            if (Halo2CopyFinishedEyeFrame(halo2Scope))
                halo2Scope.captures = 1;
            g_halo2SynchronousCaptureCount[halo2Scope.eye].store(halo2Scope.captures, std::memory_order_relaxed);
            g_halo2SynchronousCaptureSerial[halo2Scope.eye].store(halo2Scope.preparedSerial, std::memory_order_release);
        }
        halo2Scope = {};
    }
    // Promote any newly identified history, then save this eye's copies of
    // every tracked target before the other eye (or next frame) overwrites
    // them. A ping-pong pair only reveals its read side a frame after its
    // write side, so discovery stays open for a fixed window (~2 s of
    // stereo) instead of closing at the first find.
    g_eyeGpuTiming.EndRaster(g_context);
    DlssCaptureEyeDepth(completedEye);
    DlssKickEyeAtEyeEnd(completedEye);
    // A borrowed engine depth view expires when this eye returns. A failed
    // kick must not retry at Present using the next eye's overwritten depth.
    if (completedEye >= 0 && completedEye < 2 && g_dlssEye[completedEye].depthDirect)
        g_dlssEye[completedEye].depthValid = false;
    g_eyeGpuTiming.End(g_context);
    g_rasterOwnerThread.store(0, std::memory_order_release);
    g_rasterEye = -1;
}

namespace
{
}

// One presented frame for the adaptive scope pass scale. Only frames with an
// opted-in scope pass in the last kScopedGapMs count; the title changing or the key
// off returns to full size. Each change logs the frame-interval window that made it.
static void UpdateScopeAdaptiveScale(float intervalMs)
{
    using namespace scope_render_scale;
    const GameTitle title = TitleAdapter_GetActiveTitle();
    const uint64_t nowMs = GetTickCount64();
    if (title != g_scopeScaleTitle || !g_config.scope_adaptive_resolution || !ScopeAdaptiveTitle(title))
    {
        if (g_scopeScaleController.Scale() < 1.0f)
            LOG("scope resolution %.2f -> 1.00 (%s)", g_scopeScaleController.Scale(),
                title != g_scopeScaleTitle ? "title changed" : "scope_adaptive_resolution off");
        g_scopeScaleController.Reset();
        g_scopeScaleTitle = title;
        g_scopeAdaptiveScale.store(1.0f, std::memory_order_release);
        return;
    }
    const uint64_t passMs = g_scopeScalePassMs.load(std::memory_order_acquire);
    const bool scoped = passMs && nowMs >= passMs && nowMs - passMs <= kScopedGapMs;
    const float hz = VR_HeadsetRefreshHz(), budget = BudgetMs(hz);
    const Change c = g_scopeScaleController.Update(intervalMs, scoped, budget, nowMs);
    if (!c.changed)
        return;
    g_scopeAdaptiveScale.store(c.to, std::memory_order_release);
    char hold[96] = "";
    if (c.holdMs)
        snprintf(hold, sizeof hold, "; full size again at the earliest in %.0f s, once the last 3 s run clean",
            c.holdMs / 1000.0);
    else
        snprintf(hold, sizeof hold, "; after %.1f s at half size", c.lowMs / 1000.0);
    LOG("scope resolution %.2f -> %.2f (%s): last %d scoped frames p50 %.2f p95 %.2f max %.2f ms, "
        "%d over %.2f ms (budget %.2f ms, %.1f Hz)%s",
        c.from, c.to, DlssTitleName(title), c.frames, c.p50, c.p95, c.maxMs, c.misses, budget * kMissFactor, budget, hz,
        hold);
}

bool VR_RedirectRenderTargets(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView* const* input,
    ID3D11DepthStencilView* depth, ID3D11RenderTargetView** output)
{
    // Halo 2 never redirects a render target. Its classic renderer draws the
    // world straight into the backbuffer (a draw census showed the primary scene
    // RTV slot 0x197EE60 and the backbuffer slot 0x197EE58 hold the SAME
    // view), and its postprocess then reads that texture back through the
    // engine's own SRV. Redirecting the binds put each eye's world pass in
    // our texture while the postprocess re-read the engine's untouched
    // backbuffer and painted it over both eyes: a validation run measured
    // 0/23842 differing pixels, i.e. identical eyes. Each eye is instead
    // copied whole after render_view returns (VR_EndRasterEye). Halo 2 is
    // also never fed through Halo 3's 0xA8 shape discovery below.
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
    {
        // Match by resource. Anniversary binds its own RTV over the
        // backbuffer texture, never the slot's RTV (g_halo2ScopeSourceRtv).
        if (g_halo2ScopeCopyActive && !g_halo2ScopeSourceBound &&
            AnyBoundViewOf(g_halo2ScopeSource, count, input,
                [](ID3D11RenderTargetView* view) -> const void*
                {
                    ID3D11Resource* resource = nullptr;
                    view->GetResource(&resource);
                    if (resource)
                        resource->Release();
                    return resource;
                }))
            g_halo2ScopeSourceBound = true;
        Halo2SynchronousEyeScope& halo2Scope = g_halo2SynchronousEyeScope;
        if (halo2Scope.active && count && input && input[0])
        {
            halo2Scope.lastBoundRtv = input[0];
            ++halo2Scope.binds;
            bool known = false;
            for (int i = 0; i < halo2Scope.boundCount && !known; ++i)
                known = halo2Scope.bound[i] == input[0];
            if (!known && halo2Scope.boundCount < kHalo2EyeBoundRtvSlots)
                halo2Scope.bound[halo2Scope.boundCount++] = input[0];
        }
        // DLSS depth for Halo 2: both renderers draw into the final target
        // directly, so the depth view bound together with it is the scene
        // depth; any other full-raster depth inside the eye is the fallback
        // rule. Pointer compares only, inside a raster-eye scope only.
        if (depth && VR_DlssWantsEyeCamera())
        {
            const int halo2Eye = g_rasterEye.load(std::memory_order_relaxed);
            if (halo2Eye >= 0 && halo2Eye <= 1)
            {
                ID3D11RenderTargetView* const finalRtv = g_halo2SynchronousFinalRtv.load(std::memory_order_relaxed);
                DlssNoteEyeDepth(depth, finalRtv && count && input && input[0] == finalRtv);
            }
        }
        return false;
    }
    ProbeFsrTargets(context, count, input);
    auto& hudRoute = g_nativeHudEyeRoute;
    if (hudRoute.bypassOmRedirect)
        return false;
    const int eye = g_rasterEye.load();
    const bool scope = g_rasterScope.load(std::memory_order_acquire);
    // DLSS depth for a title that renders its eyes outside the raster-eye
    // scope (Reach): inside its depth window every depth view bound is
    // classified by the full-raster rule. Pointer compares only.
    if (depth && eye < 0 && VR_DlssWantsEyeCamera() && g_dlssDepthWindowEye.load(std::memory_order_relaxed) >= 0)
        DlssNoteEyeDepth(depth, false);
    const bool nativeHud = hudRoute.active && hudRoute.eye >= 0 && hudRoute.eye <= 1;
    if ((!scope && !nativeHud && (eye < 0 || eye > 1)) || !input || !output || !g_gameSwapchain)
        return false;

    // Halo 4 rebinds the learned scene target inside CUI playback. While the
    // face-crosshair art is being captured, route that exact bind to the
    // authored texture instead of the eye. This is the only ownership change:
    // the existing VR crosshair still owns all placement.
    if (g_reticleCaptureState.active && TitleAdapter_GetActiveTitle() == GameTitle::Halo4)
    {
        ID3D11RenderTargetView* const captureTarget =
            g_reticleCaptureState.publishesAuthored ? g_authoredReticleRtv : g_authoredReticleDiscardRtv;
        bool captureChanged = false;
        for (UINT i = 0; i < count; ++i)
        {
            output[i] = input[i];
            if (AuthoredReticleCaptureOwnsSceneBind(
                    true, input[i] && input[i] == g_sceneColorRtv, captureTarget != nullptr))
            {
                output[i] = captureTarget;
                captureChanged = true;
            }
        }
        if (captureChanged)
        {
            g_authoredReticleOmReroutes.fetch_add(1, std::memory_order_relaxed);
            // Halo 4 rebinds this exact scene target up to 3 times inside one
            // captured replay (measured: "9 exact capture OM reroutes" against
            // 3 captures). Each rebind can carry whatever viewport the engine's
            // own preceding pass left set - SCENEPROBE measured the SAME
            // learned RTV bound with a 947x683 viewport at one point and a
            // full-raster viewport at another. Left alone, the 3 rebinds paint
            // 3 differently-scaled passes into the same 512x512 texture, which
            // is a smeared composite, not a wrong crop - this is the direct
            // cause of the "some random asset" result. Putting the ONE viewport
            // this capture opened with back here, immediately after our own OM
            // rewrite and therefore after the engine's own preceding
            // RSSetViewports for this same rebind, keeps every rebind
            // consistent with the others instead of independently scaled.
            if (g_reticleCaptureState.framingCaptured && context == g_context)
            {
                g_context->RSSetViewports(1, &g_reticleCaptureState.captureViewport);
                g_context->RSSetScissorRects(1, &g_reticleCaptureState.captureScissor);
                g_authoredReticleFramingReasserts.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return captureChanged;
    }
    int targetEye = eye;
    if (nativeHud)
        targetEye = hudRoute.eye;
    ID3D11RenderTargetView* target = scope ? g_scopeCacheRtv : g_eyeCacheRtvs[targetEye];
    bool changed = false;      // any slot rewritten (scene color or sun shaft)
    bool sceneChanged = false; // scene-color redirect only: marks the eye image valid
    for (UINT i = 0; i < count; ++i)
    {
        output[i] = input[i];
        if (!input[i])
            continue;

        // ODST's CHUD phase is logically inside this eye render just like Halo
        // 3, but its recompiled phase can bind the flat output RTV. Redirect
        // only the exact target saved at the proven CHUD phase boundary. This
        // is a pointer comparison in the OM hot hook; all COM work occurs once
        // at phase entry/exit.
        if (nativeHud)
        {
            if (target && input[i] == hudRoute.phaseOutputRtv)
            {
                output[i] = target;
                g_nativeHudProvenOmMatches.fetch_add(1, std::memory_order_relaxed);
                changed = true;
            }
            // A phase-local target which is not the captured proven pointer
            // stays stock. In particular, never enter scene-target discovery
            // while a CHUD phase is active.
            continue;
        }

        // Normal steady-state path: two pointer comparisons and no COM calls.
        if (g_sceneColorRtv)
        {
            if (input[i] == g_sceneColorRtv && target)
            {
                output[i] = target;
                changed = true;
                sceneChanged = true;
            }
            continue;
        }

        // One-time discovery, before the exact scene RTV has been learned.
        ID3D11Resource* resource = nullptr;
        input[i]->GetResource(&resource);
        ID3D11Texture2D* candidate = nullptr;
        D3D11_TEXTURE2D_DESC candidateDesc{};
        const bool isTexture = resource && SUCCEEDED(resource->QueryInterface(
                                               __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&candidate)));
        if (isTexture)
            candidate->GetDesc(&candidateDesc);

        // Halo 3's completed frame is the unique full-resolution typeless RGBA
        // resource with RTV+SRV+UAV bindings at the end of the inner render.  The
        // preceding typed RGBA RT is an intermediate and can remain black.  Keep
        // our eye caches typed (from the game backbuffer) so they can be sampled
        // directly by the OpenXR blit.
        const bool isInternalSceneColor =
            i == 0 && candidate && g_gameBackbufferDescValid && candidateDesc.Width == g_gameBackbufferDesc.Width &&
            candidateDesc.Height == g_gameBackbufferDesc.Height &&
            candidateDesc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS &&
            (candidateDesc.BindFlags &
                (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS)) ==
                (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);

        // Halo 4 does NOT present that shape, and picking its scene target by
        // "first thing that looks right" is measurably WRONG: an earlier rule latched
        // the FIRST full-size 8-bit colour target bound in the eye and the
        // headset showed unlit meshes with no lighting, shadows, post or HUD
        // in one test. That target is an early pass; everything
        // after it composites into other targets we never touched.
        //
        // So Halo 4 does not decide at bind time at all. It OBSERVES a whole
        // eye, remembers the LAST qualifying target bound in it, and latches
        // that once two consecutive eyes agree - the last full-size colour
        // target written inside the eye scope is the composited result, not an
        // input to it. Qualification stays narrow: full backbuffer size,
        // single-sampled, renderable AND samplable, 8-bit RGBA/BGRA (MCC's
        // backbuffer here is R8G8B8A8_UNORM, so the composited image is in
        // that family; anything wider still has tonemapping ahead of it).
        const bool rgba8Family = candidateDesc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ||
                                 candidateDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
                                 candidateDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                                 candidateDesc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS ||
                                 candidateDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                                 candidateDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        const bool halo4Learning =
            !isInternalSceneColor && i == 0 && candidate && g_gameBackbufferDescValid && eye >= 0 && eye <= 1 &&
            TitleAdapter_GetActiveTitle() == GameTitle::Halo4 && candidateDesc.Width == g_gameBackbufferDesc.Width &&
            candidateDesc.Height == g_gameBackbufferDesc.Height && candidateDesc.SampleDesc.Count == 1 && rgba8Family &&
            (candidateDesc.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE)) ==
                (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        if (halo4Learning)
        {
            // Remember it and keep going. The eye's LAST one wins, decided in
            // VR_EndRasterEye. Nothing is redirected during a learning eye.
            D3D11_RENDER_TARGET_VIEW_DESC lastViewDesc{};
            input[i]->GetDesc(&lastViewDesc);
            g_h4LastCandidate = input[i];
            g_h4LastCandidateDesc = candidateDesc;
            g_h4LastCandidateViewFormat = lastViewDesc.Format;
            ++g_h4CandidatesThisEye;
        }
        // Never latch at bind time for Halo 4.
        const bool isHalo4SceneColor = false;

        const bool learnSceneColor = isInternalSceneColor || isHalo4SceneColor;
        D3D11_RENDER_TARGET_VIEW_DESC sceneViewDesc{};
        if (learnSceneColor)
            input[i]->GetDesc(&sceneViewDesc);
        D3D11_TEXTURE2D_DESC eyeDesc = candidateDesc;
        if (learnSceneColor)
            eyeDesc.Format = sceneViewDesc.Format;

        if (learnSceneColor && EnsureEyeCaches(eyeDesc) && g_eyeCacheRtvs[eye])
        {
            input[i]->AddRef();
            g_sceneColorRtv = input[i];
            g_sceneColorResource = resource;
            resource = nullptr;
            output[i] = g_eyeCacheRtvs[eye];
            changed = true;
            sceneChanged = true;
            // Say WHICH rule matched and with what shape. If the relaxed rule
            // ever latches the wrong target, this line plus the SCENEPROBE
            // census above are what identify the right one without another
            // guess.
            LOG("raster: learned scene-color RTV %p via the %s rule "
                "(%ux%u fmt=%u viewfmt=%u bind=0x%X); steady-state redirect "
                "is pointer-only",
                g_sceneColorRtv, isInternalSceneColor ? "Halo 3 exact-shape" : "Halo 4 relaxed", candidateDesc.Width,
                candidateDesc.Height, static_cast<UINT>(candidateDesc.Format), static_cast<UINT>(sceneViewDesc.Format),
                candidateDesc.BindFlags);
        }
        if (candidate)
            candidate->Release();
        if (resource)
            resource->Release();
    }
    if (sceneChanged)
    {
        if (scope)
        {
            g_scopeRedirected = true;
        }
        else
        {
            g_rasterRedirected[eye] = true;
        }
    }
    // DLSS depth tracking: pointer compares only, and only inside a real eye
    // render while the upscaler is selected.
    if (depth && !scope && eye >= 0 && eye <= 1 && VR_DlssWantsEyeCamera())
        DlssNoteEyeDepth(depth, sceneChanged);
    return changed;
}

void VR_GetPadState(VrPadState& out)
{
    if (!g_headCsInit)
    {
        out = {};
        return;
    }
    EnterCriticalSection(&g_headCs);
    out = g_padState;
    // The menu trigger must not also navigate native tabs or fire on resume.
    // Drain a consumed hold through release, even if the option is switched off.
    static std::atomic<bool> nativeMenuTriggerHeld{false};
    if (out.trigR <= 0.35f)
        nativeMenuTriggerHeld.store(false);
    else if (NativeMenuPointer_ConsumesTrigger())
        nativeMenuTriggerHeld.store(true);
    if (nativeMenuTriggerHeld.load())
        out.trigR = 0;
    const uint64_t now = GetTickCount64();
    const GameTitle weaponTitle = TitleAdapter_GetActiveTitle();
    const int weaponIndex = weapon_interaction::TitleIndex(weaponTitle);
    const unsigned weaponOptions =
        (g_config.manual_reload ? 1u : 0u) | (g_config.weapon_holsters ? 2u : 0u) | (g_config.left_handed ? 4u : 0u) |
        (!g_config.weapon_holster_slide ? 8u : 0u) | (g_config.weapon_holster_click ? 16u : 0u) |
        (g_config.weapon_needler_shake ? 32u : 0u) |
        (g_config.weapon_grenade_throw || g_config.weapon_grenade_type_slot || g_config.weapon_equipment_slot ? 64u
                                                                                                              : 0u) |
        (g_config.flashlight_head_gesture ? 128u : 0u);
    if (out.weaponOptions != weaponOptions || out.weaponSpace != g_contactSpaceEpoch.load(std::memory_order_acquire) ||
        weaponIndex < 0 || out.weaponReloadBinding != WeaponGestureBinding(weaponTitle, vr_mapping::Reload, now) ||
        out.weaponSwitchBinding != WeaponGestureBinding(weaponTitle, vr_mapping::SwitchWeapon, now) ||
        !weapon_interaction::Fresh(now, out.weaponSampleMs, weaponTitle, out.weaponTitle,
            TitleAdapter_GetGeneration(weaponTitle), out.weaponGeneration,
            (g_config.manual_reload || g_config.weapon_holsters || g_config.flashlight_head_gesture ||
                g_config.weapon_grenade_throw || g_config.weapon_grenade_type_slot || g_config.weapon_equipment_slot) &&
                Game_IsHeadTracking() && VR_IsStereoEnabled() &&
                TitleAdapter_GetRuntimeMode() == RuntimeMode::Gameplay &&
                g_sessionStateShared.load(std::memory_order_acquire) == XR_SESSION_STATE_FOCUSED && !Menu_IsOpen() &&
                !VR_IsPausePresentation() && !VR_IsPausePresentationTarget() && !VR_IsCutsceneTheaterActive()))
    {
        out.weaponButtons = 0;
        out.weaponConsumePrimary = out.weaponConsumeSupport = false;
    }
    if (now >= out.weaponPulseUntilMs)
        out.weaponButtons = 0;
    if (g_config.manual_reload &&
        out.weaponGraph != VR_GetWeaponModelIdentity(weaponTitle, out.weaponGeneration, out.weaponSpace, now))
        out.weaponButtons = 0;
    const uint64_t sampled = g_thumbrestDpadSampleMs.load(std::memory_order_acquire);
    if (!g_config.quest_thumbrest_dpad || !sampled || now < sampled || now - sampled > 250 ||
        g_sessionStateShared.load(std::memory_order_acquire) != XR_SESSION_STATE_FOCUSED || Menu_IsOpen())
    {
        out.thumbrestDpad = false;
        out.dpadX = out.dpadY = 0.0f;
    }
    LeaveCriticalSection(&g_headCs);
}

void VR_ObserveWeaponModel(GameTitle title, uint32_t generation, uint64_t identity) noexcept
{
    // Published for every consumer (zoom kind, pistol class, grab zones,
    // holsters, reload, alignment); each consumer applies its own switch.
    if (title != TitleAdapter_GetActiveTitle() || generation != TitleAdapter_GetGeneration(title))
    {
        // Diagnostic: a dropped publish leaves the held identity 0.
        static uint32_t loggedGeneration[16]{};
        const unsigned t = static_cast<unsigned>(title) & 15u;
        if (loggedGeneration[t] != generation + 1u)
        {
            loggedGeneration[t] = generation + 1u;
            LOG("weapon identity: title %u generation %u dropped (active title %u generation %u) identity %016llX",
                static_cast<unsigned>(title), generation, static_cast<unsigned>(TitleAdapter_GetActiveTitle()),
                TitleAdapter_GetGeneration(title), static_cast<unsigned long long>(identity));
        }
        return;
    }
    {
        // Every title: an identity the catalog does not know never reaches the
        // lens rows or zoom kind. One line per identity (bounded seen-set).
        static uint64_t loggedMiss[16]{};
        static unsigned missNext = 0;
        if (identity && !weapon_model::Find(title, identity) &&
            std::find(std::begin(loggedMiss), std::end(loggedMiss), identity) == std::end(loggedMiss))
        {
            loggedMiss[missNext++ & 15u] = identity;
            LOG("weapon identity: title %u identity %016llX has no catalog row", static_cast<unsigned>(title),
                static_cast<unsigned long long>(identity));
        }
    }
    (void)g_weaponModels.Publish(
        {title, generation, identity, g_contactSpaceEpoch.load(std::memory_order_acquire), GetTickCount64()});
}

uint64_t VR_GetWeaponModelIdentity(GameTitle title, uint32_t generation, uint64_t space, uint64_t now) noexcept
{
    if (title == GameTitle::HaloCE)
        return HaloCEFirstPerson_WeaponGraph(generation, space, now);
    return g_weaponModels.Read(title, generation, space, now);
}

namespace
{
}

void VR_PublishReloadTarget(GameTitle title, uint32_t generation, uint64_t identity, uint64_t serial,
    const contact_melee::TrackingToWorld& trackingToWorld, const float world[3]) noexcept
{
    if (!g_config.manual_reload || !world || title != TitleAdapter_GetActiveTitle() ||
        generation != TitleAdapter_GetGeneration(title))
        return;
    VrContactTrackingSnapshot current{};
    contact_melee::TrackingToWorld primaryController{};
    if (!VR_GetContactTrackingSnapshot(current) || current.serial != serial || !current.rawPrimaryValid ||
        !primaryController.SetPose(current.rawPrimaryOrientation, current.rawPrimaryPosition))
        return;
    weapon_interaction::ReloadTarget target{};
    if (weapon_interaction::BuildReloadTarget(title, generation, identity, current.referenceEpoch, GetTickCount64(),
            current.leftHanded, {world[0], world[1], world[2]}, trackingToWorld, primaryController, target))
        (void)g_reloadTargets.Publish(target);
}

void VR_UpdateWeaponAlignment()
{
    static GameTitle previousTitle = GameTitle::None;
    static uint32_t previousGeneration = 0;
    static weapon_profile::IdentityBridge held;
    const auto title = TitleAdapter_GetActiveTitle();
    const auto generation = TitleAdapter_GetGeneration(title);
    if (title != previousTitle || generation != previousGeneration)
        held.Reset();
    previousTitle = title;
    previousGeneration = generation;
    const uint64_t now = GetTickCount64();
    const uint64_t space = g_contactSpaceEpoch.load(std::memory_order_acquire);
    // Just before Present, CE Classic's scope pass has cleared the
    // eye passes' commit for the whole raise; read across it (CommittedPaletteMs).
    const uint64_t identity = title == GameTitle::HaloCE ? HaloCEFirstPerson_ProfileWeaponGraph(generation, space, now)
                                                         : VR_GetWeaponModelIdentity(title, generation, space, now);
    // A pause/F1 menu may stop native palette submission. Retain only the
    // previously admitted gun in this generation while the player edits it;
    // outside menus, bridge single-update zeros for up to 1 s.
    uint64_t waitedUs = 0;
    Config_ApplyWeaponProfileTimed(
        held.Update(identity, now, Menu_IsOpen() || VR_IsPausePresentation() || VR_IsPausePresentationTarget()),
        waitedUs);
    // The only other holder of the profile lock is the title
    // supervisor's Config_ApplyTitleProfile. Report the longest render-thread
    // wait each second there was one, and how many frames waited.
    static uint64_t windowStart = 0, windowMaxUs = 0;
    static unsigned windowFrames = 0;
    if (!windowStart)
        windowStart = now;
    if (waitedUs)
    {
        windowMaxUs = std::max(windowMaxUs, waitedUs);
        ++windowFrames;
    }
    if (now - windowStart >= 1000)
    {
        if (windowFrames)
            LOG("weapon profile: render thread waited for the profile lock on %u frame(s) in the last second, longest %llu us",
                windowFrames, static_cast<unsigned long long>(windowMaxUs));
        windowStart = now;
        windowMaxUs = 0;
        windowFrames = 0;
    }
}

void VR_PublishSupportInvocationReceipt(const support_grip::SupportInvocationReceipt& receipt) noexcept
{
    // Title hook only, and only for a title that actually owns an FP
    // interpolate seam: an unwired/foreign title must never poison the record
    // a consumer qualifies against.
    if (!VR_SupportGripWiredForTitle(receipt.title) ||
        !support_grip::TitlePublishesSupportInvocationReceipt(receipt.title))
        return;
    PublishSupportInvocationReceiptInternal(receipt);
}

bool VR_ReadSupportInvocationReceipt(GameTitle title, support_grip::SupportInvocationReceipt& receipt) noexcept
{
    return ReadSupportInvocationReceiptInternal(title, receipt);
}

void VR_SetSupportInvocationUntrusted(bool untrusted) noexcept
{
    // Thread-local, fail-open: a stray set can only force the ordinary
    // calibrated one-hand path for this invocation. Never mutates the durable
    // relationship and never consulted while the feature is off.
    g_supportInvocationUntrusted = untrusted;
}

bool VR_SupportInvocationUntrusted() noexcept
{
    return g_supportInvocationUntrusted;
}

void VR_ObserveSecondaryWeaponPresentation(GameTitle title, uint32_t generation)
{
    const size_t slot = TitleRuntimeSlotIndex(title);
    if (slot < kTitleRuntimeSlotCount && generation && TitleAdapter_GetActiveTitle() == title &&
        TitleAdapter_GetGeneration(title) == generation)
        g_secondaryWeaponPresentation[slot].Publish(generation, GetTickCount64());
}

VRShotFlagSnapshot VR_GetShotFlagSnapshot() noexcept
{
    // Three independent bounded reads of already-published state. They are not
    // a single coherent transaction (the firing path only needs each fact as
    // observed at publish time); no aim getter, lock, allocation or sampling.
    VRShotFlagSnapshot snapshot{};
    snapshot.twoHandActive = g_twoHandActive.load(std::memory_order_acquire);
    snapshot.leftHanded = g_capturedLeftHanded.load(std::memory_order_acquire);
    snapshot.dualActive = SecondaryWeaponPresentationActive();
    return snapshot;
}

uint64_t VR_CurrentPreparedSerial() noexcept
{
    return g_preparedSerialPublished.load(std::memory_order_acquire);
}

bool VR_GetContactTrackingSnapshot(VrContactTrackingSnapshot& snapshot)
{
    snapshot = {};
    const uint32_t index = g_contactTrackingIndex.load(std::memory_order_seq_cst);
    if (index >= 2)
        return false;
    uint32_t readers = g_contactTrackingStates[index].load(std::memory_order_seq_cst);
    if (readers >= 64 ||
        !g_contactTrackingStates[index].compare_exchange_strong(readers, readers + 1, std::memory_order_seq_cst))
        return false;
    bool valid = false;
    if (index == g_contactTrackingIndex.load(std::memory_order_seq_cst))
    {
        snapshot = g_contactTrackingSnapshots[index];
        valid = snapshot.serial && snapshot.serial == g_preparedSerialPublished.load(std::memory_order_acquire);
    }
    g_contactTrackingStates[index].fetch_sub(1, std::memory_order_seq_cst);
    if (!valid)
        snapshot = {};
    return valid;
}

bool VR_GetGameRenderAspect(float& outAspect)
{
    if (!g_gameBackbufferDescValid || !g_gameBackbufferDesc.Width || !g_gameBackbufferDesc.Height)
        return false;
    outAspect = static_cast<float>(g_gameBackbufferDesc.Width) / static_cast<float>(g_gameBackbufferDesc.Height);
    return isfinite(outAspect) && outAspect > 0.1f;
}

void VR_GetStatus(VrStatus& out)
{
    out = g_status;
}

bool VR_RoomscaleTrackingFresh() noexcept
{
    const auto at = g_roomscaleHeadSampleMs.load(std::memory_order_acquire);
    const auto now = GetTickCount64();
    return at && now >= at && now - at <= 100 &&
           g_sessionStateShared.load(std::memory_order_acquire) == XR_SESSION_STATE_FOCUSED;
}
uint64_t VR_PhysicalCrouchEpoch() noexcept
{
    return (uint64_t(g_crouchCalibrationEpoch.load(std::memory_order_acquire)) << 32) |
           uint32_t(g_contactSpaceEpoch.load(std::memory_order_acquire));
}
void VR_RecalibratePhysicalCrouch() noexcept
{
    g_crouchCalibrationEpoch.fetch_add(1, std::memory_order_acq_rel);
}
