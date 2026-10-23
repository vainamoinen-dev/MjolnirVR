// halo3_runtime.cpp: Halo 3's runtime beside its camera core (moved from game.cpp, unchanged): vehicles and
// seats with the native seat patch, the scope pass, lifecycle and install machines, AutoVrTickHalo3, the Halo 3
// family HUD chain and the vehicle wheel API. The camera install and its hooks stay in game.cpp until the vr.cpp carve.
// Core reaches it through halo3_bridge.h and halo3_family_internal.h; it reaches core through
// game_internal.h. The Halo 3 .inl files are included at the same points and scopes as before.
#include "../../core/halo3_family_internal.h"
#include "halo3_bridge.h"
#include "../../core/guard_count.h"
using namespace game_core;

namespace
{
    // Inside the anonymous namespace, so the forwarders at the end can name the anonymous functions as ::Name.
    using namespace halo3_bridge;
} // namespace

namespace halo3_bridge
{
    bool Halo3VehicleFpActive();
} // namespace halo3_bridge
namespace game_core
{
    constinit Halo3RollStableFollowPublication g_halo3RollStableFollow;
} // namespace game_core
namespace
{
    enum class Halo3PitchFollowState : uint8_t
    {
        Inactive = 0,
        Active,
        IntentionalYawOnly,
        YawOnlyFallback,
    };
    constinit std::atomic<uint8_t> g_halo3PitchFollowState{static_cast<uint8_t>(Halo3PitchFollowState::Inactive)};
    constinit std::atomic<uint32_t> g_halo3PitchFollowSerial{0};

    void Halo3SetPitchFollowState(Halo3PitchFollowState state)
    {
        // Single camera-thread writer; the worker logs transitions later.
        static Halo3PitchFollowState previous = Halo3PitchFollowState::Inactive;
        if (state == previous)
            return;
        previous = state;
        g_halo3PitchFollowState.store(static_cast<uint8_t>(state), std::memory_order_relaxed);
        g_halo3PitchFollowSerial.fetch_add(1, std::memory_order_release);
    }
    void Halo3PublishRollStableFollow(float hullYaw, float hullPitch, float hullRoll = 0.0f)
    {
        Halo3PublishRollStableFollowFor(g_halo3RuntimeGeneration.load(std::memory_order_relaxed),
            VehicleFollowOwner::Halo3, hullYaw, hullPitch, hullRoll);
    }
} // namespace
namespace halo3_bridge
{
    constinit std::atomic<uint64_t> g_halo3ShotlessFacingSamples{0};
} // namespace halo3_bridge
namespace game_core
{
    // Halo 3 / ODST floating-hands node map from the interpolation context.
    legacy_floating_hands::Layout LegacyFloatingHandsLayout(const FpInterpolationContext& c)
    {
        legacy_floating_hands::Layout l{};
        l.count = c.count;
        l.rightKeep = c.wristDescendants;
        l.leftKeep = c.lWristDescendants;
        l.rightWrist = c.wrist;
        l.rightElbow = c.elbow;
        l.rightShoulder = c.shoulder;
        l.leftWrist = c.lWrist;
        l.leftElbow = c.lElbow;
        l.leftShoulder = c.lShoulder;
        l.cameraControl = c.cameraControl;
        return l;
    }
} // namespace game_core
namespace halo3_bridge
{
    bool DiagnosticH3Primary(
        uint32_t expectedGeneration, uint32_t& unitOut, uint32_t& weaponOut, uint32_t& detailOut) noexcept;
} // namespace halo3_bridge
namespace
{
    inline constexpr char kLegacyCollisionSchedulerSignature[] =
        "48 83 EC 58 F3 41 0F 10 01 F3 41 0F 5C 00 F3 41 0F 10 49 04 "
        "48 8B 84 24 98 00 00 00 F3 41 0F 5C 48 04 48 89 44 24 38 8B "
        "84 24 90 00 00 00 89 44 24 30 8B 84 24 88 00 00 00 F3 0F 11 "
        "44 24 40 F3 41 0F 10 41 08 4C 8D 4C 24 40 F3 41 0F 5C 40 08";
    bool LegacyBuildAuthoredWeaponBounds(GameTitle title, uint16_t renderModelTag, const BoneMatrix& modelRoot,
        const float collisionRoot[3], const float wrist[3], float output[][3]);
} // namespace
namespace game_core
{
    constinit std::atomic<uint64_t> g_legacyAnatomicalApplications[2]{}, g_legacyAnatomicalRefusals[2]{};
} // namespace game_core
namespace halo3_bridge
{
    uint32_t Halo3AuthoredCrosshairColorState(void* placementOutput)
    {
        // Pinned halo3.dll chud placement at +0x2EEFC8 writes its computed
        // four-float colour to its fourth argument +0x88 before returning.
        // The earlier headset probe independently measured this output region
        // as colour/alpha/animation state. Compare the ordering of all four
        // channels, so the state identity does not depend on assuming whether
        // this engine layout stores alpha first or last. Uniform fade/opacity
        // changes preserve that ordering and therefore cost no upload.
        if (!placementOutput)
            return 0;
        float channel[4]{};
        __try
        {
            memcpy(channel, static_cast<const uint8_t*>(placementOutput) + 0x88, sizeof(channel));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the Halo 3 crosshair colour faulted");
            return 0;
        }
        return ClassifyAuthoredReticleColorOrdering(channel);
    }
} // namespace halo3_bridge
namespace game_core
{
    constinit Halo3FamilyHudSize g_halo3HudSize{"Halo 3", hud_element::SetHalo3},
        g_odstHudSize{"ODST", hud_element::SetOdst};
} // namespace game_core
namespace
{
    constinit std::atomic<uint64_t> g_halo3FamilyHudElementApplied{0};
} // namespace
namespace game_core
{
    constinit thread_local Halo3FamilyHudChain g_halo3FamilyHudChain;

    // After every anchor level returns, before any height adjustment.
    void Halo3FamilyObserveHudAnchor(int anchorType, const void* basis, Halo3FamilyHudSize& size, uint32_t generation)
    {
        auto& chain = g_halo3FamilyHudChain;
        if (anchorType == hud_element::kHalo3ParentAnchor)
            return;
        if (!hud_element::IsHalo3ScreenAnchor(anchorType) || !basis)
        {
            chain.skip = true;
            return;
        }
        const float* origin = reinterpret_cast<const float*>(basis);
        const float x = origin[0x28 / sizeof(float)];
        const float y = origin[0x2C / sizeof(float)];
        if (!chain.haveAnchor)
        {
            chain.haveAnchor = true;
            chain.x = x;
            chain.y = y;
        }
        if (anchorType != hud_element::kHalo3BottomRightAnchor || !generation ||
            !hud_element::PlausibleVirtualSize(x, y))
            return;
        if (size.generation.load(std::memory_order_relaxed) == generation &&
            size.width.load(std::memory_order_relaxed) == x && size.height.load(std::memory_order_relaxed) == y)
            return;
        size.width.store(x, std::memory_order_relaxed);
        size.height.store(y, std::memory_order_relaxed);
        size.generation.store(generation, std::memory_order_release);
        LOG("%s HUD elements: virtual HUD size %.0fx%.0f learned for generation %u", size.title, x, y, generation);
    }

    // Outermost call only, under the same admission as the height setting.
    void Halo3FamilyAdjustHudElement(void* basis, const Halo3FamilyHudSize& size, uint32_t generation)
    {
        const auto& chain = g_halo3FamilyHudChain;
        if (!g_config.hud_per_element || !basis || chain.skip || !chain.haveAnchor || !generation ||
            size.generation.load(std::memory_order_acquire) != generation)
            return;
        const float width = size.width.load(std::memory_order_relaxed);
        const float height = size.height.load(std::memory_order_relaxed);
        const int slot = hud_element::SlotForPoint(chain.x, chain.y, width, height);
        if (slot < 0)
            return;
        const hud_element::Placement placement = g_config.hud_place[size.placementSet].At(slot);
        float dx = 0, dy = 0;
        if (!hud_element::TranslationFromPoint(placement, true, chain.x, chain.y, false, width, height, dx, dy))
            return;
        float* origin = reinterpret_cast<float*>(basis);
        const float x = origin[0x28 / sizeof(float)] + dx;
        const float y = origin[0x2C / sizeof(float)] + dy;
        if (!isfinite(x) || !isfinite(y))
            return;
        origin[0x28 / sizeof(float)] = x;
        origin[0x2C / sizeof(float)] = y;
        g_halo3FamilyHudElementApplied.fetch_add(1, std::memory_order_relaxed);
    }
} // namespace game_core
namespace halo3_bridge
{
    constinit HookRegistry<kMaxInstalledGameHooks> g_halo3Hooks;
    // Per-title freshness beacons. A resident-but-idle game module cannot
    // borrow another title's camera ownership.
    constinit std::atomic<uint64_t> g_halo3LastCamCopyMs{0};
    constinit std::atomic<uintptr_t> g_nativePauseFlag{0};
    bool PublishHalo3Lifecycle(bool installed, bool armed, bool teardownRequested)
    {
        const uint32_t generation = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
        if (!generation)
            return false;
        TitleRuntimeLifecycle lifecycle{};
        lifecycle.installed = installed;
        lifecycle.armed = armed;
        lifecycle.teardownRequested = teardownRequested;
        lifecycle.enabledCapabilities =
            installed && !teardownRequested ? kHalo3RuntimeCapabilities : TitleCapability_None;
        if (!g_cinematicTlsIndex)
            lifecycle.enabledCapabilities &= ~TitleCapability_CutsceneTheater;
        return TitleAdapter_PublishLifecycle(GameTitle::Halo3, generation, lifecycle);
    }
    // Keep each title's authored curvature baseline across teardown and title
    // switches. The adjusted tag can remain resident; discarding the baseline
    // would make the next scan treat an already-adjusted Z as authored and
    // compound curvature. Reuse still requires exact title-anchor verification.
    constinit HudLayoutRememberedCache g_halo3HudRemembered{};
    // The rigid, vehicle-parented seat placement the hands hang off while
    // a first-person vehicle seat owns the view. False everywhere else.
    bool Halo3ComputeSeatBodyAnchor(float out[3]);
    // ---- Weapon-order diagnostic tranche (read-only evidence) ----
    void PublishH3FpDiagnostic(WeaponOrderEventKind kind, WeaponOrderEventStatus status, uint64_t preparedSerial,
        uint32_t unit, uint32_t weapon, uint64_t aux0, uint64_t aux1) noexcept
    {
        if (!Telemetry_WeaponEventsAccepting())
            return;
        Telemetry_PublishWeaponEvent(static_cast<uint8_t>(kind), static_cast<uint8_t>(status),
            static_cast<uint8_t>(GameTitle::Halo3), g_halo3RuntimeGeneration.load(std::memory_order_acquire),
            preparedSerial, unit, weapon, aux0, aux1);
    }
    // First-person vehicle camera helpers, defined with the vehicle probe
    // module below (they run on this same engine camera thread).
    bool Halo3ComputeAuthoredAnchor(float out[3]);
} // namespace halo3_bridge
namespace
{
    bool Halo3ComputeNativeParentedAnchor(const float nativeWorld[3], float out[3], int* outTrimSlot = nullptr);
} // namespace
namespace halo3_bridge
{
    bool Halo3VehicleFpActive();
    // Folds the hull's rotation into the shared yaw reference and decides
    // whether this seat's steering is authored instead of closed-loop aimed.
    void Halo3ApplySeatYawFollow();
} // namespace halo3_bridge
namespace
{
    static_assert(sizeof(Halo3ObjectMarker) == 0x70);
    static_assert(offsetof(Halo3ObjectMarker, nodeMatrix) == 0x04);
    static_assert(offsetof(Halo3ObjectMarker, matrix) == 0x38);
    static_assert(offsetof(Halo3ObjectMarker, matrix) + offsetof(Halo3Matrix4x3, position) == 0x60);
} // namespace
namespace halo3_bridge
{
    constinit std::atomic<uint8_t> g_halo3VehicleBinding{static_cast<uint8_t>(Halo3VehicleBindingState::NotInstalled)};
    constinit Halo3VehicleTypeFn g_halo3VehicleTypeAccessor = nullptr;
    constinit Halo3InterpolatedNodesFn g_halo3InterpolatedNodes = nullptr;
    constinit Halo3MarkersInternalFn g_halo3MarkersInternal = nullptr;
    constinit std::atomic<uint8_t> g_halo3NodeBinding{static_cast<uint8_t>(Halo3NodeBindingState::NotInstalled)};
    constinit std::atomic<uint8_t> g_halo3HeadBinding{static_cast<uint8_t>(Halo3NodeBindingState::NotInstalled)};
    constinit std::atomic<uint64_t> g_halo3VehicleSnapshot{0};
} // namespace halo3_bridge
namespace
{
    constinit std::atomic<uint32_t> g_halo3VehicleFaults{0};
    constinit std::atomic<uint64_t> g_halo3VehicleSampleMs{0};

    struct Halo3VehicleProbe
    {
        std::atomic<uint32_t> seq{0};   // even = stable, odd = mid-write
        std::atomic<uint32_t> state{0}; // Halo3VehicleState
        std::atomic<int32_t> unitHandle{-1};
        std::atomic<int32_t> seatIndex{-1};
        std::atomic<int32_t> parentHandle{-1};
        std::atomic<uint32_t> parentKind{0};
        std::atomic<int32_t> directType{-1};
        std::atomic<uint32_t> nativeInVehicle{0};
        std::atomic<uint32_t> frameStatus{0}; // Halo3FrameStatus
        std::atomic<uint32_t> definitionIndex{0xFFFFFFFFu};
        std::atomic<uint32_t> vehicleId{static_cast<uint32_t>(Halo3VehicleId::Unknown)};
        std::atomic<uint32_t> mountedTurret{0};
    };
    constinit Halo3VehicleProbe g_halo3VehicleProbe;
    constinit std::atomic<uint32_t> g_halo3VehicleProbeLoggedSeq{0};

    struct Halo3ObserverCapture
    {
        std::atomic<uint32_t> seq{0}; // even = stable, odd = mid-write
        std::atomic<uint32_t> valid{0};
        std::atomic<uint64_t> captureMs{0};
        float floats[kHalo3ObserverCaptureFloats];
    };
    // not constinit: MSVC rejects it (C2127), the float array has no initializer; static zero-init, so behaviour is unchanged.
    Halo3ObserverCapture g_halo3ObserverCapture;

    // Transform probe capture (log-only): a bounded window of the seated
    // DIRECT parent's object data, so the worker can measure the vehicle's
    // world position/orientation offsets against the proven observer
    // focus. Nothing consumes these values; the log is the deliverable.
    struct Halo3ParentCapture
    {
        std::atomic<uint32_t> seq{0}; // even = stable, odd = mid-write
        std::atomic<uint32_t> valid{0};
        std::atomic<uint64_t> captureMs{0};
        float floats[kHalo3ParentCaptureFloats];
    };
    // not constinit: MSVC rejects it (C2127), the float array has no initializer; static zero-init, so behaviour is unchanged.
    Halo3ParentCapture g_halo3ParentCapture;
} // namespace
namespace halo3_bridge
{
    // not constinit: MSVC rejects it (C2127), the float array has no initializer; static zero-init, so behaviour is unchanged.
    Halo3VehicleTransform g_halo3VehicleTransform;
} // namespace halo3_bridge
namespace
{
    struct Halo3SeatNodeCache
    {
        uint32_t generation = 0;
        int objectHandle = -1;
        uint32_t definitionIndex = 0xFFFFFFFFu;
        int nodeIndex = -1;
        int seatIndex = -1;
        uint32_t vehicleId = static_cast<uint32_t>(Halo3VehicleId::Unknown);
        bool mounted = false;
        bool resolved = false;
        bool valid = false;
        Halo3Matrix4x3 defaultInverse{};
    };
    constinit Halo3SeatNodeCache g_halo3SeatNodeCache;

    struct Halo3HeadReference
    {
        uint32_t generation = 0;
        int unitHandle = -1;
        int parentHandle = -1;
        int nodeObjectHandle = -1;
        int seatIndex = -1;
        int nodeIndex = -1;
        bool interpolateNodes = false;
        bool nodeInterpolated = false;
        Halo3HeadSettleLatch settle{};
    };
    constinit Halo3HeadReference g_halo3HeadReference;

    struct Halo3NativeAnchorCalibration
    {
        uint32_t serial = 0;
        bool valid = false;
        float nativeLocal[3] = {0.0f, 0.0f, 0.0f};
    };
    constinit Halo3NativeAnchorCalibration g_halo3NativeAnchorCalibration;

    // Vibration diagnostic. Counters are written only by the camera thread
    // and read/reset by the 50 ms worker; lastPos/lastUp never leave the
    // camera thread. Nothing here feeds behavior.
    struct Halo3MotionDiag
    {
        std::atomic<uint32_t> frames{0};
        std::atomic<uint32_t> posChanged{0};
        std::atomic<uint32_t> tiltChanged{0};
        std::atomic<uint32_t> interpolatedNode{0};
        std::atomic<uint32_t> rawNode{0};
        std::atomic<uint32_t> headParented{0};
        std::atomic<uint32_t> anchorFallback{0};
        std::atomic<float> maxPosStep{0.0f}; // world units
        std::atomic<float> maxTiltDeg{0.0f};
        float lastPos[3] = {0.0f, 0.0f, 0.0f};
        float lastUp[3] = {0.0f, 0.0f, 0.0f};
    };
    constinit Halo3MotionDiag g_halo3MotionDiag;

