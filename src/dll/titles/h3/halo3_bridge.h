// halo3_bridge.h: the Halo 3 runtime state and functions that core (game.cpp) uses, in namespace
// halo3_bridge. Defined in src/dll/titles/h3/halo3_runtime.cpp (until the move: in game.cpp). Included
// by game.cpp and the Halo 3 runtime. Core reaches Halo 3 only through what is declared here (and the
// Halo 3 family names of halo3_family_internal.h); it must not grow core logic.
#pragma once
#include <atomic>
#include "../../core/halo3_family_internal.h"

namespace halo3_bridge
{
    using namespace game_core;
    // Vehicle transform snapshot: the measured world position and
    // orientation basis of the seated DIRECT parent, plus the seat identity,
    // published by the sampler each camera frame while seated and consumed by
    // the authored-point camera. Single writer (camera thread), seqlock.
    struct Halo3VehicleTransform
    {
        std::atomic<uint32_t> seq{0}; // even = stable, odd = mid-write
        std::atomic<uint32_t> valid{0};
        std::atomic<uint64_t> sampleMs{0};
        float pos[3];
        float fwd[3];
        float up[3];
        // The live rendered seat/attachment-node forward. The view-follow
        // reads this, never a synthetic blend: the follow integrates what it is
        // given, so any rotation lost by a predictor becomes permanent drift.
        float rawFwd[3];
        // Exact world-space placement after default-node -> live-node
        // parenting. Kept separate from the node origin because the authored
        // Blender points are tag/model-space coordinates.
        float anchor[3];
        // The same placement BEFORE the occupant-head bounce is folded in
        // - rigid in the vehicle's own frame. The camera wants the bounce; the
        // hands must not have it, or they inherit the seated biped's head
        // animation. See Halo3ComputeSeatBodyAnchor.
        float anchorBase[3];
        uint32_t anchorValid = 0;
        uint32_t anchorBaseValid = 0;
        uint32_t nodeInterpolated = 0;
        uint32_t headParented = 0;
        int32_t directType = -1;
        int32_t seatIndex = -1;
        int32_t parentHandle = -1;
        int32_t nativeInVehicle = 0;
        // The identity the authored point is keyed on - the seated
        // parent's, or the CARRIER's when the player is a mounted-turret
        // gunner (whose point is authored in carrier space). Unknown never
        // resolves to a point, so an unidentified vehicle stays stock.
        uint32_t vehicleId = static_cast<uint32_t>(Halo3VehicleId::Unknown);
        int32_t mountedTurret = 0;
    };
    enum class Halo3NodeBindingState : uint8_t
    {
        NotInstalled = 0,
        Installed,
        StockFallback,
    };
    // ---- Halo 3 vehicle state and rendered-node camera parent ---------------
    // Detection identities are byte-proven against the retail module
    // and bound at install time by unique signatures. The camera-thread sampler
    // publishes the concrete seat plus the exact rendered-node-authored anchor
    // consumed by first-person placement, yaw follow, and controls. A resolve
    // failure or runtime fault fails that feature open, loudly, and never
    // touches the camera core.
    enum class Halo3VehicleBindingState : uint8_t
    {
        NotInstalled = 0,
        Installed,
        StockFallback,
    };
    // One seqlock read of the published vehicle frame, shared by the
    // authored-point camera, the hull-follow view and the steering author, so
    // those three can never disagree about which seat is live. Frame-level
    // fail-open: stale (>100 ms), torn, non-finite or non-orthonormal inputs
    // return false and every caller keeps its stock behaviour.
    struct Halo3SeatSnapshot
    {
        float pos[3] = {0.0f, 0.0f, 0.0f};
        float fwd[3] = {0.0f, 0.0f, 0.0f};
        float rawFwd[3] = {0.0f, 0.0f, 0.0f};
        float up[3] = {0.0f, 0.0f, 0.0f};
        float anchor[3] = {0.0f, 0.0f, 0.0f};
        float anchorBase[3] = {0.0f, 0.0f, 0.0f};
        bool anchorValid = false;
        bool anchorBaseValid = false;
        bool nodeInterpolated = false;
        bool headParented = false;
        int seatIndex = -1;
        int parentHandle = -1;
        uint32_t identity = static_cast<uint32_t>(Halo3VehicleId::Unknown);
        bool mounted = false;
    };
    // Optional pitch follow. The camera thread publishes the current
    // rendered hull yaw+pitch; camera, hands, room translation, and aim all
    // consume the same roll-free transform contract. A short freshness bound
    // makes a missing pitch sample retain the accepted yaw-only follow.
    constexpr uint64_t kHalo3RollStableFollowFreshMs = 100;

