// vr_internal.h: vr.cpp's shared frame state, in namespace vr_core: what the title frame sources
// (titles/<t>/<t>_frame_source.cpp) read and write. Defined in vr.cpp. The types and constants the
// declarations need are defined here. Shared state only; functions a unit provides go in its own header
// (vr_frame.h for vr.cpp).
#pragma once
#include <d3d11.h>
#include <openxr/openxr.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
#include "../../common/weapon_muzzle.h"
#include "../../common/visor_zoom_logic.h"
#include "../../common/glass_reprojection_logic.h"
#include "../titles/ce/haloce_hud_target.h"
#include "../../common/dlss_logic.h"
#include "../../common/scope_logic.h"
#include "../../common/view_cache_logic.h"
#include "../../common/weapon_hand_logic.h"
#include "../../common/weapon_haptic_pulses.h"
#include "../../common/virtual_stock_settings.h"
#include "../../common/virtual_stock_test_profiles.h"
#include "../../common/two_hand_lab_logic.h"
#include "game.h"
#include "gpu_eye_timing.h"
#include "../../common/vehicle_monitors_logic.h"
#include "../../common/titles/ce/haloce_snapshot.h"
#include "../../common/dual_reticle_logic.h"
#include "../../common/dlss_frame_result.h"
#include "../../common/weapon_interaction_logic.h"
#include "../../common/weapon_reload_target.h"
#include "../../common/weapon_accessory_logic.h"
#include "../../common/virtual_stock_neutral_capture.h"
#include "../../common/two_hand_input_smoothing.h"
#include "../../common/scope_sight_logic.h"
#include "../../common/scope_parallax.h"
#include "../../common/lens_anchor.h"
#include "../../common/title_runtime_state.h"
#include "../../common/hud_reveal_logic.h"
#include "committed_aim_sample.h"
#include "vr.h"