    // The sampled hull frame walked across the 60 Hz simulation tick, so
    // the seat camera moves with the interpolated mesh instead of stepping
    // against it. Camera thread only; the atomics are for the worker log.
    constinit Halo3FrameInterp g_halo3FrameInterp;
    constinit std::atomic<uint32_t> g_halo3SmoothingActive{0};
    constinit std::atomic<float> g_halo3TickSeconds{0.0f};
    // The measured display-phase lock (halo3_vehicle_logic.h). Camera
    // thread only; the two atomics mirror its state for the worker log.
    // 0 = smoothing off/no ticks, 1 = waiting for a parked latch,
    // 2 = latched, still measuring, 3 = bounds centre proven NOT
    // render-interpolated (fixed tick pacing stands, loudly), 4 = locked.
    constinit Halo3PhaseLock g_halo3PhaseLock;
    constinit std::atomic<uint32_t> g_halo3PhaseLockState{0};
    constinit std::atomic<float> g_halo3PhaseLeadTicks{0.0f};
    // The published blend fraction. Above 1.0 the seat is extrapolated
    // past the newest simulation state; a value pinned AT 1.00 while driving
    // is the clamp defect that caused the boost/flight bounce.
    constinit std::atomic<float> g_halo3BlendAlpha{0.0f};
    // There is no armed-then-expiring seat-entry request. The facing
    // rebase is keyed on the concrete seat in Halo3ApplySeatYawFollow, so
    // there is no pending state to stamp, expire, or rate-limit.

    // Definition index -> vehicle identity, resolved once per distinct
    // vehicle instead of per frame. Direct-mapped and tiny; a collision only
    // costs a re-read. Tag data is immutable while a map is loaded, and the
    // runtime generation keys the entries so a title change cannot serve a
    // previous session's identity.
    struct Halo3IdentitySlot
    {
        uint32_t defIndex = 0xFFFFFFFFu;
        uint32_t generation = 0;
        Halo3VehicleId id = Halo3VehicleId::Unknown;
    };
    constinit Halo3IdentitySlot g_halo3IdentityCache[16];

    // Defined with the identity probe below; SEH-guarded internally.
    bool Halo3ReadDefinitionFields(uint32_t defIndex, int32_t counts[10], float jeepFields[2], int32_t* scoutSpecific,
        float* scoutAccel, float tails[3]);

    // Physics type is a class, not a vehicle. The definition fields
    // confirmed in process (engine_moment 2000 hog / 650 mongoose;
    // alien_scout specific_type 1/3/4) turn it into an identity. A read that
    // fails, or a value matching nothing, yields Unknown - never a guess.
    Halo3VehicleId Halo3ResolveIdentityCached(uint32_t defIndex, int physicsType, uint32_t generation)
    {
        if (defIndex > 0xFFFFu)
            return Halo3VehicleId::Unknown;
        // Mix the high nibble in: a seated gunner resolves both the gun and
        // its carrier every frame, and adjacent tag indices must not collide
        // into the same slot and thrash.
        Halo3IdentitySlot& slot = g_halo3IdentityCache[(defIndex ^ (defIndex >> 4)) & 15u];
        if (slot.defIndex == defIndex && slot.generation == generation)
            return slot.id;
        Halo3DefinitionFields fields;
        fields.physicsType = physicsType;
        int32_t counts[10] = {};
        float jeep[2] = {-1.0f, -1.0f};
        int32_t scoutSpecific = -1;
        float scoutAccel = -1.0f;
        float tails[3] = {-1.0f, -1.0f, -1.0f};
        if (Halo3ReadDefinitionFields(defIndex, counts, jeep, &scoutSpecific, &scoutAccel, tails))
        {
            fields.jeepValid = counts[1] == 1;
            fields.engineMoment = jeep[0];
            fields.scoutValid = counts[3] == 1;
            fields.specificType = scoutSpecific;
        }
        slot.defIndex = defIndex;
        slot.generation = generation;
        slot.id = Halo3ResolveVehicleId(fields);
        return slot.id;
    }

    // Live-node camera facts, retail-consumer and official H3EK verified.
    // An attached object's +0x14 byte is its node on the parent. The raw node
    // bank is a signed relative offset at +0x138; +0x136 is its byte size,
    // which the retail provider divides by the 0x34-byte matrix stride.
    // The renderer and native marker camera both prefer the read-only
    // interpolated bank accessor, then fall back to this same raw bank.
    inline constexpr uintptr_t kHalo3ObjectParentNodeOffset = 0x14;
    inline constexpr uintptr_t kHalo3ObjectNodeByteSizeOffset = 0x136;
    inline constexpr uintptr_t kHalo3ObjectNodeMatricesOffset = 0x138;
    inline constexpr int kHalo3MaximumRenderNodes = 256;
    inline constexpr int kHalo3HeadMarkerSid = 0x9F;

