// reach_bridge.h: the Reach state and functions that core (game.cpp) uses, in namespace reach_bridge.
// Defined in src/dll/titles/reach (until the move: in game.cpp). Included by game.cpp and the Reach files.
// Core reaches Reach only through what is declared here; it must not grow core logic.
#pragma once
#include "../../core/game_internal.h"

namespace reach_bridge
{
    using namespace game_core;
    // Reach's committed head-centre camera (world), for the shot lines.
    struct ShotHeadPose
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<float> pos[3]{}, fwd[3]{}, up[3]{};
    };
    struct ReachFpInterpolationContext;
    using ReachPlayerUnitByOutputUserFn = int(__fastcall*)(int);
    using ReachUnitInVehicleFn = bool(__fastcall*)(int);
    struct ReachNativeUnitCameraInfo
    {
        int32_t directParent = -1;
        int16_t seatIndex = -1;
        int16_t reserved06 = 0;
        const void* seatCamera = nullptr;
        float worldCameraPoint[3]{};
        int32_t relatedDatum = -1;
    };
    using ReachUnitGetCameraInfoFn = void(__fastcall*)(int32_t, ReachNativeUnitCameraInfo*);
    using ReachObjectMarkersFn = int16_t(__fastcall*)(int32_t, int32_t, Halo3ObjectMarker*, int32_t, bool, bool);
    using ReachObjectUltimateParentFn = int32_t(__fastcall*)(int32_t);
    using ReachVehicleTypeFn = int32_t(__fastcall*)(uint32_t);
    using ReachTagGetFn = void*(__fastcall*)(uint32_t, uint32_t);
    enum class ReachVehicleInputBindingState : uint8_t
    {
        StockFallback = 0,
        CleanupRequired = 1,
        Installed = 2,
    };
    enum class ReachVehicleCameraBindingState : uint8_t
    {
        StockFallback = 0,
        CleanupRequired = 1,
        Installed = 2,
    };
    struct ReachCameraCore
    {
        std::atomic<bool> installed{false};
        std::atomic<bool> armed{false};
        std::atomic<bool> teardownRequested{false};
        // Counts every Reach detour from wrapper entry until every
        // trampoline/original call has returned. Teardown disables all hooks,
        // then proves that neither a callback nor a MinHook relay ingress
        // remains before freeing either trampoline or the retained title DLL.
        std::atomic<int> activeCallbacks{0};
        uintptr_t base = 0;
        size_t size = 0;
        std::atomic<uint32_t> generation{0};
        HMODULE moduleReference = nullptr;
        void* innerTarget = nullptr;
        void* outerTarget = nullptr;
        uint64_t installedAtMs = 0;
        void* decoratorWindState = nullptr;
        MotionBlurVar motionBlurVars[2]{};
        bool motionBlurResolved = false;
        bool motionBlurSuppressed = false;
        uint8_t* patchyFogFlags = nullptr;
        // Player-facing Reach weather controls, asserted at the top of every
        // owned eye render. `renderRainSlot` is the title's own `render_rain`
        // boolean, resolved by name; `atmosphereFogTlsIndex` is the module
        // global holding the render TLS index the atmosphere helper walks to
        // reach its flags byte. Both suppression bits are touched only on the
        // render thread inside that assert.
        uint8_t* renderRainSlot = nullptr;
        uint8_t renderRainOriginal = 1;
        bool renderRainSuppressed = false;
        const uint32_t* atmosphereFogTlsIndex = nullptr;
        bool atmosphereFogSuppressed = false;
        // Live `render_lightmap_shadows` boolean. The owned-eye scope saves,
        // clears, and restores it around player_view_render to isolate Reach's
        // proven object-caster -> static-lightmap receiver pass.
        uint8_t* lightmapShadowsEnabled = nullptr;
        uint8_t* nativeWeaponIkDisable = nullptr;
        uint8_t nativeWeaponIkDisableOriginal = 0;
        bool nativeWeaponIkBypassActive = false;
        void* fpInterpolateTarget = nullptr;
        void* fpPaletteTarget = nullptr;
        void* fpCameraTarget = nullptr;
        void* ssaoTarget = nullptr;
        std::atomic<bool> ssaoIsolationActive{false};
        void* hudDrawWidgetTarget = nullptr;
        void* observerCameraTarget = nullptr;
        void* effectLocationTarget = nullptr;
        void* rainRenderTarget = nullptr;
        ReachPlayerUnitByOutputUserFn playerUnitByOutputUser = nullptr;
        ReachUnitInVehicleFn unitInVehicle = nullptr;
        std::atomic<uint8_t> vehicleInputBindingState{
            static_cast<uint8_t>(ReachVehicleInputBindingState::StockFallback)};
        ReachPlayerUnitByOutputUserFn vehiclePlayerUnitByOutputUser = nullptr;
        ReachUnitGetCameraInfoFn unitGetCameraInfo = nullptr;
        ReachObjectMarkersFn objectMarkers = nullptr;
        ReachObjectUltimateParentFn objectUltimateParent = nullptr;
        ReachVehicleTypeFn vehicleType = nullptr;
        ReachTagGetFn tagGet = nullptr;
        std::atomic<uint8_t> vehicleCameraBindingState{
            static_cast<uint8_t>(ReachVehicleCameraBindingState::StockFallback)};
    };
    struct ReachShotOccupationSnapshot
    {
        uint32_t sequence = 0;
        uint32_t generation = 0;
        uint64_t sampleMs = 0;
        bool active = false;
        bool allowsWeapons = false;
        ReachSeatLeaseKey key{};
    };
    // Arguments 3 and 4 are FULL 32-bit values, not short/byte. Reach's own
    // code proves it: at 0x2DA39D it does "mov r12d, r8d" and at 0x2DA41D
    // "cmp r12d, dword ptr [rdi+4]" - a full 32-bit compare of argument 3 -
    // and at 0x2DA39A "mov eax, r9d" reads argument 4 the same way. Declaring
    // them narrower truncated the upper bits on the way back into the engine,
    // so it took the wrong branch, decoded an invalid Blam pool handle, and
    // faulted at haloreach.dll+0x2ED80C ("mov eax,[rcx+0x98]"). That was the
    // crash, four times over, not the address and not a skipped call.
    using ReachHudDrawWidgetFn = void(__fastcall*)(int, void*, unsigned int, unsigned int, void*);
    // Retail 0x2AF648 emits at most four interpolation -> palette transactions
    // per invocation (two palette banks across two held-weapon slots). Keep one
    // bounded context for each transaction, exactly as Halo 3/ODST keep one per
    // weapon slot, then pair the final palette by its source pointer.
    constexpr size_t kReachFpTransactionCapacity = 4;
    struct ReachFpInterpolationContext
    {
        bool valid = false;
        bool transformed = false;
        uint32_t generation = 0;
        uint32_t muzzleUnit = UINT32_MAX, muzzleWeapon = UINT32_MAX;
        uint64_t preparedSerial = 0;
        uint64_t captureSerial = 0;
        BoneMatrix* source = nullptr;
        int liveSourceCount = 0;
        int interpolationView = 0;
        int interpolationId = 0;
        int interpolationSlot = 0;
        uint16_t bodyTag = 0;
        ReachFpBodyLayout layout{};
        FpExplicitPoseTargets targets{};
        BoneMatrix untouchedLive[kReachFpMaxSourceNodeCount]{};
    };

    // Defined in namespace reach_bridge.
    bool ReachAuthoredSensorOrigin(uintptr_t slot, uint8_t out[8]) noexcept;
    uint32_t ReachRetainedRuntimeGeneration();
    bool ReachReadYawReferencePair(float& gameYaw, float& headYaw);
    bool ReachReadAimFeedback(float out[3]);
    bool ReachReadShotOccupation(ReachShotOccupationSnapshot& out);
    bool ReachBoneMatrixFinite(const BoneMatrix& matrix);
    bool ReachResolveRenderModelDescriptor(uint16_t tag, const uint8_t*& descriptor);
    bool ReachReadRenderModelIdentity(uint16_t tag, uint32_t& runtimeImportChecksum, int& count);
    bool ReachResolvePaletteNodeCount(uint16_t tag, int& count);
    unsigned char* ReachVehicleObjectData(int32_t handle, uint8_t& kind);
    bool ReachVehicleFpActive();
    bool ReachFollowContextValid(uint32_t& outGeneration);
    bool ReachSeatAuthorsSteeringNow();
    bool ReachSeatUsesWheel();
    bool ReachReadGyroHull(turret_gyro::Hull& out, ReachVehicleId& identity);
    void TickReach(const TitleTickContext& c);
    bool AutoVrTickReach();
    bool ReachControllerAimActive();
    int ReachVehicleTrimSlotNow();
    bool ReachPrimaryWeaponEvidence(uint32_t expectedGeneration, uint32_t& unitOut, uint32_t& weaponOut,
        uint32_t& detailOut, bool* primaryAbsentOut = nullptr) noexcept;
    bool DiagnosticReachPrimary(
        uint32_t expectedGeneration, uint32_t& unitOut, uint32_t& weaponOut, uint32_t& detailOut) noexcept;

    extern ShotHeadPose g_reachShotHead;
    extern std::atomic<float> g_reachStockHeadingYaw;
    extern std::atomic<bool> g_reachMoveHeadingValid;
    extern std::atomic<uint64_t> g_reachLastCamCopyMs;
    extern std::atomic<uint32_t> g_reachScopeFaultGeneration;
    extern std::atomic<uint32_t> g_reachWindReplayFaults;
    extern ReachCameraCore g_reachCamera;
    extern std::atomic<uint64_t> g_reachVehicleInputSnapshot;
    extern std::atomic<uint64_t> g_reachVehicleSampleMs;
    extern std::atomic<uint32_t> g_reachAimFeedbackReadFallbacks;
    extern std::atomic<uint32_t> g_reachHandShotMoves;
    extern std::atomic<uint32_t> g_reachHandShotMisses;
    extern std::atomic<uintptr_t> g_reachFirstPersonRenderGateReturn;
    extern std::atomic<float> g_reachRenderHalfFovX[2];
    extern std::atomic<float> g_reachRenderHalfFovY[2];
    extern std::atomic<uint64_t> g_reachRenderFovSerial[2];
    extern ReachHudDrawWidgetFn g_reachOrigHudDrawWidget;
    extern thread_local ReachFpInterpolationContext g_reachFpInterpolations[kReachFpTransactionCapacity];
    extern std::atomic<uintptr_t> g_reachNativePauseFlag;
    extern std::atomic<int> g_reachEnginePauseCache;
    extern std::atomic<uint64_t> g_reachAnatomicalApplications;
    extern std::atomic<uint64_t> g_reachAnatomicalRefusals;

    // Defined in reach_camera_core.cpp: forward to the Reach .inl functions of the same name.
    bool ReachContactMeleeReady();
    void ReachPublishContactHand(int hand, const FpExplicitPoseTargets& targets, const FpInterpolationContext& context,
        const BoneMatrix& root, const BoneMatrix* solved, const float points[][3], int count, int32_t unit,
        uint32_t generation);
    void ReportReachContactMelee();
    void ReportReachGrabPickup();
    void ReportReachMuzzle();
    void ReportReachWeaponHaptics();
    uint32_t ReachGrabChosenWeapon(uint64_t now);
    bool ReachGrabPickupReady();
    void ReachGrabSetSteer(bool steer);
    void ReachGrabPublishHead(const float q[4]);
    // Reach hand owner (reach_grab_pickup.inl; GRAB-G1-PLAN.md 9.2).
    bool ReachHandOnFoot(uint64_t now);
    int ReachHandSeatKind(uint64_t now);           // holster::Seat
    void ReachHandPostRequest(int kind, int slot); // kind: holster::ProbeAction Stow, Draw or Drop
    bool ReachHandRead(uint32_t slots[4], int& role, int& planMode);
    bool ReachReadReserveWeapon(uint32_t owner, uint32_t& primary, uint32_t& reserve);
} // namespace reach_bridge