namespace vr_core
{
    struct ReticleCaptureState
    {
        ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
        ID3D11DepthStencilView* dsv = nullptr;
        D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT viewportCount = 0;
        D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT scissorCount = 0;
        bool active = false;
        bool publishesAuthored = false;
        bool cePrivateRaster = false;
        bool ceRestoring = false;
        bool ceStateValid = false;
        CeHudTargetSnapshot ceTarget;
        // The ONE viewport/scissor this capture was opened with. Halo 4 rebinds
        // its scene target up to 3 times inside a single captured replay, and
        // each rebind can carry whatever viewport the engine's OWN prior pass
        // left behind (proven by SCENEPROBE: the same learned RTV was seen
        // bound with BOTH a 947x683 and a full-raster viewport across
        // different binds). Left alone, those 3 rebinds draw at 3 different,
        // uncorrelated scales into the SAME 512x512 texture - not a wrong crop,
        // a smeared composite of unrelated passes. Re-applying this ONE
        // retained viewport at each Halo-4 rebind (see VR_RedirectRenderTargets)
        // keeps every rebind consistent with the one this capture computed.
        D3D11_VIEWPORT captureViewport{};
        D3D11_RECT captureScissor{};
        bool framingCaptured = false;
    };
    // --- Optional NVIDIA DLSS eye resolve (config.upscaler) ----------------
    // Everything below is render-thread state. The OM hook only ever does
    // pointer compares here (plus one bounded description per newly seen
    // depth view); allocation, NGX and logging happen at eye end or in the
    // Present-time publish.
    struct DlssEyeState
    {
        dlss::CameraSample current{};
        dlss::CameraSample previous{};
        bool published = false;               // a camera sample arrived for this eye render
        bool depthValid = false;              // the depth copy landed for this eye render
        bool depthDirect = false;             // read the engine SRV before the next eye
        bool resetPending = true;             // tell DLSS to drop its history next time
        ID3D11Texture2D* depthCopy = nullptr; // engine depth, typeless
        ID3D11ShaderResourceView* depthCopySrv = nullptr;
        ID3D11ShaderResourceView* borrowedCeDepth = nullptr; // exact CE color/depth lease only
        D3D11_TEXTURE2D_DESC depthCopyDesc{};
        ID3D11Texture2D* colorIn = nullptr; // UNORM twin of an sRGB eye
        D3D11_TEXTURE2D_DESC colorInDesc{};
        ID3D11Texture2D* motion = nullptr; // RG16F, previous - current
        ID3D11RenderTargetView* motionRtv = nullptr;
        ID3D11ShaderResourceView* motionSrv = nullptr;
        ID3D11Texture2D* depthOut = nullptr; // R32F raw depth for DLSS
        ID3D11RenderTargetView* depthOutRtv = nullptr;
        ID3D11Texture2D* output = nullptr; // DLSS result, UAV-capable
        D3D11_TEXTURE2D_DESC outputDesc{};
        // This render's colour as a view of colorIn (the source of the flow
        // input copy), and the optical-flow input frames the backend hands
        // out (BGRA8; each render's colour is copied into the one
        // Dlss_FlowInputIndex names). Null without object motion.
        ID3D11ShaderResourceView* colorInSrv = nullptr;
        ID3D11Texture2D* flowInput[2]{};
        ID3D11RenderTargetView* flowInputRtv[2]{};
        // The last shape whose textures could not be created; that shape is
        // not retried every frame (the DLSS-9 log: two lines per eye per
        // frame, 30,000 lines in two minutes).
        uint32_t failedRenderW = 0, failedRenderH = 0, failedOutputW = 0, failedOutputH = 0;
    };
    // Fallback rule: the last full-raster depth view bound inside the eye.
    // Every distinct view is described once and then referenced so its
    // pointer cannot be recycled for a different-shaped buffer.
    struct DlssDepthViewEntry
    {
        ID3D11DepthStencilView* view = nullptr;
        ID3D11Device* device = nullptr; // borrowed while retained DSV is alive
        bool fullRaster = false;
        bool seenThisFrame = false;
        D3D11_TEXTURE2D_DESC desc{};
    };
    inline constexpr int kDlssDepthViewSlots = 8;
    // One OpenXR frame stays begun while Halo renders. VR_BeforePresent
    // submits it; after DXGI Present returns, VR_AfterPresent obtains the
    // runtime's exact prediction for the next Halo render.
    struct PreparedFrame
    {
        XrFrameState state{XR_TYPE_FRAME_STATE};
        bool begun = false;
        bool viewsValid = false;
        uint32_t viewCount = 0;
        uint64_t serial = 0;
        XrDuration predictedDisplayDelta = 0;
    };
    struct AimPoseInputs
    {
        bool rightValid = false;
        XrPosef right{{0, 0, 0, 1}, {0, 0, 0}};
        bool leftValid = false;
        XrPosef left{{0, 0, 0, 1}, {0, 0, 0}};
        XrVector3f supportPosition{0.0f, 0.0f, 0.0f};
        bool supportEndpointUsedGrip = false;
        bool supportGripPoseEnabled = false;
        // Two-Hand Lab snapshot. One coherent copy of the runtime
        // store per solve, attached by CurrentStockAimPoseInputs and cleared
        // (enable only) by AimPoseInputsForProfile so the A/B/C
        // control/counterfactual solves stay Lab-inert. Plain scalars only:
        // the offline fixtures compile this struct without the Lab header
        // (two_hand_lab types resolve through aim_pose_trace.h for the solver
        // body). Anchor/agreement/temporal ordinals match
        // two_hand_lab::AnchorMode / AgreementMode / TemporalMode declaration
        // order (Production / LegacyHard / None = 0).
        bool twoHandLabEnabled = false;
        int twoHandLabAnchor = 0;
        float twoHandLabOffhandInfluence = 1.0f;
        int twoHandLabAgreement = 0;
        float twoHandLabSoftFullAgreement = 0.9f;
        int twoHandLabTemporal = 0;
        // Two-Hand Lab settings generation (race guard). Stamped
        // by CurrentStockAimPoseInputs from the runtime store's process-wide
        // generation counter, which increments on every publish. The Lab
        // temporal packet carries the generation of the solve its correction
        // belongs to; consumers fail open when it does not match their own
        // solve, closing the same-serial settings-toggle class. Plain scalar:
        // the offline fixtures compile this struct, and counterfactual
        // copies (AimPoseInputsForProfile) inherit it unchanged.
        uint64_t twoHandLabGeneration = 0;
        // Raw grip endpoints, from the same controller sample as right/left
        // (same-sample proof at the Lab store block below). They are the
        // positional pivots of the free two-hand production geometry
        // (Grip -> Grip) and of the Lab anchor matrix. The Virtual Stock
        // support endpoint still comes from supportPosition above and is
        // selected independently ("Reduce Support-Hand Rotation").
        bool primaryGripValid = false;
        XrVector3f primaryGripPosition{0.0f, 0.0f, 0.0f};
        bool supportGripValid = false;
        XrVector3f supportGripPosition{0.0f, 0.0f, 0.0f};
        // Prepared-serial smoothed copies for the two-hand directional solve
        // (Virtual Stock on or off). Raw `right` remains authoritative for
        // output/base position; these copies are consumed only by the two-hand
        // solver branch and only when the layer proves them for this serial.
        bool twoHandSmoothingGeometryValid = false;
        XrQuaternionf twoHandSmoothedPrimaryOrientation{0.0f, 0.0f, 0.0f, 1.0f};
        XrVector3f twoHandSmoothedPrimaryAimPosition{0.0f, 0.0f, 0.0f};
        XrVector3f twoHandSmoothedSupportAimPosition{0.0f, 0.0f, 0.0f};
        bool twoHandSmoothedPrimaryGripValid = false;
        XrVector3f twoHandSmoothedPrimaryGripPosition{0.0f, 0.0f, 0.0f};
        bool twoHandSmoothedSupportGripValid = false;
        XrVector3f twoHandSmoothedSupportGripPosition{0.0f, 0.0f, 0.0f};
        VirtualStockTestProfile testProfileUsed = VirtualStockTestProfile::Custom;
        bool twoHandEnabled = false;
        bool twoHandLatched = false;
        bool twoHandToggle = true;
        // Free two-hand (VS-OFF) offhand directional authority, stamped by
        // CurrentStockAimPoseInputs from `two_hand_offhand_influence`. 0 = the
        // primary controller's own aim is authoritative (exact primary
        // orientation, no positional rebuild); 1 = the accepted support
        // direction owns presentation entirely; intermediate values blend
        // primary direction -> accepted B through the tested authority helper.
        // The default here is full authority, so any caller that does not stamp
        // it - the offline fixtures in particular - keeps the original product
        // shape. Virtual Stock solves never read it.
        float twoHandOffhandInfluence = 1.0f;
        // Persistent support grip solve-time qualification. Plain
        // scalars only, exactly like the Lab fields above: the offline aim
        // fixtures compile this struct without the support-grip or vr headers.
        // `supportMayConsume` is the qualification's permission (this
        // invocation proved the relationship owner); `supportEpoch` is the
        // durable relationship epoch (0 = coherently disengaged, all-ones =
        // the relationship could not be read); the assembly stamps
        // `supportSolveSerial` unconditionally (the presented-ray publication
        // needs it) while every other value stays zero/false when the feature
        // is off, so a PG-off solve never reports support provenance.
        bool supportMayConsume = false;
        uint64_t supportEpoch = 0;
        bool supportRelationshipReadable = false;
        bool supportRelationshipEngaged = false;
        uint64_t supportSolveSerial = 0;
        // Persistent support grip steering retention. True
        // only when this invocation is qualified to consume support geometry:
        // the SAME qualification bit as supportMayConsume (wired title,
        // readable + engaged relationship, proven same owner). With it, the
        // VS-off legacy selection keeps the support-derived direction across
        // the 0.35 agreement floor instead of silently dropping to one-hand
        // while the durable relationship stays engaged. Default false keeps
        // PG-off, unwired, unreadable, disengaged and untrusted invocations on
        // the original legacy rule.
        bool supportSteeringRetained = false;
        // Persistent support grip provenance carry (data only). A verbatim copy
        // of the frozen qualification's forceOneHand flag, which the assembly
        // applies to `twoHandEnabled` above. It exists so same-frame telemetry
        // can name EXPLICIT forcing separately from an ordinary two-hand-off
        // frame; nothing in the solve reads it and it changes no control flow.
        bool supportForceOneHand = false;
        float leftHandForwardM = 0.0f;
        float leftGripForwardM = 0.0f;
        float gunYawDeg = 0.0f;
        float gunPitchDeg = 0.0f;
        // two_hand_exact_line: an engaged two-hand aim is the hand line itself,
        // without the one-hand controller mount yaw/pitch trim (roll is kept).
        bool twoHandExactLine = false;
        // One-handed weapon class (pistol, plasma pistol, needler...): a grabbed
        // support hand attaches to the gun visually only; the aim stays the
        // single-hand aim (no hand line, no Virtual Stock). Long guns unchanged.
        bool twoHandAimSuppressed = false;
        float gunRollDeg = 0.0f;
        // Virtual-stock fields. CurrentAimPoseInputs() leaves these stock-
        // unaware (disabled); only CurrentStockAimPoseInputs() sets them, so
        // physical and independent paths can never inherit stock orientation.
        bool virtualStockEnabled = kVirtualStockEnabledDefault;
        float virtualStockStrength = kVirtualStockStrengthDefault;
        float virtualStockRearHeightM = kVirtualStockRearHeightDefaultM;
        int virtualStockRearReference = kVirtualStockRearReferenceDefault;
        float virtualStockShoulderBackM = kVirtualStockShoulderBackDefaultM;
        float virtualStockShoulderSideM = kVirtualStockShoulderSideDefaultM;
        float virtualStockChestHeightM = kVirtualStockChestHeightDefaultM;
        float virtualStockChestBackM = kVirtualStockChestBackDefaultM;
        float virtualStockChestSideM = kVirtualStockChestSideDefaultM;
        float virtualStockAdaptiveTopHeightM = kVirtualStockAdaptiveTopHeightDefaultM;
        float virtualStockAdaptiveBottomHeightM = kVirtualStockAdaptiveBottomHeightDefaultM;
        float virtualStockAdaptiveTopHalfWidthM = kVirtualStockAdaptiveTopHalfWidthDefaultM;
        float virtualStockAdaptiveBottomHalfWidthM = kVirtualStockAdaptiveBottomHalfWidthDefaultM;
        float virtualStockHybridOffhandInfluence = kVirtualStockHybridOffhandInfluenceDefault;
        int virtualStockHybridAdsReference = kVirtualStockHybridAdsReferenceDefault;
        float virtualStockHybridSeatFullM = kVirtualStockHybridSeatFullDefaultM;
        float virtualStockHybridSeatReleaseM = kVirtualStockHybridSeatReleaseDefaultM;
        bool hybridHorizontalRearReleaseEnabled = kVirtualStockHybridHorizontalRearReleaseEnabledDefault;
        float hybridHorizontalRearReleaseFullM = kVirtualStockHybridHorizontalRearReleaseFullDefaultM;
        float hybridHorizontalRearReleaseReleaseM = kVirtualStockHybridHorizontalRearReleaseReleaseDefaultM;
        bool hybridInverseNeckEnabled = kVirtualStockHybridInverseNeckEnabledDefault;
        float hybridInverseNeckStrength = kVirtualStockHybridInverseNeckStrengthDefault;
        float hybridInverseNeckForwardM = kVirtualStockHybridInverseNeckForwardDefaultM;
        float hybridInverseNeckUpM = kVirtualStockHybridInverseNeckUpDefaultM;
        float hybridInverseNeckLateralM = kVirtualStockHybridInverseNeckLateralDefaultM;
        HybridDiagnosticOverride hybridDiagnosticOverride = HybridDiagnosticOverride::Normal;
        bool virtualStockLeftHanded = false;
        bool virtualStockProximityRelease = false;
        float virtualStockProximityFullM = 0.250f;
        float virtualStockProximityReleaseM = 0.450f;
        bool headValid = false;
        XrVector3f headPosition{0.0f, 0.0f, 0.0f};
        XrQuaternionf headOrientation{0.0f, 0.0f, 0.0f, 1.0f};
        bool inverseNeckNeutralValid = false;
        XrQuaternionf inverseNeckNeutralOrientation{0.0f, 0.0f, 0.0f, 1.0f};
        uint64_t inverseNeckNeutralCaptureSerial = 0;
        uint64_t inverseNeckNeutralCaptureContactSpaceEpoch = 0;
    };
    struct AimPoseResult
    {
        bool valid = false;
        // False when the right pose itself is invalid; callers then preserve
        // the existing two-hand activity indicator exactly as before.
        bool updateTwoHandActivity = false;
        bool twoHandActive = false;
        bool rejectedExtreme = false;
        float rejectedAgreement = 0.0f;
        XrPosef pose{{0, 0, 0, 1}, {0, 0, 0}};
        // Solve-time support receipt (persistent support grip). Plain
        // scalars for the same fixture reason as AimPoseInputs. `supportTrusted`
        // is set only when this exact solve consumed support-capable two-hand
        // geometry under a proven same-owner invocation; the epoch/coherence
        // fields are the assembly's qualification so an engaged or unreadable
        // relationship is never reported as epoch 0.
        uint64_t supportEpoch = 0;
        bool supportTrusted = false;
        bool supportRelationshipReadable = false;
        bool supportRelationshipEngaged = false;
        uint64_t supportSolveSerial = 0;
        // Data-only carry of the assembly's forcing flag (see AimPoseInputs).
        // Copied straight from the frozen qualification for telemetry; no
        // consumer treats it as a control input.
        bool supportForceOneHand = false;
    };
    // CE prepares its eyes asynchronously: the pose submitted with a pair is the
    // tracked head position plus that eye's offset and orientation it was drawn at.
    // The gun camera the current glass image was rendered with (lens receipt
    // forward/up, tangents), valid for the image with g_glassCameraSerial (render thread).
    struct GlassCamera
    {
        glass_reprojection::V3 forward, up;
        float tanH{}, tanV{};
    };
    // Comfort: a hull frame repeats the eye pair, so it is taken only
    // while the vehicle is nearly stopped and not turning.
    inline constexpr float kAnnivHullStillSpeedMps = 1.0f;
    // Hip (live glass, not raised): one borrowed frame in this many.
    inline constexpr unsigned kAnnivGlassHipEvery = 12;
    // World stereo swapchain failure messages, chosen in the order the exact titles
    // are checked (CE, Halo 2, Halo 4, otherwise Reach).
    struct WorldSwapchainMessages
    {
        const char* acquire;
        const char* wait;
        const char* release;
    };