    // Defined in namespace halo3_bridge.
    bool DiagnosticH3Primary(
        uint32_t expectedGeneration, uint32_t& unitOut, uint32_t& weaponOut, uint32_t& detailOut) noexcept;
    // Weapon-order diagnostic tranche: read-only semantic-primary
    // observations reusing the title readers below. Defined after those
    // readers; called from the FP interpolate/palette hooks above them.
    // expectedGeneration is the title adapter generation captured by
    // Game_DiagnosticReadPrimaryWeapon; every reader rejects a mismatch or a
    // mid-read generation change as a lifecycle guard rejection.
    // Production-safe H3 semantic-primary read: the shared body of the
    // diagnostic probe. `primaryAbsentOut` (optional) reports an explicit raw
    // empty primary slot from the same guarded native read; it is never set by
    // a validation/lifecycle failure.
    bool H3PrimaryWeaponEvidence(uint32_t expectedGeneration, uint32_t& unitOut, uint32_t& weaponOut,
        uint32_t& detailOut, bool* primaryAbsentOut = nullptr) noexcept;
    void Halo3ApplySeatYawFollow();
    uint32_t Halo3AuthoredCrosshairColorState(void* placementOutput);
    void Halo3CaptureObserver();
    bool Halo3ComputeAuthoredAnchor(float out[3]);
    bool Halo3ComputeSeatBodyAnchor(float out[3]);
    bool Halo3GuardedScopePass(void* view, char* camera, const unsigned char* saved);
    bool Halo3ReadSeatSnapshot(Halo3SeatSnapshot& out);
    bool Halo3SeatAuthorsSteering();
    bool Halo3SeatUsesWheelNow();
    bool Halo3VehicleFpActive();
    void PublishH3FpDiagnostic(WeaponOrderEventKind kind, WeaponOrderEventStatus status, uint64_t preparedSerial,
        uint32_t unit, uint32_t weapon, uint64_t aux0, uint64_t aux1) noexcept;
    bool PublishHalo3Lifecycle(bool installed, bool armed, bool teardownRequested);
    bool TickHalo3Lifecycle(const TitleTickContext& c);
    InstallStep TryInstallHalo3(const TitleTickContext& c, bool haloAvailableForInstall);
    void AutoVrTickHalo3();

    extern std::atomic<uint32_t> g_halo3CoreCallbacks;
    extern std::atomic<uint8_t> g_halo3HeadBinding;
    constexpr size_t kMaxInstalledGameHooks = 16;
    extern HookRegistry<kMaxInstalledGameHooks> g_halo3Hooks;
    extern HudLayoutRememberedCache g_halo3HudRemembered;
    extern std::atomic<bool> g_halo3InstallReady;
    extern Halo3InterpolatedNodesFn g_halo3InterpolatedNodes;
    extern std::atomic<uint64_t> g_halo3LastCamCopyMs;
    extern Halo3MarkersInternalFn g_halo3MarkersInternal;
    extern std::atomic<uint8_t> g_halo3NodeBinding;
    extern std::atomic<uint32_t> g_halo3ScopeFaultGeneration;
    extern std::atomic<uint64_t> g_halo3ScopeFaults;
    extern std::atomic<uint32_t> g_halo3SeatAimReOrigins;
    extern std::atomic<uint64_t> g_halo3ShotlessFacingSamples;
    extern std::atomic<uint8_t> g_halo3VehicleBinding;
    extern std::atomic<uint64_t> g_halo3VehicleSnapshot;
    extern Halo3VehicleTransform g_halo3VehicleTransform;
    extern Halo3VehicleTypeFn g_halo3VehicleTypeAccessor;
    extern std::atomic<uintptr_t> g_nativePauseFlag;

    // Defined in halo3_runtime.cpp: forward to the anonymous functions of the same name.
    uint32_t Halo3OccupiedSeatFlags();
    void Halo3RestoreNativeSeatPatch();
    void Halo3SampleVehicleState(uint64_t nowMs);
    void LogHalo3VehicleProbeIfNew();
} // namespace halo3_bridge
