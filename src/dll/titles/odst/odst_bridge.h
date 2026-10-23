// odst_bridge.h: the ODST state and functions that core (game.cpp) uses, in namespace odst_bridge.
// Defined in src/dll/titles/odst (until the move: in game.cpp). Included by game.cpp and the ODST files.
// Core reaches ODST only through what is declared here; it must not grow core logic.
#pragma once
#include <atomic>
#include "../../core/halo3_family_internal.h"

namespace odst_bridge
{
    using namespace game_core;
    // ODST's two prepare-view native-CHUD phases both take the local user index.
    // Their target functions are separately proven by unique ODST signatures.
    using OdstHudPhaseFn = void(__fastcall*)(int userIndex);
    // Engine target copy used by the secondary phase: source ID 1 into ODST's
    // title-specific temporary ID 0x35. The caller ignores the COM release count.
    using OdstHudTargetCopyFn = uint32_t(__fastcall*)(int sourceTargetId, int destinationTargetId);
    enum class OdstFallbackReason : int
    {
        None,
        LevelUnloaded,
        EyeRedirectUnavailable,
        UnsupportedCameraMode,
        NoCameraHeartbeat,
        InstallFailure,
        TitleLeft,
        NativePause,
        ManualRecovery,
    };
    struct OdstMotionBlurVar
    {
        float* slot = nullptr;
        float original = 0.0f;
    };
#pragma warning(push)
#pragma warning(disable : 4324) // alignas(16) camera buffers: the padding is intended
    struct OdstCameraRuntimeState
    {
        std::atomic<bool> installed{false};
        std::atomic<bool> armed{false};
        std::atomic<bool> cameraArrayReady{false};
        std::atomic<bool> teardownRequested{false};
        std::atomic<bool> sawValidCamera{false};
        std::atomic<bool> waitingLogged{false};
        std::atomic<int> fallbackReason{static_cast<int>(OdstFallbackReason::None)};
        std::atomic<int> activeCallbacks{0};
        std::atomic<int> activeRenderCallbacks{0};
        std::atomic<unsigned> captureFailures{0};
        std::atomic<uint64_t> installedAtMs{0};
        std::atomic<uint64_t> presentationDetachRequested{0};
        std::atomic<uint64_t> presentationDetachCompleted{0};
        std::atomic_flag presentationDetachInProgress = ATOMIC_FLAG_INIT;
        uintptr_t moduleBase = 0;
        size_t moduleSize = 0;
        HMODULE moduleReference = nullptr;
        CamCopyFn originalCamCopy = nullptr;
        ObserverCameraEffectFn originalObserverCameraEffect = nullptr;
        RenderViewFn originalRenderView = nullptr;
        PrepareViewFn prepareView = nullptr;
        BuildViewportFn buildViewport = nullptr;
        BuildMatricesFn buildMatrices = nullptr;
        FpCameraRebuildFn originalFpCameraRebuild = nullptr;
        FpCameraUploadFn fpCameraUpload = nullptr;
        FpDriverFn originalFpDriver = nullptr;
        OdstHudPhaseFn originalHudPhasePrimary = nullptr;
        OdstHudPhaseFn originalHudPhaseSecondary = nullptr;
        OdstHudTargetCopyFn originalHudTargetCopy = nullptr;
        // ODST's title-proven chud_compute_anchor_basis equivalent. Kept
        // separate from Halo 3's trampoline so cross-title teardown can never
        // leave a stale original pointer.
        void* originalHudAnchorBasis = nullptr;
        // ODST FP weapon/arm hook originals. Stored as void* because the typed
        // aliases are declared after this lifecycle state.
        void* originalFpInterpolate = nullptr;
        void* originalFpVisiblePalette = nullptr;
        uint8_t* nativeWeaponIkBranch = nullptr;
        bool nativeWeaponIkPatched = false;
        // ODST class-2 CHUD crosshair hider (parity with Halo 3). The playback
        // short-circuit inside chud_draw_widget is NOP'd here and restored on
        // teardown, exactly like nativeWeaponIkBranch above.
        uint8_t* crosshairClassGate = nullptr;
        bool crosshairClassGatePatched = false;
        DWORD crosshairClassGateOriginalProtect = 0;
        uintptr_t gunCameraArray = 0;
        std::atomic<void*> eyeView{nullptr};
        alignas(16) unsigned char eyeCompactCamera[0x90]{};
        alignas(16) unsigned char eyeDerivedBlock[0xC0]{};
        OdstMotionBlurVar motionBlurVars[2]{};
        bool motionBlurResolved = false;
        bool motionBlurZeroed = false;
        // ODST cinematic camera object, proven by the title's own user-input-
        // constraint setter. Its current/pending maximum-look-angle state is
        // the theatre discriminator for player-controllable cinematic shots.
        uint32_t cinematicCameraTlsMemberOffset = 0;
        // 10 camera/weapon/CHUD core hooks + optional HUD height + two
        // all-or-nothing crosshair hooks + optional brightness + the optional
        // native world-collision resolver. Trampolines are
        // recorded alongside targets so optional-hook absence can never shift
        // lifecycle mapping.
        HookRegistry<18> hooks; // 15 + pod look feed (builder, two test readers)
        void* renderHookTarget = nullptr;
    };
#pragma warning(pop)
    constexpr float kOdstWorldUnitsPerMeter = 1.0f / 3.048f;
    // Published seat state. Same seqlock contract as Halo 3's transform: a
    // reader that sees an odd or changed sequence, a stale sample, or any
    // non-finite value takes the stock camera instead.
    struct OdstVehicleTransform
    {
        std::atomic<uint32_t> seq{0};
        std::atomic<uint32_t> valid{0};
        std::atomic<uint64_t> sampleMs{0};
        float rawFwd[3]{};
        float anchor[3]{};     // authored point + bounce: the camera
        float anchorBase[3]{}; // same point rigid in the seat: the hands
        uint32_t anchorValid = 0;
        uint32_t anchorBaseValid = 0;
        uint32_t headParented = 0;
        int32_t seatIndex = -1;
        int32_t parentHandle = -1;
        uint32_t vehicleId = static_cast<uint32_t>(OdstVehicleId::Unknown);
        int32_t mountedTurret = 0;
        // The OCCUPIED seat's own flags: whether the occupant fires a personal
        // weapon from here decides whether their shots leave their own eye.
        uint32_t occupiedSeatFlags = 0;
    };
    struct OdstSeatSnapshot
    {
        float rawFwd[3];
        float anchor[3];
        float anchorBase[3];
        bool anchorValid;
        bool anchorBaseValid;
        int seatIndex;
        int parentHandle;
        OdstVehicleId identity;
        bool mounted;
    };