    extern XrSpace g_localSpace;
    extern std::atomic<int64_t> g_contactSpaceChangeAtNs;
    extern std::atomic<uint64_t> g_contactSpaceEpoch;
    extern XrSpace g_viewSpace;
    extern std::vector<XrView> g_views;
    extern ID3D11Device* g_device;
    extern ID3D11DeviceContext* g_context;
    extern int64_t g_xrFormat;
    extern XrSwapchain g_screenChain;
    extern uint32_t g_screenW;
    extern uint32_t g_screenH;
    extern std::vector<ID3D11Texture2D*> g_screenImages;
    extern std::vector<ID3D11RenderTargetView*> g_screenRtvs;
    extern halo_ce::Snapshot<weapon_muzzle::Ray> g_scopeLogMuzzle[7];
    extern halo_ce::Snapshot<weapon_muzzle::Ray> g_scopeLogLens[7];
    extern std::atomic<uint64_t> g_scopeLensMs[7];
    extern std::atomic<bool> g_scopeGlassLive;
    extern std::atomic<bool> g_reticleChainFailed;
    extern bool g_abortFrameForSwapchainFailure;
    extern std::atomic<bool> g_scopeActive;
    extern std::atomic<uint8_t> g_zoomTier;
    extern ID3D11Texture2D* g_scopeCache;
    extern ID3D11ShaderResourceView* g_scopeCacheSrv;
    extern D3D11_TEXTURE2D_DESC g_scopeCacheDesc;
    extern std::atomic<bool> g_scopeHasImage;
    extern GameTitle g_scopeImageTitle;
    extern GameTitle g_scopeRequestTitle;
    extern std::atomic<float> g_scopeImageGlassAspect;
    extern uint32_t g_scopeImageGeneration;
    extern uint32_t g_scopeRequestGeneration;
    extern std::atomic<float> g_scopeGlassAspect;
    extern std::atomic<int> g_monitorPendingView;
    extern std::atomic<uint32_t> g_scopeImageSerial;
    extern std::atomic<float> g_scopeRuntimeZoom;
    extern ID3D11RenderTargetView* g_authoredReticleRtv;
    extern ID3D11RenderTargetView* g_authoredReticleDiscardRtv;
    extern bool g_authoredReticleGoodValid;
    extern uint32_t g_authoredReticleLastAlphaInk;
    extern uint32_t g_authoredReticleLastColorInk;
    extern bool g_authoredReticleReady;
    extern uint64_t g_authoredReticleSerial;
    extern bool g_reticleContainsAuthored;
    extern ReticleCaptureState g_reticleCaptureState;
    extern std::atomic<uint64_t> g_authoredReticleFramingReasserts;
    extern ID3D11Texture2D* g_eyeCache[2];
    extern ID3D11RenderTargetView* g_eyeCacheRtvs[2];
    extern D3D11_TEXTURE2D_DESC g_eyeCacheDesc;
    extern std::atomic<bool> g_stereoEnabled;
    extern bool g_eyeHasImage[2];
    extern std::atomic<int> g_rasterEye;
    extern bool g_rasterRedirected[2];
    extern ID3D11RenderTargetView* g_sceneColorRtv;
    extern DlssEyeState g_dlssEye[2];
    extern DlssDepthViewEntry g_dlssDepthViews[kDlssDepthViewSlots];
    extern int g_dlssDepthViewCount;
    extern std::atomic<int> g_dlssDepthWindowEye;
    extern D3D11_TEXTURE2D_DESC g_gameBackbufferDesc;
    extern bool g_gameBackbufferDescValid;
    extern bool g_haveCenter;
    extern std::atomic<bool> g_screenFollow;
    extern std::atomic<bool> g_pausePresentation;
    extern XrPosef g_headPose;
    extern bool g_headPoseValid;
    extern XrPosef g_rightAimPose;
    extern bool g_rightAimPoseValid;
    extern std::atomic<bool> g_capturedLeftHanded;
    extern XrPosef g_leftAimPose;
    extern bool g_leftAimPoseValid;
    extern PreparedFrame g_preparedFrame;
    extern std::atomic<bool> g_preparedShouldRender;
    extern std::atomic<uint64_t> g_preparedViewSerialPublished;
    extern std::atomic<uint64_t> g_displayPeriodNs;
    extern std::atomic<uint64_t> g_preparedSerialPublished;
    extern VrPadState g_padState;
    extern ControllerFingerSample g_controllerFingerSample;
    extern GlassCamera g_glassCamera;
    extern uint32_t g_glassCameraSerial;
    extern float g_glassImageCap;
    extern uint32_t g_glassImageCapSerial;
    extern std::atomic<uint64_t> g_annivStolen;
    extern std::atomic<uint64_t> g_annivStolenVisor;
    extern std::atomic<uint64_t> g_annivMovingSkips;
    extern std::atomic<uint64_t> g_annivStolenMoving;
    extern std::atomic<int> g_annivHullMoving;
    extern std::atomic<uint64_t> g_annivHullWantedMs;
    extern std::atomic<uint64_t> g_annivVisorWantedMs;
    extern visor_zoom::ZoomBoost g_annivZoomBoost;
    extern std::atomic<uint64_t> g_annivGlassWantedMs;
    extern std::atomic<uint64_t> g_annivGlassImageMs;
    extern std::atomic<uint64_t> g_annivStolenGlass;
    extern std::atomic<unsigned> g_annivGlassImages;