    // Current MCC loaded-tag layout, independently consumed by retail's
    // marker resolver: vehi -> hlmt -> mode -> node inverse matrix.
    inline constexpr uintptr_t kHalo3VehicleModelDatumOffset = 0x40;
    inline constexpr uintptr_t kHalo3ModelRenderModelDatumOffset = 0x0C;
    inline constexpr size_t kHalo3RenderModelNodeStride = 0x60;
    inline constexpr uintptr_t kHalo3RenderModelNodeInverseOffset = 0x28;
    // Cache the selected node's default inverse once per concrete vehicle/seat,
    // then map the Blender point plus live vehicle-local trims through it. The
    // render-model tag stores that inverse directly at node+0x28, so no
    // quaternion convention or guessed parent recursion is involved.
    bool Halo3ResolveAuthoredNodeLocal(uint32_t generation, int nodeObjectHandle, uint32_t nodeDefinitionIndex,
        int nodeIndex, Halo3VehicleId vehicleId, int seatIndex, bool mounted, float trimForward, float trimUp,
        float trimRight, float outNodeLocal[3])
    {
        Halo3SeatNodeCache& cache = g_halo3SeatNodeCache;
        const Halo3SeatPoint* point = Halo3FindSeatPoint(vehicleId, seatIndex, mounted);
        const auto emitAdjustedPoint = [&]()
        {
            if (!point || !cache.valid)
                return false;
            const float authored[3] = {point->x + trimForward, point->y - trimRight, point->z + trimUp};
            return Halo3MatrixTransformPoint(cache.defaultInverse, authored, outNodeLocal);
        };
        const bool same = cache.generation == generation && cache.objectHandle == nodeObjectHandle &&
                          cache.definitionIndex == nodeDefinitionIndex && cache.nodeIndex == nodeIndex &&
                          cache.seatIndex == seatIndex && cache.vehicleId == static_cast<uint32_t>(vehicleId) &&
                          cache.mounted == mounted;
        if (same && cache.resolved)
            return emitAdjustedPoint();

        cache = {};
        cache.generation = generation;
        cache.objectHandle = nodeObjectHandle;
        cache.definitionIndex = nodeDefinitionIndex;
        cache.nodeIndex = nodeIndex;
        cache.seatIndex = seatIndex;
        cache.vehicleId = static_cast<uint32_t>(vehicleId);
        cache.mounted = mounted;
        cache.resolved = true;

        if (!point || nodeDefinitionIndex > 0xFFFFu || nodeIndex < 0 || nodeIndex >= kHalo3MaximumRenderNodes)
            return false;

        Halo3Matrix4x3 defaultInverse{};
        bool readOk = false;
        __try
        {
            unsigned char* objectDef = Halo3LoadedTagDefinition(nodeDefinitionIndex);
            const uint32_t modelDatum =
                objectDef ? *reinterpret_cast<const uint32_t*>(objectDef + kHalo3VehicleModelDatumOffset) : 0xFFFFFFFFu;
            unsigned char* modelDef = Halo3LoadedTagDefinition(modelDatum);
            const uint32_t renderModelDatum =
                modelDef ? *reinterpret_cast<const uint32_t*>(modelDef + kHalo3ModelRenderModelDatumOffset)
                         : 0xFFFFFFFFu;
            unsigned char* renderDef = Halo3LoadedTagDefinition(renderModelDatum);
            const int32_t nodeCount =
                renderDef ? *reinterpret_cast<const int32_t*>(renderDef + kHalo3RenderModelNodesBlockOffset) : 0;
            const uint32_t nodeAddress =
                renderDef ? *reinterpret_cast<const uint32_t*>(renderDef + kHalo3RenderModelNodesBlockOffset + 4) : 0;
            void** baseSlot = g_halo3TagDataBase;
            auto* tagBase = baseSlot ? static_cast<unsigned char*>(*baseSlot) : nullptr;
            if (renderDef && tagBase && nodeCount > 0 && nodeCount <= kHalo3MaximumRenderNodes &&
                nodeIndex < nodeCount && nodeAddress)
            {
                const unsigned char* node = tagBase + static_cast<size_t>(nodeAddress) * 4 +
                                            static_cast<size_t>(nodeIndex) * kHalo3RenderModelNodeStride;
                memcpy(&defaultInverse, node + kHalo3RenderModelNodeInverseOffset, sizeof(defaultInverse));
                readOk = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("resolving a Halo 3 authored node faulted");
            readOk = false;
        }
        cache.defaultInverse = defaultInverse;
        cache.valid = readOk && Halo3MatrixValid(cache.defaultInverse);
        return emitAdjustedPoint();
    }

    // Copy one live world-space node matrix. ON follows Halo's exact
    // render-interpolated node bank; a false accessor result takes the same raw
    // fallback the visible renderer takes. OFF intentionally selects raw as an
    // A/B. A signature/fault failure disables only this camera-parent feature.
    bool Halo3ReadLiveNodeMatrix(
        int objectHandle, unsigned char* objectData, int nodeIndex, Halo3Matrix4x3& out, bool& outInterpolated)
    {
        outInterpolated = false;
        if (!objectData || nodeIndex < 0 || nodeIndex >= kHalo3MaximumRenderNodes)
            return false;

        bool accessorReturned = false;
        bool accessorFaulted = false;
        if (g_config.vehicle_cam_smoothing)
        {
            if (g_halo3NodeBinding.load(std::memory_order_acquire) !=
                    static_cast<uint8_t>(Halo3NodeBindingState::Installed) ||
                !g_halo3InterpolatedNodes)
                return false;
            __try
            {
                Halo3Matrix4x3* matrices = nullptr;
                int count = 0;
                accessorReturned = g_halo3InterpolatedNodes(objectHandle, &matrices, &count) != 0;
                if (accessorReturned)
                {
                    if (!matrices || count <= 0 || count > kHalo3MaximumRenderNodes || nodeIndex >= count)
                        return false;
                    out = matrices[nodeIndex];
                    outInterpolated = true;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                GUARD_COUNT("reading the Halo 3 interpolated nodes faulted");
                accessorFaulted = true;
            }
            if (accessorFaulted)
            {
                g_halo3NodeBinding.store(
                    static_cast<uint8_t>(Halo3NodeBindingState::StockFallback), std::memory_order_release);
                return false;
            }
        }

        if (!accessorReturned)
        {
            bool rawOk = false;
            __try
            {
                const int nodeByteSize = *reinterpret_cast<const int16_t*>(objectData + kHalo3ObjectNodeByteSizeOffset);
                const int nodeCount =
                    nodeByteSize > 0
                        ? Halo3MatrixCountFromByteSize(static_cast<uint16_t>(nodeByteSize), kHalo3MaximumRenderNodes)
                        : 0;
                const int matrixOffset = *reinterpret_cast<const int16_t*>(objectData + kHalo3ObjectNodeMatricesOffset);
                if (nodeCount > 0 && nodeIndex < nodeCount && matrixOffset != 0)
                {
                    memcpy(&out, objectData + matrixOffset + static_cast<size_t>(nodeIndex) * sizeof(out), sizeof(out));
                    rawOk = true;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                GUARD_COUNT("reading the Halo 3 object node bank faulted");
                rawOk = false;
            }
            if (!rawOk)
                return false;
        }
        return Halo3MatrixValid(out);
    }

    bool Halo3ReadPhaseMatchedHead(int unitHandle, bool interpolateNodes, float outHeadWorld[3])
    {
        if (g_halo3HeadBinding.load(std::memory_order_acquire) !=
                static_cast<uint8_t>(Halo3NodeBindingState::Installed) ||
            !g_halo3MarkersInternal)
            return false;
        Halo3ObjectMarker marker{};
        bool faulted = false;
        int count = 0;
        __try
        {
            count = g_halo3MarkersInternal(unitHandle, kHalo3HeadMarkerSid, &marker, 1, false, interpolateNodes);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the Halo 3 head marker faulted");
            faulted = true;
        }
        if (faulted)
        {
            g_halo3HeadBinding.store(
                static_cast<uint8_t>(Halo3NodeBindingState::StockFallback), std::memory_order_release);
            return false;
        }
        if (count <= 0 || !Halo3MatrixValid(marker.matrix))
            return false;

        // The internal resolver is allowed to fall back to the raw unit bank
        // even when interpolateNodes=true. A preflight cannot prove which bank
        // it ultimately used. Recompose the returned marker-local matrix with
        // the provider's unit node and require its final translation to match
        // the resolver output. If it does not, omit head bounce for this frame;
        // the exact vehicle-node-authored anchor remains valid.
        if (interpolateNodes)
        {
            if (g_halo3NodeBinding.load(std::memory_order_acquire) !=
                    static_cast<uint8_t>(Halo3NodeBindingState::Installed) ||
                !g_halo3InterpolatedNodes || marker.nodeIndex < 0)
                return false;

            Halo3Matrix4x3 unitNode{};
            bool providerReturned = false;
            bool providerFaulted = false;
            __try
            {
                Halo3Matrix4x3* matrices = nullptr;
                int matrixCount = 0;
                providerReturned = g_halo3InterpolatedNodes(unitHandle, &matrices, &matrixCount) != 0;
                if (providerReturned)
                {
                    if (!matrices || matrixCount <= 0 || matrixCount > kHalo3MaximumRenderNodes ||
                        marker.nodeIndex >= matrixCount)
                        return false;
                    memcpy(&unitNode, &matrices[marker.nodeIndex], sizeof(unitNode));
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                GUARD_COUNT("reading the Halo 3 head-phase nodes faulted");
                providerFaulted = true;
            }
            if (providerFaulted)
            {
                // This second provider call is only a provenance witness for
                // optional head bounce. Its failure must not disarm the live
                // vehicle-node anchor that already succeeded this frame.
                g_halo3HeadBinding.store(
                    static_cast<uint8_t>(Halo3NodeBindingState::StockFallback), std::memory_order_release);
                return false;
            }
            float predicted[3] = {};
            if (!providerReturned || !Halo3MatrixValid(marker.nodeMatrix) ||
                !Halo3MatrixTransformPoint(unitNode, marker.nodeMatrix.position, predicted))
                return false;
            for (int i = 0; i < 3; ++i)
            {
                const float magnitude = fmaxf(1.0f, fmaxf(fabsf(predicted[i]), fabsf(marker.matrix.position[i])));
                if (fabsf(predicted[i] - marker.matrix.position[i]) > 8.0f * FLT_EPSILON * magnitude)
                    return false;
            }
        }
        memcpy(outHeadWorld, marker.matrix.position, sizeof(marker.matrix.position));
        return true;
    }

    // First-person camera state, all owned by the engine camera thread
    // (the sampler and ApplyHeadLook both run inside CamCopyHook). The
    // debounce keeps seat entry/exit swings and rapid seat switches from
    // flickering the substitution; the atomic mirror is what the camera gate
    // reads each frame.
    constinit Halo3VehicleDebounce g_halo3VehicleFpDebounce;
    constinit std::atomic<uint32_t> g_halo3VehicleFpStable{static_cast<uint32_t>(Halo3VehicleState::Unknown)};
    // How many settled seat entry/exit edges have asked for a play-space
    // re-neutralisation. The sampler is log-free, so the worker reports it.
    constinit std::atomic<uint32_t> g_halo3SeatRecenters{0};
} // namespace
namespace halo3_bridge
{
    // How many aim frames took the seated personal-weapon re-origin. Reported
    // by the worker so a "shots do not follow the crosshair" result can be told
    // apart from the correction never running.
    constinit std::atomic<uint32_t> g_halo3SeatAimReOrigins{0};
} // namespace halo3_bridge
namespace
{
#include "halo3_native_seat_patch.inl"

    void Halo3DisableVehicleProbe(const char* /*reason logged by worker*/)
    {
        Halo3RestoreNativeSeatPatch();
        g_halo3VehicleSnapshot.store(0, std::memory_order_release);
        Halo3VehicleTransform& xf = g_halo3VehicleTransform;
        xf.seq.fetch_add(1, std::memory_order_acq_rel);
        xf.anchorValid = 0;
        xf.anchorBaseValid = 0;
        xf.nodeInterpolated = 0;
        xf.headParented = 0;
        xf.valid.store(0, std::memory_order_relaxed);
        xf.seq.fetch_add(1, std::memory_order_release);
        g_halo3SeatNodeCache = {};
        g_halo3HeadReference = {};
        g_halo3VehicleFpDebounce = {};
        g_halo3VehicleFpStable.store(static_cast<uint32_t>(Halo3VehicleState::Unknown), std::memory_order_release);
        g_halo3VehicleFaults.fetch_add(1, std::memory_order_relaxed);
        g_halo3VehicleBinding.store(
            static_cast<uint8_t>(Halo3VehicleBindingState::StockFallback), std::memory_order_release);
    }

    constinit std::atomic<uint32_t> g_halo3VehicleInvalidHandles{0};

    // Same independently verified H3 datum_get layout as the H3 contact
    // adapter: H3EK 364640, retail 830F0..83107. Never follow only a reused
    // low-16-bit object index into a native vehicle/marker accessor.
    unsigned char* Halo3VehicleObjectData(unsigned char* table, uint32_t handle, uint8_t expectedKind)
    {
        if (handle == UINT32_MAX)
            return nullptr;
        if (table && (handle >> 16) && *reinterpret_cast<const int32_t*>(table + 0x24) == 0x18)
        {
            const int32_t count = *reinterpret_cast<const int32_t*>(table + 0x3C);
            auto* entries = *reinterpret_cast<unsigned char**>(table + 0x48);
            if (entries && count > 0 && count <= 0x10000 && (handle & 0xFFFF) < static_cast<uint32_t>(count))
            {
                auto* entry = entries + (handle & 0xFFFF) * 0x18;
                if (*reinterpret_cast<const uint16_t*>(entry) == (handle >> 16) && entry[3] == expectedKind)
                    return *reinterpret_cast<unsigned char**>(entry + 0x10);
            }
        }
        g_halo3VehicleInvalidHandles.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    // Sampled from CamCopyHook: the engine's own camera thread, whose engine
    // TLS is proven live by ReadCinematicControl running in ApplyHeadLook.
    // Allocation-, log-, and lock-free; ~66 Hz via the tick latch. All engine
    // reads sit under SEH; a fault degrades only this probe (StockFallback).
    void Halo3SampleVehicleState(uint64_t nowMs)
    {
        if (g_halo3VehicleBinding.load(std::memory_order_acquire) !=
                static_cast<uint8_t>(Halo3VehicleBindingState::Installed) ||
            !g_engineTlsIndex)
            return;
        const uint32_t generation = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
        if (!generation)
            return;
        // Seated: sample every camera frame so the authored-point camera
        // tracks the vehicle at render rate. On foot the 15 ms latch stands.
        const bool seatedLastSample = g_halo3VehicleTransform.valid.load(std::memory_order_relaxed) != 0;
        const uint64_t last = g_halo3VehicleSampleMs.load(std::memory_order_relaxed);
        if (!seatedLastSample && nowMs - last < 15)
            return;
        g_halo3VehicleSampleMs.store(nowMs, std::memory_order_relaxed);

        int unit = -1;
        int seat = -1;
        int parent = -1;
        int directType = -1;
        unsigned parentKind = 0;
        unsigned native = 0;
        float parentWindow[kHalo3ParentCaptureFloats];
        bool parentWindowValid = false;
        unsigned char* parentObjectData = nullptr;
        int unitParentNode = -1;
        // An attached direct parent (mounted turret child) stores its
        // +0x50 frame parent-relative, so the sampler also captures the
        // carrier (grandparent) window to compose a world frame.
        int parentOfParent = -1;
        float grandWindow[kHalo3ParentCaptureFloats];
        bool grandWindowValid = false;
        unsigned char* grandObjectData = nullptr;
        int directParentNode = -1;
        bool chainTooDeep = false;
        uint32_t defIndex = 0xFFFFFFFFu;
        // The carrier's own definition, so a mounted-turret gunner can be
        // identified (and anchored) by the vehicle carrying the gun.
        uint32_t carrierDefIndex = 0xFFFFFFFFu;
        int carrierType = -1;
        bool faulted = false;
        __try
        {
            // Output user is the constant 0: the retail getter has no bounds
            // check of its own.
            unit = g_halo3PlayerUnitGetter(0);
            if (unit != -1)
            {
                auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
                auto* tls = slots ? reinterpret_cast<unsigned char*>(slots[*g_engineTlsIndex]) : nullptr;
                unsigned char* table =
                    tls ? *reinterpret_cast<unsigned char**>(tls + kHalo3TlsObjectTableOffset) : nullptr;
                unsigned char* data = Halo3VehicleObjectData(table, static_cast<uint32_t>(unit), 0);
                if (data)
                {
                    seat = *reinterpret_cast<const int16_t*>(data + kHalo3UnitSeatWordOffset);
                    if (seat != -1)
                    {
                        unitParentNode = *reinterpret_cast<const int8_t*>(data + kHalo3ObjectParentNodeOffset);
                        parent = *reinterpret_cast<const int32_t*>(data + kHalo3ObjectParentOffset);
                        if (parent != -1)
                        {
                            unsigned char* parentData =
                                Halo3VehicleObjectData(table, static_cast<uint32_t>(parent), kHalo3ObjectKindVehicle);
                            parentKind = parentData ? kHalo3ObjectKindVehicle : 0xFF;
                            if (parentData && parentKind == kHalo3ObjectKindVehicle)
                            {
                                parentObjectData = parentData;
                                directParentNode =
                                    *reinterpret_cast<const int8_t*>(parentData + kHalo3ObjectParentNodeOffset);
                                const uint16_t definitionIndex = *reinterpret_cast<const uint16_t*>(parentData);
                                defIndex = definitionIndex;
                                directType = g_halo3VehicleTypeAccessor(definitionIndex);
                                memcpy(parentWindow, parentData + kHalo3ParentCaptureBase, sizeof(parentWindow));
                                parentWindowValid = true;
                                // Walk one attachment level. The carrier
                                // must itself be a ROOT vehicle; a deeper
                                // chain publishes no frame (stock + log).
                                parentOfParent =
                                    *reinterpret_cast<const int32_t*>(parentData + kHalo3ObjectParentOffset);
                                if (parentOfParent != -1)
                                {
                                    unsigned char* grandData = Halo3VehicleObjectData(
                                        table, static_cast<uint32_t>(parentOfParent), kHalo3ObjectKindVehicle);
                                    if (grandData)
                                    {
                                        grandObjectData = grandData;
                                        memcpy(grandWindow, grandData + kHalo3ParentCaptureBase, sizeof(grandWindow));
                                        grandWindowValid = true;
                                        carrierDefIndex = *reinterpret_cast<const uint16_t*>(grandData);
                                        carrierType =
                                            g_halo3VehicleTypeAccessor(static_cast<unsigned short>(carrierDefIndex));
                                        chainTooDeep = *reinterpret_cast<const int32_t*>(
                                                           grandData + kHalo3ObjectParentOffset) != -1;
                                    }
                                    else
                                    {
                                        chainTooDeep = true;
                                    }
                                }
                            }
                            if (parentData)
                                native = g_halo3UnitInVehicle(unit);
                        }
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("sampling the Halo 3 vehicle state faulted");
            faulted = true;
        }
        if (faulted)
        {
            Halo3DisableVehicleProbe("sampler fault");
            return;
        }

        const bool seated = seat >= 0 && parent != -1 && parentKind == kHalo3ObjectKindVehicle;
        // While the player is in a seat, tell Halo that seat is first
        // person so it stops drawing the character model around the headset.
        // Off foot, on feature-disable, or on any layout failure this restores
        // the seat's own value immediately - including when a config reload
        // crosses an old live session.
        if (seated)
            Halo3EnsureFirstPersonSeatFlag(defIndex, seat, generation);
        else
            Halo3RestoreNativeSeatPatch();
        // Publish the vehicle transform for the authored-point camera.
        // Root parents publish their +0x50 frame directly; attached
        // parents (mounted turrets) compose carrier o local, since their
        // stored frame is parent-relative (proven in the log). Either way the
        // frame origin must pass the bounding-sphere sanity gate or the
        // stock chase view stands for the whole seat session.
        // Identity, resolved outside the sampler's SEH block (the tag walk
        // carries its own) and cached, so this is a lookup on all but the
        // first frame of a seat session.
        const Halo3VehicleId directId = Halo3ResolveIdentityCached(defIndex, directType, generation);
        // A mounted turret gunner: the direct parent is the gun (physics type
        // 5) but the native flag still reports the ULTIMATE parent as a
        // non-turret, i.e. there is a carrier under it. Logged: chaingun
        // directType=5 nativeInVehicle=1, stationary turret directType=5
        // nativeInVehicle=0.
        const bool mountedTurret = seated && directType == 5 && native == 1 && grandWindowValid && !chainTooDeep;
        const Halo3VehicleId carrierId = mountedTurret
                                             ? Halo3ResolveIdentityCached(carrierDefIndex, carrierType, generation)
                                             : Halo3VehicleId::Unknown;
        // A turret with no carrier under it is one the player walked up to.
        const bool stationaryTurret = seated && directType == 5 && native == 0;
        const Halo3VehicleId anchorId =
            mountedTurret ? carrierId : (stationaryTurret ? Halo3VehicleId::StationaryTurret : directId);

        Halo3FrameStatus frameStatus = Halo3FrameStatus::None;
        {
            Halo3VehicleTransform& xf = g_halo3VehicleTransform;
            Halo3Frame world{};
            bool frameOk = false;
            float frameBoundsCenter[3] = {};
            float frameBoundsRadius = 0.0f;
            bool frameBoundsValid = false;
            if (seated && parentWindowValid && !chainTooDeep)
            {
                const auto frameFrom = [](const float* window)
                {
                    const auto at = [&](uintptr_t off) { return window + (off - kHalo3ParentCaptureBase) / 4; };
                    Halo3Frame f{};
                    memcpy(f.pos, at(kHalo3ObjectPositionOffset), sizeof(f.pos));
                    memcpy(f.fwd, at(kHalo3ObjectForwardOffset), sizeof(f.fwd));
                    memcpy(f.up, at(kHalo3ObjectUpOffset), sizeof(f.up));
                    return f;
                };
                const Halo3Frame local = frameFrom(parentWindow);
                // Which object's bounding sphere the anchor is sanity-checked
                // against - the frame's own owner.
                const float* boundsWindow = parentWindow;
                if (mountedTurret)
                {
                    // The gunner point is authored in CARRIER space, so
                    // take the carrier's frame verbatim. Composing
                    // carrier o local instead is ~0.42 wu (1.3 m) low:
                    // an attached child's stored transform is relative to the
                    // parent's ATTACHMENT NODE, not its object origin (the
                    // hog's `turret` marker's raw node-local translation
                    // matched the measured child position exactly, while the
                    // node-composed tag position did not). Anchoring in
                    // carrier space never touches that node matrix. Nothing
                    // is lost: a mounted turret's object frame is mount-fixed
                    // and does not follow gun aim.
                    world = frameFrom(grandWindow);
                    frameOk = Halo3FrameOrthonormal(world);
                    frameStatus = frameOk ? Halo3FrameStatus::Carrier : Halo3FrameStatus::Insane;
                    boundsWindow = grandWindow;
                }
                else if (parentOfParent == -1)
                {
                    world = local;
                    frameOk = Halo3FrameOrthonormal(world);
                    frameStatus = frameOk ? Halo3FrameStatus::Direct : Halo3FrameStatus::Insane;
                }
                else if (grandWindowValid)
                {
                    const Halo3Frame carrier = frameFrom(grandWindow);
                    if (Halo3FrameOrthonormal(carrier) && Halo3FrameOrthonormal(local))
                    {
                        world = Halo3ComposeFrame(carrier, local);
                        frameOk = Halo3FrameOrthonormal(world);
                    }
                    frameStatus = frameOk ? Halo3FrameStatus::Composed : Halo3FrameStatus::Insane;
                }
                if (frameOk)
                {
                    const float* center = boundsWindow + (0x1C - kHalo3ParentCaptureBase) / 4;
                    const float radius = boundsWindow[(0x28 - kHalo3ParentCaptureBase) / 4];
                    if (!Halo3AnchorWithinBounds(world.pos, center, radius, 2.0f))
                    {
                        frameOk = false;
                        frameStatus = Halo3FrameStatus::Insane;
                    }
                    else
                    {
                        memcpy(frameBoundsCenter, center, sizeof(frameBoundsCenter));
                        frameBoundsRadius = radius;
                        frameBoundsValid = true;
                    }
                }
            }
            else if (seated && chainTooDeep)
            {
                frameStatus = Halo3FrameStatus::TooDeep;
            }
            if (frameOk)
            {
                // Select the node Halo actually attached this occupant to.
                // A normal rider's unit+0x14 names a node on the direct parent.
                // A mounted gunner's direct parent is the gun child, whose own
                // +0x14 names its attachment node on the carrier. The authored
                // mounted points were authored in that carrier's tag frame.
                const int nodeObject = mountedTurret ? parentOfParent : parent;
                unsigned char* nodeObjectData = mountedTurret ? grandObjectData : parentObjectData;
                const uint32_t nodeDefinition = mountedTurret ? carrierDefIndex : defIndex;
                const int nodeIndex = mountedTurret ? directParentNode : unitParentNode;
                // Forward means toward the windshield, up means out of the
                // seat, and right is the negative tag-left axis. They are
                // placement coordinates in vehicle/tag space,
                // so they must go through the same live node as the Blender
                // point; applying them later in HMD/world axes would slide a
                // tuned camera against the mesh while turning or banking.
                const int trimSlot = ConfigSeatTrimSlot(static_cast<int>(anchorId), seat, mountedTurret);
                const float trimScale = g_worldScale.load();
                const float trimForward = ConfigSeatCamForward(g_config, trimSlot) * trimScale;
                const float trimUp = ConfigSeatCamUp(g_config, trimSlot) * trimScale;
                const float trimRight = ConfigSeatCamRight(g_config, trimSlot) * trimScale;

                Halo3Frame meshFrame = world;
                Halo3Matrix4x3 liveNode{};
                float authoredNodeLocal[3] = {};
                float cameraAnchor[3] = {};
                float bodyAnchor[3] = {};
                bool nodeInterpolated = false;
                bool anchorValid = false;
                bool bodyAnchorValid = false;
                bool headParented = false;
                const bool haveLiveNode =
                    frameBoundsValid &&
                    Halo3ResolveAuthoredNodeLocal(generation, nodeObject, nodeDefinition, nodeIndex, anchorId, seat,
                        mountedTurret, trimForward, trimUp, trimRight, authoredNodeLocal) &&
                    Halo3ReadLiveNodeMatrix(nodeObject, nodeObjectData, nodeIndex, liveNode, nodeInterpolated);
                if (haveLiveNode)
                {
                    memcpy(meshFrame.pos, liveNode.position, sizeof(meshFrame.pos));
                    memcpy(meshFrame.fwd, liveNode.forward, sizeof(meshFrame.fwd));
                    memcpy(meshFrame.up, liveNode.up, sizeof(meshFrame.up));

                    anchorValid = Halo3MatrixTransformPoint(liveNode, authoredNodeLocal, cameraAnchor) &&
                                  Halo3AnchorWithinBounds(cameraAnchor, frameBoundsCenter, frameBoundsRadius, 2.0f);
                    // Keep this pre-bounce placement. It is the seat
                    // itself, rigid in the vehicle frame, and it is what the
                    // hands hang off; the head bounce below belongs only to
                    // the camera.
                    memcpy(bodyAnchor, cameraAnchor, sizeof(bodyAnchor));
                    bodyAnchorValid = anchorValid;

                    // The attached mod's natural bounce comes from this exact
                    // phase-matched occupant-head marker. The enum-only seat
                    // debounce is much shorter than Halo's boarding and
                    // seat-switch animations, so it cannot define H0. Keep the
                    // exact authored anchor until this concrete seat identity's
                    // head has aged and remained still in node space.
                    const bool stableSeat = g_halo3VehicleFpStable.load(std::memory_order_relaxed) ==
                                            static_cast<uint32_t>(Halo3VehicleState::Vehicle);
                    Halo3HeadReference& headRef = g_halo3HeadReference;
                    const bool sameHeadSeat = headRef.generation == generation && headRef.unitHandle == unit &&
                                              headRef.parentHandle == parent &&
                                              headRef.nodeObjectHandle == nodeObject && headRef.seatIndex == seat &&
                                              headRef.nodeIndex == nodeIndex &&
                                              headRef.interpolateNodes == g_config.vehicle_cam_smoothing &&
                                              headRef.nodeInterpolated == nodeInterpolated;
                    if (!sameHeadSeat || !stableSeat)
                    {
                        headRef = {};
                        headRef.generation = generation;
                        headRef.unitHandle = unit;
                        headRef.parentHandle = parent;
                        headRef.nodeObjectHandle = nodeObject;
                        headRef.seatIndex = seat;
                        headRef.nodeIndex = nodeIndex;
                        headRef.interpolateNodes = g_config.vehicle_cam_smoothing;
                        headRef.nodeInterpolated = nodeInterpolated;
                    }
                    float headWorld[3] = {};
                    float currentHeadLocal[3] = {};
                    if (anchorValid && stableSeat && Halo3ReadPhaseMatchedHead(unit, nodeInterpolated, headWorld) &&
                        Halo3MatrixInverseTransformPoint(liveNode, headWorld, currentHeadLocal))
                    {
                        // Once this seat has settled, the head
                        // contribution stays on for the whole seat session and
                        // SATURATES at the limit instead of dropping out. The
                        // old in-limit gate plus its re-settle is what stepped
                        // the camera every time the car moved enough to breach
                        // the limit - and on a moving vehicle the head never
                        // holds still long enough to earn the contribution
                        // back, so it stayed off until the car stopped.
                        float headAnchor[3] = {};
                        if (headRef.settle.Update(nowMs, currentHeadLocal) &&
                            Halo3ComputeHeadParentedPoint(liveNode, headWorld, headRef.settle.reference,
                                authoredNodeLocal, g_config.vehicle_bounce, headAnchor) &&
                            Halo3AnchorWithinBounds(headAnchor, frameBoundsCenter, frameBoundsRadius, 2.0f))
                        {
                            memcpy(cameraAnchor, headAnchor, sizeof(cameraAnchor));
                            headParented = true;
                        }
                    }
                    else if (!headRef.settle.valid)
                    {
                        // A quiet window must consist of valid phase-matched
                        // samples; a gap restarts observation, never the camera.
                        headRef.settle = {};
                    }
                }
                else
                {
                    g_halo3HeadReference = {};
                }

                // Diagnostic (log-only): headset testing reported the vehicle mesh
                // "vibrating, fighting against the camera override". The two
                // candidate causes look completely different here. If the
                // engine advances the hull only on its own simulation tick
                // while MCC renders faster and INTERPOLATES the mesh, the
                // camera steps in a staircase the mesh does not follow and
                // posChanged lands far below the camera-frame count. If it is
                // instead the hull rocking on its suspension - which the
                // anchor tracks but the deliberately horizon-stable view does
                // not - posChanged tracks the frame count and the tilt step is
                // what is large. One drive answers it.

                // The camera already consumes Halo's render-interpolated node bank.
                // Feeding it through the frame interpolator and phase lock would double-filter it and
                // recreate the catch-up lag. Leave that predictor dormant.
                g_halo3FrameInterp.valid = false;
                g_halo3PhaseLock = Halo3PhaseLock{};
                g_halo3SmoothingActive.store(0, std::memory_order_relaxed);
                g_halo3TickSeconds.store(0.0f, std::memory_order_relaxed);
                g_halo3PhaseLockState.store(0, std::memory_order_relaxed);
                g_halo3PhaseLeadTicks.store(0.0f, std::memory_order_relaxed);

                xf.seq.fetch_add(1, std::memory_order_acq_rel); // -> odd
                memcpy(xf.pos, meshFrame.pos, sizeof(xf.pos));
                memcpy(xf.fwd, meshFrame.fwd, sizeof(xf.fwd));
                memcpy(xf.up, meshFrame.up, sizeof(xf.up));
                memcpy(xf.rawFwd, meshFrame.fwd, sizeof(xf.rawFwd));
                memcpy(xf.anchor, cameraAnchor, sizeof(xf.anchor));
                memcpy(xf.anchorBase, bodyAnchor, sizeof(xf.anchorBase));
                xf.anchorValid = anchorValid ? 1u : 0u;
                xf.anchorBaseValid = bodyAnchorValid ? 1u : 0u;
                xf.nodeInterpolated = nodeInterpolated ? 1u : 0u;
                xf.headParented = headParented ? 1u : 0u;
                xf.directType = directType;
                xf.seatIndex = seat;
                xf.parentHandle = parent;
                xf.nativeInVehicle = static_cast<int32_t>(native);
                xf.vehicleId = static_cast<uint32_t>(anchorId);
                xf.mountedTurret = mountedTurret ? 1 : 0;
                xf.valid.store(1, std::memory_order_relaxed);
                xf.sampleMs.store(nowMs, std::memory_order_relaxed);
                xf.seq.fetch_add(1, std::memory_order_release); // -> even
            }
            else if (xf.valid.load(std::memory_order_relaxed))
            {
                xf.seq.fetch_add(1, std::memory_order_acq_rel); // -> odd
                xf.anchorValid = 0;
                xf.anchorBaseValid = 0;
                xf.nodeInterpolated = 0;
                xf.headParented = 0;
                xf.valid.store(0, std::memory_order_relaxed);
                xf.seq.fetch_add(1, std::memory_order_release); // -> even
                // Leaving the seat starts the diagnostic window over, so one
                // drive's numbers never carry into the next, and drops the
                // interpolator so the next vehicle cannot be blended toward
                // from this one's last known pose.
                g_halo3FrameInterp.valid = false;
                g_halo3PhaseLock = Halo3PhaseLock{};
                g_halo3PhaseLockState.store(0, std::memory_order_relaxed);
                g_halo3SmoothingActive.store(0, std::memory_order_relaxed);
                g_halo3SeatNodeCache = {};
                g_halo3HeadReference = {};
            }
        }
        const Halo3VehicleState state =
            unit == -1 ? Halo3VehicleState::Unknown : (seated ? Halo3VehicleState::Vehicle : Halo3VehicleState::OnFoot);
        // The type refinement is published only for an authored 0..9 value;
        // 0xB (no authored physics: stationary turrets, shade, mounted turret
        // child tags) stays a probe-log observation until it is classified.
        const int snapshotType = seated && directType >= 0 && directType <= 9 ? directType : -1;
        g_halo3VehicleSnapshot.store(
            Halo3VehicleSnapshot(generation, state, snapshotType, seated ? seat : -1), std::memory_order_release);

        // Settle the first-person camera gate and fire the one-shot
        // recenter on a settled seat entry/exit, so the head frame rebases to
        // the vehicle's heading once the engine's entry swing is over. With
        // the feature off this changes nothing - no recenter, and the gate
        // mirror is never consumed.
        const Halo3VehicleState previousStable = g_halo3VehicleFpDebounce.stable;
        if (g_halo3VehicleFpDebounce.Update(state, kHalo3VehicleDebounceFrames))
        {
            g_halo3VehicleFpStable.store(
                static_cast<uint32_t>(g_halo3VehicleFpDebounce.stable), std::memory_order_relaxed);
            if (g_config.vehicle_first_person && (g_halo3VehicleFpDebounce.stable == Halo3VehicleState::Vehicle ||
                                                     previousStable == Halo3VehicleState::Vehicle))
            {
                // Re-neutralise the room-space origin on BOTH settled
                // edges. Whatever the player physically walked before boarding
                // was otherwise carried into the seat as a standing lean, and
                // whatever they leaned in the seat was carried back out on
                // foot - the player had to reach for the L3+R3 chord after
                // boarding a Banshee to get back into place. This is the
                // position half of that chord only: it captures the neutral
                // head position and never touches the yaw baseline, so it
                // cannot snap the facing that the entry nose-align (in) or the
                // hull follow (out) has already established.
                if (g_config.vehicle_recenter_on_seat)
                {
                    g_needPosRecenter.store(true, std::memory_order_release);
                    g_halo3SeatRecenters.fetch_add(1, std::memory_order_relaxed);
                }
                // The facing rebase onto the hull's nose used to be
                // ARMED here, on the settled edge, and then had to find a live
                // seat before its expiry. That made the whole boarding
                // animation face the wrong way. It is now a per-seat
                // transaction driven by the raw authored seat in
                // Halo3ApplySeatYawFollow, which fires on the first frame the
                // engine reports the seat -- earlier than this edge can exist.
            }
        }

        // Publish probe fields for the worker only when something changed.
    }
} // namespace
namespace halo3_bridge
{
    // Captured from ObserverCameraEffectHook (slot 0 only): a bounded float
    // window of the live observer record so the worker can measure where the
    // per-seat focus fields sit. memcpy of 160 bytes, no allocation, SEH.
    void Halo3CaptureObserver()
    {
        if (g_halo3VehicleBinding.load(std::memory_order_acquire) !=
                static_cast<uint8_t>(Halo3VehicleBindingState::Installed) ||
            !g_engineTlsIndex)
            return;
        bool faulted = false;
        __try
        {
            auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
            auto* tls = slots ? reinterpret_cast<unsigned char*>(slots[*g_engineTlsIndex]) : nullptr;
            unsigned char* observer = tls ? *reinterpret_cast<unsigned char**>(tls + kHalo3TlsObserverOffset) : nullptr;
            if (observer)
            {
                Halo3ObserverCapture& cap = g_halo3ObserverCapture;
                cap.seq.fetch_add(1, std::memory_order_acq_rel); // -> odd
                memcpy(cap.floats, observer + kHalo3ObserverCaptureBase, sizeof(cap.floats));
                cap.valid.store(1, std::memory_order_relaxed);
                cap.captureMs.store(GetTickCount64(), std::memory_order_relaxed);
                cap.seq.fetch_add(1, std::memory_order_release); // -> even
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("capturing the Halo 3 observer faulted");
            faulted = true;
        }
        if (faulted)
            Halo3DisableVehicleProbe("observer capture fault");
    }

    bool Halo3ReadSeatSnapshot(Halo3SeatSnapshot& out)
    {
        Halo3VehicleTransform& xf = g_halo3VehicleTransform;
        if (!xf.valid.load(std::memory_order_acquire))
            return false;
        if (GetTickCount64() - xf.sampleMs.load(std::memory_order_relaxed) > 100)
            return false;
        const uint32_t seq = xf.seq.load(std::memory_order_acquire);
        if (seq & 1u)
            return false;
        memcpy(out.pos, xf.pos, sizeof(out.pos));
        memcpy(out.fwd, xf.fwd, sizeof(out.fwd));
        memcpy(out.rawFwd, xf.rawFwd, sizeof(out.rawFwd));
        memcpy(out.up, xf.up, sizeof(out.up));
        memcpy(out.anchor, xf.anchor, sizeof(out.anchor));
        memcpy(out.anchorBase, xf.anchorBase, sizeof(out.anchorBase));
        out.anchorValid = xf.anchorValid != 0;
        out.anchorBaseValid = xf.anchorBaseValid != 0;
        out.nodeInterpolated = xf.nodeInterpolated != 0;
        out.headParented = xf.headParented != 0;
        out.seatIndex = xf.seatIndex;
        out.parentHandle = xf.parentHandle;
        out.identity = xf.vehicleId;
        out.mounted = xf.mountedTurret != 0;
        if (xf.seq.load(std::memory_order_acquire) != seq)
            return false;
        for (int i = 0; i < 3; ++i)
            if (!isfinite(out.pos[i]) || !isfinite(out.fwd[i]) || !isfinite(out.rawFwd[i]) || !isfinite(out.up[i]) ||
                (out.anchorValid && !isfinite(out.anchor[i])) || (out.anchorBaseValid && !isfinite(out.anchorBase[i])))
                return false;
        const float fLen = out.fwd[0] * out.fwd[0] + out.fwd[1] * out.fwd[1] + out.fwd[2] * out.fwd[2];
        const float rawFLen =
            out.rawFwd[0] * out.rawFwd[0] + out.rawFwd[1] * out.rawFwd[1] + out.rawFwd[2] * out.rawFwd[2];
        const float uLen = out.up[0] * out.up[0] + out.up[1] * out.up[1] + out.up[2] * out.up[2];
        const float fu = out.fwd[0] * out.up[0] + out.fwd[1] * out.up[1] + out.fwd[2] * out.up[2];
        if (fabsf(fLen - 1.0f) > 0.05f || fabsf(rawFLen - 1.0f) > 0.05f || fabsf(uLen - 1.0f) > 0.05f ||
            fabsf(fu) > 0.15f)
            return false;
        return true;
    }

    // The sampler has already converted the Blender/tag-space point
    // through the node's stored default inverse and the live rendered node.
    // Consume that exact world point directly. Applying the authored point to
    // the node frame a second time would shift every camera by its full offset.
    // Where the occupant's BODY is, as opposed to where the camera is.
    //
    // On foot the hand/gun origin is the engine's own camera-source position,
    // which is carried by the player's body and is therefore a sound anchor.
    // In a first-person vehicle seat it is not: Halo's seated camera evaluator
    // resolves the OCCUPANT'S `head` MARKER (halo3+0x355853, string id 0x9F),
    // so the origin the hands hang off animates with the seated biped's head
    // and neck as the view turns. That is the exact "chained to my face"
    // behaviour - rotate the view and the arms and gun are dragged with it.
    //
    // This returns the seat placement BEFORE the occupant-head bounce is added:
    // the authored point carried through the live rendered seat node and
    // nothing else, so it is rigid in the vehicle's own frame. Rotating the
    // head cannot move it, while the vehicle's motion, suspension and banking
    // still do. The camera keeps the bounce; only the arms lose it.
    //
    // It is deliberately the same POINT the camera uses, not a lowered chest
    // position: the controller displacement added to it is measured from the
    // room-space head reference captured at this exact seat, so shifting the
    // origin down would drop the hands by that amount rather than re-parent
    // them. What changes is what the origin FOLLOWS, which is the body.
    bool Halo3ComputeSeatBodyAnchor(float out[3])
    {
        if (!out || !Halo3VehicleFpActive())
            return false;
        Halo3SeatSnapshot seat;
        if (!Halo3ReadSeatSnapshot(seat) || !seat.anchorBaseValid)
            return false;
        memcpy(out, seat.anchorBase, sizeof(seat.anchorBase));
        return true;
    }

    bool Halo3ComputeAuthoredAnchor(float out[3])
    {
        Halo3SeatSnapshot seat;
        if (!Halo3ReadSeatSnapshot(seat) || !seat.anchorValid)
            return false;
        // The trim slot is keyed by SEAT, not just vehicle - a mounted
        // gunner shares seat index 0 with the driver, so it takes its own.
        memcpy(out, seat.anchor, sizeof(seat.anchor));
        return true;
    }
} // namespace halo3_bridge
namespace
{
    // Keep the engine-computed seated camera as the motion parent. On the
    // first stable native frame, measure its vehicle-local point; afterward add
    // only the local difference to the Blender-authored point. Native motion,
    // including render interpolation and seat/vehicle animation, passes through
    // exactly instead of being reconstructed from the 60 Hz object sampler.
    bool Halo3ComputeNativeParentedAnchor(const float nativeWorld[3], float out[3], int* outTrimSlot)
    {
        if (g_halo3NativeSeatState.load(std::memory_order_acquire) !=
                static_cast<uint32_t>(Halo3NativeSeatState::Active) ||
            !nativeWorld || !out)
            return false;
        for (int i = 0; i < 3; ++i)
            if (!isfinite(nativeWorld[i]))
                return false;

        Halo3SeatSnapshot seat;
        if (!Halo3ReadSeatSnapshot(seat))
            return false;
        const Halo3SeatPoint* point =
            Halo3FindSeatPoint(static_cast<Halo3VehicleId>(seat.identity), seat.seatIndex, seat.mounted);
        if (!point)
            return false;
        if (outTrimSlot)
            *outTrimSlot = ConfigSeatTrimSlot(static_cast<int>(seat.identity), seat.seatIndex, seat.mounted);

        Halo3Frame frame{};
        memcpy(frame.pos, seat.pos, sizeof(frame.pos));
        memcpy(frame.fwd, seat.fwd, sizeof(frame.fwd));
        memcpy(frame.up, seat.up, sizeof(frame.up));
        Halo3NativeAnchorCalibration& calibration = g_halo3NativeAnchorCalibration;
        const uint32_t serial = g_halo3NativeSeatSerial.load(std::memory_order_acquire);
        if (!calibration.valid || calibration.serial != serial)
        {
            const float relative[3] = {
                nativeWorld[0] - frame.pos[0], nativeWorld[1] - frame.pos[1], nativeWorld[2] - frame.pos[2]};
            Halo3FrameUnrotate(frame, relative, calibration.nativeLocal);
            for (int i = 0; i < 3; ++i)
                if (!isfinite(calibration.nativeLocal[i]) || fabsf(calibration.nativeLocal[i]) > 10.0f)
                    return false;
            calibration.serial = serial;
            calibration.valid = true;
        }
        const float authoredLocal[3] = {point->x, point->y, point->z};
        Halo3NativeParentedAnchor(frame, nativeWorld, calibration.nativeLocal, authoredLocal, out);
        return isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]);
    }
} // namespace
namespace halo3_bridge
{
    // Camera gate, evaluated once per camera write on the camera thread.
    // Both the debounced state (entry/exit settling) and the raw snapshot
    // (immediate frame-level staleness, generation keying, 500 ms sampler
    // heartbeat) must agree on Vehicle before the substitution runs.
    bool Halo3VehicleFpActive()
    {
        if (!g_config.vehicle_first_person)
            return false;
        if (g_halo3VehicleFpStable.load(std::memory_order_relaxed) != static_cast<uint32_t>(Halo3VehicleState::Vehicle))
            return false;
        return Game_Halo3VehicleState().state == Halo3VehicleState::Vehicle;
    }
} // namespace halo3_bridge
namespace
{
    // ---- The view turns with the vehicle -----------------------------------
    // "the camera does not rotate with the car" (headset report, 2026-07-31). With the
    // eye on the authored seat point but facing world-locked, driving a
    // circle left "straight ahead" pointing out of the side of the vehicle.
    //
    // The hull's rotation is folded into g_gameYawRef one frame step at a time.
    // Everything the player sees is already expressed against that one
    // reference - view, hand-aim ray, rendered hands and gun, crosshair, move
    // stick - so they all turn together and the reticle stays under the hand.
    // Owned by the engine camera thread, like every other writer of that
    // reference.
    constinit Halo3SeatYaw g_halo3SeatYaw;
    constinit Halo3Wheel g_halo3Wheel;
} // namespace
namespace halo3_bridge
{
    constinit std::atomic<uint32_t> g_halo3SeatAuthorsSteering{0};
    // Halo 3's answers to the cross-title seat dispatchers (game.cpp): the
    // lines the dispatchers used to read from Halo 3's state, unchanged.
    bool Halo3SeatAuthorsSteering()
    {
        if (g_halo3SeatAuthorsSteering.load(std::memory_order_acquire) != 0 && Halo3VehicleFpActive())
            return true;
        return false;
    }
    bool Halo3SeatUsesWheelNow()
    {
        Halo3SeatSnapshot seat;
        if (!Halo3ReadSeatSnapshot(seat))
            return false;
        return Halo3VehicleUsesWheel(static_cast<Halo3VehicleId>(seat.identity));
    }
} // namespace halo3_bridge
namespace
{
    constinit std::atomic<float> g_halo3SeatYawTotal{0.0f};
} // namespace
namespace game_core
{
    // Steering the author is publishing this frame, + = right. Written by the
    // input thread's wheel update, read by the aim-stick author.
    constinit std::atomic<float> g_halo3SteerAxis{0.0f};
    constinit std::atomic<uint32_t> g_halo3WheelDriving{0};
} // namespace game_core
namespace
{
    constinit std::atomic<uint32_t> g_halo3WheelSwallowsGrips{0};
    vehicle_wheel::SteerClass Halo3SteerClass(Halo3VehicleId id)
    {
        using C = vehicle_wheel::SteerClass;
        if (!Halo3VehicleUsesWheel(id))
            return C::None;
        return id == Halo3VehicleId::Mongoose || id == Halo3VehicleId::Ghost || id == Halo3VehicleId::Chopper ? C::Bike
                                                                                                              : C::Car;
    }
    bool Halo3DriverArmed(Halo3VehicleId id)
    {
        return id == Halo3VehicleId::Ghost || id == Halo3VehicleId::Chopper;
    }
} // namespace
namespace halo3_bridge
{
    void Halo3ApplySeatYawFollow()
    {
        Halo3SeatSnapshot seat;
        const bool fpActive = Halo3VehicleFpActive();
        const bool haveAuthoredSeat = fpActive && Halo3ReadSeatSnapshot(seat) && seat.anchorValid;
        const bool active = haveAuthoredSeat && Halo3SeatFollowsHull(static_cast<Halo3VehicleId>(seat.identity),
                                                    seat.seatIndex, seat.mounted);
        // Wheel/Stick steering makes the driver seat follow the hull too.
        const Halo3VehicleId seatId =
            haveAuthoredSeat ? static_cast<Halo3VehicleId>(seat.identity) : Halo3VehicleId::Unknown;
        const bool seatDriver = haveAuthoredSeat && Halo3SeatIsDriver(seatId, seat.seatIndex, seat.mounted);
        const vehicle_wheel::SteerClass steerClass = Halo3SteerClass(seatId);
        if (TitleAdapter_GetActiveTitle() == GameTitle::Halo3) // on foot publishes "not the driver"
            PublishVehicleSteerContext(GameTitle::Halo3, fpActive && seatDriver, steerClass, Halo3DriverArmed(seatId),
                Halo3VehicleIsLookSteered(seatId) && !Halo3VehicleUsesWheel(seatId));
        const bool followEnabled = vehicle_wheel::EffectiveFollow(g_config.vehicle_view_follow, seatDriver, steerClass,
            vehicle_wheel::ModeFor(steerClass, g_config.vehicle_steer_car, g_config.vehicle_steer_bike));
        const bool pitchEligible =
            !g_config.vehicle_horizon_lock && active &&
            Halo3SeatFollowsPitch(static_cast<Halo3VehicleId>(seat.identity), seat.seatIndex, seat.mounted);

        // Position-neutral and yaw-neutral are different transactions. Every
        // concrete authored seat gets a fresh OpenXR position zero exactly once,
        // including a seat switch, a quick genuine re-entry, or a stationary
        // turret whose yaw intentionally does not follow. A torn/missing frame
        // leaves the last key intact, so it cannot re-zero a player's lean.
        static Halo3SeatPositionKey positionSeat;
        // Yaw uses the same concrete identity boundary. Unlike the old
        // magnitude cap, this distinguishes a real seat/vehicle switch from a
        // violent rotation of the same car.
        static Halo3SeatPositionKey yawSeat;
        const bool positionGateStable =
            g_config.vehicle_first_person &&
            g_halo3VehicleFpStable.load(std::memory_order_relaxed) == static_cast<uint32_t>(Halo3VehicleState::Vehicle);
        if (!positionGateStable)
        {
            positionSeat = {};
            yawSeat = {};
            g_halo3SeatYaw = {};
            Halo3ClearRollStableFollow();
        }
        else if (!followEnabled)
        {
            yawSeat = {};
            g_halo3SeatYaw = {};
            Halo3ClearRollStableFollow();
        }
        else if (haveAuthoredSeat && !pitchEligible)
        {
            // Aircraft seats keep the proven yaw-only follow. This is
            // a feature-local refusal; their view and controls remain live.
            Halo3ClearRollStableFollow();
        }
        if (haveAuthoredSeat)
        {
            const uint32_t generation = g_halo3RuntimeGeneration.load(std::memory_order_relaxed);
            if (!positionSeat.Matches(generation, seat.parentHandle, seat.seatIndex, seat.identity, seat.mounted))
            {
                float positionQ[4], position[3];
                if (VR_GetHeadPose(positionQ, position))
                {
                    memcpy(g_headPosRef, position, sizeof(g_headPosRef));
                    VR_NotePositionRecenter(); // the cabin fade re-captures its seated eye
                    g_needPosRecenter.store(false, std::memory_order_relaxed);
                    positionSeat.Set(generation, seat.parentHandle, seat.seatIndex, seat.identity, seat.mounted);
                }
            }
        }

        // Settled seat entry rebases "straight ahead" onto the hull's own
        // nose. The generic recenter cannot do this - it reads the heading off
        // the ENGINE camera, which at that moment is still swinging around the
        // vehicle from the entry animation, so whichever way the player
        // happened to walk in became their forward.
        //
        // This is a per-SEAT transaction, not a timed one, and it does not
        // wait for the entry debounce.
        //
        // It used to be armed only when the 15-sample debounce settled, then
        // had to find a live seat inside a 2 s expiry, and was silently
        // dropped altogether by a 1.5 s global rate limit. Halo's boarding
        // animation is longer than all of that on the vehicles you climb onto
        // rather than step into - the Banshee swings the player right around
        // the hull - so the whole mount was spent facing wherever the player
        // walked in from, and the nose only arrived afterwards as a snap.
        // The headset report is exactly that: "im not in the default spot i
        // chose, it takes a second ... i just need the camera oriented proper
        // when entering a banshee".
        //
        // Keyed on the concrete seat instead. It runs on the FIRST frame the
        // engine reports a valid authored seat and exactly once per
        // occupation, so the facing is already the hull's nose before the
        // player can see out of it. The key is what makes that safe: a
        // debounced-state flap does not change the seat identity, so it can
        // never re-snap mid-drive, which is what the old rate limit was for.
        // A blipped player-unit read cannot trigger it either, because a seat
        // only counts once its authored anchor has resolved inside the
        // vehicle's own bounds.
        static Halo3SeatPositionKey alignSeat;
        Halo3SeatSnapshot rawSeat;
        const bool alignEligible =
            g_config.vehicle_first_person && Game_Halo3VehicleState().state == Halo3VehicleState::Vehicle &&
            Halo3ReadSeatSnapshot(rawSeat) && rawSeat.anchorValid &&
            Halo3SeatFollowsHull(static_cast<Halo3VehicleId>(rawSeat.identity), rawSeat.seatIndex, rawSeat.mounted);
        if (!alignEligible)
        {
            alignSeat = {};
        }
        else
        {
            const uint32_t alignGeneration = g_halo3RuntimeGeneration.load(std::memory_order_relaxed);
            float hq[4], hp[3];
            if (!alignSeat.Matches(
                    alignGeneration, rawSeat.parentHandle, rawSeat.seatIndex, rawSeat.identity, rawSeat.mounted) &&
                VR_GetHeadPose(hq, hp))
            {
                const float x = hq[0], y = hq[1], z = hq[2], w = hq[3];
                const float fx = -2.0f * (w * y + x * z);
                const float fz = -(1.0f - 2.0f * (x * x + y * y));
                g_gameYawRef = atan2f(rawSeat.rawFwd[1], rawSeat.rawFwd[0]);
                g_headYawRef = atan2f(fx, -fz);
                alignSeat.Set(
                    alignGeneration, rawSeat.parentHandle, rawSeat.seatIndex, rawSeat.identity, rawSeat.mounted);
                LOG("H3 vehicle: seat entry aligned to the vehicle's nose "
                    "(%.1f deg) on the first authored frame",
                    g_gameYawRef * 57.2958f);
            }
        }

        // Full follow consumes every wrapped heading change from the same
        // concrete seat, including collision/spin steps above the old 0.60-rad
        // cutoff. A torn/missing frame keeps the identity and last valid yaw,
        // so the next valid sample catches up instead of accumulating drift.
        const bool followActive = followEnabled && active;
        if (followActive)
        {
            const uint32_t generation = g_halo3RuntimeGeneration.load(std::memory_order_relaxed);
            if (!yawSeat.Matches(generation, seat.parentHandle, seat.seatIndex, seat.identity, seat.mounted))
            {
                Halo3ClearRollStableFollow();
                yawSeat.Set(generation, seat.parentHandle, seat.seatIndex, seat.identity, seat.mounted);
                g_halo3SeatYaw = {};
            }
            const float delta = Halo3SeatYawDelta(g_halo3SeatYaw, seat.rawFwd, true);
            NoteVehicleFollowYaw(delta);
            if (delta != 0.0f)
                g_gameYawRef = WrapPi(g_gameYawRef + delta);

            // The live rendered forward supplies pitch without consuming roll.
            // A malformed frame keeps the last publication only until its
            // freshness bound; the new feature then fails to the old mapping.
            float hullPitch = 0.0f;
            float hullRoll = 0.0f;
            if (pitchEligible && g_halo3SeatYaw.armed && Halo3RollStablePitchFromForward(seat.rawFwd, hullPitch))
                Halo3PublishRollStableFollow(g_halo3SeatYaw.lastYaw, hullPitch,
                    Halo3RollFromBasis(seat.rawFwd, seat.up, hullRoll)
                        ? PartialHorizonRoll(hullRoll, g_config.vehicle_horizon_hold_deg)
                        : 0.0f);
        }
        g_halo3SeatYawTotal.store(g_halo3SeatYaw.total, std::memory_order_relaxed);
        Halo3PitchFollowState pitchState = Halo3PitchFollowState::Inactive;
        if (followEnabled && fpActive)
        {
            if (active && !pitchEligible)
            {
                pitchState = Halo3PitchFollowState::IntentionalYawOnly;
            }
            else
            {
                float publishedYaw = 0.0f, publishedPitch = 0.0f;
                pitchState = Halo3ReadRollStableFollow(publishedYaw, publishedPitch)
                                 ? Halo3PitchFollowState::Active
                                 : Halo3PitchFollowState::YawOnlyFallback;
            }
        }
        Halo3SetPitchFollowState(pitchState);
        // Steering ownership switches on exactly the condition that removes
        // the closed loop's feedback, so the two can never be out of step.
        const bool authors = followActive && Halo3SeatAuthorsSteering(static_cast<Halo3VehicleId>(seat.identity),
                                                 seat.seatIndex, seat.mounted, followEnabled);
        // The wheel state itself belongs to the input thread, which releases it
        // on the same condition; only the published atomics are cleared here,
        // so a game that stopped polling input cannot leave a steer latched.
        if (!authors)
        {
            g_halo3SteerAxis.store(0.0f, std::memory_order_relaxed);
            g_halo3WheelDriving.store(0, std::memory_order_relaxed);
        }
        g_halo3SeatAuthorsSteering.store(authors ? 1u : 0u, std::memory_order_release);
    }
} // namespace halo3_bridge
namespace
{
    // Identity probe (log-only, worker thread, transition-gated by the
    // caller): resolve the seated vehicle's LOADED definition and dump the
    // discriminator fields so the values can be verified in-process
    // before any camera keys off them. These identity fields are not part of
    // the live seat transaction; SEH covers unload races.
    bool Halo3ReadDefinitionFields(uint32_t defIndex, int32_t counts[10], float jeepFields[2], int32_t* scoutSpecific,
        float* scoutAccel, float tails[3])
    {
        bool ok = false;
        __try
        {
            void** instSlot = g_halo3TagInstanceTable;
            void** baseSlot = g_halo3TagDataBase;
            unsigned char* instTable = instSlot ? static_cast<unsigned char*>(*instSlot) : nullptr;
            unsigned char* tagBase = baseSlot ? static_cast<unsigned char*>(*baseSlot) : nullptr;
            if (instTable && tagBase && defIndex <= 0xFFFFu)
            {
                const uint32_t instanceDword =
                    *reinterpret_cast<const uint32_t*>(instTable + static_cast<size_t>(defIndex) * 8 + 4);
                if (instanceDword)
                {
                    unsigned char* def = tagBase + static_cast<size_t>(instanceDword) * 4;
                    for (int t = 0; t < 10; ++t)
                        counts[t] =
                            *reinterpret_cast<const int32_t*>(def + kHalo3VehiclePhysicsBlocksOffset +
                                                              static_cast<size_t>(t) * kHalo3VehiclePhysicsBlockStride);
                    const uint32_t jeepAddr = *reinterpret_cast<const uint32_t*>(
                        def + kHalo3VehiclePhysicsBlocksOffset + 1 * kHalo3VehiclePhysicsBlockStride + 4);
                    if (counts[1] == 1 && jeepAddr)
                    {
                        unsigned char* elem = tagBase + static_cast<size_t>(jeepAddr) * 4;
                        jeepFields[0] = *reinterpret_cast<const float*>(elem + kHalo3JeepEngineMomentOffset);
                        jeepFields[1] = *reinterpret_cast<const float*>(elem + kHalo3JeepTurnRateOffset);
                    }
                    const uint32_t scoutAddr = *reinterpret_cast<const uint32_t*>(
                        def + kHalo3VehiclePhysicsBlocksOffset + 3 * kHalo3VehiclePhysicsBlockStride + 4);
                    if (counts[3] == 1 && scoutAddr)
                    {
                        unsigned char* elem = tagBase + static_cast<size_t>(scoutAddr) * 4;
                        *scoutSpecific = *reinterpret_cast<const int8_t*>(elem + kHalo3ScoutSpecificTypeOffset);
                        *scoutAccel = *reinterpret_cast<const float*>(elem + kHalo3ScoutAccelerationOffset);
                    }
                    tails[0] = *reinterpret_cast<const float*>(def + kHalo3DefMinFlipVelocityOffset);
                    tails[1] = *reinterpret_cast<const float*>(def + kHalo3DefMaxFlipVelocityOffset);
                    tails[2] = *reinterpret_cast<const float*>(def + kHalo3DefBlurSpeedOffset);
                    ok = true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading Halo 3 definition fields faulted");
            ok = false;
        }
        return ok;
    }

    // Called from the 50 ms worker (NOT a hot hook), so logging is safe here.
    // Keep feature installation, rollback, and StockFallback transitions live:
    // those are required failure-isolation evidence. The discovery capture
    // below them remains available only when the retired diagnostic gate is on.
    void LogHalo3VehicleProbeIfNew()
    {
        static uint64_t nextHandleReportMs = 0;
        const uint64_t handleReportNow = GetTickCount64();
        const uint32_t rejectedHandles = handleReportNow >= nextHandleReportMs
                                             ? g_halo3VehicleInvalidHandles.exchange(0, std::memory_order_relaxed)
                                             : 0;
        if (rejectedHandles)
            nextHandleReportMs = handleReportNow + 2000;
        if (rejectedHandles)
            LOG("H3 vehicle sampler: rejected %u stale/out-of-range or wrong-kind object handles; native seat fallback only",
                rejectedHandles);

        // The node anchor and occupant bounce are independent optional
        // transactions. Runtime faults are raised from the camera thread and
        // reported here so neither can degrade silently.
        static uint8_t loggedNodeBinding = 0xFF;
        static uint8_t loggedHeadBinding = 0xFF;
        const uint8_t nodeBinding = g_halo3NodeBinding.load(std::memory_order_acquire);
        if (nodeBinding != loggedNodeBinding &&
            nodeBinding != static_cast<uint8_t>(Halo3NodeBindingState::NotInstalled))
        {
            if (nodeBinding == static_cast<uint8_t>(Halo3NodeBindingState::Installed))
                LOG("H3 vehicle node anchor: Installed; authored points follow "
                    "Halo's rendered seat/attachment nodes");
            else
                LOG("H3 vehicle node anchor: StockFallback; rendered-node "
                    "source unavailable (smoothing ON stays stock; raw-node A/B "
                    "remains available), camera core unaffected");
            loggedNodeBinding = nodeBinding;
        }
        const uint8_t headBinding = g_halo3HeadBinding.load(std::memory_order_acquire);
        if (headBinding != loggedHeadBinding &&
            headBinding != static_cast<uint8_t>(Halo3NodeBindingState::NotInstalled))
        {
            if (headBinding == static_cast<uint8_t>(Halo3NodeBindingState::Installed))
                LOG("H3 vehicle occupant motion: Installed; animated head "
                    "translation is relative to the exact authored baseline");
            else
                LOG("H3 vehicle occupant motion: StockFallback; relative head "
                    "bounce disabled, exact node anchor and camera core "
                    "unaffected");
            loggedHeadBinding = headBinding;
        }

        static uint32_t pitchFollowSerialLogged = 0;
        const uint32_t pitchFollowSerial = g_halo3PitchFollowSerial.load(std::memory_order_acquire);
        if (pitchFollowSerial != pitchFollowSerialLogged)
        {
            const auto state =
                static_cast<Halo3PitchFollowState>(g_halo3PitchFollowState.load(std::memory_order_relaxed));
            if (state == Halo3PitchFollowState::Active)
                LOG("H3 vehicle pitch follow: Active; view, hands and aim use "
                    "the roll-stable vehicle pitch frame");
            else if (state == Halo3PitchFollowState::IntentionalYawOnly)
                LOG("H3 vehicle pitch follow: YawOnly; aircraft pitch is "
                    "intentionally excluded for control and vertical safety");
            else if (state == Halo3PitchFollowState::YawOnlyFallback)
                LOG("H3 vehicle pitch follow: YawOnlyFallback; live finite "
                    "seat pitch unavailable/stale, camera core unaffected");
            else
                LOG("H3 vehicle pitch follow: Inactive; optional toggle off "
                    "or no first-person vehicle seat");
            pitchFollowSerialLogged = pitchFollowSerial;
        }

        static uint32_t nativeSeatSerialLogged = 0;
        const uint32_t nativeSeatSerial = g_halo3NativeSeatSerial.load(std::memory_order_acquire);
        if (nativeSeatSerial != nativeSeatSerialLogged)
        {
            const auto nativeState =
                static_cast<Halo3NativeSeatState>(g_halo3NativeSeatState.load(std::memory_order_acquire));
            if (nativeState == Halo3NativeSeatState::Active)
            {
                const uint32_t seatFlags = Halo3OccupiedSeatFlags();
                LOG("H3 first-person seat: Active; occupied seat reports "
                    "first person, so Halo stops drawing the player's own "
                    "character model. Camera anchor unchanged. Seat flags "
                    "%08X (allows-weapons=%d); personal-weapon shots are "
                    "re-origined onto the engine's own eye so they pass "
                    "through the floating reticle (%u aim frames so far)",
                    seatFlags, Halo3SeatFiresPersonalWeapon(seatFlags) ? 1 : 0,
                    g_halo3SeatAimReOrigins.load(std::memory_order_relaxed));
            }
            else if (nativeState == Halo3NativeSeatState::StockFallback)
                LOG("H3 first-person seat: StockFallback; loaded seat layout "
                    "did not validate, character model stays drawn and the "
                    "camera core is unaffected");
            else
                LOG("H3 first-person seat: restored the seat's own camera "
                    "flag");
            nativeSeatSerialLogged = nativeSeatSerial;
        }

        static uint32_t seatRecentersLogged = 0;
        const uint32_t seatRecenters = g_halo3SeatRecenters.load(std::memory_order_relaxed);
        if (seatRecenters != seatRecentersLogged)
        {
            LOG("H3 vehicle: play space re-centred on a settled seat "
                "entry/exit (%u total, position only, facing untouched)",
                seatRecenters);
            seatRecentersLogged = seatRecenters;
        }
        static bool fallbackLogged = false;
        const uint8_t binding = g_halo3VehicleBinding.load(std::memory_order_acquire);
        if (binding == static_cast<uint8_t>(Halo3VehicleBindingState::StockFallback))
        {
            if (!fallbackLogged)
            {
                fallbackLogged = true;
                LOG("H3 vehicle probe: runtime fault (%u total) -> "
                    "StockFallback; vehicle probe disabled, camera core "
                    "unaffected",
                    g_halo3VehicleFaults.load(std::memory_order_relaxed));
            }
            return;
        }
        fallbackLogged = false;
    }
    // Enclosed driver/pilot seat (Scorpion, Wraith, Banshee) with a fresh seat
    // snapshot: the scope pass renders the cockpit screen's forward hull camera
    // from the seat's anchor and hull forward. False = no screen this frame.
    bool Halo3CockpitCamera(ScopeCameraPose& pose, float aspect, ScopeProjectionTangents& lens)
    {
        Halo3SeatSnapshot seat;
        if (!g_config.vehicle_cockpit_screen || !Halo3VehicleFpActive() || !Halo3ReadSeatSnapshot(seat) ||
            !seat.anchorValid || VR_IsCutsceneTheaterActive())
            return false;
        const cockpit_screen::Class cls =
            cockpit_screen::LegacyClassOf(static_cast<int>(seat.identity), seat.seatIndex, seat.mounted);
        if (cls == cockpit_screen::Class::None)
            return false;
        float monitorFov = 0;
        if (VR_VehicleMonitorCamera(static_cast<int>(vehicle_monitors::Game::Halo3Odst),
                vehicle_monitors::LegacyVehicleName(static_cast<int>(seat.identity), seat.seatIndex, seat.mounted),
                static_cast<int>(cls), seat.anchor, seat.rawFwd, cockpit_screen::kWorldUnitsPerMeter, pose.position,
                pose.forward, pose.up, monitorFov))
        {
            const cockpit_screen::Lens ml = cockpit_screen::LensFor(monitorFov, aspect);
            lens = {ml.horizontal, ml.vertical};
            return true;
        }
        const float ahead = (g_config.cockpit_screen_ahead_m > 0 ? g_config.cockpit_screen_ahead_m
                                                                 : cockpit_screen::CameraAheadM(cls)) *
                            cockpit_screen::kWorldUnitsPerMeter;
        const float up = cockpit_screen::CameraUpM(cls) * cockpit_screen::kWorldUnitsPerMeter;
        if (!cockpit_screen::CameraPose(seat.anchor, seat.rawFwd, ahead, up, pose.position, pose.forward, pose.up))
            return false;
        const cockpit_screen::Lens l = cockpit_screen::LensFor(g_config.cockpit_screen_fov_deg, aspect);
        lens = {l.horizontal, l.vertical};
        return true;
    }

    bool BuildRightHandScopeCamera(char* camera, const unsigned char* saved)
    {
        float controllerBasis[9], weaponSeat[3], meshScale = 1.0f;
        if (!ControllerWorldPoseEx(false, controllerBasis, weaponSeat, meshScale))
            return false;
        const float* cameraOrigin = reinterpret_cast<const float*>(saved);
        // Look along the barrel the shot leaves from (the muzzle receipt),
        // not the engine aim: a steady ~2 deg was measured between the two.
        const float aimForward[3] = {g_aimFwdX.load(), g_aimFwdY.load(), g_aimFwdZ.load()};
        float barrel[3]{}, bulletForward[3]{};
        uint64_t barrelAgeMs = 0;
        const bool barrelFresh = VR_ScopeBarrelForward(GameTitle::Halo3, barrel, barrelAgeMs);
        const bool fromBarrel = SelectScopeForward(barrel, barrelFresh, aimForward, bulletForward);
        for (int i = 0; i < 3; ++i)
            g_h3ScopeFwd[i].store(bulletForward[i], std::memory_order_relaxed);
        g_h3ScopeFwdBarrel.store(fromBarrel, std::memory_order_relaxed);
        g_h3ScopeFwdAgeMs.store(barrelAgeMs, std::memory_order_relaxed);
        g_h3ScopeFwdBuiltMs.store(GetTickCount64(), std::memory_order_release);
        ScopeCameraPose pose{};
        // The camera at the muzzle (the earlier CE VR mod), else the head origin.
        float muzzleOrigin[3];
        const float* origin =
            VR_ScopeCameraOrigin(GameTitle::Halo3, cockpit_screen::kWorldUnitsPerMeter, bulletForward, muzzleOrigin)
                ? muzzleOrigin
                : cameraOrigin;
        if (!ComputeScopeCameraPose(controllerBasis, origin, bulletForward, pose))
            return false;
        VR_NoteScopeCamera(GameTitle::Halo3, pose.position, cameraOrigin, cockpit_screen::kWorldUnitsPerMeter);
        memcpy(camera, saved, 0x90);
        memcpy(camera + 0x00, pose.position, sizeof(pose.position));
        if (!VR_VisorZoomHeadView()) // the visor keeps the head camera's own view
        {
            memcpy(camera + 0x0C, pose.forward, sizeof(pose.forward));
            memcpy(camera + 0x18, pose.up, sizeof(pose.up));
        }
        return true;
    }
} // namespace
namespace halo3_bridge
{
    // Fault containment for the Halo 3 scope / cockpit pass, the only third view that
    // had none. A fault inside the native render now leaves the scope
    // raster closed and g_scopeRenderActive false, retires the scope for this module
    // generation (Game_HasScopeRenderer), and RenderViewHook still restores the camera.
    constinit std::atomic<uint32_t> g_halo3ScopeFaultGeneration{0};
    constinit std::atomic<uint64_t> g_halo3ScopeFaults{0};
} // namespace halo3_bridge
namespace
{
    void Halo3ScopePassBody(void* view, char* camera, const unsigned char* saved)
    {
        // One mono world-only camera at Halo's collision-safe gameplay origin.
        // Its bullet-aligned direction remains hand-aimed, and the circular
        // display remains mounted on the gun independently.
        // Enclosed seat: the second render is the cockpit screen's hull camera.
        float cockpitAspect = 4.0f / 3.0f;
        VR_GetScopeRenderAspect(cockpitAspect);
        ScopeCameraPose cockpitPose{};
        ScopeProjectionTangents cockpitLens{};
        const bool cockpitCamera = Halo3CockpitCamera(cockpitPose, cockpitAspect, cockpitLens);
        VR_NoteCockpitScreen(cockpitCamera);
        if (g_buildViewport && g_buildMatrices && VR_ScopeShouldRenderThisFrame() &&
            (cockpitCamera ? true : BuildRightHandScopeCamera(camera, saved)) && VR_BeginScopeRaster())
        {
            alignas(16) unsigned char scopeTemporary[0x40]{};
            if (cockpitCamera)
            {
                memcpy(camera, saved, 0x90);
                memcpy(camera + 0x00, cockpitPose.position, sizeof(cockpitPose.position));
                memcpy(camera + 0x0C, cockpitPose.forward, sizeof(cockpitPose.forward));
                memcpy(camera + 0x18, cockpitPose.up, sizeof(cockpitPose.up));
            }
            const float zoom = cockpitCamera ? 1.0f : VR_GetScopeZoom();
            float sourceAspect = 4.0f / 3.0f;
            VR_GetScopeRenderAspect(sourceAspect);
            const ScopeProjectionTangents scopeProjection =
                cockpitCamera ? cockpitLens : ComputeScopeProjectionTangents(zoom, sourceAspect);
            const float tanX = scopeProjection.horizontal;
            const float tanY = scopeProjection.vertical;
            if (tanX > 1e-4f && tanY > 1e-4f)
            {
                float* cameraTangents = reinterpret_cast<float*>(camera + 0x28);
                cameraTangents[0] = tanX;
                cameraTangents[1] = tanY;
            }
            // The adaptive scope resolution (VR_ScopeRenderScale) renders
            // into the top-left scale x scale of the target by scaling the camera's
            // window bounds (+0x38) and title-safe bounds (+0x40), rect16
            // {top,left,bottom,right}, as split-screen gives each player a window
            // (Reach kit 0x825A40). render_pixel_bounds (+0x4C) stays the full
            // resolution (kit asserts 0x823F20). The camera is restored from `saved`
            // below for the eye passes. The glass samples only that corner.
            float subrect = 1.0f;
            if (!cockpitCamera)
            {
                const float s = VR_ScopeRenderScale();
                if (std::isfinite(s) && s >= 0.25f && s < 0.999f)
                {
                    int16_t* rects = reinterpret_cast<int16_t*>(camera + 0x38);
                    bool ok = rects[0] == 0 && rects[1] == 0 && rects[2] > 0 && rects[3] > 0;
                    if (ok)
                    {
                        for (int i = 0; i < 8; ++i)
                            rects[i] = static_cast<int16_t>(std::lround(rects[i] * s));
                        subrect = s;
                    }
                    static float logged = 0;
                    static bool loggedOk = false;
                    if (logged != s || loggedOk != ok)
                    {
                        logged = s;
                        loggedOk = ok;
                        LOG("H3 scope pass window bounds scaled to %.2f (%s): window %dx%d", s,
                            ok ? "applied" : "refused, window not at the origin", int(rects[3]), int(rects[2]));
                    }
                }
            }
            g_buildViewport(camera, scopeTemporary);
            g_buildMatrices(camera, scopeTemporary, reinterpret_cast<char*>(view) + 0x98, 0.0f);
            if (tanX > 1e-4f && tanY > 1e-4f)
            {
                float* projection = reinterpret_cast<float*>(reinterpret_cast<char*>(view) + 0x98 + 0x78);
                projection[0] = 1.0f / tanX;
                projection[5] = 1.0f / tanY;
            }
            memcpy(reinterpret_cast<char*>(view) + 0x158, camera, 0x90);
            memcpy(reinterpret_cast<char*>(view) + 0x1E8, reinterpret_cast<char*>(view) + 0x98, 0x90);
            g_scopeRenderActive.store(true, std::memory_order_release);
            if (g_prepareView)
                g_prepareView(view, 0);
            g_origRenderView(view);
            g_scopeRenderActive.store(false, std::memory_order_release);
            VR_SetScopeImageSubrect(subrect);
            VR_CaptureScope();
            VR_EndScopeRaster();
            static std::atomic<bool> logged{false};
            if (!logged.exchange(true))
                LOG("scope camera active: collision-safe bullet origin, %.2fx circular lens", zoom);
        }
    }
} // namespace
namespace halo3_bridge
{
    bool Halo3GuardedScopePass(void* view, char* camera, const unsigned char* saved)
    {
        __try
        {
            Halo3ScopePassBody(view, camera, saved);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("the Halo 3 scope pass faulted");
            g_scopeRenderActive.store(false, std::memory_order_release);
            VR_EndScopeRaster();
            g_halo3ScopeFaultGeneration.store(
                g_halo3RuntimeGeneration.load(std::memory_order_acquire), std::memory_order_release);
            g_halo3ScopeFaults.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }
} // namespace halo3_bridge
namespace
{
    bool ReadEnginePaused(bool& paused)
    {
        const uintptr_t flag = g_nativePauseFlag.load(std::memory_order_acquire);
        if (!flag)
            return false;
        __try
        {
            const uint8_t value = *reinterpret_cast<const uint8_t*>(flag);
            if (value > 1)
                return false;
            paused = value != 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading Halo 3's pause flag faulted");
            return false;
        }
    }
} // namespace
namespace halo3_bridge
{
    constinit std::atomic<uint32_t> g_halo3CoreCallbacks{0};
    constinit std::atomic<bool> g_halo3InstallReady{false};
} // namespace halo3_bridge
namespace
{
    bool LegacySelectWeaponRenderModel(
        GameTitle title, uint16_t candidateTag, uint32_t generation, uint16_t& selectedTag, uint32_t& checksum)
    {
        selectedTag = 0xFFFFu;
        checksum = 0;
        if (LegacyObserveWeaponRenderModel(title, candidateTag, generation) &&
            LegacyReadWeaponRenderModelChecksum(title, candidateTag, checksum) &&
            LegacyWeaponBoundsForTitle(title, checksum))
        {
            selectedTag = candidateTag;
            return true;
        }

        LegacyWorldCollisionFeature* feature = LegacyCollisionForTitle(title);
        if (!feature)
            return false;
        const uint64_t now = GetTickCount64();
        const uint64_t observedAt = feature->weaponObservation[0].atMs.load(std::memory_order_acquire);
        const uint32_t observedGeneration = feature->weaponObservation[0].generation.load(std::memory_order_relaxed);
        if (!LegacyWeaponCollisionCacheCanSupply(now, observedAt, generation, observedGeneration))
            return false;
        const uint32_t rawTag = feature->weaponObservation[0].tag.load(std::memory_order_relaxed);
        const uint32_t observedChecksum = feature->weaponObservation[0].checksum.load(std::memory_order_relaxed);
        if (rawTag > 0xFFFEu || !observedChecksum)
            return false;
        const uint16_t cachedTag = static_cast<uint16_t>(rawTag);
        uint32_t currentChecksum = 0;
        if (!LegacyReadWeaponRenderModelChecksum(title, cachedTag, currentChecksum) ||
            currentChecksum != observedChecksum || !LegacyWeaponBoundsForTitle(title, currentChecksum))
            return false;
        selectedTag = cachedTag;
        checksum = currentChecksum;
        return true;
    }

    bool LegacyBuildAuthoredWeaponBounds(GameTitle title, uint16_t renderModelTag, const BoneMatrix& modelRoot,
        const float collisionRoot[3], const float wrist[3], float output[][3])
    {
        // Catalog identity was found, but the combined body's node 0
        // is not proof of the submitted weapon's visible root. Keep this
        // rejected publication dormant until a mapped weapon root is supplied.
        constexpr bool kEnableCombinedBodyWeaponBounds = false;
        if (!kEnableCombinedBodyWeaponBounds)
            return false;
        LegacyWorldCollisionFeature* feature = LegacyCollisionForTitle(title);
        if (!feature)
            return false;
        uint16_t selectedTag = 0xFFFFu;
        uint32_t checksum = 0;
        if (!LegacySelectWeaponRenderModel(title, renderModelTag, feature->generation, selectedTag, checksum))
            return false;
        (void)selectedTag;
        const LegacyWeaponCollisionBounds* authored = LegacyWeaponBoundsForTitle(title, checksum);
        if (!authored)
            return false;
        float basis[9]{};
        if (!NormalizedBasis(modelRoot, basis))
            return false;
        Halo4WeaponCollisionBounds bounds{};
        bounds.runtimeImportChecksum = checksum;
        memcpy(bounds.minimum, authored->minimum, sizeof(bounds.minimum));
        memcpy(bounds.maximum, authored->maximum, sizeof(bounds.maximum));
        return Halo4BuildWeaponCollisionBoundsSamples(bounds, modelRoot.scale, basis, modelRoot.translation,
                   collisionRoot, wrist, output, kLegacyCollisionWeaponSamples) == kLegacyCollisionWeaponSamples;
    }
} // namespace
namespace halo3_bridge
{
    // Decides whether Halo 3 is the live install target and retires its hooks at a level-liveness, title or
    // generation boundary. Returns haloAvailableForInstall.
    bool TickHalo3Lifecycle(const TitleTickContext& c)
    {
        const TitleDescriptor* activeTitle = c.activeTitle;
        (void)activeTitle;
        const bool activeLevelRunning = c.activeLevelRunning;
        (void)activeLevelRunning;
        const uint64_t pollNow = c.now;
        (void)pollNow;
        const TitleAdapterRuntimeSnapshot& runtime = *c.runtime;
        const TitleHookPlan hookPlan = c.hookPlan;
        bool& gameHooked = g_supervisorTitles.gameHooked;
        bool& hookRefreshPending = g_supervisorTitles.hookRefreshPending;
        uintptr_t& hookedBase = g_supervisorTitles.hookedBase;
        (void)hookedBase;
        uint64_t& haloInstalledAtMs = g_supervisorTitles.haloInstalledAtMs;
        uint32_t& haloAttemptedGeneration = g_supervisorTitles.haloAttemptedGeneration;
        const bool manualHaloRecovery = g_supervisorTitles.manualHaloRecovery;
        (void)manualHaloRecovery;
        const bool haloAvailableForInstall = hookPlan == TitleHookPlan::Halo3Full;
        const uint32_t haloGeneration = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
        const uint32_t observedHaloGeneration = TitleAdapter_GetGeneration(GameTitle::Halo3);
        const bool haloGenerationMismatch = gameHooked && (!haloGeneration || haloGeneration != observedHaloGeneration);
        const bool haloRuntimeRetained = gameHooked && haloGeneration && runtime.runtime.owner == GameTitle::Halo3 &&
                                         runtime.runtime.generation == haloGeneration &&
                                         (runtime.runtime.qualifyingOwnerCount == 1 || runtime.ownershipPending);
        const bool haloTitleActive = !haloGenerationMismatch && (haloAvailableForInstall || haloRuntimeRetained);
        const bool haloLevelRunning = activeTitle && activeTitle->title == GameTitle::Halo3 && activeLevelRunning;
        const uint64_t lastHaloCamera = g_halo3LastCamCopyMs.load(std::memory_order_acquire);
        const bool haloCameraExpired = CameraNeedsRecovery(pollNow, haloInstalledAtMs, lastHaloCamera, gameHooked);
        const bool haloActive = haloTitleActive && haloLevelRunning && !haloCameraExpired && !manualHaloRecovery;
        // Same title-exit re-arm as ODST above: stillness observed while
        // Halo 3 is not the active title must never satisfy the frozen
        // half of the NEXT level's proof.
        if (!haloTitleActive && !gameHooked)
        {
            g_halo3LevelLoadGate.Rearm();
            haloAttemptedGeneration = 0;
        }
        if (gameHooked && !haloActive)
        {
            LOG("Halo 3 camera retirement: generation=%u observed=%u "
                "titleActive=%d levelRunning=%d cameraExpired=%d manual=%d age=%llu ms; "
                "next install requires renewed level liveness",
                haloGeneration, observedHaloGeneration, haloTitleActive ? 1 : 0, haloLevelRunning ? 1 : 0,
                haloCameraExpired ? 1 : 0, manualHaloRecovery ? 1 : 0,
                static_cast<unsigned long long>(
                    lastHaloCamera && pollNow >= lastHaloCamera ? pollNow - lastHaloCamera : 0));
            PublishHalo3Lifecycle(true, false, true);
            TitleAdapter_ClearHeartbeat(GameTitle::Halo3, haloGeneration);
            TitleAdapter_ClearCinematicControl(GameTitle::Halo3, haloGeneration);
            g_halo3LastCamCopyMs.store(0, std::memory_order_release);
            // Stop new camera transactions immediately. Presentation
            // detaches on the render thread in Game_AutoVrTick.
            g_enabled.store(false, std::memory_order_release);
            g_autoVrOwned.store(false, std::memory_order_release);
            g_autoVrUserVeto.store(false, std::memory_order_release);
            g_halo3RuntimeGeneration.store(0, std::memory_order_release);
            haloAttemptedGeneration = 0;
            // The level-liveness gate closes while MCC still owns the
            // mapping. Retire MinHook's records now, before the loader can
            // unmap halo3.dll and reuse the same address for the next
            // level. This is non-owning: no loader refcount is released.
            hookRefreshPending = !RemoveHalo3CameraHooks();
            LOG("Halo 3 hook epoch retirement at level-liveness boundary: %s",
                hookRefreshPending ? "cleanup pending" : "complete");
            gameHooked = false;
            g_hooked = false;
            // The next install must prove the level is running again.
            g_halo3LevelLoadGate.Rearm();
            // CamCopyHook can start executing again (a lingering detour,
            // or a fresh InstallHalo3CameraHooks() re-enabling it) before a resolve
            // pass republishes these pointers for the next halo3.dll
            // instance. -1 matches "not yet resolved" so
            // ApplyMotionBlurSetting stays a no-op across the gap instead
            // of dereferencing pointers into this now-inactive instance.
            g_motionBlurVarCount.store(-1, std::memory_order_release);
            g_bodyVarCount.store(0, std::memory_order_release);
            g_bodyVarGeneration.store(0, std::memory_order_release);
        }

        return haloAvailableForInstall;
    }

    // Installs the Halo 3 hooks once the level-load gate is open for a new generation.
    InstallStep TryInstallHalo3(const TitleTickContext& c, bool haloAvailableForInstall)
    {
        const TitleDescriptor* activeTitle = c.activeTitle;
        (void)activeTitle;
        const bool activeLevelRunning = c.activeLevelRunning;
        (void)activeLevelRunning;
        const uint64_t pollNow = c.now;
        (void)pollNow;
        bool& gameHooked = g_supervisorTitles.gameHooked;
        bool& hookRefreshPending = g_supervisorTitles.hookRefreshPending;
        uintptr_t& hookedBase = g_supervisorTitles.hookedBase;
        (void)hookedBase;
        uint64_t& haloInstalledAtMs = g_supervisorTitles.haloInstalledAtMs;
        uint32_t& haloAttemptedGeneration = g_supervisorTitles.haloAttemptedGeneration;
        const bool manualHaloRecovery = g_supervisorTitles.manualHaloRecovery;
        (void)manualHaloRecovery;
        const bool odstHooked = g_supervisorTitles.odstHooked;
        if (!gameHooked && haloAvailableForInstall && !odstHooked)
        {
            const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::Halo3);
            uintptr_t base = 0;
            size_t size = 0;
            // The gate is the last condition on purpose: until it opens,
            // haloAttemptedGeneration must stay untouched or this
            // generation would never be retried. Halo 3 had no readiness
            // check of any kind before this - it hooked the instant
            // halo3.dll appeared, loading screen or not.
            // IsOpen(), not AllowsInstall(): the gate was already sampled
            // once this tick at the top of the worker. Sampling again here
            // would consume two fingerprints per tick and halve the
            // observation the decision rests on.
            if (generation && generation != haloAttemptedGeneration &&
                sig::ModuleRange(activeTitle->moduleName, base, size) && g_halo3LevelLoadGate.IsOpen())
            {
                haloAttemptedGeneration = generation;
                if (hookRefreshPending)
                {
                    if (!RemoveHalo3CameraHooks())
                    {
                        haloAttemptedGeneration = 0;
                        return InstallStep::RestartTick; // TitleHookSupervisorThread sleeps 25 ms and restarts the tick
                    }
                    hookRefreshPending = false;
                }
                LOG("%ls loaded at %p, size 0x%zX", activeTitle->moduleName, (void*)base, size);
                const bool prepared = InstallHalo3CameraHooks(base, size, generation);
                // Staging every core hook first keeps native callbacks out
                // of partial install rollback and stale dependency slots.
                const MH_STATUS activated = prepared ? MH_ApplyQueued() : MH_UNKNOWN;
                if (prepared && activated == MH_OK)
                {
                    g_halo3InstallReady.store(true, std::memory_order_release);
                    g_hooked = true;
                    gameHooked = true;
                    haloInstalledAtMs = GetTickCount64();
                    hookRefreshPending = false;
                    hookedBase = base;
                }
                else if (g_halo3Hooks.Count())
                {
                    g_halo3RuntimeGeneration.store(0, std::memory_order_release);
                    hookRefreshPending = !RemoveHalo3CameraHooks();
                    LOG("Halo 3 install activation failed (%d); rollback %s", static_cast<int>(activated),
                        hookRefreshPending ? "pending" : "complete");
                }
            }
        }

        return InstallStep::Continue;
    }
} // namespace halo3_bridge
// Halo 3 render-thread diagnostics, reported on Present: cinematic facing,
// VRIK arm-solve fallbacks and (retired) first-person bone-map snapshots.
// Logs only stable state transitions; never from the palette or HUD hooks.
static void ReportHalo3PresentDiagnostics()
{
    {
        static uint64_t loggedShotless = 0, nextShotlessReport = 0;
        const uint64_t now = GetTickCount64();
        const uint64_t shotless = g_halo3ShotlessFacingSamples.load(std::memory_order_relaxed);
        if (shotless != loggedShotless && now >= nextShotlessReport)
        {
            loggedShotless = shotless;
            nextShotlessReport = now + 2000;
            LOG("H3 cinematic facing: ignored %llu shotless effect samples; gameplay yaw retained",
                static_cast<unsigned long long>(shotless));
        }
        static uint32_t loggedSerial = 0;
        const uint32_t serial = g_cinematicRebaseSerial.load(std::memory_order_acquire);
        if (serial != loggedSerial)
        {
            loggedSerial = serial;
            const int32_t scene = g_cinematicRebaseScene.load(std::memory_order_relaxed);
            const int32_t shot = g_cinematicRebaseShot.load(std::memory_order_relaxed);
            if (scene >= 0 && shot >= 0)
                LOG("cutscene facing: aligned to scene %d shot %d", scene, shot);
            else
                LOG("cutscene facing: aligned to gameplay camera on exit");
        }
    }
    // Render-thread diagnostics are reported here, on Present. Log only a
    // stable state transition; never log from the palette or HUD hot hooks.
    {
        static int loggedSide = -2;
        static const char* loggedWhy = reinterpret_cast<const char*>(1);
        static uint64_t lastLogMs = 0;
        const int side = g_armFailureSide.load(std::memory_order_acquire);
        const char* why = g_armFailurePublished.load(std::memory_order_relaxed);
        const uint64_t diagNow = GetTickCount64();
        if ((side != loggedSide || why != loggedWhy) && diagNow - lastLogMs >= 500)
        {
            loggedSide = side;
            loggedWhy = why;
            lastLogMs = diagNow;
            if (side == 0)
                LOG("arm IK safe diagnostic: both arms applied to controllers");
            else if (side == 1)
                LOG("arm IK safe diagnostic: right-arm solve fell back (%s); authored "
                    "support hand remains on weapon",
                    why ? why : "pre-solve");
            else if (side == 2)
                LOG("arm IK safe diagnostic: left arm not applied (%s)", why ? why : "pre-solve");
        }
    }
}
namespace halo3_bridge
{
    // Halo 3 (and the shared fallback when no other title owns the tick): lifecycle,
    // pause presentation, runtime mode publication and auto-VR arming.
    void AutoVrTickHalo3()
    {
        if (!Game_AllowsSharedGameplayFeatures())
        {
            const TitleAdapterRuntimeSnapshot transition = RuntimeSnapshot(GetTickCount64());
            if (TitleAdapter_GetActiveTitle() == GameTitle::Unknown && transition.ownershipPending &&
                transition.runtime.owner == GameTitle::Halo3 && transition.runtime.installed &&
                !transition.runtime.teardownRequested)
            {
                // The bounded pending state retains an already-running transaction
                // only long enough for its next camera copy. It grants no input,
                // HUD, aim, haptics, or new arming work.
                return;
            }
            InvalidateHudLayoutProfile(HudLayoutProfile::Halo3);
            if (g_enabled.load(std::memory_order_relaxed) || VR_IsStereoEnabled() ||
                g_autoVrOwned.load(std::memory_order_relaxed))
            {
                g_enabled.store(false, std::memory_order_release);
                PublishHalo3Lifecycle(true, false, false);
                g_autoVrOwned.store(false, std::memory_order_release);
                g_autoVrUserVeto.store(false, std::memory_order_release);
                VR_DetachGamePresentation();
            }
            return;
        }

        // A same-title unload/reload can complete between two Present calls. Do
        // not inherit the prior instance's arm or fresh-camera debounce: detach on
        // this render thread and require a full new one-second stable interval.
        static uint32_t haloFreshGeneration = 0;
        static uint64_t freshSince = 0;
        const uint32_t currentHaloGeneration = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
        if (currentHaloGeneration != haloFreshGeneration)
        {
            haloFreshGeneration = currentHaloGeneration;
            freshSince = 0;
            const bool presentationWasActive = g_enabled.exchange(false, std::memory_order_acq_rel) ||
                                               VR_IsStereoEnabled() || g_autoVrOwned.load(std::memory_order_acquire);
            g_autoVrOwned.store(false, std::memory_order_release);
            g_autoVrUserVeto.store(false, std::memory_order_release);
            if (currentHaloGeneration)
                PublishHalo3Lifecycle(true, false, false);
            if (presentationWasActive)
                VR_DetachGamePresentation();
        }

        UpdateCinematicFovPolicy();
        HudLayoutAutoTick(HudLayoutProfile::Halo3); // shared behavior, H3 tag adapter
        ReportHalo3PresentDiagnostics();
        const uint64_t now = GetTickCount64();
        const uint64_t last = g_halo3LastCamCopyMs.load(std::memory_order_relaxed);
        const bool cameraFresh = last != 0 && now >= last && (now - last) < 500; // camera driving now
        const bool cameraStale = last == 0 || now < last || (now - last) > 2000; // menu / loading

        // Debounce entry: require the camera to have been fresh continuously for a
        // short spell before arming, so a single stray frame doesn't flip us.
        if (cameraFresh)
        {
            if (freshSince == 0)
                freshSince = now;
        }
        else
            freshSince = 0;
        const bool inLevelStable = freshSince != 0 && (now - freshSince) > 1000;

        const GameTitle availableTitle = TitleAdapter_GetActiveTitle();
        const TitleAdapterRuntimeSnapshot titleRuntime = RuntimeSnapshot(now);
        const bool haloTitleActive =
            availableTitle == GameTitle::Halo3 || titleRuntime.runtime.owner == GameTitle::Halo3;
        const bool pausePresentation = VR_IsPausePresentation();
        bool enginePaused = false;
        static bool previousEnginePaused = false;
        static bool enginePauseLogged = false;
        static uint64_t pauseMismatchSince = 0;
        static bool pauseMismatchValue = false;
        // The native flag below is a Halo 3 proof. Keep it completely out of
        // resident-module transitions so H2/Reach/ODST cannot inherit Halo 3's
        // pause state merely because halo3.dll is still loaded.
        if (haloTitleActive && ReadEnginePaused(enginePaused))
        {
            if (!enginePauseLogged || enginePaused != previousEnginePaused)
            {
                LOG("pause state: native engine flag=%d, presentation target=%d", enginePaused ? 1 : 0,
                    VR_IsPausePresentationTarget() ? 1 : 0);
                previousEnginePaused = enginePaused;
                enginePauseLogged = true;
            }
            const bool targetPaused = VR_IsPausePresentationTarget();
            if (g_enginePauseValidated.load() && enginePaused != targetPaused)
            {
                if (pauseMismatchSince == 0 || pauseMismatchValue != enginePaused)
                {
                    pauseMismatchSince = now;
                    pauseMismatchValue = enginePaused;
                }
                else if (now - pauseMismatchSince >= 50)
                {
                    VR_RequestPausePresentation(enginePaused);
                    LOG("pause state: authoritative engine value corrected "
                        "presentation to %s",
                        enginePaused ? "head-locked 2D" : "stereo 3D");
                    pauseMismatchSince = 0;
                }
            }
            else
                pauseMismatchSince = 0;
        }
        static PauseLevelRecovery pauseLevelRecovery;
        if (haloTitleActive && !g_enginePauseValidated.load() &&
            pauseLevelRecovery.Update(pausePresentation, cameraStale, inLevelStable))
        {
            // Restart Level leaves Halo's native pause screen without producing a
            // second Start edge. Re-enter stereo only after the replacement
            // level's camera has been stable for the normal debounce interval.
            VR_RequestPausePresentation(false);
            LOG("pause transition: restarted level is stable, restoring stereo 3D");
        }
        static bool pauseExitClearRequested = false;
        if (pausePresentation && !haloTitleActive)
        {
            // Leaving the title through Halo's pause menu must not strand the next
            // level in the pause override. This changes presentation only; it does
            // not inject another Start press into the MCC shell.
            if (!pauseExitClearRequested)
            {
                pauseExitClearRequested = true;
                VR_RequestPausePresentation(false);
                LOG("pause transition: title left, clearing 2D pause override");
            }
        }
        else
            pauseExitClearRequested = false;
        if (haloTitleActive)
        {
            const uint32_t generation = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
            if (generation)
            {
                RuntimeMode mode = pausePresentation ? RuntimeMode::Paused
                                                     : (inLevelStable ? RuntimeMode::Gameplay : RuntimeMode::Loading);
                if (mode == RuntimeMode::Gameplay)
                {
                    // Halo 3-only refinement: report the measured seated
                    // state. Paused/Loading precedence is untouched, and both
                    // existing consumers (locomotion, haptics) already accept
                    // Vehicle/Turret alongside Gameplay, so an Unknown or stale
                    // snapshot keeps today's behavior exactly. ODST and Reach
                    // never publish this snapshot, so their modes are unchanged.
                    const Halo3VehicleStateSnapshot vehicle = Game_Halo3VehicleState();
                    switch (Halo3ClassifyGameplayUpgrade(vehicle.state, vehicle.typeValid ? vehicle.vehicleType : -1))
                    {
                    case 1:
                        mode = RuntimeMode::Vehicle;
                        break;
                    case 2:
                        mode = RuntimeMode::Turret;
                        break;
                    default:
                        break;
                    }
                }
                TitleAdapter_PublishMode(GameTitle::Halo3, generation, mode);
            }
        }

        // MCC keeps several game DLLs resident, so module presence cannot identify
        // the active renderer. The camera hook is the proven ownership signal.
        // Once its heartbeat is absent for 500 ms, disarm per-eye rendering and
        // release Halo's retained scene target before another engine takes over.
        // Pause is exempt because its stable 2D presentation is already detached.
        if (!pausePresentation && !cameraFresh && (g_enabled.load() || VR_IsStereoEnabled() || g_autoVrOwned.load()))
        {
            g_enabled = false;
            PublishHalo3Lifecycle(true, false, false);
            g_autoVrOwned = false;
            g_autoVrUserVeto = false;
            VR_DetachGamePresentation();
        }

        const bool manualRecovery = haloTitleActive && ManualVrRecoveryFor(GameTitle::Halo3);
        if ((!g_config.auto_vr && !manualRecovery) || pausePresentation)
            return;

        if (inLevelStable)
        {
            if (!g_enabled.load() && !g_autoVrUserVeto.load())
            {
                g_enabled = true;
                PublishHalo3Lifecycle(true, true, false);
                g_needRecenter = true;
                if (!VR_IsStereoEnabled())
                    VR_ToggleStereo();
                g_autoVrOwned = true;
                LOG("auto-VR: level detected - head tracking + stereo ON");
            }
        }
        else if (cameraStale)
        {
            g_autoVrUserVeto = false; // reset veto on leaving the level
            if (g_autoVrOwned.load() && g_enabled.load())
            {
                g_enabled = false;
                PublishHalo3Lifecycle(true, false, false);
                if (VR_IsStereoEnabled())
                    VR_ToggleStereo();
                g_autoVrOwned = false;
                LOG("auto-VR: left the level - back to the flat menu screen");
            }
        }
    }
} // namespace halo3_bridge
// The virtual steering wheel. Two hands make a wheel out of the line
// between them - no fixed pivot, so it is wherever the player's hands are and
// taking it never snaps the steering. Double-click both grips to take it,
// double-click again to let go: the right grip is the dismount, so a sustained
// squeeze would either eject the driver or cost them the dismount entirely.
//
// Called once per XInput poll with THAT poll's pad, before the buttons below
// are built. One shared sample is load-bearing: sampling the pad again here
// could see a grip on one side of the press threshold and the button code the
// other, which is exactly the frame that would fire the dismount.
// Point mode (and the follow switch): the original two-grip double-click
// wheel, kept exactly as it was.
static void Halo3UpdateLegacyToggleWheel(const VrPadState& pad)
{
    // Its own switch (default off since v13); vehicle_motion stays the master
    // switch for H2/CE vehicle control and is not consulted here any more.
    const bool eligible =
        pad.valid && g_config.vehicle_double_grip_wheel && g_enabled.load() && g_vrAim.load() && SeatUsesWheel();
    if (Halo3UpdateWheelToggle(g_halo3Wheel, eligible, pad.gripL, pad.gripR, GetTickCount64()))
    {
        // The only feedback a toggle can give. VR_SetGameHaptics peak-holds,
        // so this lands as one blip and the game's own rumble resumes.
        VR_SetGameHaptics(g_halo3Wheel.engaged ? 0.6f : 0.35f);
        LOG("H3 vehicle: steering wheel %s (double-click both grips)", g_halo3Wheel.engaged ? "taken" : "released");
    }
    g_halo3WheelSwallowsGrips.store(Halo3WheelSwallowsGrips(g_halo3Wheel) ? 1u : 0u, std::memory_order_release);

    float lq[4]{}, lp[3]{}, rq[4]{}, rp[3]{}, hq[4]{}, hp[3]{};
    const bool posesOk = g_halo3Wheel.engaged && VR_GetLeftControllerPose(lq, lp) &&
                         VR_GetRightControllerPose(rq, rp) && VR_GetHeadPose(hq, hp);
    float headYaw = 0.0f;
    if (posesOk)
    {
        const float x = hq[0], y = hq[1], z = hq[2], w = hq[3];
        const float fx = -2.0f * (w * y + x * z);
        const float fz = -(1.0f - 2.0f * (x * x + y * y));
        headYaw = atan2f(fx, -fz);
    }
    const bool driving = posesOk && Halo3ComputeWheelSteer(g_halo3Wheel, lp, rp, headYaw,
                                        g_config.vehicle_wheel_max_deg, g_config.vehicle_wheel_deadzone_deg);
    // Publish the payload before the release flag. Readers acquire the flag,
    // then consume the matching steer value and can free the turn stick.
    g_halo3SteerAxis.store(driving ? g_halo3Wheel.steer : 0.0f, std::memory_order_relaxed);
    g_halo3WheelDriving.store(driving ? 1u : 0u, std::memory_order_release);
}

void Game_Halo3UpdateVehicleWheel(const VrPadState& pad)
{
    // Grab wheel in the style of Vice City VR (vehicle_wheel_logic.h). Available in a
    // driver seat whose steering is authored (the seat follows the hull) and
    // whose class is not set to Stick.
    static vehicle_wheel::GrabWheel wheel;
    static weapon_interaction::BodyYaw bodyYaw;
    static uint64_t exitPulseUntil = 0;
    const uint64_t now = GetTickCount64();
    const bool contextFresh = VehicleSteerContextFresh();
    const int mode = contextFresh ? g_vehicleSteer.mode.load(std::memory_order_relaxed) : vehicle_wheel::Point;
    const bool contextDriver = contextFresh && g_vehicleSteer.driver.load(std::memory_order_relaxed);
    if (!vehicle_wheel::UsesGrabWheel(mode))
    {
        // Point (or Stick, where the old path finds no authored wheel seat
        // only if follow is off): the old double-click toggle, untouched.
        if (mode == vehicle_wheel::Stick)
        {
            g_halo3WheelSwallowsGrips.store(0, std::memory_order_release);
            g_halo3SteerAxis.store(0.0f, std::memory_order_relaxed);
            g_halo3WheelDriving.store(0, std::memory_order_release);
        }
        else
        {
            wheel.Reset();
            exitPulseUntil = 0;
            g_vehicleExitButtons.store(0, std::memory_order_relaxed);
            Halo3UpdateLegacyToggleWheel(pad);
        }
        // Stick mode still gets the grip exit below.
        if (mode != vehicle_wheel::Stick)
            return;
    }
    const bool eligible = pad.valid && g_config.vehicle_motion && g_enabled.load() && g_vrAim.load() &&
                          SeatUsesWheel() && mode == vehicle_wheel::Wheel;
    vehicle_wheel::Input in{};
    in.now = now;
    float lq[4]{}, rq[4]{}, hq[4]{};
    const bool poses = VR_GetLeftControllerPose(lq, in.left.pos) && VR_GetRightControllerPose(rq, in.right.pos) &&
                       VR_GetHeadPose(hq, in.head);
    in.left.valid = in.right.valid = poses;
    in.left.grip = pad.gripL;
    in.right.grip = pad.gripR;
    for (int i = 0; i < 4; ++i)
    {
        in.left.q[i] = lq[i];
        in.right.q[i] = rq[i];
    }
    weapon_interaction::Vec facing{};
    const weapon_interaction::Quat headQ{hq[0], hq[1], hq[2], hq[3]};
    const bool facingOk = poses && bodyYaw.Update(headQ, now, wheel.Holding(), facing);
    in.bodyForward[0] = facing.x;
    in.bodyForward[1] = facing.y;
    in.bodyForward[2] = facing.z;
    in.available = eligible && facingOk;
    vehicle_wheel::Settings settings{};
    settings.radius = g_config.vehicle_wheel_radius_m;
    settings.forward = g_config.vehicle_wheel_forward_m;
    settings.down = g_config.vehicle_wheel_down_m;
    settings.lockDeg = g_config.vehicle_wheel_max_deg;
    settings.deadzone = g_config.vehicle_wheel_max_deg > 1.0f
                            ? g_config.vehicle_wheel_deadzone_deg / g_config.vehicle_wheel_max_deg
                            : 0.03f;
    settings.exitHoldMs = g_config.vehicle_exit_hold_ms;
    settings.rotation = g_config.vehicle_wheel_v2;
    // Point mode keeps the old lone-grip dismount: no grip exit, grip untouched.
    settings.gripExit = contextDriver && (mode == vehicle_wheel::Wheel || mode == vehicle_wheel::Stick);
    // Stick mode: no wheel to grab, but the exit grip still needs the frame.
    if (mode == vehicle_wheel::Stick)
        in.available = in.available || (pad.valid && contextDriver && facingOk);
    settings.allowGrab = mode == vehicle_wheel::Wheel && eligible;
    const vehicle_wheel::Output out = wheel.Update(in, settings);
    // Ticks on the hand that grabbed; the physical left hand is the support
    // hand unless left-handed.
    // Pad and poses are by role here ("left" = support, "right" = weapon hand),
    // as is VR_PulseContactHaptics(true = support).
    if (out.grabLeft)
        VR_PulseContactHaptics(true, feedback::Strength(feedback::Cue::WheelGrab));
    if (out.grabRight)
        VR_PulseContactHaptics(false, feedback::Strength(feedback::Cue::WheelGrab));
    if (out.released)
    {
        VR_PulseContactHaptics(true, feedback::Strength(feedback::Cue::WheelRelease));
        VR_PulseContactHaptics(false, feedback::Strength(feedback::Cue::WheelRelease));
    }
    static uint64_t lastWheelLogMs = 0;
    if ((out.grabbed || out.released) && now - lastWheelLogMs >= 250)
    {
        LOG("Action fired: wheel %s", out.grabbed ? "grab" : "release");
        lastWheelLogMs = now;
    }
    // A grip held outside the wheel exits through the game's own Use.
    if (out.exitRequested && contextFresh && (mode == vehicle_wheel::Wheel || mode == vehicle_wheel::Stick))
    {
        const uint32_t use = Game_VrActionTransport(vr_mapping::Interact, now);
        if (use)
        {
            g_vehicleExitButtons.store(use, std::memory_order_relaxed);
            exitPulseUntil = now + 400; // a held Use: the game's exit
            LOG("Action fired: vehicle exit via grip hold");
        }
    }
    // End the held Use the moment the player is no longer the driver, so
    // H3/ODST hold-to-swap cannot pick up a weapon after the exit.
    if (now >= exitPulseUntil || !contextDriver)
    {
        exitPulseUntil = 0;
        g_vehicleExitButtons.store(0, std::memory_order_relaxed);
    }
    g_halo3WheelSwallowsGrips.store(out.consumeLeft || out.consumeRight ? 1u : 0u, std::memory_order_release);
    // Publish the payload before the release flag. Readers acquire the flag,
    // then consume the matching steer value and can free the turn stick.
    g_halo3SteerAxis.store(out.driving ? out.steer : 0.0f, std::memory_order_relaxed);
    g_halo3WheelDriving.store(out.driving ? 1u : 0u, std::memory_order_release);
    // Log the edge where the wheel (or stick mode) actually takes the steering.
    static bool wasDriving = false;
    static uint64_t lastEngageLogMs = 0;
    if (out.driving && !wasDriving && now - lastEngageLogMs >= 1000)
    {
        lastEngageLogMs = now;
        LOG("Vehicle wheel steering: engaged (title=%u seat=driver mode=%s)",
            unsigned(g_vehicleSteer.title.load(std::memory_order_relaxed)),
            mode == vehicle_wheel::Wheel ? "wheel" : "stick");
    }
    wasDriving = out.driving;
}
// The input hook withholds the grip buttons ONLY while both grips are down at
// once, which is the wheel gesture itself. A lone right grip is never touched,
// so the dismount it performs keeps working whether the wheel is held or not.
bool Game_Halo3VehicleSwallowsGrips()
{
    return g_halo3WheelSwallowsGrips.load(std::memory_order_acquire) != 0 ||
           g_turretSwallowsGrips.load(std::memory_order_acquire) != 0;
}

// True while the wheel, not the stick, is authoring the steering.
bool Game_Halo3VehicleWheelActive()
{
    return g_halo3WheelDriving.load(std::memory_order_acquire) != 0;
}
// The storage slot for the SEAT the player occupies right now (config.h
// ConfigSeatTrimSlot; a mounted gunner reports the CARRIER vehicle and its own
// gunner slot). -1 on foot, unidentified, unauthored seat, or a stale/torn
// seat frame. The F1 seat sliders bind to this.
int Game_Halo3CurrentSeatTrimSlot()
{
    if (!Halo3VehicleFpActive())
        return -1;
    Halo3SeatSnapshot seat;
    if (!Halo3ReadSeatSnapshot(seat))
        return -1;
    return ConfigSeatTrimSlot(static_cast<int>(seat.identity), seat.seatIndex, seat.mounted);
}

// "Warthog gunner", "Hornet passenger" - the F1 label for a trim slot. Takes
// the slot so menu.cpp needs no enum include; never returns null. Menu thread
// only: the formatted result lives in a static buffer.
const char* Game_Halo3SeatTrimName(int slot)
{
    static char text[48];
    if (slot < 0 || slot >= kVehicleTrimSlots)
        return "this seat";
    const int v = slot / kVehicleSeatSlots;
    const int s = slot % kVehicleSeatSlots;
    static const char* const kNiceVehicle[kVehicleTrimCount] = {
        "Scorpion", "Warthog", "Mongoose", "Ghost", "Wraith", "Prowler", "Banshee", "Hornet", "Chopper", "Turret"};
    static const char* const kNiceSeat[kVehicleSeatSlots] = {"driver", "passenger", "passenger 2", "gunner"};
    // The walk-up turret has exactly one seat; "Turret driver" reads wrong.
    if (v == kVehicleTrimCount - 1 && s == 0)
        return "Turret";
    _snprintf_s(text, sizeof(text), _TRUNCATE, "%s %s", kNiceVehicle[v], kNiceSeat[s]);
    return text;
}
Halo3VehicleStateSnapshot Game_Halo3VehicleState()
{
    Halo3VehicleStateSnapshot out{};
    if (g_halo3VehicleBinding.load(std::memory_order_acquire) !=
        static_cast<uint8_t>(Halo3VehicleBindingState::Installed))
        return out;
    const uint32_t generation = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
    if (!generation)
        return out;
    // The sampler heartbeat doubles as a staleness bound: if the camera
    // thread stops feeding samples (menus, teardown), every consumer sees
    // Unknown within half a second and behaves exactly as today.
    const uint64_t lastSampleMs = g_halo3VehicleSampleMs.load(std::memory_order_relaxed);
    if (GetTickCount64() - lastSampleMs > 500)
        return out;
    const uint64_t snapshot = g_halo3VehicleSnapshot.load(std::memory_order_acquire);
    out.state = Halo3VehicleSnapshotState(snapshot, generation);
    out.vehicleType = Halo3VehicleSnapshotType(snapshot, generation);
    out.seatIndex = Halo3VehicleSnapshotSeat(snapshot, generation);
    out.typeValid = out.vehicleType >= 0;
    return out;
}
namespace halo3_bridge
{
    bool H3PrimaryWeaponEvidence(uint32_t expectedGeneration, uint32_t& unitOut, uint32_t& weaponOut,
        uint32_t& detailOut, bool* primaryAbsentOut) noexcept
    {
        if (primaryAbsentOut)
            *primaryAbsentOut = false;
        if (!g_halo3PlayerUnitGetter || !g_halo3UnitInVehicle)
            return false;
        if (g_halo3RuntimeGeneration.load(std::memory_order_acquire) != expectedGeneration)
        {
            detailOut = kDiagnosticGuard;
            return false;
        }
        const uint32_t owner = uint32_t(g_halo3PlayerUnitGetter(0));
        if (owner == UINT32_MAX || !(owner >> 16))
            return false;
        if (g_halo3UnitInVehicle(int32_t(owner)))
            return false;
        uint32_t weapons[2]{};
        if (!Halo3ReadOwnedWeapons(owner, weapons, false, primaryAbsentOut))
            return false;
        if (weapons[0] == UINT32_MAX)
            return false;
        detailOut = (weapons[1] != UINT32_MAX) ? 1u : 0u;
        // FP-side slot agreement, mirroring the existing stable commit point
        // without its optional-feature gates. The Capture probe records this
        // raw; the FP commit requires it.
        if (g_engineTlsIndex && *g_engineTlsIndex < 0x200)
        {
            auto** slots = reinterpret_cast<uint8_t**>(__readgsqword(0x58));
            const auto* tls = slots ? slots[*g_engineTlsIndex] : nullptr;
            const auto* users = tls ? *reinterpret_cast<const uint8_t* const*>(tls + 0x568) : nullptr;
            if (users && *reinterpret_cast<const uint32_t*>(users + 0x3C) == weapons[0])
                detailOut |= kDiagnosticFpAgreement;
        }
        if (g_halo3RuntimeGeneration.load(std::memory_order_acquire) != expectedGeneration)
        {
            detailOut = kDiagnosticGuard;
            return false;
        }
        unitOut = owner;
        weaponOut = weapons[0];
        return true;
    }
    bool DiagnosticH3Primary(
        uint32_t expectedGeneration, uint32_t& unitOut, uint32_t& weaponOut, uint32_t& detailOut) noexcept
    {
        return H3PrimaryWeaponEvidence(expectedGeneration, unitOut, weaponOut, detailOut, nullptr);
    }
} // namespace halo3_bridge

namespace halo3_bridge
{
    // Core calls these. They are defined in the anonymous namespace above (two by halo3_native_seat_patch.inl;
    // two call those), beside the Halo 3 code that calls them; each forwarder only forwards.
    uint32_t Halo3OccupiedSeatFlags()
    {
        return ::Halo3OccupiedSeatFlags();
    }
    void Halo3RestoreNativeSeatPatch()
    {
        return ::Halo3RestoreNativeSeatPatch();
    }
    void Halo3SampleVehicleState(uint64_t nowMs)
    {
        return ::Halo3SampleVehicleState(nowMs);
    }
    void LogHalo3VehicleProbeIfNew()
    {
        return ::LogHalo3VehicleProbeIfNew();
    }
} // namespace halo3_bridge