    // Defined in namespace odst_bridge.
    bool OdstFollowContextValid(uint32_t& outGeneration);
    // ODST and Reach production-safe semantic-primary reads: the same body the
    // title's diagnostic probe uses, plus the raw-primary-slot absence flag
    // ODST reads its own role bytes +276/+277 and FP agreement at
    // tls+0x598; Reach reads its own +34A/+34B and tls+0x6A0. Neither copies
    // Halo 3's layout.
    bool OdstPrimaryWeaponEvidence(uint32_t expectedGeneration, uint32_t& unitOut, uint32_t& weaponOut,
        uint32_t& detailOut, bool* primaryAbsentOut = nullptr) noexcept;
    bool DiagnosticOdstPrimary(
        uint32_t expectedGeneration, uint32_t& unitOut, uint32_t& weaponOut, uint32_t& detailOut) noexcept;
    bool PublishOdstLifecycle();
    bool OdstComputeSeatBodyAnchor(float out[3]);
    CinematicControlState ReadOdstCinematicControl(int32_t& scene, int32_t& shot);
    bool OdstSeatUsesWheel();
    bool OdstSeatAuthorsSteeringNow();
    bool OdstCameraOnlyContext();
    void OdstRequestFallback(OdstFallbackReason reason);
    unsigned char* OdstLoadedTagDefinition(uint32_t datum);
    bool OdstReadSeatSnapshot(OdstSeatSnapshot& out);
    bool OdstVehicleFpActive();
    void LogOdstNonFpCameraIfNew();
    void LogOdstRenderSkipIfNew();
    void LogOdstFpLayoutSelfCheckIfNew();
    void LogOdstNativeHudRouteOnce();
    bool TickOdstLifecycle(const TitleTickContext& c);
    void TryInstallOdst(const TitleTickContext& c, bool odstAvailableForInstall);
    bool AutoVrTickOdst();
    bool OdstDropPodHeadAimActive();

    extern OdstCameraRuntimeState g_odstCamera;
    extern std::atomic<uint32_t> g_odstRuntimeGeneration;
    extern std::atomic<uintptr_t> g_odstNativePauseFlag;
    extern std::atomic<float> g_odstRenderHalfFovX[2];
    extern std::atomic<float> g_odstRenderHalfFovY[2];
    extern LegacyWorldCollisionFeature g_odstWorldCollision;
    extern std::atomic<uint64_t> g_odstLastCamCopyMs;
    extern HudLayoutRememberedCache g_odstHudRemembered;
    extern std::atomic<uint8_t> g_odstVehicleBinding;
    extern Halo3PlayerUnitGetterFn g_odstPlayerUnitGetter;
    extern void** g_odstTagDataBase;
    extern OdstVehicleTransform g_odstVehicleTransform;
    extern std::atomic<uint32_t> g_odstPodServoFrames[4];
    extern std::atomic<uint64_t> g_odstRoomscaleOnFootMs;
    extern std::atomic<uint32_t> g_odstVehicleFpStable;

    // Defined in odst_camera_core.cpp: forward to the anonymous functions of the same name.
    void ReportOdstScope();
    bool OdstContactMeleeReady();
    const uint8_t* OdstContactTls();
    bool OdstContactObject(uint32_t handle, bool requireBiped = false);
    void OdstPublishContactHand(int hand, const FpInterpolationContext& context, const BoneMatrix& root,
        const BoneMatrix* solved, const float points[][3], int count, int32_t unit, uint32_t generation);
    void ReportOdstContactMelee();
    bool OdstReadReserveWeapon(uint32_t owner, uint32_t& primary, uint32_t& reserve);
    void ReportOdstMuzzle();
    void ReportOdstWeaponHaptics();
    void ReportOdstControllerFingers();
    // Defined in odst_camera_core.cpp: reads odst_scope.inl's g_odstScopeFaultGeneration.
    uint32_t OdstScopeFaultGeneration() noexcept;
} // namespace odst_bridge