    // ---- vr.cpp state the subject units share (E2-VR 7b) ----
    // The primary reticle ray as last drawn (OpenXR local space), read by
    // the per-shot log lines on the game thread. A sequence counter keeps it whole.
    struct LastReticleRay
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<float> origin[3]{}, direction[3]{};
        std::atomic<bool> barrel{false}, held{false};
        std::atomic<uint64_t> ms{0};
    };
    // Coherent settings-plus-generation snapshot for the assembly sites.
    // Defined before PublishTwoHandLabSettings so both publish and snapshot
    // agree on the field order.
    struct TwoHandLabSettingsSnapshot
    {
        two_hand_lab::Settings settings{};
        uint64_t generation = 0;
    };
    struct ResolvedVirtualStockAimSettings
    {
        VirtualStockTestProfile profile = VirtualStockTestProfile::Custom;
        VirtualStockAimSettings settings{};
    };
    enum class State
    {
        Uninitialized,
        Ready,
        Failed
    };
    inline constexpr uint32_t kReticleSize = 512;
    inline constexpr uint32_t kScopeScreenWidth = 1024;
    inline constexpr uint32_t kScopeScreenHeight = 1024;
    inline constexpr uint32_t kVisorScreenWidth = 1536; // 1.5x the old window: same pixels per degree
    inline constexpr uint32_t kVisorScreenHeight = 1152;
    // One fresh-left + fresh-right atlas evaluated by one temporal feature.
    // The halves never alternate across frames: eye 0 always occupies the
    // left half and eye 1 the right half, so NGX history remains stable.
    struct DlssStereoBatch
    {
        ID3D11Texture2D* color = nullptr;
        ID3D11Texture2D* motion = nullptr;
        ID3D11Texture2D* depth = nullptr;
        ID3D11Texture2D* output = nullptr;
        ID3D11Texture2D* eyeOutput[2]{};
        ID3D11RenderTargetView* motionRtv = nullptr;
        ID3D11RenderTargetView* depthRtv = nullptr;
        D3D11_TEXTURE2D_DESC inputDesc{};
        D3D11_TEXTURE2D_DESC outputDesc{};
        D3D11_TEXTURE2D_DESC eyeOutputDesc{};
        dlss::StereoBatchPlan plan{};
        bool eyePrepared[2]{};
        bool reset = false;
    };
    // An eye whose upscale is in flight. DlssKickEye submits it the moment
    // the eye's render ends; DlssFinishEye, at present, waits once for both
    // eyes and stretches the finished image into the headset image.
    struct DlssKickedEye
    {
        ID3D11Texture2D* output = nullptr; // null: already written (debug view)
        D3D11_TEXTURE2D_DESC outputDesc{};
    };
    struct NativeHudEyeRouteState
    {
        ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
        ID3D11DepthStencilView* dsv = nullptr;
        ID3D11RenderTargetView* phaseOutputRtv = nullptr;
        D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT viewportCount = 0;
        UINT scissorCount = 0;
        int eye = -1;
        bool active = false;
        bool bypassOmRedirect = false;
        bool targetCopy = false;
    };
    // Source views for the upload path, and SRV-capable intermediate copies for
    // sources we cannot sample directly.
    //
    // Both used to be a SINGLE slot. The eye publish alternates between
    // g_eyeCache[0] and g_eyeCache[1], so a one-entry cache keyed on the source
    // pointer missed on every eye of every frame and asked the device for a new
    // view each time - a COM call per eye per frame in a hot hook. The
    // intermediate was keyed on shape, so two different-sized slow-path sources
    // in one frame destroyed and recreated a FULL-RESOLUTION texture every
    // frame, which the comment in Blit() below already names as the kind of
    // cost that halves the frame rate on level load.
    //
    // VRAM, deliberately: the two capacities are NOT the same kind of thing.
    //
    // A source view is a descriptor, not an image - a few hundred bytes of
    // driver memory - so 32 of them cost nothing measurable. What a view DOES
    // cost is a strong reference on its texture, which is why every site that
    // destroys a sampled texture calls ForgetSourceView. In steady play the
    // cached set is the two eyes, the menu, the screen quad, the reticle and
    // the scope: all textures that are alive regardless. Added VRAM: none.
    //
    // An intermediate IS a full copy of its source, so this number is a
    // CEILING, not a preallocation - and that distinction is the whole reason
    // 32 is safe here. Slots fill lazily, one per distinct shape actually
    // requested, and only sources we cannot sample directly ever reach this
    // pool at all: a non-SRV-capable or multisampled texture. The eye caches
    // are neither (the scene target carries a UAV binding, which forces
    // single-sampling, and they are created SRV-capable), so the whole stereo
    // path skips this pool. In practice one or two shapes are ever live.
    //
    // An earlier revision capped this at 4, sized against a worst case of 32
    // full-resolution copies at ~29 MB each. That worst case cannot occur: it
    // would need 32 genuinely different shapes in one session, and the pool
    // holds only what is asked for. Sizing against an unreachable worst case
    // just reintroduced a smaller version of the thrash this pool exists to
    // remove. The live slot count and its real byte total are logged every two
    // seconds, so the actual figure is read from a session rather than argued
    // about here - and LRU eviction plus the full drain on resize and detach
    // bound it either way.
    inline constexpr std::size_t kSrcViewCacheCapacity = 32;
    inline constexpr std::size_t kIntermediatePoolCapacity = 32;
    struct TheaterProjectionParams
    {
        float clipPositions[4][4]{};
        // x: source is sRGB; y: non-sRGB XR target needs perceptual output;
        // z: source already passed through the normal IQ output encoding.
        float color[4]{};
        // x/y: first and last source V the cine bars leave showing; z: nonzero
        // once those bars are active at all; w: first V covered by the subtitle
        // band, or negative when no band was captured.
        float matte[4]{};
        // x: paint the captured band unfiltered instead of selecting glyphs.
        float band[4]{};
    };
    // Exact transition capture. The render thread publishes one immutable POD
    // record only after the corresponding DXGI Present returns. The existing
    // 50 ms title worker drains this SPSC queue and is solely responsible for
    // transition detection, formatting, allocation, and LOG/file I/O.
    struct FramePacingRecord
    {
        uint64_t serial = 0;
        uint64_t waitSequence = 0;
        uint64_t overlappingWorkerWaitSequence = 0;
        uint64_t nextWaitDispatchSequence = 0;
        uint32_t sessionEpoch = 0;
        uint32_t layerCount = 0;
        int64_t predictedDisplayTimeNs = 0;
        int64_t predictedDisplayPeriodNs = 0;
        int64_t waitCallStartQpc = 0;
        int64_t waitCallEndQpc = 0;
        int64_t waitReadyQpc = 0;
        int64_t renderWaitStartQpc = 0;
        int64_t renderWaitEndQpc = 0;
        int64_t nextWaitDispatchGateStartQpc = 0;
        int64_t nextWaitDispatchGateEndQpc = 0;
        int64_t beginStartQpc = 0;
        int64_t beginEndQpc = 0;
        int64_t consumedSignalQpc = 0;
        int64_t prepareEndQpc = 0;
        int64_t beforePresentQpc = 0;
        int64_t endStartQpc = 0;
        int64_t endEndQpc = 0;
        int64_t presentStartQpc = 0;
        int64_t presentEndQpc = 0;
        int64_t afterPresentQpc = 0;
        int32_t waitResult = XR_SUCCESS;
        int32_t beginResult = XR_SUCCESS;
        int32_t endResult = XR_SUCCESS;
        int32_t presentResult = S_OK;
        uint32_t eventWaitResult = WAIT_OBJECT_0;
        uint32_t nextWaitEventResult = WAIT_OBJECT_0;
        uint8_t title = static_cast<uint8_t>(GameTitle::None);
        bool waitPacketCoherent = true;
        bool nextWaitDispatchObservedBeforeBegin = false;
        bool shouldRender = false;
        bool focused = false;
        bool stereo = false;
        bool headTracking = false;
        bool scopeActive = false;
        bool submitted = false;
        GameFramePerfCounters perf{};
    };
    inline constexpr size_t kFramePacingPreFrames = 64;
    inline constexpr size_t kFramePacingPostFrames = 128;
    inline constexpr size_t kFramePacingCaptureFrames = kFramePacingPreFrames + 1 + kFramePacingPostFrames;
    struct FramePacingWorkerState
    {
        std::array<FramePacingRecord, kFramePacingPreFrames> history{};
        size_t historyNext = 0;
        size_t historyCount = 0;
        std::array<FramePacingRecord, kFramePacingCaptureFrames> capture{};
        size_t captureCount = 0;
        size_t triggerIndex = 0;
        size_t postRemaining = 0;
        int64_t oldPeriodNs = 0;
        int64_t newPeriodNs = 0;
        uint64_t dropsAtStart = 0;
        uint64_t lastDataMs = 0;
        bool capturing = false;
        bool exact = true;
    };
    // ---- Grab/release aim continuity (Virtual Stock, Standard and Plus) ----
    //
    // The layer in src/common/virtual_stock_aim_continuity.h is advanced once
    // per prepared serial on the OpenXR frame thread, after input capture
    // (including UpdateTwoHandLatch) and head capture and before any
    // publication that must show the corrected aim. Game/render threads read a
    // lock-free, fail-open correction packet instead (VR_GetAimPose is called
    // from several threads), so the feature works with recording off and never
    // takes a lock in the getter.
    struct AimContinuityLayerState
    {
        virtual_stock::AimContinuityState transition{};
        // The layer's own last presented orientation, i.e. the Apply() output
        // actually emitted for the previous engaged serial. Used as the release
        // seed and for rapid re-grab continuity.
        bool lastPresentedValid = false;
        virtual_stock::Quat4 lastPresented{};
        bool publishedValid = false;
        // Process-lifetime monotonic count of prepared serials that advanced the
        // layer (the module's own duplicate-serial and non-positive-dt guards
        // leave it untouched). Telemetry evidence; never reset.
        uint64_t advanceCount = 0;
        GameTitle title = GameTitle::None;
        uint32_t generation = 0;
        uint64_t contactSpaceEpoch = 0;
        bool haveIdentity = false;
    };
    // Same-thread (prepared-frame) view of the last serial the layer ran for,
    // with the exact orientations the seam solved. Consumed by the presentation
    // publishers and by telemetry capture in that same prepared serial.
    struct AimContinuityPreparedOutput
    {
        uint64_t serial = 0;
        bool liveValid = false;
        virtual_stock::Quat4 live{};
        bool oneHandValid = false;
        virtual_stock::Quat4 oneHand{};
        bool presentedValid = false;
        virtual_stock::Quat4 presented{};
    };
    // Cross-thread publication for VR_GetAimPose. Seqlock shape mirrors
    // ReticleAimPosePublication: no locks, no allocation, trivially small.
    struct AimContinuityPublication
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint64_t> serial{0};
        std::atomic<uint64_t> contactSpaceEpoch{0};
        std::atomic<uint8_t> valid{0};
        // Assembly identity of the live solve the correction belongs to: the
        // stock-aware head sample and the support endpoint source. A reader
        // whose own solve was assembled differently must fail open, because a
        // correction is only meaningful against the live pose it was derived
        // from.
        std::atomic<uint8_t> stockHeadCoherent{0};
        std::atomic<uint8_t> supportEndpointUsedGrip{0};
        std::atomic<float> cx{0.0f};
        std::atomic<float> cy{0.0f};
        std::atomic<float> cz{0.0f};
        std::atomic<float> cw{1.0f};
    };
    // Two-hand controller-input smoothing is advanced by the prepared-frame
    // owner only (Virtual Stock on or off; the filter sees directional input
    // copies, never the raw captured poses). Game/title consumers read its
    // exact-serial filtered geometry through the small lock-free publication;
    // raw capture and every grip-acquisition/retention path remain untouched.
    struct TwoHandInputSmoothingLayerState
    {
        two_hand_input_smoothing::State filter{};
        uint64_t lastPreparedSerial = 0;
        uint64_t advanceCount = 0;
        GameTitle title = GameTitle::None;
        uint32_t generation = 0;
        uint64_t contactSpaceEpoch = 0;
        bool leftHanded = false;
        bool haveIdentity = false;
    };
    struct TwoHandInputSmoothingPreparedOutput
    {
        uint64_t serial = 0;
        bool active = false;
        bool sampleValid = false;
        // Frozen configured strength (0..25) for this exact prepared serial.
        // Consumers and telemetry read this instead of the live config, so one
        // serial can never mix two slider values.
        float strength = 0.0f;
        two_hand_input_smoothing::Sample raw{};
        two_hand_input_smoothing::Sample filtered{};
        two_hand_input_smoothing::Sample mixed{};
        // INTERNAL speed-25 temporal alpha = clamp(25*dt, 0, 1); never the
        // user-facing strength amount.
        float alpha = 0.0f;
        float orientationErrorDeg = 0.0f;
        float primaryPositionErrorM = 0.0f;
        float supportPositionErrorM = 0.0f;
    };
    // --- GPU timing for the eye publish ------------------------------------
    //
    // Until now NOTHING in this repo measured GPU time. Every number in the log
    // - renderWindow, xrEndFrame, fps - is CPU wall clock, so the cost of the
    // work we actually ask the GPU to do had never once been observed. That is
    // why three reasoned performance passes in a row missed and two made it
    // worse (AGENTS.md). This prices the eye publish directly: the resolve pass
    // and the post passes (FXAA/SMAA/RCAS) are timed separately, per eye, so
    // "what does the second pass cost?" becomes a number instead of an
    // argument.
    //
    // It never blocks. Results are read back several frames later with
    // DONOTFLUSH and simply skipped while the GPU is still busy, so a query
    // that is not ready costs a load and nothing else. A disjoint frame (the
    // GPU changed clocks mid-measurement) is discarded rather than reported.
    inline constexpr std::size_t kIqTimerDepth = 4;  // frames in flight
    inline constexpr std::size_t kIqTimerStamps = 6; // 3 per eye, 2 eyes
    struct IqTimerSlot
    {
        ID3D11Query* disjoint = nullptr;
        ID3D11Query* stamps[kIqTimerStamps]{};
        ID3D11Query* dlssWait[4]{};
        uint32_t waitMask = 0;
        uint32_t written = 0;
        bool pending = false;
    };
    // =====================================================================
    // Optional NVIDIA DLSS eye resolve
    //
    // Replaces BlitImageQuality for one eye when config.upscaler selects DLSS
    // and everything DLSS needs exists for that eye: the title published the
    // camera it rasterized with (VR_PublishEyeCamera), the eye's depth buffer
    // was copied at eye end (DlssCaptureEyeDepth), NGX initialized on this
    // device and a feature exists for this render/output shape. Any missing
    // piece falls back to the mod's own resolve for that eye and frame and
    // says why, at most once every two seconds. Nothing here can disarm a
    // title, end the OpenXR session, or change what the game renders except
    // the sub-pixel jitter the title asks for through VR_GetEyeJitter.
    //
    // Halo 3 at eye end: direct eye colour + direct engine-depth SRV ->
    // camera/depth pass -> native D3D11 NGX evaluation. Other title paths keep
    // their input captures. The ordinary configured output resolve runs at
    // Present. Image-search stays disabled. The independently measured
    // full-output resolve combines resampling/sharpening when compatible.
    struct DlssMotionParams
    {
        float curPos[4], curRight[4], curUp[4], curFwd[4], curProj[4], curDepth[4];
        float prevPos[4], prevRight[4], prevUp[4], prevFwd[4], prevProj[4], prevDepth[4];
        float size[4];  // w, h, 1/w, 1/h
        float debug[4]; // x: motion-vector scale for the diagnostic view
    };
    // 1 Hz proof that the glass image has content (some earlier glass captures were
    // opaque black): the centre 64x64 of each eye's image, copied to staging
    // now and mapped (DO_NOT_WAIT) on a later frame, so it never stalls.
    struct ScopeImageProbe
    {
        ID3D11Texture2D* staging = nullptr;
        UINT width = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        bool pending = false, stereo = false;
        uint64_t lastMs = 0;
    };
    // Same-thread (prepared-frame) view of the last serial the layer ran
    // for, with the exact orientations the seam solved. Consumed by the
    // frame-thread presentation publishers and by telemetry capture in that
    // same prepared serial.
    struct TwoHandLabTemporalPreparedOutput
    {
        uint64_t serial = 0;
        // Generation of the Lab snapshot the seam's solve was assembled with.
        // A publisher whose own solve used a newer snapshot fails open.
        uint64_t labGeneration = 0;
        bool baseValid = false;
        virtual_stock::Quat4 base{};
        bool presentedValid = false;
        virtual_stock::Quat4 presented{};
        bool temporalActive = false;
        two_hand_lab::TemporalMode temporalMode = two_hand_lab::TemporalMode::None;
        float errorDeg = 0.0f;
        // Local-frame correction: presented = product-presented base (x)
        // correction.
        virtual_stock::Quat4 correction{};
    };
    // Cross-thread publication for VR_GetAimPose. Seqlock shape mirrors
    // AimContinuityPublication: no locks, no allocation, trivially small. In
    // addition to serial/epoch the packet carries the settings generation
    // AND the product-presented base orientation the Lab correction belongs
    // to: a reader whose own presented solve differs in any of the three
    // fails open.
    struct TwoHandLabTemporalPublication
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint64_t> serial{0};
        std::atomic<uint64_t> contactSpaceEpoch{0};
        std::atomic<uint64_t> generation{0};
        std::atomic<uint8_t> valid{0};
        std::atomic<float> sx{0.0f};
        std::atomic<float> sy{0.0f};
        std::atomic<float> sz{0.0f};
        std::atomic<float> sw{1.0f};
        std::atomic<float> cx{0.0f};
        std::atomic<float> cy{0.0f};
        std::atomic<float> cz{0.0f};
        std::atomic<float> cw{1.0f};
    };

    extern State g_state;
    extern XrInstance g_instance;
    extern XrSession g_session;
    extern virtual_stock::InverseNeckNeutralCaptureState g_inverseNeckNeutralCapture;
    extern XrAction g_hapticAction;
    extern XrAction g_actMenu;
    extern std::atomic<uint64_t> g_thumbrestDpadSampleMs;
    extern XrPath g_leftHandPath;
    extern XrPath g_rightHandPath;
    extern std::atomic<vr_mapping::Profile> g_controllerProfiles[2];
    extern std::atomic<uint32_t> g_controllerProfileEpoch;
    extern std::atomic<uint32_t> g_interactionProfileEpoch;
    extern bool g_touchProProfileEnabled;
    extern std::atomic<float> g_panelRefreshHz;
    extern bool g_sessionRunning;
    extern XrSessionState g_sessionState;
    extern std::atomic<float> g_requestedHaptics;
    extern std::atomic<float> g_peakHaptics;
    extern std::atomic<float> g_contactHaptics[2];
    extern WeaponHapticAdmission g_weaponHapticAdmission;
    extern bool g_nativeTheatreCaptionReady;
    extern XrSwapchain g_dualReticleChain;
    extern halo_ce::Snapshot<dual_reticle::ProjectedRay> g_weaponReticleRays[7][2];
    extern halo_ce::Snapshot<dual_reticle::ProjectedRay> g_weaponLensRays[7][2];
    extern std::atomic<uint64_t> g_scopeGlassHeldMs;
    extern lens_anchor::ReadReport g_lensReadReport;
    extern XrSwapchain g_reticleChain;
    extern std::atomic<bool> g_authoredReticlePreparationReady;
    extern std::atomic<bool> g_scopeSighting;
    extern std::atomic<uint64_t> g_scopeSightingMs;
    extern scope_sight::Detector g_scopeSightDetector;
    extern XrSwapchain g_visorScreenChain;
    extern ID3D11RenderTargetView* g_scopeCacheRtv;
    extern std::atomic<bool> g_rasterScope;
    extern bool g_scopeRedirected;
    extern std::atomic<float> g_scopeAdaptiveScale;
    extern std::atomic<uint64_t> g_scopeScalePassMs;
    extern bool g_visorCockpitMode;
    extern std::atomic<int> g_monitorCurrentView;
    extern std::atomic<uint64_t> g_monitorViewSinceMs;
    extern std::atomic<uint64_t> g_monitorRequestMs;
    extern std::atomic<const char*> g_monitorVehicle;
    extern int g_scopeImageView;
    extern XrSwapchain g_monitorChains[vehicle_monitors::kViewCount];
    extern bool g_monitorHasImage[vehicle_monitors::kViewCount];
    extern std::atomic<uint64_t> g_monitorViewsRendered[vehicle_monitors::kViewCount];
    extern std::atomic<uint64_t> g_scopeImageCapturedMs;
    extern std::atomic<float> g_scopeZoomStickY;
    extern std::atomic<bool> g_scopeResetRequested;
    extern std::atomic<bool> g_zoomWeaponBlocked;
    extern float g_reticlePaintedOpacity;
    extern bool g_authoredReticleProbePending;
    extern uint32_t g_authoredReticleGoodInk;
    extern uint32_t g_authoredReticleConsecutiveHolds;
    extern uint32_t g_authoredReticleLastCoverage;
    extern uint32_t g_authoredReticleBlankHeld;
    extern bool g_authoredReticleHeldBlank;
    extern uint64_t g_authoredReticleUploadedSerial;
    extern std::atomic<uint64_t> g_authoredReticleOmReroutes;
    extern uint32_t g_stereoW;
    extern uint32_t g_stereoH;
    extern bool g_stereoValidationDone;
    extern std::atomic<DWORD> g_rasterOwnerThread;
    extern IDXGISwapChain* g_gameSwapchain;
    extern DlssStereoBatch g_dlssBatch;
    extern dlss::FrameResult<DlssKickedEye> g_dlssKicked[2];
    extern ID3D11DepthStencilView* g_dlssSceneDepthDsv;
    extern bool g_dlssEyeSceneDepthSeen;
    extern bool g_dlssDepthViewTableFull;
    extern ID3D11DepthStencilView* g_dlssEyeLastFullRasterDsv;
    extern bool g_dlssJitterArmed;
    extern uint32_t g_dlssEyesResolvedThisFrame;
    extern bool g_dlssDepthSourceLogged;
    extern bool g_dlssResourcesLogged;
    extern uint32_t g_dlssBatchFailedFrames;
    extern bool g_dlssBatchSuppressed;
    extern SRWLOCK g_dlssStatusLock;
    extern char g_dlssStatusText[256];
    extern ID3D11Resource* g_sceneColorResource;
    extern thread_local NativeHudEyeRouteState g_nativeHudEyeRoute;
    extern std::atomic<unsigned> g_nativeHudPhaseScopes;
    extern std::atomic<unsigned> g_nativeHudProvenOmMatches;
    extern std::atomic<unsigned> g_nativeHudExactCopyScopes;
    extern std::atomic<unsigned> g_nativeHudCopySubstitutions;
    extern std::atomic<ID3D11Texture2D*> g_nextGameBackbuffer;
    extern XrQuaternionf g_centerRot;
    extern XrVector3f g_centerPos;
    extern float g_cutsceneTheaterAuthoredAspect;
    extern CRITICAL_SECTION g_headCs;
    extern bool g_headCsInit;
    extern hud_reveal::Publication g_hudReveal;
    extern std::atomic<bool> g_stockAimFresh;
    extern std::atomic<bool> g_supportGripPoseFresh;
    extern std::atomic<uint64_t> g_roomscaleHeadSampleMs;
    extern XrPosef g_physicalControllerPose[2];
    extern bool g_physicalControllerValid[2];
    extern uint64_t g_physicalControllerAtMs;
    extern XrVector3f g_rightAimLinearVelocity;
    extern bool g_rightAimLinearVelocityValid;
    extern uint64_t g_rightAimLinearVelocityAtMs;
    extern XrVector3f g_supportGripPosePosition;
    extern bool g_supportGripPoseValid;
    extern XrVector3f g_primaryGripPosePosition;
    extern bool g_primaryGripPoseValid;
    extern XrVector3f g_leftAimLinearVelocity;
    extern bool g_leftAimLinearVelocityValid;
    extern uint64_t g_leftAimLinearVelocityAtMs;
    extern ID3D11VertexShader* g_blitVs;
    extern ID3D11SamplerState* g_blitSampler;
    extern ID3D11RasterizerState* g_blitRasterizer;
    extern ID3D11DepthStencilState* g_blitDepthOff;
    extern ViewCacheTable<kSrcViewCacheCapacity> g_srcSrvCache;
    extern IntermediatePoolTable<kIntermediatePoolCapacity> g_intermediatePool;
    extern ID3D11Texture2D* g_intermediateTex[kIntermediatePoolCapacity];
    extern ID3D11ShaderResourceView* g_intermediateSrvs[kIntermediatePoolCapacity];
    extern uint64_t g_srcViewCreated;
    extern uint64_t g_intermediateCreated;
    extern ID3D11Texture2D* g_theaterResolved[2];
    extern ID3D11RenderTargetView* g_theaterResolvedRtv[2];
    extern ID3D11ShaderResourceView* g_theaterResolvedSrv[2];
    extern D3D11_TEXTURE2D_DESC g_theaterResolvedDesc;
    extern ID3D11Texture2D* g_theaterSubtitleBand;
    extern ID3D11ShaderResourceView* g_theaterSubtitleBandSrv;
    extern D3D11_TEXTURE2D_DESC g_theaterSubtitleBandDesc;
    extern float g_theaterSubtitleBandStartV;
    extern ID3D11VertexShader* g_scopeUploadVs;
    extern ID3D11SamplerState* g_scopeUploadSampler;
    extern ID3D11RasterizerState* g_scopeUploadRasterizer;
    extern ID3D11DepthStencilState* g_scopeUploadDepthOff;
    extern const char* g_scopeShapeApplied;
    extern std::atomic<uint64_t> g_framePacingQueueDrops;
    extern std::atomic<uint32_t> g_framePacingSessionEpoch;
    extern HANDLE g_waitReadyEvent;
    extern HANDLE g_waitConsumedEvent;
    extern HANDLE g_waitStartedEvent;
    extern std::atomic<bool> g_waitThreadStop;
    extern std::atomic<bool> g_waitPipelineFaulted;
    extern const char* g_frameWaitFatalExitReason;
    extern bool g_frameWaitExitRequestAccepted;
    extern uint64_t g_frameWaitExitLastRequestMs;
    extern uint64_t g_frameWaitExitLastLogMs;
    extern AimContinuityLayerState g_aimContinuityLayer;
    extern AimContinuityPreparedOutput g_aimContinuityOutput;
    extern AimContinuityPublication g_aimContinuityPublication;
    extern committed_aim::Publication g_committedAimSample;
    extern TwoHandInputSmoothingLayerState g_twoHandInputSmoothingLayer;
    extern TwoHandInputSmoothingPreparedOutput g_twoHandInputSmoothingOutput;
    extern IqTimerSlot g_iqTimer[kIqTimerDepth];
    extern GpuEyeTiming g_eyeGpuTiming;
    extern std::size_t g_iqTimerCursor;
    extern uint32_t g_iqTimerStampIndex;
    extern bool g_iqTimerArmed;
    extern bool g_iqTimerUsable;
    extern uint64_t g_scopeMaskIdentity;
    extern float g_scopeKitAspect;
    extern LastReticleRay g_lastReticleRay;
    extern bool g_scopeMaskWide;
    extern std::atomic<uint64_t> g_lensSubmittedMs;
    extern ScopeImageProbe g_scopeImageProbe;
    extern ScopeQuadTransform g_lensPlanTransform;
    extern scope_parallax::Eye g_lensPlanEyes[2];
    extern ID3D11PixelShader* g_vignettePs;
    extern ID3D11PixelShader* g_dashPs;
    extern float g_nativePointerTrigger;
    extern std::atomic<uint64_t> g_pauseEndedByGameMs;
    extern std::atomic<uint64_t> g_lastResumeInputMs;
    extern std::atomic<uint64_t> g_lastPauseEndMs;
    extern std::atomic<bool> g_twoHandLatched;
    extern std::atomic<bool> g_twoHandActive;
    extern weapon_interaction::State g_weaponInteraction;
    extern weapon_interaction::ReloadTargets g_reloadTargets;
    extern weapon_accessory::Presentation g_weaponAccessory;
    extern std::atomic<HRESULT> g_weaponAccessoryResult;
    extern std::array<RecentSecondaryWeaponPresentation, kTitleRuntimeSlotCount> g_secondaryWeaponPresentation;
    extern thread_local bool g_supportInvocationUntrusted;
    extern TwoHandLabTemporalPreparedOutput g_twoHandLabTemporalOutput;
    extern TwoHandLabTemporalPublication g_twoHandLabTemporalPublication;
} // namespace vr_core
